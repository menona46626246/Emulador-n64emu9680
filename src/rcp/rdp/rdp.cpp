#include "n64/rcp/rdp/rdp.hpp"

#include "n64/bus/bus.hpp"
#include "n64/bus/mi.hpp"
#include "n64/bus/mmio.hpp"
#include "n64/common/log.hpp"

#include <algorithm>
#include <cstring>

namespace n64 {

void Rdp::reset() {
    color_image_ = {};
    texture_image_ = {};
    scissor_ = {};
    scissor_.xl = 320;
    scissor_.yl = 240;
    for (auto& t : tiles_) {
        t = {};
    }
    tmem_.fill(0);
    fill_color_ = 0;
    blend_color_ = 0;
    other_modes_ = 0;
    busy_ = false;
    cmds_executed_ = 0;
    fill_pixels_ = 0;
    tex_pixels_ = 0;
    N64_DEBUG("RDP reset");
}

void Rdp::flush() {
    busy_ = false;
}

u32 Rdp::read_cmd_word(u32 addr, bool xbus_dmem) const {
    if (!bus_) {
        return 0;
    }
    if (xbus_dmem) {
        return bus_->read32(0x0400'0000u + (addr & 0xFFFu));
    }
    return bus_->read32(addr & 0x00FF'FFFFu);
}

u64 Rdp::read_cmd64(u32 addr, bool xbus_dmem) const {
    const u64 hi = read_cmd_word(addr, xbus_dmem);
    const u64 lo = read_cmd_word(addr + 4, xbus_dmem);
    return (hi << 32) | lo;
}

u8 Rdp::read_rdram_u8(u32 addr) const {
    if (!bus_) {
        return 0;
    }
    auto rdram = bus_->rdram();
    if (rdram.empty()) {
        return 0;
    }
    return rdram[addr % rdram.size()];
}

void Rdp::copy_rdram_to_tmem(u32 dram, u32 tmem_byte, u32 nbytes) {
    if (!bus_ || nbytes == 0) {
        return;
    }
    auto rdram = bus_->rdram();
    if (rdram.empty()) {
        return;
    }
    for (u32 i = 0; i < nbytes; ++i) {
        const u32 dst = (tmem_byte + i) % kTmemBytes;
        tmem_[dst] = rdram[(dram + i) % rdram.size()];
    }
}

void Rdp::run_command_list(u32 start, u32 end, bool xbus_dmem) {
    if (!bus_ || end <= start) {
        return;
    }
    busy_ = true;

    u32 pc = start & ~0x7u;
    const u32 stop = end & ~0x7u;

    while (pc < stop) {
        const u64 cmd = read_cmd64(pc, xbus_dmem);
        const u32 op = opcode(cmd);

        if (op == RdpOp::TextureRectangle || op == RdpOp::TextureRectangleFlip) {
            if (pc + 16 > stop) {
                N64_WARN("RDP TexRect truncated at {:08X}", pc);
                break;
            }
            const u64 cmd2 = read_cmd64(pc + 8, xbus_dmem);
            execute_command_pair(cmd, cmd2);
            pc += 16;
            continue;
        }

        execute_command(cmd);
        pc += 8;
    }

    busy_ = false;
    N64_DEBUG("RDP ran list {:08X}..{:08X} (xbus={}) cmds={}",
              start, end, xbus_dmem, cmds_executed_);
}

void Rdp::execute_command_pair(u64 hi, u64 lo) {
    ++cmds_executed_;
    const u32 op = opcode(hi);
    if (op == RdpOp::TextureRectangle) {
        cmd_texture_rectangle(hi, lo, false);
    } else if (op == RdpOp::TextureRectangleFlip) {
        cmd_texture_rectangle(hi, lo, true);
    } else {
        N64_TRACE("RDP unexpected pair op={:02X}", op);
    }
}

void Rdp::execute_command(u64 cmd) {
    ++cmds_executed_;
    const u32 op = opcode(cmd);

    switch (op) {
    case RdpOp::NoOp:
    case RdpOp::SyncLoad:
    case RdpOp::SyncPipe:
    case RdpOp::SyncTile:
        break;
    case RdpOp::SyncFull:
        cmd_sync_full(cmd);
        break;
    case RdpOp::SetScissor:
        cmd_set_scissor(cmd);
        break;
    case RdpOp::SetOtherModes:
        cmd_set_other_modes(cmd);
        break;
    case RdpOp::FillRectangle:
        cmd_fill_rectangle(cmd);
        break;
    case RdpOp::SetFillColor:
        cmd_set_fill_color(cmd);
        break;
    case RdpOp::SetBlendColor:
        cmd_set_blend_color(cmd);
        break;
    case RdpOp::SetColorImage:
        cmd_set_color_image(cmd);
        break;
    case RdpOp::SetTextureImage:
        cmd_set_texture_image(cmd);
        break;
    case RdpOp::SetTile:
        cmd_set_tile(cmd);
        break;
    case RdpOp::SetTileSize:
        cmd_set_tile_size(cmd);
        break;
    case RdpOp::LoadBlock:
        cmd_load_block(cmd);
        break;
    case RdpOp::LoadTile:
        cmd_load_tile(cmd);
        break;
    case RdpOp::SetFogColor:
    case RdpOp::SetPrimColor:
    case RdpOp::SetEnvColor:
    case RdpOp::SetCombine:
    case RdpOp::SetMaskImage:
    case RdpOp::LoadTLut:
    case RdpOp::SetPrimDepth:
    case RdpOp::SetKeyGB:
    case RdpOp::SetKeyR:
    case RdpOp::SetConvert:
    case RdpOp::FillTriangle:
    case RdpOp::TextureTriangle:
        N64_TRACE("RDP stub op={:02X}", op);
        break;
    default:
        N64_DEBUG("RDP unknown op={:02X} cmd={:016X}", op, cmd);
        break;
    }
}

void Rdp::cmd_set_color_image(u64 cmd) {
    color_image_.format  = static_cast<u8>((cmd >> 53) & 0x7u);
    color_image_.size    = static_cast<u8>((cmd >> 51) & 0x3u);
    color_image_.width   = static_cast<u32>(((cmd >> 32) & 0x3FFu) + 1u);
    color_image_.address = static_cast<u32>(cmd & 0x03FF'FFFFu) & ~0x7u;
    color_image_.valid   = color_image_.width > 0;
    N64_DEBUG("RDP SetColorImage addr={:08X} w={} size={}",
              color_image_.address, color_image_.width, color_image_.size);
}

void Rdp::cmd_set_texture_image(u64 cmd) {
    // Same layout as SetColorImage.
    texture_image_.format  = static_cast<u8>((cmd >> 53) & 0x7u);
    texture_image_.size    = static_cast<u8>((cmd >> 51) & 0x3u);
    texture_image_.width   = static_cast<u32>(((cmd >> 32) & 0x3FFu) + 1u);
    texture_image_.address = static_cast<u32>(cmd & 0x03FF'FFFFu) & ~0x7u;
    texture_image_.valid   = texture_image_.width > 0;
    N64_DEBUG("RDP SetTextureImage addr={:08X} w={} size={}",
              texture_image_.address, texture_image_.width, texture_image_.size);
}

void Rdp::cmd_set_scissor(u64 cmd) {
    const u32 xh = static_cast<u32>((cmd >> 44) & 0xFFFu);
    const u32 yh = static_cast<u32>((cmd >> 32) & 0xFFFu);
    const u32 xl = static_cast<u32>((cmd >> 12) & 0xFFFu);
    const u32 yl = static_cast<u32>(cmd & 0xFFFu);
    scissor_.xh = fx10_2_floor(xh);
    scissor_.yh = fx10_2_floor(yh);
    scissor_.xl = fx10_2_floor(xl);
    scissor_.yl = fx10_2_floor(yl);
    scissor_.set = true;
    N64_DEBUG("RDP SetScissor ({},{})-({},{})",
              scissor_.xh, scissor_.yh, scissor_.xl, scissor_.yl);
}

void Rdp::cmd_set_fill_color(u64 cmd) {
    fill_color_ = static_cast<u32>(cmd & 0xFFFF'FFFFu);
}

void Rdp::cmd_set_blend_color(u64 cmd) {
    blend_color_ = static_cast<u32>(cmd & 0xFFFF'FFFFu);
}

void Rdp::cmd_set_other_modes(u64 cmd) {
    other_modes_ = cmd;
}

void Rdp::cmd_sync_full(u64 /*cmd*/) {
    if (mi_) {
        mi_->raise(mmio::MiIntr::DP);
    }
}

void Rdp::cmd_set_tile(u64 cmd) {
    // SetTile encoding (n64brew):
    //  [55:53] format [52:51] size [49:41] line [40:32] tmem
    //  [26:24] tile [23:20] palette
    //  [19] ct [18] mt [17:14] mask_t [13:10] shift_t
    //  [9] cs [8] ms [7:4] mask_s [3:0] shift_s
    const u32 tile_idx = static_cast<u32>((cmd >> 24) & 0x7u);
    auto& t = tiles_[tile_idx];
    t.format  = static_cast<u8>((cmd >> 53) & 0x7u);
    t.size    = static_cast<u8>((cmd >> 51) & 0x3u);
    t.line    = static_cast<u16>((cmd >> 41) & 0x1FFu);
    t.tmem    = static_cast<u16>((cmd >> 32) & 0x1FFu);
    t.palette = static_cast<u8>((cmd >> 20) & 0xFu);
    t.ct = static_cast<u8>((cmd >> 19) & 1u);
    t.mt = static_cast<u8>((cmd >> 18) & 1u);
    t.mask_t  = static_cast<u8>((cmd >> 14) & 0xFu);
    t.shift_t = static_cast<u8>((cmd >> 10) & 0xFu);
    t.cs = static_cast<u8>((cmd >> 9) & 1u);
    t.ms = static_cast<u8>((cmd >> 8) & 1u);
    t.mask_s  = static_cast<u8>((cmd >> 4) & 0xFu);
    t.shift_s = static_cast<u8>(cmd & 0xFu);
    t.defined = true;
    N64_DEBUG("RDP SetTile[{}] fmt={} size={} line={} tmem={}",
              tile_idx, t.format, t.size, t.line, t.tmem);
}

void Rdp::cmd_set_tile_size(u64 cmd) {
    // SetTileSize: sl:12 tl:12 tile:3 sh:12 th:12  (10.2)
    const u32 tile_idx = static_cast<u32>((cmd >> 24) & 0x7u);
    auto& t = tiles_[tile_idx];
    t.sl = static_cast<u16>((cmd >> 44) & 0xFFFu);
    t.tl = static_cast<u16>((cmd >> 32) & 0xFFFu);
    t.sh = static_cast<u16>((cmd >> 12) & 0xFFFu);
    t.th = static_cast<u16>(cmd & 0xFFFu);
    t.defined = true;
}

void Rdp::cmd_load_block(u64 cmd) {
    // LoadBlock: sl:12 tl:12 tile:3 sh:12 dxt:12
    // Loads consecutive texels from texture image into TMEM.
    if (!texture_image_.valid) {
        N64_WARN("RDP LoadBlock without SetTextureImage");
        return;
    }
    const u32 tile_idx = static_cast<u32>((cmd >> 24) & 0x7u);
    auto& t = tiles_[tile_idx];
    const u32 sl = static_cast<u32>((cmd >> 44) & 0xFFFu);
    const u32 sh = static_cast<u32>((cmd >> 12) & 0xFFFu);
    // sl/sh are 10.2; texel count along S.
    const u32 s0 = sl >> 2;
    const u32 s1 = sh >> 2;
    const u32 texels = (s1 >= s0) ? (s1 - s0 + 1) : 1;

    u32 bpp = 2;
    if (texture_image_.size == 3) bpp = 4;
    else if (texture_image_.size == 1) bpp = 1;
    else if (texture_image_.size == 0) bpp = 1; // 4bpp packed — treat as 1 for size

    const u32 nbytes = texels * bpp;
    const u32 dram = texture_image_.address + s0 * bpp;
    const u32 tmem_byte = static_cast<u32>(t.tmem) * 8u;
    copy_rdram_to_tmem(dram, tmem_byte, std::min(nbytes, static_cast<u32>(kTmemBytes)));
    N64_DEBUG("RDP LoadBlock tile={} texels={} bytes={} → tmem {:04X}",
              tile_idx, texels, nbytes, tmem_byte);
}

void Rdp::cmd_load_tile(u64 cmd) {
    // LoadTile: sl,tl,tile,sh,th — copy a rectangle from texture image to TMEM.
    if (!texture_image_.valid) {
        N64_WARN("RDP LoadTile without SetTextureImage");
        return;
    }
    const u32 tile_idx = static_cast<u32>((cmd >> 24) & 0x7u);
    auto& t = tiles_[tile_idx];
    const u32 sl = static_cast<u32>((cmd >> 44) & 0xFFFu) >> 2;
    const u32 tl = static_cast<u32>((cmd >> 32) & 0xFFFu) >> 2;
    const u32 sh = static_cast<u32>((cmd >> 12) & 0xFFFu) >> 2;
    const u32 th = static_cast<u32>(cmd & 0xFFFu) >> 2;

    t.sl = static_cast<u16>(sl << 2);
    t.tl = static_cast<u16>(tl << 2);
    t.sh = static_cast<u16>(sh << 2);
    t.th = static_cast<u16>(th << 2);

    u32 bpp = 2;
    if (texture_image_.size == 3) bpp = 4;
    else if (texture_image_.size <= 1) bpp = 1;

    const u32 tw = (sh >= sl) ? (sh - sl + 1) : 1;
    const u32 thh = (th >= tl) ? (th - tl + 1) : 1;
    // line in 64-bit words; if zero, derive from width.
    u32 line_bytes = tw * bpp;
    if (t.line != 0) {
        line_bytes = static_cast<u32>(t.line) * 8u;
    }

    u32 tmem_byte = static_cast<u32>(t.tmem) * 8u;
    for (u32 row = 0; row < thh; ++row) {
        const u32 dram = texture_image_.address +
                         (tl + row) * texture_image_.width * bpp + sl * bpp;
        const u32 row_bytes = tw * bpp;
        copy_rdram_to_tmem(dram, tmem_byte, row_bytes);
        tmem_byte += line_bytes;
    }
    N64_DEBUG("RDP LoadTile[{}] {}x{} from ({},{})", tile_idx, tw, thh, sl, tl);
}

bool Rdp::clip_point(int x, int y) const noexcept {
    if (x < 0 || y < 0) {
        return false;
    }
    if (scissor_.set) {
        if (x < scissor_.xh || x >= scissor_.xl ||
            y < scissor_.yh || y >= scissor_.yl) {
            return false;
        }
    }
    if (color_image_.valid && static_cast<u32>(x) >= color_image_.width) {
        return false;
    }
    return true;
}

void Rdp::write_color_pixel(int x, int y, u16 pix16, u32 pix32, bool use32) {
    if (!bus_ || !color_image_.valid || !clip_point(x, y)) {
        return;
    }
    auto rdram = bus_->rdram();
    if (rdram.empty()) {
        return;
    }

    if (color_image_.size == 2 && !use32) {
        const u32 addr = color_image_.address +
                         static_cast<u32>(y) * color_image_.width * 2u +
                         static_cast<u32>(x) * 2u;
        if (addr + 1 >= rdram.size()) return;
        rdram[addr]     = static_cast<u8>((pix16 >> 8) & 0xFF);
        rdram[addr + 1] = static_cast<u8>(pix16 & 0xFF);
    } else if (color_image_.size == 3 || use32) {
        // If FB is 16-bit but sample is 32-bit, downsample to 5551.
        if (color_image_.size == 2) {
            const u8 r8 = static_cast<u8>((pix32 >> 24) & 0xFF);
            const u8 g8 = static_cast<u8>((pix32 >> 16) & 0xFF);
            const u8 b8 = static_cast<u8>((pix32 >> 8) & 0xFF);
            const u8 a1 = (pix32 & 0xFF) ? 1u : 0u;
            const u16 p = static_cast<u16>(((r8 >> 3) << 11) | ((g8 >> 3) << 6) |
                                           ((b8 >> 3) << 1) | a1);
            const u32 addr = color_image_.address +
                             static_cast<u32>(y) * color_image_.width * 2u +
                             static_cast<u32>(x) * 2u;
            if (addr + 1 >= rdram.size()) return;
            rdram[addr]     = static_cast<u8>((p >> 8) & 0xFF);
            rdram[addr + 1] = static_cast<u8>(p & 0xFF);
        } else {
            const u32 addr = color_image_.address +
                             static_cast<u32>(y) * color_image_.width * 4u +
                             static_cast<u32>(x) * 4u;
            if (addr + 3 >= rdram.size()) return;
            rdram[addr]     = static_cast<u8>((pix32 >> 24) & 0xFF);
            rdram[addr + 1] = static_cast<u8>((pix32 >> 16) & 0xFF);
            rdram[addr + 2] = static_cast<u8>((pix32 >> 8) & 0xFF);
            rdram[addr + 3] = static_cast<u8>(pix32 & 0xFF);
        }
    }
}

void Rdp::write_fill_pixel(int x, int y) {
    if (!color_image_.valid || !clip_point(x, y)) {
        return;
    }
    if (color_image_.size == 2) {
        // In 16-bit fill mode the 32-bit fill word supplies two adjacent
        // pixels. Select its upper/lower half according to the destination
        // halfword within the word.
        const u32 addr = color_image_.address +
                         static_cast<u32>(y) * color_image_.width * 2u +
                         static_cast<u32>(x) * 2u;
        const u16 pix = (addr & 2u) != 0
                            ? static_cast<u16>(fill_color_ & 0xFFFFu)
                            : static_cast<u16>((fill_color_ >> 16) & 0xFFFFu);
        write_color_pixel(x, y, pix, 0, false);
        ++fill_pixels_;
    } else if (color_image_.size == 3) {
        write_color_pixel(x, y, 0, fill_color_, true);
        ++fill_pixels_;
    }
}

void Rdp::cmd_fill_rectangle(u64 cmd) {
    const u32 xl_fx = static_cast<u32>((cmd >> 44) & 0xFFFu);
    const u32 yl_fx = static_cast<u32>((cmd >> 32) & 0xFFFu);
    const u32 xh_fx = static_cast<u32>((cmd >> 12) & 0xFFFu);
    const u32 yh_fx = static_cast<u32>(cmd & 0xFFFu);

    int xh = fx10_2_floor(xh_fx);
    int yh = fx10_2_floor(yh_fx);
    int xl = fx10_2_floor(xl_fx);
    int yl = fx10_2_floor(yl_fx);
    if (xl_fx & 3u) ++xl;
    if (yl_fx & 3u) ++yl;
    if (xl < xh) std::swap(xl, xh);
    if (yl < yh) std::swap(yl, yh);

    N64_DEBUG("RDP FillRect ({},{})-({},{}) color={:08X}",
              xh, yh, xl, yl, fill_color_);

    if (!color_image_.valid) {
        N64_WARN("RDP FillRect without SetColorImage");
        return;
    }
    for (int y = yh; y < yl; ++y) {
        for (int x = xh; x < xl; ++x) {
            write_fill_pixel(x, y);
        }
    }
}

bool Rdp::sample_tile(u32 tile_idx, int s, int t, u16& out16, u32& out32,
                      bool& is32) const {
    const auto& tile = tiles_[tile_idx & 7];
    if (!tile.defined && texture_image_.valid) {
        // Fall back: sample texture image directly as a linear atlas.
    }

    // Apply mask (wrap) if set.
    auto apply_mask = [](int v, u8 mask) -> int {
        if (mask == 0 || mask >= 10) return v;
        const int m = (1 << mask) - 1;
        return v & m;
    };
    s = apply_mask(s, tile.mask_s);
    t = apply_mask(t, tile.mask_t);
    if (s < 0 || t < 0) {
        return false;
    }

    u8 size = tile.defined ? tile.size : texture_image_.size;
    u32 line_words = tile.line;
    u32 tmem_base = static_cast<u32>(tile.tmem) * 8u;

    // If tile line is 0, derive from texture width.
    u32 bpp = 2;
    if (size == 3) bpp = 4;
    else if (size <= 1) bpp = 1;

    u32 line_bytes = line_words ? (line_words * 8u)
                                : (texture_image_.valid ? texture_image_.width * bpp
                                                        : static_cast<u32>(s + 1) * bpp);

    const u32 offset = tmem_base + static_cast<u32>(t) * line_bytes +
                       static_cast<u32>(s) * bpp;
    if (offset + bpp > kTmemBytes) {
        return false;
    }

    if (size == 2) {
        out16 = static_cast<u16>((tmem_[offset] << 8) | tmem_[offset + 1]);
        is32 = false;
        return true;
    }
    if (size == 3) {
        out32 = (static_cast<u32>(tmem_[offset]) << 24) |
                (static_cast<u32>(tmem_[offset + 1]) << 16) |
                (static_cast<u32>(tmem_[offset + 2]) << 8) |
                (static_cast<u32>(tmem_[offset + 3]));
        is32 = true;
        return true;
    }
    // 8-bit: replicate to grayscale 5551
    const u8 p = tmem_[offset];
    out16 = static_cast<u16>(((p >> 3) << 11) | ((p >> 3) << 6) | ((p >> 3) << 1) | 1);
    is32 = false;
    return true;
}

void Rdp::cmd_texture_rectangle(u64 hi, u64 lo, bool flip) {
    // TextureRectangle (128-bit):
    //  hi: op | xl:12 | yl:12 | tile:3 | xh:12 | yh:12   (10.2 screen)
    //  lo: s:16 | t:16 | dsdx:16 | dtdy:16              (10.5 tex + 5.10 slopes)
    const u32 xl_fx = static_cast<u32>((hi >> 44) & 0xFFFu);
    const u32 yl_fx = static_cast<u32>((hi >> 32) & 0xFFFu);
    const u32 tile_idx = static_cast<u32>((hi >> 24) & 0x7u);
    const u32 xh_fx = static_cast<u32>((hi >> 12) & 0xFFFu);
    const u32 yh_fx = static_cast<u32>(hi & 0xFFFu);

    int xh = fx10_2_floor(xh_fx);
    int yh = fx10_2_floor(yh_fx);
    int xl = fx10_2_floor(xl_fx);
    int yl = fx10_2_floor(yl_fx);
    if (xl_fx & 3u) ++xl;
    if (yl_fx & 3u) ++yl;
    if (xl < xh) std::swap(xl, xh);
    if (yl < yh) std::swap(yl, yh);

    // s,t in 10.5 fixed (bits 63:48 and 47:32 of lo word when viewed as 64-bit)
    // lo is the second 64-bit word: s:16 t:16 dsdx:16 dtdy:16
    const s32 s0_fx = static_cast<s32>(static_cast<s16>((lo >> 48) & 0xFFFF));
    const s32 t0_fx = static_cast<s32>(static_cast<s16>((lo >> 32) & 0xFFFF));
    s32 dsdx = static_cast<s32>(static_cast<s16>((lo >> 16) & 0xFFFF)); // 5.10
    s32 dtdy = static_cast<s32>(static_cast<s16>(lo & 0xFFFF));         // 5.10

    if (flip) {
        // Flip swaps dsdx/dtdy roles for the minor axis — simplified: swap.
        std::swap(dsdx, dtdy);
    }

    // Convert: 10.5 → texel = fx / 32; 5.10 → delta per pixel = fx / 1024.
    // We'll accumulate in 10.5 space: dsdx_10_5 = dsdx_5_10 << 0?
    // 1 pixel step in 5.10 = dsdx/1024 texels = dsdx * 32 / 1024 = dsdx / 32 in 10.5 units.
    // So add (dsdx >> 5) to the 10.5 accumulator per pixel?
    // dsdx is 5.10: value / 1024 = texels per pixel.
    // s is 10.5: value / 32 = texels.
    // per pixel: s_fx += dsdx * 32 / 1024 = dsdx / 32.
    const s32 ds_per_px = dsdx >> 5; // approx
    const s32 dt_per_py = dtdy >> 5;

    N64_DEBUG("RDP TexRect tile={} ({},{})-({},{}) s0={} t0={} dsdx={} dtdy={}",
              tile_idx, xh, yh, xl, yl, s0_fx, t0_fx, dsdx, dtdy);

    if (!color_image_.valid) {
        N64_WARN("RDP TexRect without SetColorImage");
        return;
    }

    s32 t_fx = t0_fx;
    for (int y = yh; y < yl; ++y) {
        s32 s_fx = s0_fx;
        for (int x = xh; x < xl; ++x) {
            const int s = s_fx >> 5; // 10.5 → int texel
            const int t = t_fx >> 5;
            u16 p16 = 0;
            u32 p32 = 0;
            bool is32 = false;
            if (sample_tile(tile_idx, s, t, p16, p32, is32)) {
                write_color_pixel(x, y, p16, p32, is32);
                ++tex_pixels_;
            }
            s_fx += ds_per_px;
        }
        t_fx += dt_per_py;
    }
}

} // namespace n64
