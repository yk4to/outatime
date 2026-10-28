#include "alt_tap_detector.hpp"

namespace outatime {
namespace {

constexpr std::uint32_t kVkLeftCtrl = 0xA2;  // VK_LCONTROL

// AltGr layouts report Right Alt as a fake Left Ctrl with this scan code plus Right Alt.
constexpr std::uint32_t kAltGrFakeCtrlScanCode = 0x21D;

// VK_LBUTTON, VK_RBUTTON, VK_MBUTTON, VK_XBUTTON1, VK_XBUTTON2
constexpr std::uint32_t kMouseButtons[] = {0x01, 0x02, 0x04, 0x05, 0x06};

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
// that are no longer physically held before deciding nothing else is pressed.
bool AltTapDetector::NoOtherKeysPressed() {
    for (std::uint32_t vk = 0; vk < pressed_.size(); ++vk) {
        if (pressed_[vk] && !input_.IsDown(vk)) {
            pressed_[vk] = false;
        }
    }
    return pressed_.none();
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
        if (!repeat) {
            // Pressing the same Alt key again right after a tap means the user wants a
            // plain Alt (e.g. to open the menu bar). Any other key in between cancels this.
            const std::uint32_t doubleTapMs = doubleTapMs_;
            const bool doubleTap = vk == lastTapVk_ && IsWatched(vk) && doubleTapMs != 0 &&
                                   event.time - lastTapTime_ <= doubleTapMs;
            lastTapVk_ = 0;
            if (doubleTap) {
                candidate_ = 0;
                decision = Decision::DoubleTap;
            } else {
                const bool alone = NoOtherKeysPressed() && !AnyMouseButtonDown();
                candidate_ = IsWatched(vk) && alone ? vk : 0;
            }
        }
        pressed_[vk] = true;
        return decision;
    }

    pressed_[vk] = false;
    if (vk == candidate_) {
        candidate_ = 0;
        if (!AnyMouseButtonDown()) {
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
