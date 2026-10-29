#pragma once

#include <atomic>
#include <bitset>
#include <cstdint>

#include "tap_key.hpp"

namespace outatime {

enum class KeySource {
    Physical,  // Typed on a keyboard
    Injected,  // Sent by another program, e.g. a remote desktop tool or AutoHotkey
    Ours,      // Our own replayed keys
};

struct KeyEvent {
    std::uint32_t vk;
    std::uint32_t scanCode;
    bool down;
    KeySource source;
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
    DoubleTap,    // This key-down repeats a tap: let it through as a plain key, report it.
};

// Decides which presses of the tap keys (Alt and Windows) are taps and double
// taps, from low-level input events.
//
// A tap is a watched key released within kTapTimeoutMs with no other key pressed
// and no mouse button used meanwhile. Keys already held when it goes down are
// fine if they are let go first (fast typing often overlaps), except modifiers.
// It has no Win32 dependencies so it can be unit tested.
//
// OnKey, OnMouseInput and OnTapNotReplayed must be called from one thread;
// the setters may be called from any thread.
class TapDetector {
public:
    // Holding a key longer than this and letting go is not a tap, as when the user
    // changes their mind. Same as Karabiner-Elements' to_if_alone default.
    static constexpr std::uint32_t kTapTimeoutMs = 1000;

    explicit TapDetector(const InputState& input) : input_(input) {}

    // Which tap keys to watch. The others always pass through.
    void SetWatchedKeys(TapKeySet keys);
    // Time from a tap's key-up to the next key-down of the same key that counts
    // as a double tap. 0 turns double taps off.
    void SetDoubleTapMs(std::uint32_t ms);
    // Whether keys sent by other programs count like typed ones. Remote desktop
    // tools such as Parsec send all their keys this way.
    void SetAcceptInjected(bool accept);

    Decision OnKey(const KeyEvent& event);
    // A click or wheel turns the held key into a combination (e.g. Alt+click).
    void OnMouseInput();
    // The caller could not replay a Tap and let the real key-up through, so it
    // was a plain key press and must not arm a double tap.
    void OnTapNotReplayed();

    // Whether a key that may become a tap is held, i.e. mouse input matters.
    bool TapKeyHeld() const { return candidate_ != 0; }

private:
    bool IsWatched(std::uint32_t vk) const;
    void DropReleasedKeys();
    bool AnyModifierPressed() const;
    bool AnyMouseButtonDown() const;

    const InputState& input_;
    std::atomic<unsigned long> watched_ = 0;  // TapKeySet::to_ulong()
    std::atomic<std::uint32_t> doubleTapMs_ = 0;
    std::atomic<bool> acceptInjected_ = false;

    std::bitset<256> pressed_;
    std::uint32_t candidate_ = 0;      // VK of the key that may become a tap, or 0
    std::uint32_t candidateTime_ = 0;  // When it went down
    std::uint32_t lastTapVk_ = 0;      // The last tap, so a quick second press can undo it
    std::uint32_t lastTapTime_ = 0;
};

}  // namespace outatime
