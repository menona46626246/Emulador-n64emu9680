#include "n64/cart/header.hpp"

#include "n64/common/log.hpp"

#include <cstring>

namespace n64 {

u32 crc32_bytes(std::span<const u8> data) {
    u32 crc = 0xFFFF'FFFFu;
    for (u8 b : data) {
        crc ^= b;
        for (int i = 0; i < 8; ++i) {
            const u32 mask = static_cast<u32>(-(static_cast<s32>(crc & 1u)));
            crc = (crc >> 1) ^ (0xEDB8'8320u & mask);
        }
    }
    return ~crc;
}

namespace {

// Well-known IPL3 CRC32 → CIC (public emulator-community databases).
CicType cic_from_ipl3_crc(u32 crc) {
    switch (crc) {
    case 0x6170A4A1u: return CicType::Cic6101;
    case 0x90BB6CB5u: return CicType::Cic6102;
    case 0x0B050EE0u: return CicType::Cic6102;
    case 0x98BC2C86u: return CicType::Cic6103;
    case 0xACC8580Au: return CicType::Cic6105;
    case 0x0E018FFFu: return CicType::Cic6106;
    case 0xA536C0F0u: return CicType::Cic6106;
    case 0xC2A167C6u: return CicType::Cic5101;
    // libdragon / open IPL3 fingerprints (may evolve)
    case 0x009E9EA3u: return CicType::CicX103;
    default:
        return CicType::Unknown;
    }
}

} // namespace

u8 cic_seed(CicType cic) noexcept {
    switch (cic) {
    case CicType::Cic6101:
    case CicType::Cic6102:
        return 0x3F;
    case CicType::Cic6103:
    case CicType::CicX103:
        return 0x78;
    case CicType::Cic6105:
    case CicType::CicX105:
        return 0x91;
    case CicType::Cic6106:
    case CicType::CicX106:
        return 0x85;
    case CicType::Cic5101:
        return 0xAC;
    default:
        return 0x3F;
    }
}

u32 fixup_entrypoint(CicType cic, u32 raw_pc) noexcept {
    switch (cic) {
    case CicType::Cic6103:
    case CicType::CicX103:
    case CicType::Cic5101:
        return raw_pc - 0x0010'0000u;
    case CicType::Cic6106:
    case CicType::CicX106:
        return raw_pc - 0x0020'0000u;
    default:
        return raw_pc;
    }
}

TvType tv_from_country(char country) noexcept {
    // Public region codes used in cart headers / libultra osTvType.
    switch (country) {
    case 'E': // North America
    case 'J': // Japan
    case 'U': // occasional homebrew "USA"
    case 0x37: // '7' — beta / some dumps
        return TvType::NTSC;
    case 'P': // Europe (generic PAL)
    case 'D': // Germany
    case 'F': // France
    case 'I': // Italy
    case 'S': // Spain
    case 'H': // Netherlands / Holland
    case 'X': // PAL alternate
    case 'Y': // PAL alternate
    case 'W': // Nordic
        return TvType::PAL;
    case 'B': // Brazil (often MPAL)
    case 'N': // Canada sometimes
        return TvType::MPAL;
    default:
        return TvType::NTSC;
    }
}

CicType detect_cic(std::span<const u8> rom) {
    if (rom.size() < 0x1000) {
        N64_DEBUG("ROM < 4 KiB — defaulting CIC to 6102 (homebrew stub)");
        return CicType::Cic6102;
    }

    const auto ipl3 = rom.subspan(0x40, 0x1000 - 0x40);
    const u32 crc = crc32_bytes(ipl3);
    const CicType known = cic_from_ipl3_crc(crc);
    if (known != CicType::Unknown) {
        N64_DEBUG("IPL3 CRC32={:08X} → CIC {}", crc, cic_name(known));
        return known;
    }

    std::size_t nonzero = 0;
    for (u8 b : ipl3) {
        if (b != 0) {
            ++nonzero;
        }
    }
    if (nonzero < 64) {
        N64_DEBUG("Sparse IPL3 ({} nonzero, crc={:08X}) → CIC 6102", nonzero, crc);
        return CicType::Cic6102;
    }

    N64_INFO("Unknown IPL3 CRC32={:08X} — defaulting to CIC 6102", crc);
    return CicType::Cic6102;
}

CartHeader parse_cart_header(std::span<const u8> rom) {
    CartHeader h{};
    if (rom.size() < 0x40) {
        N64_ERROR("ROM too small for header ({} bytes)", rom.size());
        return h;
    }

    h.pi_bsd_dom1 = be_u32(rom, 0x00);
    h.clock_rate  = be_u32(rom, 0x04);
    h.pc          = be_u32(rom, 0x08);
    h.release     = be_u32(rom, 0x0C);
    h.crc1        = be_u32(rom, 0x10);
    h.crc2        = be_u32(rom, 0x14);

    std::memset(h.name.data(), 0, h.name.size());
    for (int i = 0; i < 20; ++i) {
        h.name[static_cast<std::size_t>(i)] =
            static_cast<char>(rom[0x20u + static_cast<std::size_t>(i)]);
    }

    h.media = static_cast<char>(rom[0x38]);
    h.cart_id[0] = static_cast<char>(rom[0x3C]);
    h.cart_id[1] = static_cast<char>(rom[0x3D]);
    h.country = static_cast<char>(rom[0x3E]);
    h.version = rom[0x3F];

    h.tv = tv_from_country(h.country);
    h.cic = detect_cic(rom);
    h.cic_seed = cic_seed(h.cic);
    h.entrypoint = fixup_entrypoint(h.cic, h.pc);

    if (rom.size() >= 0x1000) {
        h.ipl3_crc32 = crc32_bytes(rom.subspan(0x40, 0x1000 - 0x40));
    }

    if (h.entrypoint == 0) {
        h.entrypoint = 0x8000'0400u;
        N64_WARN("Header PC is 0 — defaulting entrypoint to {:08X}", h.entrypoint);
    }

    return h;
}

} // namespace n64
