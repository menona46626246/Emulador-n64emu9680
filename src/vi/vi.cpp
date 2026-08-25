#include "n64/vi/vi.hpp"

#include "n64/bus/bus.hpp"
#include "n64/bus/mi.hpp"
#include "n64/bus/mmio.hpp"
#include "n64/common/log.hpp"

#include <algorithm>

namespace n64 {
namespace {

// Decode RGBA5551 (big-endian halfword in RDRAM as stored by N64) → 8-bit channels.
// Layout: rrrrr ggggg bbbbb a
inline void decode_5551(u16 p, u8& r, u8& g, u8& b, u8& a) noexcept {
    const u32 R5 = (p >> 11) & 0x1Fu;
    const u32 G5 = (p >> 6)  & 0x1Fu;
    const u32 B5 = (p >> 1)  & 0x1Fu;
    const u32 A1 = p & 1u;
    // Expand 5→8 with bit replication (common HLE).
    r = static_cast<u8>((R5 << 3) | (R5 >> 2));
    g = static_cast<u8>((G5 << 3) | (G5 >> 2));
    b = static_cast<u8>((B5 << 3) | (B5 >> 2));
    a = A1 ? 0xFFu : 0x00u;
}

// 65536-entry precomputed lookup table for RGBA5551 to packed 32-bit RGBA8888
struct Rgba5551Lut {
    u32 table[65536];
    constexpr Rgba5551Lut() : table{} {
        for (u32 p = 0; p < 65536; ++p) {
            const u32 R5 = (p >> 11) & 0x1Fu;
            const u32 G5 = (p >> 6)  & 0x1Fu;
            const u32 B5 = (p >> 1)  & 0x1Fu;
            const u32 A1 = p & 1u;
            const u32 r = (R5 << 3) | (R5 >> 2);
            const u32 g = (G5 << 3) | (G5 >> 2);
            const u32 b = (B5 << 3) | (B5 >> 2);
            const u32 a = A1 ? 0xFFu : 0x00u;
            table[p] = r | (g << 8) | (b << 16) | (a << 24);
        }
    }
};

inline constexpr Rgba5551Lut k5551Lut{};

// N64 32-bit framebuffer pixel is typically 0xRRGGBBAA in big-endian memory
// (R at lowest address). Some homebrew use RGBA8888 matching that order.
inline void decode_8888(u32 p, u8& r, u8& g, u8& b, u8& a) noexcept {
    r = static_cast<u8>((p >> 24) & 0xFF);
    g = static_cast<u8>((p >> 16) & 0xFF);
    b = static_cast<u8>((p >> 8) & 0xFF);
    a = static_cast<u8>(p & 0xFF);
}

} // namespace

void VideoInterface::reset() {
    regs_.fill(0);
    // NTSC-ish defaults after IPL / libultra osViSetMode defaults (simplified).
    regs_[Control / 4]  = 0x0000'3202u; // AA + 16-bit type
    regs_[Origin  / 4]  = 0;
    regs_[Width   / 4]  = 320;
    regs_[VIntr   / 4]  = 0x002;        // early line; games rewrite
    regs_[VCurrent / 4] = 0;
    regs_[Burst   / 4]  = 0x03E5'2239u;
    regs_[VSync   / 4]  = 0x20D;
    regs_[HSync   / 4]  = 0xC15;
    regs_[Leap    / 4]  = 0x0C15'0C15u;
    regs_[HStart  / 4]  = 0x006C'02EC;
    regs_[VStart  / 4]  = 0x0025'01FF;
    regs_[VBurst  / 4]  = 0x000E'0204;
    regs_[XScale  / 4]  = 0x200;
    regs_[YScale  / 4]  = 0x400;

    cycle_accum_ = 0;
    cycles_per_line_ = kDefaultCyclesPerLine;
    lines_per_frame_ = kDefaultLinesPerFrame;
    frame_count_ = 0;
    frame_ready_ = false;
    interrupt_line_ = false;
}

u32 VideoInterface::read(u32 offset) const {
    const u32 idx = (offset & 0x3Cu) / 4;
    if (idx >= RegCount) {
        return 0;
    }
    return regs_[idx];
}

void VideoInterface::write(u32 offset, u32 value) {
    const u32 idx = (offset & 0x3Cu) / 4;
    if (idx >= RegCount) {
        return;
    }
    // Writing VI_CURRENT clears the pending VI interrupt.
    if (idx == VCurrent / 4) {
        interrupt_line_ = false;
        if (mi_) {
            mi_->clear(mmio::MiIntr::VI);
        }
        return;
    }

    regs_[idx] = value;

    if (idx == VSync / 4) {
        // V_SYNC register holds half-lines; use as lines-per-frame approx.
        const u32 vs = value & 0x3FFu;
        if (vs >= 100 && vs < 800) {
            lines_per_frame_ = vs;
        }
    }
}

void VideoInterface::raise_interrupt() {
    interrupt_line_ = true;
    if (mi_) {
        mi_->raise(mmio::MiIntr::VI);
    }
}

void VideoInterface::set_v_current_raw(u32 line) noexcept {
    // Bit 0 is the field (interlace); we keep progressive field=0.
    regs_[VCurrent / 4] = (line & 0x3FEu);
}

int VideoInterface::bytes_per_pixel() const noexcept {
    switch (pixel_format()) {
    case ViPixelFormat::Rgba5551: return 2;
    case ViPixelFormat::Rgba8888: return 4;
    default: return 0;
    }
}

u32 VideoInterface::line_pitch_bytes() const noexcept {
    // VI_WIDTH is in pixels. Pitch = width * bpp.
    // Real hardware has more subtle HWIDTH behavior; good enough for Phase 4.
    const int bpp = bytes_per_pixel();
    if (bpp <= 0) {
        return 0;
    }
    return static_cast<u32>(fb_width() * bpp);
}

int VideoInterface::fb_width() const noexcept {
    // Prefer VI_WIDTH; fall back to H_START window.
    u32 w = width_reg();
    if (w == 0) {
        const u32 hs = h_start_reg();
        const u32 start = (hs >> 16) & 0x3FFu;
        const u32 end   = hs & 0x3FFu;
        if (end > start) {
            w = end - start;
        }
    }
    // Clamp to sane range for homebrew / safety.
    if (w == 0) {
        return 0;
    }
    if (w > 640) {
        w = 640;
    }
    return static_cast<int>(w);
}

int VideoInterface::fb_height() const noexcept {
    const u32 vs = v_start_reg();
    const u32 start = (vs >> 16) & 0x3FFu;
    const u32 end   = vs & 0x3FFu;
    u32 h = 0;
    if (end > start) {
        h = end - start;
        // V_START is in half-lines; divide by 2 for progressive pixel rows.
        h /= 2;
    }
    if (h == 0) {
        // Fallback common NTSC height.
        h = 240;
    }
    if (h > 480) {
        h = 480;
    }
    return static_cast<int>(h);
}

void VideoInterface::tick(Cycles cpu_cycles) {
    if (cpu_cycles == 0 || cycles_per_line_ == 0) {
        return;
    }
    cycle_accum_ += cpu_cycles;

    while (cycle_accum_ >= cycles_per_line_) {
        cycle_accum_ -= cycles_per_line_;

        u32 line = (v_current() + 2) & 0x3FEu; // step half-lines by 2 → +1 line
        if (line > (lines_per_frame_ & 0x3FEu) || line == 0) {
            // Frame wrap
            line = 0;
            ++frame_count_;
            frame_ready_ = true;
            interrupt_line_ = false; // allow V_INTR to fire again next frame
        }
        set_v_current_raw(line);

        // Fire VI interrupt when crossing / matching V_INTR line.
        const u32 vintr = v_intr() & 0x3FEu;
        if (!interrupt_line_ && vintr != 0 && line == vintr) {
            raise_interrupt();
        }
    }
}

bool VideoInterface::sample_pixel(const Bus& bus, int x, int y,
                                  u8& r, u8& g, u8& b, u8& a) const {
    const int w = fb_width();
    const int h = fb_height();
    const int bpp = bytes_per_pixel();
    if (bpp == 0 || w <= 0 || h <= 0) {
        return false;
    }
    if (x < 0 || y < 0 || x >= w || y >= h) {
        return false;
    }

    const u32 org = this->origin() & ~0x3u; // 4-byte align
    const u32 pitch = line_pitch_bytes();
    const u32 addr = org + static_cast<u32>(y) * pitch + static_cast<u32>(x) * static_cast<u32>(bpp);

    if (addr + static_cast<u32>(bpp) > bus.rdram_size()) {
        // Still try via bus read (mirrors) — but guard huge OOB.
        if (org >= bus.rdram_size()) {
            return false;
        }
    }

    if (bpp == 2) {
        const u16 pix = bus.read16_const(addr);
        decode_5551(pix, r, g, b, a);
    } else {
        const u32 pix = bus.read32_const(addr);
        decode_8888(pix, r, g, b, a);
    }
    return true;
}

bool VideoInterface::copy_framebuffer_rgba(const Bus& bus, std::vector<u8>& out) const {
    const int w = fb_width();
    const int h = fb_height();
    const int bpp = bytes_per_pixel();
    if (bpp == 0 || w <= 0 || h <= 0) {
        out.clear();
        return false;
    }

    const u32 org = this->origin() & ~0x3u;
    const u32 pitch = line_pitch_bytes();
    const std::size_t need = static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4u;
    out.resize(need);

    // Fast path: direct RDRAM span when origin is inside RDRAM.
    const auto rdram = bus.rdram();
    if (!rdram.empty() && org < rdram.size()) {
        if (bpp == 2) {
            for (int y = 0; y < h; ++y) {
                const u32 row = org + static_cast<u32>(y) * pitch;
                for (int x = 0; x < w; ++x) {
                    const u32 addr = row + static_cast<u32>(x) * 2;
                    const std::size_t di =
                        (static_cast<std::size_t>(y) * static_cast<std::size_t>(w) +
                         static_cast<std::size_t>(x)) * 4u;
                    u32 rgba = 0xFF000000u;
                    if (addr + 1 < rdram.size()) {
                        const u16 pix = (static_cast<u16>(rdram[addr]) << 8) |
                                        static_cast<u16>(rdram[addr + 1]);
                        rgba = k5551Lut.table[pix];
                    }
                    // Store channels explicitly: std::vector<u8> has no u32
                    // alignment guarantee, and the output byte order must not
                    // depend on host endianness.
                    out[di + 0] = static_cast<u8>(rgba & 0xFFu);
                    out[di + 1] = static_cast<u8>((rgba >> 8) & 0xFFu);
                    out[di + 2] = static_cast<u8>((rgba >> 16) & 0xFFu);
                    out[di + 3] = static_cast<u8>((rgba >> 24) & 0xFFu);
                }
            }
            return true;
        }
        if (bpp == 4) {
            for (int y = 0; y < h; ++y) {
                const u32 row = org + static_cast<u32>(y) * pitch;
                for (int x = 0; x < w; ++x) {
                    const u32 addr = row + static_cast<u32>(x) * 4;
                    const std::size_t di =
                        (static_cast<std::size_t>(y) * static_cast<std::size_t>(w) +
                         static_cast<std::size_t>(x)) * 4u;
                    if (addr + 3 < rdram.size()) {
                        // N64 RGBA8888 is already R,G,B,A in increasing RDRAM
                        // addresses, so copy bytes rather than a host u32.
                        out[di + 0] = rdram[addr + 0];
                        out[di + 1] = rdram[addr + 1];
                        out[di + 2] = rdram[addr + 2];
                        out[di + 3] = rdram[addr + 3];
                    } else {
                        out[di + 0] = 0;
                        out[di + 1] = 0;
                        out[di + 2] = 0;
                        out[di + 3] = 255;
                    }
                }
            }
            return true;
        }
    }

    // Slow path via bus reads
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            u8 r = 0, g = 0, b = 0, a = 255;
            (void)sample_pixel(bus, x, y, r, g, b, a);
            const std::size_t di = (static_cast<std::size_t>(y) * static_cast<std::size_t>(w) +
                                    static_cast<std::size_t>(x)) * 4u;
            out[di + 0] = r;
            out[di + 1] = g;
            out[di + 2] = b;
            out[di + 3] = a;
        }
    }
    return true;
}

} // namespace n64
