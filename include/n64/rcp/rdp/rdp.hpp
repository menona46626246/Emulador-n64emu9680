#pragma once

#include "n64/common/types.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace n64 {

class Bus;
class MipsInterface;

/// RDP command opcodes (bits 61:56 of the 64-bit command word).
namespace RdpOp {
    inline constexpr u32 NoOp              = 0x00;
    inline constexpr u32 FillTriangle      = 0x08;
    inline constexpr u32 TextureTriangle   = 0x09;
    inline constexpr u32 TextureRectangle  = 0x24;
    inline constexpr u32 TextureRectangleFlip = 0x25;
    inline constexpr u32 SyncLoad          = 0x26;
    inline constexpr u32 SyncPipe          = 0x27;
    inline constexpr u32 SyncTile          = 0x28;
    inline constexpr u32 SyncFull          = 0x29;
    inline constexpr u32 SetKeyGB          = 0x2A;
    inline constexpr u32 SetKeyR           = 0x2B;
    inline constexpr u32 SetConvert        = 0x2C;
    inline constexpr u32 SetScissor        = 0x2D;
    inline constexpr u32 SetPrimDepth      = 0x2E;
    inline constexpr u32 SetOtherModes     = 0x2F;
    inline constexpr u32 LoadTLut          = 0x30;
    inline constexpr u32 SetTileSize       = 0x32;
    inline constexpr u32 LoadBlock         = 0x33;
    inline constexpr u32 LoadTile          = 0x34;
    inline constexpr u32 SetTile           = 0x35;
    inline constexpr u32 FillRectangle     = 0x36;
    inline constexpr u32 SetFillColor      = 0x37;
    inline constexpr u32 SetFogColor       = 0x38;
    inline constexpr u32 SetBlendColor     = 0x39;
    inline constexpr u32 SetPrimColor      = 0x3A;
    inline constexpr u32 SetEnvColor       = 0x3B;
    inline constexpr u32 SetCombine        = 0x3C;
    inline constexpr u32 SetTextureImage   = 0x3D;
    inline constexpr u32 SetMaskImage      = 0x3E;
    inline constexpr u32 SetColorImage     = 0x3F;
}

struct RdpColorImage {
    u32 address = 0;
    u32 width   = 0;
    u8  size    = 2;   // 0=4 1=8 2=16 3=32 bpp
    u8  format  = 0;
    bool valid  = false;
};

struct RdpTextureImage {
    u32 address = 0;
    u32 width   = 0;
    u8  size    = 2;
    u8  format  = 0;
    bool valid  = false;
};

struct RdpTile {
    u8  format = 0;
    u8  size   = 2;
    u16 line   = 0;    // words (64-bit) per texture line
    u16 tmem   = 0;    // TMEM word address (64-bit words)
    u8  palette = 0;
    u8  ct = 0, mt = 0, cs = 0, ms = 0;
    u8  mask_t = 0, shift_t = 0, mask_s = 0, shift_s = 0;
    // Tile size (SetTileSize / LoadTile): 10.2 coords
    u16 sl = 0, tl = 0, sh = 0, th = 0;
    bool defined = false;
};

struct RdpScissor {
    int xh = 0, yh = 0, xl = 320, yl = 240;
    bool set = false;
};

/// Reality Display Processor — Phase 6+: FillRect, TexRect, scissor, TMEM blit.
class Rdp {
public:
    static constexpr std::size_t kTmemBytes = 4096;
    static constexpr std::size_t kTileCount = 8;

    void reset();
    void connect(Bus* bus, MipsInterface* mi) noexcept {
        bus_ = bus;
        mi_ = mi;
    }

    void run_command_list(u32 start, u32 end, bool xbus_dmem);
    void execute_command(u64 cmd);
    void execute_command_pair(u64 hi, u64 lo);
    void flush();

    [[nodiscard]] bool busy() const noexcept { return busy_; }
    [[nodiscard]] const RdpColorImage& color_image() const noexcept { return color_image_; }
    [[nodiscard]] const RdpTextureImage& texture_image() const noexcept { return texture_image_; }
    [[nodiscard]] const RdpScissor& scissor() const noexcept { return scissor_; }
    [[nodiscard]] const RdpTile& tile(std::size_t i) const noexcept {
        return tiles_[i & 7];
    }
    [[nodiscard]] u32 fill_color() const noexcept { return fill_color_; }
    [[nodiscard]] u64 commands_executed() const noexcept { return cmds_executed_; }
    [[nodiscard]] u64 fill_pixels() const noexcept { return fill_pixels_; }
    [[nodiscard]] u64 tex_pixels() const noexcept { return tex_pixels_; }
    [[nodiscard]] std::span<const u8> tmem() const noexcept { return tmem_; }
    [[nodiscard]] std::span<u8> tmem() noexcept { return tmem_; }

private:
    [[nodiscard]] static u32 opcode(u64 cmd) noexcept {
        return static_cast<u32>((cmd >> 56) & 0x3Fu);
    }
    [[nodiscard]] static int fx10_2_floor(u32 v) noexcept {
        return static_cast<int>(v >> 2);
    }
    [[nodiscard]] static int fx10_2_ceil(u32 v) noexcept {
        return static_cast<int>((v + 3u) >> 2);
    }
    /// 10.5 fixed → float-ish (return as s32 in 1/32 units).
    [[nodiscard]] static s32 fx10_5(u32 v) noexcept {
        return static_cast<s32>(static_cast<u16>(v & 0xFFFFu));
    }

    void cmd_set_color_image(u64 cmd);
    void cmd_set_texture_image(u64 cmd);
    void cmd_set_scissor(u64 cmd);
    void cmd_set_fill_color(u64 cmd);
    void cmd_set_blend_color(u64 cmd);
    void cmd_fill_rectangle(u64 cmd);
    void cmd_set_other_modes(u64 cmd);
    void cmd_set_tile(u64 cmd);
    void cmd_set_tile_size(u64 cmd);
    void cmd_load_block(u64 cmd);
    void cmd_load_tile(u64 cmd);
    void cmd_texture_rectangle(u64 hi, u64 lo, bool flip);
    void cmd_sync_full(u64 cmd);

    void write_color_pixel(int x, int y, u16 pix16, u32 pix32, bool use32);
    void write_fill_pixel(int x, int y);
    [[nodiscard]] bool clip_point(int x, int y) const noexcept;

    /// Sample TMEM for tile `tile_idx` at integer texel (s,t) → 16-bit RGBA5551
    /// (or expand 32-bit to write path). Returns false if OOB empty.
    [[nodiscard]] bool sample_tile(u32 tile_idx, int s, int t, u16& out16, u32& out32,
                                   bool& is32) const;

    [[nodiscard]] u32 read_cmd_word(u32 addr, bool xbus_dmem) const;
    [[nodiscard]] u64 read_cmd64(u32 addr, bool xbus_dmem) const;
    [[nodiscard]] u8 read_rdram_u8(u32 addr) const;
    void copy_rdram_to_tmem(u32 dram, u32 tmem_byte, u32 nbytes);

    Bus* bus_ = nullptr;
    MipsInterface* mi_ = nullptr;

    RdpColorImage color_image_{};
    RdpTextureImage texture_image_{};
    RdpScissor scissor_{};
    std::array<RdpTile, kTileCount> tiles_{};
    std::array<u8, kTmemBytes> tmem_{};

    u32 fill_color_ = 0;
    u32 blend_color_ = 0;
    u64 other_modes_ = 0;

    bool busy_ = false;
    u64 cmds_executed_ = 0;
    u64 fill_pixels_ = 0;
    u64 tex_pixels_ = 0;
};

} // namespace n64
