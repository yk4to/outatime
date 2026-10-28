#include "alt_tap_detector.hpp"

namespace outatime {
namespace {

constexpr std::uint32_t kVkLeftCtrl = 0xA2;  // VK_LCONTROL

// AltGr layouts report Right Alt as a fake Left Ctrl with this scan code plus Right Alt.
constexpr std::uint32_t kAltGrFakeCtrlScanCode = 0x21D;

// VK_LBUTTON, VK_RBUTTON, VK_MBUTTON, VK_XBUTTON1, VK_XBUTTON2
constexpr std::uint32_t kMouseButtons[] = {0x01, 0x02, 0x04, 0x05, 0x06};

// Shift, Ctrl and Alt (generic, left and right) and both Windows keys.
constexpr std::uint32_t kModifiers[] = {0x10, 0x11, 0x12, 0xA0, 0xA1, 0xA2,
                                        0xA3, 0xA4, 0xA5, 0x5B, 0x5C};

}  // namespace

void AltTapDetector::SetWatchedKeys(bool left, bool right) {
    leftWatched_ = left;
    rightWatched_ = right;
}

void AltTapDetector::SetDoubleTapMs(std::uint32_t ms) { doubleTapMs_ = ms; }

bool AltTapDetector::IsWatched(std::uint32_t vk) const {
    return (vk == kVkLeftAlt && leftWatched_) || (vk == kVkRightAlt && rightWatched_);
}

// Key-ups can be missed (e.g. released on the secure desktop), so drop keys
// that are no longer physically held before looking at what is pressed.
void AltTapDetector::DropReleasedKeys() {
    for (std::uint32_t vk = 0; vk < pressed_.size(); ++vk) {
        if (pressed_[vk] && !input_.IsDown(vk)) {
            pressed_[vk] = false;
        }
    }
}

bool AltTapDetector::AnyModifierPressed() const {
    for (const std::uint32_t vk : kModifiers) {
        if (pressed_[vk]) {
            return true;
        }
    }
    return false;
}

// Catches buttons held across the Alt press (e.g. Alt+drag started before Alt),
// which mouse input events never report going down.
bool AltTapDetector::AnyMouseButtonDown() const {
    for (const std::uint32_t vk : kMouseButtons) {
        if (input_.IsDown(vk)) {
            return true;
        }
    }
    return false;
}

Decision AltTapDetector::OnKey(const KeyEvent& event) {
    // Ignore injected keys (our own replays and other tools'), and the AltGr fake Ctrl.
    if (event.injected || event.vk >= pressed_.size() ||
        (event.vk == kVkLeftCtrl && event.scanCode == kAltGrFakeCtrlScanCode)) {
        return Decision::PassThrough;
    }

    const std::uint32_t vk = event.vk;
    if (event.down) {
        const bool repeat = pressed_[vk];
        Decision decision = Decision::PassThrough;
        if (repeat) {
            // A key held from before Alt that starts repeating types Alt+key.
            if (vk != candidate_) {
                candidate_ = 0;
            }
        } else {
            // Pressing the same Alt key again right after a tap means the user wants a
            // plain Alt (e.g. to open the menu bar). Any other key in between cancels this.
            const std::uint32_t doubleTapMs = doubleTapMs_;
            const bool doubleTap = vk == lastTapVk_ && IsWatched(vk) && doubleTapMs != 0 &&
                                   event.time - lastTapTime_ <= doubleTapMs;
            lastTapVk_ = 0;
            if (doubleTap) {
                candidate_ = 0;
                decision = Decision::DoubleTap;
            } else if (IsWatched(vk)) {
                // When typing fast, the previous key is often still held as Alt goes
                // down. That is fine as long as it is let go before Alt (checked on
                // key-up), but a held modifier means a shortcut such as Shift+Alt.
                DropReleasedKeys();
                candidate_ = !AnyModifierPressed() && !AnyMouseButtonDown() ? vk : 0;
                candidateTime_ = event.time;
            } else {
                // Any other key going down makes the held Alt key a combination. A key
                // being let go does not, as with Karabiner-Elements' to_if_alone.
                candidate_ = 0;
            }
        }
        pressed_[vk] = true;
        return decision;
    }

    pressed_[vk] = false;
    if (vk == candidate_) {
        candidate_ = 0;
        DropReleasedKeys();
        const bool heldTooLong = event.time - candidateTime_ > kTapTimeoutMs;
        if (pressed_.none() && !AnyMouseButtonDown() && !heldTooLong) {
            lastTapVk_ = vk;
            lastTapTime_ = event.time;
            return Decision::Tap;
        }
    }
    return Decision::PassThrough;
}

void AltTapDetector::OnMouseInput() { candidate_ = 0; }

void AltTapDetector::OnTapNotReplayed() { lastTapVk_ = 0; }

}  // namespace outatime
