#pragma once

#include <atomic>
#include <bitset>
#include <cstdint>

namespace outatime {

// Virtual-key codes, so this header does not need <windows.h>.
inline constexpr std::uint32_t kVkLeftAlt = 0xA4;   // VK_LMENU
inline constexpr std::uint32_t kVkRightAlt = 0xA5;  // VK_RMENU

struct KeyEvent {
    std::uint32_t vk;
    std::uint32_t scanCode;
    bool down;
    bool injected;       // Sent by a program, including our own replayed keys
    std::uint32_t time;  // Milliseconds; wraps around
};

// Physical input state that cannot be tracked from events alone.
class InputState {
public:
    virtual ~InputState() = default;
    // Whether the key or mouse button with this virtual-key code is held now.
    virtual bool IsDown(std::uint32_t vk) const = 0;
};

enum class Decision {
    PassThrough,  // Let the event through.
    Tap,          // This key-up ends a tap: swallow it, replay it masked, report the tap.
    DoubleTap,    // This key-down repeats a tap: let it through as a plain Alt, report it.
};

// Decides which Alt presses are taps (pressed and released with no other key
// or mouse button in between) and double taps, from low-level input events.
// It has no Win32 dependencies so it can be unit tested.
//
// OnKey, OnMouseInput and OnTapNotReplayed must be called from one thread;
// the setters may be called from any thread.
class AltTapDetector {
public:
    explicit AltTapDetector(const InputState& input) : input_(input) {}

    // Which Alt keys to watch. Other Alt keys always pass through.
    void SetWatchedKeys(bool left, bool right);
    // Time from a tap's key-up to the next key-down of the same key that counts
    // as a double tap. 0 turns double taps off.
    void SetDoubleTapMs(std::uint32_t ms);

    Decision OnKey(const KeyEvent& event);
    // A click or wheel turns the held Alt key into a combination (e.g. Alt+click).
    void OnMouseInput();
    // The caller could not replay a Tap and let the real key-up through, so it
    // was a plain Alt press and must not arm a double tap.
    void OnTapNotReplayed();

    // Whether an Alt key that may become a tap is held, i.e. mouse input matters.
    bool AltHeld() const { return candidate_ != 0; }

private:
    bool IsWatched(std::uint32_t vk) const;
    bool NoOtherKeysPressed();
    bool AnyMouseButtonDown() const;

    const InputState& input_;
    std::atomic<bool> leftWatched_ = false;
    std::atomic<bool> rightWatched_ = false;
    std::atomic<std::uint32_t> doubleTapMs_ = 0;

    std::bitset<256> pressed_;
    std::uint32_t candidate_ = 0;  // VK of the Alt key that may become a tap, or 0
    std::uint32_t lastTapVk_ = 0;  // The last tap, so a quick second press can undo it
    std::uint32_t lastTapTime_ = 0;
};

}  // namespace outatime
