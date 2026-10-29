#pragma once

#include <bitset>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace outatime {

// Virtual-key codes, so this header does not need <windows.h>.
inline constexpr std::uint32_t kVkLeftAlt = 0xA4;   // VK_LMENU
inline constexpr std::uint32_t kVkRightAlt = 0xA5;  // VK_RMENU
inline constexpr std::uint32_t kVkLeftWin = 0x5B;   // VK_LWIN
inline constexpr std::uint32_t kVkRightWin = 0x5C;  // VK_RWIN

// The keys whose taps can switch the IME. Used as an index.
enum class TapKey : std::uint8_t {
    LeftAlt,
    RightAlt,
    LeftWin,
    RightWin,
};
inline constexpr std::size_t kTapKeyCount = 4;

// A set of tap keys, indexed by TapKey.
using TapKeySet = std::bitset<kTapKeyCount>;

constexpr std::size_t Index(TapKey key) { return static_cast<std::size_t>(key); }

constexpr std::uint32_t VirtualKey(TapKey key) {
    switch (key) {
    case TapKey::LeftAlt:
        return kVkLeftAlt;
    case TapKey::RightAlt:
        return kVkRightAlt;
    case TapKey::LeftWin:
        return kVkLeftWin;
    case TapKey::RightWin:
        return kVkRightWin;
    }
    return 0;
}

constexpr std::optional<TapKey> TapKeyFromVirtualKey(std::uint32_t vk) {
    switch (vk) {
    case kVkLeftAlt:
        return TapKey::LeftAlt;
    case kVkRightAlt:
        return TapKey::RightAlt;
    case kVkLeftWin:
        return TapKey::LeftWin;
    case kVkRightWin:
        return TapKey::RightWin;
    default:
        return std::nullopt;
    }
}

}  // namespace outatime
