#pragma once

#include <cstdint>
#include <cstddef>

namespace n64 {

// Fixed-width aliases used across the emulator.
using u8  = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using s8  = std::int8_t;
using s16 = std::int16_t;
using s32 = std::int32_t;
using s64 = std::int64_t;

using Address = u32;
using PhysicalAddress = u32;
using Cycles = u64;

// N64 memory sizes (public documentation / well-known hardware constants).
inline constexpr std::size_t kRdramSize        = 4 * 1024 * 1024;      // 4 MiB base
inline constexpr std::size_t kRdramSizeExpanded = 8 * 1024 * 1024;     // 8 MiB with expansion pak
inline constexpr std::size_t kSpDmemSize       = 4 * 1024;             // 4 KiB
inline constexpr std::size_t kSpImemSize       = 4 * 1024;             // 4 KiB
inline constexpr std::size_t kPifRamSize       = 64;                   // 64 bytes
inline constexpr std::size_t kPifRomSize       = 2048;                 // 2 KiB (IPL1/2 region)

// CPU clock (NTSC). Exact RCP ratio is refined in timing phase.
// INCÓGNITA: confirm exact VR4300 / RCP clock relationship with a timing experiment.
inline constexpr Cycles kCpuClockHz = 93'750'000; // ~93.75 MHz NTSC

// Native byte swap helpers for big-endian memory emulation
inline constexpr u16 bswap16(u16 v) noexcept {
    return static_cast<u16>((v << 8) | (v >> 8));
}

inline constexpr u32 bswap32(u32 v) noexcept {
    return (v << 24) | ((v & 0x0000FF00u) << 8) | ((v & 0x00FF0000u) >> 8) | (v >> 24);
}

inline constexpr u64 bswap64(u64 v) noexcept {
    return ((v & 0x00000000000000FFull) << 56) |
           ((v & 0x000000000000FF00ull) << 40) |
           ((v & 0x0000000000FF0000ull) << 24) |
           ((v & 0x00000000FF000000ull) << 8)  |
           ((v & 0x000000FF00000000ull) >> 8)  |
           ((v & 0x0000FF0000000000ull) >> 24) |
           ((v & 0x00FF000000000000ull) >> 40) |
           ((v & 0xFF00000000000000ull) >> 56);
}

} // namespace n64
