#include "tap_detector.hpp"

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

void TapDetector::SetWatchedKeys(TapKeySet keys) { watched_ = keys.to_ulong(); }

void TapDetector::SetDoubleTapMs(std::uint32_t ms) { doubleTapMs_ = ms; }

void TapDetector::SetAcceptInjected(bool accept) { acceptInjected_ = accept; }

bool TapDetector::IsWatched(std::uint32_t vk) const {
    const std::optional<TapKey> key = TapKeyFromVirtualKey(vk);
    return key && TapKeySet(watched_).test(Index(*key));
}

// Key-ups can be missed (e.g. released on the secure desktop), so drop keys
// that are no longer physically held before looking at what is pressed.
void TapDetector::DropReleasedKeys() {
    for (std::uint32_t vk = 0; vk < pressed_.size(); ++vk) {
        if (pressed_[vk] && !input_.IsDown(vk)) {
            pressed_[vk] = false;
        }
    }
}

bool TapDetector::AnyModifierPressed() const {
    for (const std::uint32_t vk : kModifiers) {
        if (pressed_[vk]) {
            return true;
        }
    }
    return false;
}

// Catches buttons held across the Alt press (e.g. Alt+drag started before Alt),
// which mouse input events never report going down.
bool TapDetector::AnyMouseButtonDown() const {
    for (const std::uint32_t vk : kMouseButtons) {
        if (input_.IsDown(vk)) {
            return true;
        }
    }
    return false;
}

Decision TapDetector::OnKey(const KeyEvent& event) {
    // Ignore our own replays, other programs' keys unless accepted, and the AltGr fake Ctrl.
    const bool ignoredSource = event.source == KeySource::Ours ||
                               (event.source == KeySource::Injected && !acceptInjected_);
    if (ignoredSource || event.vk >= pressed_.size() ||
        (event.vk == kVkLeftCtrl && event.scanCode == kAltGrFakeCtrlScanCode)) {
        return Decision::PassThrough;
    }

    const std::uint32_t vk = event.vk;
    if (event.down) {
        const bool repeat = pressed_[vk];
        Decision decision = Decision::PassThrough;
        if (repeat) {
            // A key held from before the tap key that starts repeating types e.g. Alt+key.
            if (vk != candidate_) {
                candidate_ = 0;
            }
        } else {
            // Pressing the same key again right after a tap means the user wants a plain
            // key press (e.g. to open the menu bar or the Start menu). Any other key in
            // between cancels this.
            const std::uint32_t doubleTapMs = doubleTapMs_;
            const bool doubleTap = vk == lastTapVk_ && IsWatched(vk) && doubleTapMs != 0 &&
                                   event.time - lastTapTime_ <= doubleTapMs;
            lastTapVk_ = 0;
            if (doubleTap) {
                candidate_ = 0;
                decision = Decision::DoubleTap;
            } else if (IsWatched(vk)) {
                // When typing fast, the previous key is often still held as the tap key
                // goes down. That is fine as long as it is let go first (checked on
                // key-up), but a held modifier means a shortcut such as Shift+Alt.
                DropReleasedKeys();
                candidate_ = !AnyModifierPressed() && !AnyMouseButtonDown() ? vk : 0;
                candidateTime_ = event.time;
            } else {
                // Any other key going down makes the held tap key a combination. A key
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
        // The key may have stopped being watched while held, e.g. when taps were paused
        // for a full-screen app that came to the front.
        if (IsWatched(vk) && pressed_.none() && !AnyMouseButtonDown() && !heldTooLong) {
            lastTapVk_ = vk;
            lastTapTime_ = event.time;
            return Decision::Tap;
        }
    }
    return Decision::PassThrough;
}

void TapDetector::OnMouseInput() { candidate_ = 0; }

void TapDetector::OnTapNotReplayed() { lastTapVk_ = 0; }

}  // namespace outatime
