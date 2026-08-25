#pragma once

#include "n64/common/types.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <span>
#include <vector>

namespace n64 {

class Bus;
class MipsInterface;

/// Audio Interface (MMIO @ 0x0450'0000) + sample ring buffer.
///
/// Phase 8: DMA from RDRAM (stereo s16 BE), timed completion, IRQ AI,
/// host pulls interleaved s16 LE samples for SDL.
class AudioInterface {
public:
    enum Reg : u32 {
        DramAddr = 0x00,
        Len      = 0x04,
        Control  = 0x08,
        Status   = 0x0C,
        DacRate  = 0x10,
        BitRate  = 0x14,
        RegCount = 0x18 / 4,
    };

    // AI_STATUS bits (read)
    static constexpr u32 StatusFull     = 1u << 0;  // second DMA pending
    static constexpr u32 StatusBusy     = 1u << 30; // DMA in progress
    // bit 31 sometimes counted as busy on some docs — we use 30.

    // AI_CONTROL
    static constexpr u32 ControlDmaOn   = 1u << 0;

    /// NTSC VI clock used by DACRATE formula (public docs ≈ 48.681812 MHz).
    static constexpr u32 kDacClockHz = 48'681'812u;
    /// Default output rate when DACRATE is 0 / unset.
    static constexpr u32 kDefaultSampleRate = 32000;
    /// Ring capacity in stereo frames (L+R = 1 frame).
    static constexpr std::size_t kRingFrames = 8192;

    void reset();
    void connect(Bus* bus, MipsInterface* mi) noexcept {
        bus_ = bus;
        mi_ = mi;
    }
    /// Backward-compatible alias used by Bus::connect.
    void connect_mi(MipsInterface* mi) noexcept { mi_ = mi; }

    [[nodiscard]] u32 read(u32 offset) const;
    void write(u32 offset, u32 value);

    /// Advance audio DMA timing by CPU cycles. May complete DMA and raise IRQ.
    void tick(Cycles cpu_cycles);

    /// Force-complete current DMA (tests).
    void complete_dma();

    /// Host sample rate derived from AI_DACRATE.
    [[nodiscard]] u32 sample_rate() const noexcept;

    [[nodiscard]] bool dma_busy() const noexcept { return dma_active_; }
    [[nodiscard]] bool dma_full() const noexcept { return dma_queued_; }
    [[nodiscard]] u64 samples_pushed() const noexcept { return samples_pushed_; }
    [[nodiscard]] u64 frames_dropped() const noexcept { return frames_dropped_; }
    [[nodiscard]] u64 dmas_completed() const noexcept { return dmas_completed_; }

    /// Pop up to `max_frames` stereo frames into `out` as interleaved s16 LE
    /// (L,R,L,R,...). Returns frames written. Thread-safe vs tick/DMA.
    std::size_t pull_frames(std::span<s16> out_interleaved_lr);

    /// How many stereo frames currently buffered.
    [[nodiscard]] std::size_t buffered_frames() const;

    /// Push a stereo frame into the SPSC ring buffer. Thread-safe (producer side).
    void push_frame(s16 left, s16 right);

private:
    struct DmaJob {
        u32 dram_addr = 0;
        u32 length    = 0; // bytes
    };

    void start_or_queue_dma(u32 length_reg);
    void begin_dma(const DmaJob& job);
    void finish_current_dma();
    void update_status_bits();

    [[nodiscard]] static u32 align_len(u32 v) noexcept {
        // Hardware ignores low 3 bits; length is bytes of the transfer.
        return v & ~0x7u;
    }

    Bus* bus_ = nullptr;
    MipsInterface* mi_ = nullptr;

    std::array<u32, RegCount> regs_{};

    bool dma_enabled_ = false;
    bool dma_active_  = false;
    bool dma_queued_  = false;
    DmaJob current_{};
    DmaJob queued_{};

    /// Remaining bytes to "play" before IRQ (counts down with ticks).
    u32 bytes_remaining_ = 0;
    /// Fractional cycle accumulator for sample timing.
    Cycles cycle_accum_ = 0;
    /// CPU cycles per output stereo frame at current DAC rate.
    Cycles cycles_per_frame_ = 0;

    // Next RDRAM read cursor for the active DMA (byte address).
    u32 read_cursor_ = 0;

    // Lock-free Single-Producer Single-Consumer (SPSC) ring buffer.
    static constexpr std::size_t kRingMask = kRingFrames - 1;
    std::array<s16, kRingFrames * 2> ring_{}; // interleaved L,R
    std::atomic<std::size_t> ring_r_{0};
    std::atomic<std::size_t> ring_w_{0};

    u64 samples_pushed_ = 0; // stereo frames pushed to ring
    u64 frames_dropped_ = 0; // newest frames rejected while ring is full
    u64 dmas_completed_ = 0;
};

} // namespace n64
