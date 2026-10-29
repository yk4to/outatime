#include "tap_detector.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <set>

namespace outatime {
namespace {

constexpr std::uint32_t kVkTab = 0x09;
constexpr std::uint32_t kVkA = 0x41;
constexpr std::uint32_t kVkF = 0x46;
constexpr std::uint32_t kVkX = 0x58;
constexpr std::uint32_t kVkLeftShift = 0xA0;
constexpr std::uint32_t kVkLeftCtrl = 0xA2;
constexpr std::uint32_t kVkLeftButton = 0x01;
constexpr std::uint32_t kVkMask = 0xE8;
constexpr std::uint32_t kAltGrFakeCtrlScanCode = 0x21D;

// Physical state the test controls. Keys from events are held unless released.
class FakeInputState final : public InputState {
public:
    bool IsDown(std::uint32_t vk) const override { return down.contains(vk); }
    std::set<std::uint32_t> down;
};

class TapDetectorTest : public testing::Test {
protected:
    TapDetectorTest() {
        detector.SetWatchedKeys(TapKeySet().set());
        detector.SetDoubleTapMs(200);
    }

    // Injected keys update the physical state too, as SendInput does.
    Decision Down(std::uint32_t vk, std::uint32_t time = 0,
                  KeySource source = KeySource::Physical) {
        input.down.insert(vk);
        return detector.OnKey(
            {.vk = vk, .scanCode = 0, .down = true, .source = source, .time = time});
    }

    Decision Up(std::uint32_t vk, std::uint32_t time = 0, KeySource source = KeySource::Physical) {
        input.down.erase(vk);
        return detector.OnKey(
            {.vk = vk, .scanCode = 0, .down = false, .source = source, .time = time});
    }

    Decision Tap(std::uint32_t vk, std::uint32_t downTime, std::uint32_t upTime) {
        EXPECT_EQ(Down(vk, downTime), Decision::PassThrough);
        return Up(vk, upTime);
    }

    FakeInputState input;
    TapDetector detector{input};
};

TEST_F(TapDetectorTest, AltAloneIsTap) {
    EXPECT_EQ(Down(kVkLeftAlt), Decision::PassThrough);
    EXPECT_TRUE(detector.TapKeyHeld());
    EXPECT_EQ(Up(kVkLeftAlt), Decision::Tap);
    EXPECT_FALSE(detector.TapKeyHeld());
}

TEST_F(TapDetectorTest, RightAltAloneIsTap) { EXPECT_EQ(Tap(kVkRightAlt, 0, 50), Decision::Tap); }

TEST_F(TapDetectorTest, WindowsKeyAloneIsTap) {
    EXPECT_EQ(Tap(kVkLeftWin, 0, 50), Decision::Tap);
    EXPECT_EQ(Tap(kVkRightWin, 1000, 1050), Decision::Tap);
}

TEST_F(TapDetectorTest, KeyDuringWindowsKeyIsCombination) {
    // E.g. Win+E.
    Down(kVkLeftWin);
    EXPECT_EQ(Down(kVkF), Decision::PassThrough);
    EXPECT_FALSE(detector.TapKeyHeld());
    Up(kVkF);
    EXPECT_EQ(Up(kVkLeftWin), Decision::PassThrough);
}

TEST_F(TapDetectorTest, TapKeysAreWatchedSeparately) {
    detector.SetWatchedKeys(TapKeySet().set(Index(TapKey::RightWin)));
    for (const std::uint32_t vk : {kVkLeftAlt, kVkRightAlt, kVkLeftWin}) {
        EXPECT_EQ(Tap(vk, 0, 50), Decision::PassThrough) << "vk " << vk;
    }
    EXPECT_EQ(Tap(kVkRightWin, 0, 50), Decision::Tap);
}

TEST_F(TapDetectorTest, AutoRepeatKeepsTap) {
    Down(kVkLeftAlt, 0);
    EXPECT_EQ(Down(kVkLeftAlt, 500), Decision::PassThrough);
    EXPECT_EQ(Down(kVkLeftAlt, 530), Decision::PassThrough);
    EXPECT_EQ(Up(kVkLeftAlt, 600), Decision::Tap);
}

TEST_F(TapDetectorTest, KeyDuringAltIsCombination) {
    Down(kVkLeftAlt);
    EXPECT_EQ(Down(kVkTab), Decision::PassThrough);
    EXPECT_FALSE(detector.TapKeyHeld());
    Up(kVkTab);
    EXPECT_EQ(Up(kVkLeftAlt), Decision::PassThrough);
}

TEST_F(TapDetectorTest, ModifierHeldBeforeAltIsCombination) {
    Down(kVkLeftCtrl);
    EXPECT_EQ(Down(kVkLeftAlt), Decision::PassThrough);
    EXPECT_FALSE(detector.TapKeyHeld());
    EXPECT_EQ(Up(kVkLeftAlt), Decision::PassThrough);
}

TEST_F(TapDetectorTest, ModifierLetGoDuringAltIsStillCombination) {
    // E.g. Shift+Alt, the default input language hotkey, released in either order.
    for (const std::uint32_t modifier : {kVkLeftShift, kVkLeftCtrl, kVkLeftWin, kVkRightAlt}) {
        Down(modifier);
        Down(kVkLeftAlt);
        Up(modifier);
        EXPECT_EQ(Up(kVkLeftAlt), Decision::PassThrough) << "modifier " << modifier;
    }
}

// Fast typing: the last letter is still held when Alt goes down. Recorded from a
// real trace (times in ms), where this used to leave the IME unchanged.
TEST_F(TapDetectorTest, PreviousKeyLetGoDuringAltIsTap) {
    Down(kVkA, 40892109);
    EXPECT_EQ(Down(kVkLeftAlt, 40892187), Decision::PassThrough);
    EXPECT_TRUE(detector.TapKeyHeld());
    Up(kVkA, 40892203);
    EXPECT_TRUE(detector.TapKeyHeld());
    EXPECT_EQ(Up(kVkLeftAlt, 40892281), Decision::Tap);
}

TEST_F(TapDetectorTest, PreviousKeyStillHeldAtReleaseIsCombination) {
    Down(kVkA, 0);
    Down(kVkLeftAlt, 50);
    EXPECT_EQ(Up(kVkLeftAlt, 100), Decision::PassThrough);
}

TEST_F(TapDetectorTest, PreviousKeyRepeatingDuringAltIsCombination) {
    // The app sees Alt+A from the repeat.
    Down(kVkA, 0);
    Down(kVkLeftAlt, 400);
    EXPECT_EQ(Down(kVkA, 500), Decision::PassThrough);
    EXPECT_FALSE(detector.TapKeyHeld());
    Up(kVkA, 530);
    EXPECT_EQ(Up(kVkLeftAlt, 600), Decision::PassThrough);
}

TEST_F(TapDetectorTest, NewKeyAfterPreviousKeyLetGoIsCombination) {
    Down(kVkA, 0);
    Down(kVkLeftAlt, 50);
    Up(kVkA, 60);
    EXPECT_EQ(Down(kVkF, 80), Decision::PassThrough);
    Up(kVkF, 90);
    EXPECT_EQ(Up(kVkLeftAlt, 100), Decision::PassThrough);
}

TEST_F(TapDetectorTest, HeldUpToTimeoutIsTap) {
    EXPECT_EQ(Tap(kVkLeftAlt, 0, TapDetector::kTapTimeoutMs), Decision::Tap);
}

TEST_F(TapDetectorTest, HeldPastTimeoutIsNotTap) {
    EXPECT_EQ(Tap(kVkLeftAlt, 0, TapDetector::kTapTimeoutMs + 1), Decision::PassThrough);
    // Nor does it arm a double tap.
    EXPECT_EQ(Down(kVkLeftAlt, TapDetector::kTapTimeoutMs + 50), Decision::PassThrough);
}

TEST_F(TapDetectorTest, MissedModifierKeyUpIsRecovered) {
    Down(kVkLeftCtrl);
    input.down.erase(kVkLeftCtrl);
    EXPECT_EQ(Tap(kVkLeftAlt, 0, 50), Decision::Tap);
}

TEST_F(TapDetectorTest, UnwatchedKeyPassesThrough) {
    detector.SetWatchedKeys(TapKeySet().set(Index(TapKey::LeftAlt)));
    EXPECT_EQ(Down(kVkRightAlt), Decision::PassThrough);
    EXPECT_FALSE(detector.TapKeyHeld());
    EXPECT_EQ(Up(kVkRightAlt), Decision::PassThrough);
}

TEST_F(TapDetectorTest, NothingWatchedWhenPaused) {
    detector.SetWatchedKeys(TapKeySet());
    EXPECT_EQ(Tap(kVkLeftAlt, 0, 50), Decision::PassThrough);
    EXPECT_EQ(Tap(kVkLeftWin, 1000, 1050), Decision::PassThrough);
}

TEST_F(TapDetectorTest, MissedKeyUpIsRecovered) {
    // X went down, but its key-up never reached the hook (e.g. on the secure desktop).
    Down(kVkX);
    input.down.erase(kVkX);
    EXPECT_EQ(Tap(kVkLeftAlt, 0, 50), Decision::Tap);
}

TEST_F(TapDetectorTest, MouseInputDuringAltIsCombination) {
    Down(kVkLeftAlt);
    detector.OnMouseInput();
    EXPECT_FALSE(detector.TapKeyHeld());
    EXPECT_EQ(Up(kVkLeftAlt), Decision::PassThrough);
}

TEST_F(TapDetectorTest, MouseButtonHeldBeforeAltIsCombination) {
    input.down.insert(kVkLeftButton);
    EXPECT_EQ(Down(kVkLeftAlt), Decision::PassThrough);
    EXPECT_FALSE(detector.TapKeyHeld());
    EXPECT_EQ(Up(kVkLeftAlt), Decision::PassThrough);
}

TEST_F(TapDetectorTest, MouseButtonHeldAtReleaseIsCombination) {
    Down(kVkLeftAlt);
    input.down.insert(kVkLeftButton);
    EXPECT_EQ(Up(kVkLeftAlt), Decision::PassThrough);
}

TEST_F(TapDetectorTest, OwnKeysAreIgnored) {
    // Our own mask key must not turn a tap into a combination, whatever the setting.
    for (const bool accept : {false, true}) {
        detector.SetAcceptInjected(accept);
        // Far enough apart that the second round is not a double tap.
        const std::uint32_t t = accept ? 10000 : 0;
        Down(kVkLeftAlt, t);
        EXPECT_EQ(Down(kVkMask, t, KeySource::Ours), Decision::PassThrough);
        EXPECT_EQ(Up(kVkMask, t, KeySource::Ours), Decision::PassThrough);
        EXPECT_EQ(Up(kVkLeftAlt, t), Decision::Tap) << "accept=" << accept;
        // Nor does our own replayed Alt key-up count.
        EXPECT_EQ(Up(kVkLeftAlt, t, KeySource::Ours), Decision::PassThrough);
    }
}

TEST_F(TapDetectorTest, InjectedKeysAreIgnoredByDefault) {
    // Other programs' keys must not turn a tap into a combination.
    Down(kVkLeftAlt);
    EXPECT_EQ(Down(kVkA, 0, KeySource::Injected), Decision::PassThrough);
    EXPECT_EQ(Up(kVkA, 0, KeySource::Injected), Decision::PassThrough);
    EXPECT_EQ(Up(kVkLeftAlt), Decision::Tap);

    // Nor does an injected Alt count as a tap.
    EXPECT_EQ(Down(kVkLeftAlt, 0, KeySource::Injected), Decision::PassThrough);
    EXPECT_FALSE(detector.TapKeyHeld());
    EXPECT_EQ(Up(kVkLeftAlt, 0, KeySource::Injected), Decision::PassThrough);
}

TEST_F(TapDetectorTest, InjectedKeysCountWhenAccepted) {
    // E.g. Parsec, which sends everything typed on the remote keyboard this way.
    detector.SetAcceptInjected(true);
    EXPECT_EQ(Down(kVkLeftAlt, 0, KeySource::Injected), Decision::PassThrough);
    EXPECT_TRUE(detector.TapKeyHeld());
    EXPECT_EQ(Up(kVkLeftAlt, 50, KeySource::Injected), Decision::Tap);

    // And their other keys make a combination.
    Down(kVkLeftAlt, 1000, KeySource::Injected);
    Down(kVkTab, 1050, KeySource::Injected);
    Up(kVkTab, 1100, KeySource::Injected);
    EXPECT_EQ(Up(kVkLeftAlt, 1150, KeySource::Injected), Decision::PassThrough);
}

TEST_F(TapDetectorTest, AltGrFakeCtrlIsIgnored) {
    const KeyEvent fakeCtrlDown{.vk = kVkLeftCtrl,
                                .scanCode = kAltGrFakeCtrlScanCode,
                                .down = true,
                                .source = KeySource::Physical,
                                .time = 0};
    const KeyEvent fakeCtrlUp{.vk = kVkLeftCtrl,
                              .scanCode = kAltGrFakeCtrlScanCode,
                              .down = false,
                              .source = KeySource::Physical,
                              .time = 0};
    detector.OnKey(fakeCtrlDown);
    Down(kVkRightAlt);
    detector.OnKey(fakeCtrlUp);
    EXPECT_EQ(Up(kVkRightAlt), Decision::Tap);
}

TEST_F(TapDetectorTest, QuickSecondPressIsDoubleTap) {
    EXPECT_EQ(Tap(kVkLeftAlt, 0, 50), Decision::Tap);
    EXPECT_EQ(Down(kVkLeftAlt, 250), Decision::DoubleTap);
    EXPECT_FALSE(detector.TapKeyHeld());
    // The second press is a plain Alt: its release is not a tap.
    EXPECT_EQ(Up(kVkLeftAlt, 300), Decision::PassThrough);
}

TEST_F(TapDetectorTest, DoubleTapThenKeyIsPlainAltCombination) {
    Tap(kVkLeftAlt, 0, 50);
    EXPECT_EQ(Down(kVkLeftAlt, 100), Decision::DoubleTap);
    EXPECT_EQ(Down(kVkF, 150), Decision::PassThrough);
    Up(kVkF, 200);
    EXPECT_EQ(Up(kVkLeftAlt, 250), Decision::PassThrough);
}

TEST_F(TapDetectorTest, SlowSecondPressIsAnotherTap) {
    Tap(kVkLeftAlt, 0, 50);
    EXPECT_EQ(Down(kVkLeftAlt, 251), Decision::PassThrough);
    EXPECT_EQ(Up(kVkLeftAlt, 300), Decision::Tap);
}

TEST_F(TapDetectorTest, ThirdQuickPressIsATapAgain) {
    Tap(kVkLeftAlt, 0, 50);
    Down(kVkLeftAlt, 100);
    Up(kVkLeftAlt, 150);
    EXPECT_EQ(Down(kVkLeftAlt, 200), Decision::PassThrough);
    EXPECT_EQ(Up(kVkLeftAlt, 250), Decision::Tap);
}

TEST_F(TapDetectorTest, KeyBetweenTapsCancelsDoubleTap) {
    Tap(kVkLeftAlt, 0, 50);
    Down(kVkA, 60);
    Up(kVkA, 70);
    EXPECT_EQ(Down(kVkLeftAlt, 100), Decision::PassThrough);
    EXPECT_EQ(Up(kVkLeftAlt, 150), Decision::Tap);
}

TEST_F(TapDetectorTest, OtherAltKeyIsNotDoubleTap) {
    Tap(kVkLeftAlt, 0, 50);
    EXPECT_EQ(Down(kVkRightAlt, 100), Decision::PassThrough);
    EXPECT_EQ(Up(kVkRightAlt, 150), Decision::Tap);
}

TEST_F(TapDetectorTest, ZeroDoubleTapTimeTurnsDoubleTapOff) {
    detector.SetDoubleTapMs(0);
    Tap(kVkLeftAlt, 0, 50);
    // Even a press with the same timestamp as the tap's release.
    EXPECT_EQ(Down(kVkLeftAlt, 50), Decision::PassThrough);
    EXPECT_EQ(Up(kVkLeftAlt, 60), Decision::Tap);
}

TEST_F(TapDetectorTest, TapNotReplayedDoesNotArmDoubleTap) {
    Tap(kVkLeftAlt, 0, 50);
    detector.OnTapNotReplayed();
    EXPECT_EQ(Down(kVkLeftAlt, 100), Decision::PassThrough);
    EXPECT_EQ(Up(kVkLeftAlt, 150), Decision::Tap);
}

TEST_F(TapDetectorTest, WindowsKeyDoubleTap) {
    // The second press is a plain Windows key, e.g. to open the Start menu.
    EXPECT_EQ(Tap(kVkLeftWin, 0, 50), Decision::Tap);
    EXPECT_EQ(Down(kVkLeftWin, 150), Decision::DoubleTap);
    EXPECT_EQ(Up(kVkLeftWin, 200), Decision::PassThrough);
}

TEST_F(TapDetectorTest, DoubleTapNeedsWatchedKey) {
    Tap(kVkLeftAlt, 0, 50);
    detector.SetWatchedKeys(TapKeySet());
    EXPECT_EQ(Down(kVkLeftAlt, 100), Decision::PassThrough);
}

TEST_F(TapDetectorTest, DoubleTapAcrossTimerWraparound) {
    Tap(kVkLeftAlt, 0xFFFFFF00, 0xFFFFFFF0);
    EXPECT_EQ(Down(kVkLeftAlt, 0x00000010), Decision::DoubleTap);
}

}  // namespace
}  // namespace outatime
