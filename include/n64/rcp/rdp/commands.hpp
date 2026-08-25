#pragma once

/// Helpers to encode RDP 64-bit command words for tests and synthetic homebrew.

#include "n64/common/types.hpp"
#include "n64/rcp/rdp/rdp.hpp"

#include <span>
#include <utility>
#include <vector>

namespace n64::rdp_cmd {

[[nodiscard]] inline constexpr u64 pack(u32 op, u64 payload) noexcept {
    return (static_cast<u64>(op & 0x3Fu) << 56) | (payload & 0x00FF'FFFF'FFFF'FFFFull);
}

/// 10.2 fixed-point from integer pixel.
[[nodiscard]] inline constexpr u32 px(int v) noexcept {
    return static_cast<u32>(v) << 2;
}

/// 10.5 fixed-point from integer texel.
[[nodiscard]] inline constexpr u16 tex(int v) noexcept {
    return static_cast<u16>(static_cast<u32>(v) << 5);
}

/// 5.10 fixed-point slope from integer texels-per-pixel.
[[nodiscard]] inline constexpr u16 slope(int texels_per_pixel) noexcept {
    return static_cast<u16>(static_cast<u32>(texels_per_pixel) << 10);
}

[[nodiscard]] inline constexpr u64 set_color_image(u32 dram_addr, u32 width_px,
                                                    u8 size = 2, u8 format = 0) noexcept {
    const u64 w = (width_px > 0) ? (width_px - 1u) : 0u;
    const u64 payload =
        (static_cast<u64>(format & 7u) << 53) |
        (static_cast<u64>(size & 3u) << 51) |
        ((w & 0x3FFu) << 32) |
        (static_cast<u64>(dram_addr) & 0x03FF'FFFFull);
    return pack(RdpOp::SetColorImage, payload);
}

[[nodiscard]] inline constexpr u64 set_texture_image(u32 dram_addr, u32 width_px,
                                                      u8 size = 2, u8 format = 0) noexcept {
    const u64 w = (width_px > 0) ? (width_px - 1u) : 0u;
    const u64 payload =
        (static_cast<u64>(format & 7u) << 53) |
        (static_cast<u64>(size & 3u) << 51) |
        ((w & 0x3FFu) << 32) |
        (static_cast<u64>(dram_addr) & 0x03FF'FFFFull);
    return pack(RdpOp::SetTextureImage, payload);
}

[[nodiscard]] inline constexpr u64 set_scissor(int xh, int yh, int xl, int yl) noexcept {
    const u64 payload =
        (static_cast<u64>(px(xh) & 0xFFFu) << 44) |
        (static_cast<u64>(px(yh) & 0xFFFu) << 32) |
        (static_cast<u64>(px(xl) & 0xFFFu) << 12) |
        (static_cast<u64>(px(yl) & 0xFFFu));
    return pack(RdpOp::SetScissor, payload);
}

[[nodiscard]] inline constexpr u64 set_fill_color(u32 color) noexcept {
    return pack(RdpOp::SetFillColor, color);
}

[[nodiscard]] inline constexpr u64 set_blend_color(u32 color) noexcept {
    return pack(RdpOp::SetBlendColor, color);
}

[[nodiscard]] inline constexpr u32 fill_color_16(u16 pix) noexcept {
    return (static_cast<u32>(pix) << 16) | static_cast<u32>(pix);
}

[[nodiscard]] inline constexpr u64 fill_rectangle(int xh, int yh, int xl, int yl) noexcept {
    const u64 payload =
        (static_cast<u64>(px(xl) & 0xFFFu) << 44) |
        (static_cast<u64>(px(yl) & 0xFFFu) << 32) |
        (static_cast<u64>(px(xh) & 0xFFFu) << 12) |
        (static_cast<u64>(px(yh) & 0xFFFu));
    return pack(RdpOp::FillRectangle, payload);
}

[[nodiscard]] inline constexpr u64 set_other_modes_fill() noexcept {
    const u64 payload = (3ull << 52); // cycle type = fill
    return pack(RdpOp::SetOtherModes, payload);
}

[[nodiscard]] inline constexpr u64 set_other_modes_copy() noexcept {
    // cycle type = copy (2) — used with TexRect point sampling
    const u64 payload = (2ull << 52);
    return pack(RdpOp::SetOtherModes, payload);
}

[[nodiscard]] inline constexpr u64 set_tile(u8 tile, u8 size, u16 line_words, u16 tmem_word,
                                             u8 format = 0) noexcept {
    const u64 payload =
        (static_cast<u64>(format & 7u) << 53) |
        (static_cast<u64>(size & 3u) << 51) |
        ((static_cast<u64>(line_words) & 0x1FFull) << 41) |
        ((static_cast<u64>(tmem_word) & 0x1FFull) << 32) |
        ((static_cast<u64>(tile) & 7u) << 24);
    return pack(RdpOp::SetTile, payload);
}

[[nodiscard]] inline constexpr u64 set_tile_size(u8 tile, int sl, int tl, int sh, int th) noexcept {
    const u64 payload =
        (static_cast<u64>(px(sl) & 0xFFFu) << 44) |
        (static_cast<u64>(px(tl) & 0xFFFu) << 32) |
        ((static_cast<u64>(tile) & 7u) << 24) |
        (static_cast<u64>(px(sh) & 0xFFFu) << 12) |
        (static_cast<u64>(px(th) & 0xFFFu));
    return pack(RdpOp::SetTileSize, payload);
}

[[nodiscard]] inline constexpr u64 load_block(u8 tile, int sl, int tl, int sh,
                                               u16 dxt = 0) noexcept {
    const u64 payload =
        (static_cast<u64>(px(sl) & 0xFFFu) << 44) |
        (static_cast<u64>(px(tl) & 0xFFFu) << 32) |
        ((static_cast<u64>(tile) & 7u) << 24) |
        (static_cast<u64>(px(sh) & 0xFFFu) << 12) |
        (static_cast<u64>(dxt) & 0xFFFu);
    return pack(RdpOp::LoadBlock, payload);
}

[[nodiscard]] inline constexpr u64 load_tile(u8 tile, int sl, int tl, int sh, int th) noexcept {
    const u64 payload =
        (static_cast<u64>(px(sl) & 0xFFFu) << 44) |
        (static_cast<u64>(px(tl) & 0xFFFu) << 32) |
        ((static_cast<u64>(tile) & 7u) << 24) |
        (static_cast<u64>(px(sh) & 0xFFFu) << 12) |
        (static_cast<u64>(px(th) & 0xFFFu));
    return pack(RdpOp::LoadTile, payload);
}

/// Returns (hi, lo) 64-bit words for a TextureRectangle.
[[nodiscard]] inline constexpr std::pair<u64, u64>
texture_rectangle(int xh, int yh, int xl, int yl, u8 tile,
                  int s0, int t0, int dsdx_texels = 1, int dtdy_texels = 1) noexcept {
    const u64 hi_payload =
        (static_cast<u64>(px(xl) & 0xFFFu) << 44) |
        (static_cast<u64>(px(yl) & 0xFFFu) << 32) |
        ((static_cast<u64>(tile) & 7u) << 24) |
        (static_cast<u64>(px(xh) & 0xFFFu) << 12) |
        (static_cast<u64>(px(yh) & 0xFFFu));
    const u64 hi = pack(RdpOp::TextureRectangle, hi_payload);
    const u64 lo =
        (static_cast<u64>(tex(s0)) << 48) |
        (static_cast<u64>(tex(t0)) << 32) |
        (static_cast<u64>(slope(dsdx_texels)) << 16) |
        (static_cast<u64>(slope(dtdy_texels)));
    return {hi, lo};
}

[[nodiscard]] inline constexpr u64 sync_full() noexcept {
    return pack(RdpOp::SyncFull, 0);
}

[[nodiscard]] inline constexpr u64 sync_pipe() noexcept {
    return pack(RdpOp::SyncPipe, 0);
}

[[nodiscard]] inline constexpr u64 sync_load() noexcept {
    return pack(RdpOp::SyncLoad, 0);
}

[[nodiscard]] inline constexpr u64 noop() noexcept {
    return pack(RdpOp::NoOp, 0);
}

inline void store_be64(std::vector<u8>& mem, u32 offset, u64 cmd) {
    if (offset + 8 > mem.size()) {
        mem.resize(offset + 8);
    }
    for (int i = 0; i < 8; ++i) {
        mem[offset + static_cast<u32>(i)] =
            static_cast<u8>((cmd >> (56 - 8 * i)) & 0xFF);
    }
}

inline void store_be64_rdram(std::span<u8> rdram, u32 offset, u64 cmd) {
    for (int i = 0; i < 8; ++i) {
        if (offset + static_cast<u32>(i) < rdram.size()) {
            rdram[offset + static_cast<u32>(i)] =
                static_cast<u8>((cmd >> (56 - 8 * i)) & 0xFF);
        }
    }
}

} // namespace n64::rdp_cmd
