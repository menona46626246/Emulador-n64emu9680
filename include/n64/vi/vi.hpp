#pragma once

#include "n64/common/types.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace n64 {

class MipsInterface;
class Bus;

/// Pixel formats selected by VI_CONTROL bits 1:0.
enum class ViPixelFormat : u8 {
    Blank  = 0,
    Reserved = 1,
    Rgba5551 = 2, // 16-bit
    Rgba8888 = 3, // 32-bit
};

/// Video Interface (MMIO @ 0x0440'0000) + basic scanout timing.
class VideoInterface {
public:
    enum Reg : u32 {
        Control  = 0x00,
        Origin   = 0x04,
        Width    = 0x08,
        VIntr    = 0x0C,
        VCurrent = 0x10,
        Burst    = 0x14,
        VSync    = 0x18,
        HSync    = 0x1C,
        Leap     = 0x20,
        HStart   = 0x24,
        VStart   = 0x28,
        VBurst   = 0x2C,
        XScale   = 0x30,
        YScale   = 0x34,
        RegCount = 0x38 / 4,
    };

    // VI_CONTROL bits (public n64brew docs)
    static constexpr u32 CtrlTypeMask   = 0x3u;
    static constexpr u32 CtrlGammaDither= 1u << 2;
    static constexpr u32 CtrlGamma      = 1u << 3;
    static constexpr u32 CtrlDivot      = 1u << 4;
    static constexpr u32 CtrlSerrate    = 1u << 6; // interlaced
    static constexpr u32 CtrlAaMask     = 0x300u;
    static constexpr u32 CtrlPixelAdvMask = 0xF000u;

    /// NTSC-ish defaults used for scanline timing (refined in Phase 9).
    static constexpr u32 kDefaultLinesPerFrame = 262;     // progressive half-lines approx
    static constexpr Cycles kDefaultCyclesPerLine = 5963;  // ~93.75MHz / 60 / 262

    void reset();
    void connect_mi(MipsInterface* mi) noexcept { mi_ = mi; }

    [[nodiscard]] u32 read(u32 offset) const;
    void write(u32 offset, u32 value);

    /// Advance VI timing by `cpu_cycles`. May raise VI interrupt.
    void tick(Cycles cpu_cycles);

    /// Force a VI interrupt (tests).
    void raise_interrupt();

    // ----- Register accessors ------------------------------------------------
    [[nodiscard]] u32 origin() const noexcept { return regs_[Origin / 4] & 0x00FF'FFFFu; }
    [[nodiscard]] u32 width_reg() const noexcept { return regs_[Width / 4] & 0xFFFu; }
    [[nodiscard]] u32 control() const noexcept { return regs_[Control / 4]; }
    [[nodiscard]] u32 v_intr() const noexcept { return regs_[VIntr / 4] & 0x3FFu; }
    [[nodiscard]] u32 v_current() const noexcept { return regs_[VCurrent / 4] & 0x3FFu; }
    [[nodiscard]] u32 v_sync() const noexcept { return regs_[VSync / 4] & 0x3FFu; }
    [[nodiscard]] u32 h_start_reg() const noexcept { return regs_[HStart / 4]; }
    [[nodiscard]] u32 v_start_reg() const noexcept { return regs_[VStart / 4]; }

    [[nodiscard]] ViPixelFormat pixel_format() const noexcept {
        return static_cast<ViPixelFormat>(control() & CtrlTypeMask);
    }

    /// Visible framebuffer size derived from VI registers (clamped).
    [[nodiscard]] int fb_width() const noexcept;
    [[nodiscard]] int fb_height() const noexcept;

    [[nodiscard]] u64 frame_count() const noexcept { return frame_count_; }
    [[nodiscard]] bool frame_ready() const noexcept { return frame_ready_; }
    void clear_frame_ready() noexcept { frame_ready_ = false; }

    /// Convert the current VI framebuffer in RDRAM to tightly packed RGBA8888
    /// (R,G,B,A bytes). Returns false if blank / invalid geometry.
    [[nodiscard]] bool copy_framebuffer_rgba(const Bus& bus, std::vector<u8>& out) const;

    /// Sample a single pixel as R,G,B,A (for tests). Returns false if OOB/blank.
    [[nodiscard]] bool sample_pixel(const Bus& bus, int x, int y,
                                    u8& r, u8& g, u8& b, u8& a) const;

private:
    void set_v_current_raw(u32 line) noexcept;
    [[nodiscard]] int bytes_per_pixel() const noexcept;
    [[nodiscard]] u32 line_pitch_bytes() const noexcept;

    std::array<u32, RegCount> regs_{};
    MipsInterface* mi_ = nullptr;

    Cycles cycle_accum_ = 0;
    Cycles cycles_per_line_ = kDefaultCyclesPerLine;
    u32 lines_per_frame_ = kDefaultLinesPerFrame;
    u64 frame_count_ = 0;
    bool frame_ready_ = false;
    bool interrupt_line_ = false; // level for dedup when crossing V_INTR
};

} // namespace n64
