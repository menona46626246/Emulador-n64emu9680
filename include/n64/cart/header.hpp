#pragma once

#include "n64/common/types.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace n64 {

/// CIC / IPL3 family used to select entrypoint fixups and boot seeds.
enum class CicType : u8 {
    Unknown = 0,
    Cic6101, // / 7102
    Cic6102, // / 7101 — most titles + typical homebrew
    Cic6103, // / 7103
    Cic6105, // / 7105
    Cic6106, // / 7106
    Cic5101, // Aleck64
    CicX103, // libdragon / modern open IPL3 (x103)
    CicX105,
    CicX106,
};

/// TV system inferred from cart country code (osTvType values).
enum class TvType : u8 {
    PAL  = 0,
    NTSC = 1,
    MPAL = 2,
};

[[nodiscard]] constexpr std::string_view cic_name(CicType t) noexcept {
    switch (t) {
    case CicType::Cic6101: return "6101";
    case CicType::Cic6102: return "6102";
    case CicType::Cic6103: return "6103";
    case CicType::Cic6105: return "6105";
    case CicType::Cic6106: return "6106";
    case CicType::Cic5101: return "5101";
    case CicType::CicX103: return "x103";
    case CicType::CicX105: return "x105";
    case CicType::CicX106: return "x106";
    default:               return "unknown";
    }
}

[[nodiscard]] constexpr std::string_view tv_name(TvType t) noexcept {
    switch (t) {
    case TvType::PAL:  return "PAL";
    case TvType::NTSC: return "NTSC";
    case TvType::MPAL: return "MPAL";
    default:           return "NTSC";
    }
}

/// Parsed 64-byte cartridge header (big-endian fields).
struct CartHeader {
    u32 pi_bsd_dom1 = 0;   // +0x00 initial PI config / magic
    u32 clock_rate  = 0;   // +0x04
    u32 pc          = 0;   // +0x08 entrypoint (before CIC fixup)
    u32 release     = 0;   // +0x0C
    u32 crc1        = 0;   // +0x10
    u32 crc2        = 0;   // +0x14
    std::array<char, 21> name{}; // +0x20 (20 chars + NUL)
    char media      = 0;   // +0x38
    char cart_id[2]{};     // +0x3C
    char country    = 0;   // +0x3E
    u8   version    = 0;   // +0x3F

    CicType cic = CicType::Unknown;
    TvType  tv  = TvType::NTSC;
    u32 entrypoint = 0;    // after CIC fixup (virtual address)
    u8  cic_seed   = 0x3F; // seed placed in PIF RAM / gpr22 low
    u32 ipl3_crc32 = 0;    // CRC of cart[0x40..0x1000)

    [[nodiscard]] bool valid_magic() const noexcept {
        // Top byte of PI word is 0x80 for big-endian .z64
        return (pi_bsd_dom1 & 0xFF00'0000u) == 0x8000'0000u;
    }

    [[nodiscard]] std::string_view title() const noexcept {
        std::size_t len = 0;
        while (len < 20 && name[len] != '\0') {
            ++len;
        }
        while (len > 0 && (name[len - 1] == ' ' || name[len - 1] == '\0')) {
            --len;
        }
        return std::string_view(name.data(), len);
    }

    [[nodiscard]] std::string cart_id_str() const {
        char buf[3] = {cart_id[0], cart_id[1], 0};
        return std::string(buf);
    }
};

/// Read big-endian u32 from a byte span.
[[nodiscard]] inline u32 be_u32(std::span<const u8> s, std::size_t off) noexcept {
    if (off + 4 > s.size()) {
        return 0;
    }
    return (static_cast<u32>(s[off]) << 24) |
           (static_cast<u32>(s[off + 1]) << 16) |
           (static_cast<u32>(s[off + 2]) << 8) |
           (static_cast<u32>(s[off + 3]));
}

/// Parse header + detect CIC from IPL3 region (0x40–0x1000).
[[nodiscard]] CartHeader parse_cart_header(std::span<const u8> rom);

/// Detect CIC type from IPL3 bytes.
[[nodiscard]] CicType detect_cic(std::span<const u8> rom);

/// Map cart country code byte → TV type (public region tables).
[[nodiscard]] TvType tv_from_country(char country) noexcept;

/// Apply CIC-specific entrypoint fixup to the raw header PC.
[[nodiscard]] u32 fixup_entrypoint(CicType cic, u32 raw_pc) noexcept;

/// CIC seed byte used by IPL2/PIF (public tables).
[[nodiscard]] u8 cic_seed(CicType cic) noexcept;

/// CRC32 of a byte span (ISO polynomial) — used for IPL3 fingerprinting.
[[nodiscard]] u32 crc32_bytes(std::span<const u8> data);

} // namespace n64
