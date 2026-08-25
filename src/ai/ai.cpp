#include "n64/ai/ai.hpp"

#include "n64/bus/bus.hpp"
#include "n64/bus/mi.hpp"
#include "n64/bus/mmio.hpp"
#include "n64/common/log.hpp"
#include "n64/common/types.hpp"

#include <algorithm>

namespace n64 {
namespace {

// VR4300 clock used to convert sample rate → cycles/frame.
constexpr Cycles kCpuHz = kCpuClockHz;

[[nodiscard]] s16 be_s16_from_rdram(std::span<const u8> rdram, u32 addr) {
    if (rdram.empty()) {
        return 0;
    }
    const u8 hi = rdram[addr % rdram.size()];
    const u8 lo = rdram[(addr + 1) % rdram.size()];
    return static_cast<s16>((static_cast<u16>(hi) << 8) | lo);
}

} // namespace

void AudioInterface::reset() {
    regs_.fill(0);
    // Sensible defaults: ~32 kHz-ish DACRATE.
    // sample_rate ≈ DAC_CLOCK / (dacrate + 1)
    regs_[DacRate / 4] = (kDacClockHz / kDefaultSampleRate) - 1u;
    regs_[BitRate / 4] = 0xF; // 16-bit-ish placeholder
    regs_[Control / 4] = 0;
    regs_[Status / 4]  = 0;

    dma_enabled_ = false;
    dma_active_  = false;
    dma_queued_  = false;
    current_ = {};
    queued_  = {};
    bytes_remaining_ = 0;
    cycle_accum_ = 0;
    cycles_per_frame_ = 0;
    read_cursor_ = 0;

    ring_.fill(0);
    ring_r_.store(0, std::memory_order_relaxed);
    ring_w_.store(0, std::memory_order_relaxed);
    samples_pushed_ = 0;
    frames_dropped_ = 0;
    dmas_completed_ = 0;

    // Precompute cycles/frame for default rate.
    const u32 sr = sample_rate();
    cycles_per_frame_ = (sr > 0) ? (kCpuHz / sr) : (kCpuHz / kDefaultSampleRate);
}

u32 AudioInterface::sample_rate() const noexcept {
    const u32 dac = regs_[DacRate / 4] & 0x3FFFu;
    if (dac == 0) {
        return kDefaultSampleRate;
    }
    // rate = DAC_CLOCK / (dacrate + 1)
    const u32 rate = kDacClockHz / (dac + 1u);
    // Clamp to a sane host range.
    if (rate < 4000) return 4000;
    if (rate > 96000) return 96000;
    return rate;
}

void AudioInterface::update_status_bits() {
    u32 st = 0;
    if (dma_queued_) st |= StatusFull;
    if (dma_active_) st |= StatusBusy;
    regs_[Status / 4] = st;
}

u32 AudioInterface::read(u32 offset) const {
    const u32 off = offset & 0x1Cu;
    switch (off) {
    case DramAddr: return regs_[DramAddr / 4] & 0x00FF'FFF8u;
    case Len:      return regs_[Len / 4] & 0x0003'FFF8u;
    case Control:  return regs_[Control / 4] & 0x1u;
    case Status:   return regs_[Status / 4];
    case DacRate:  return regs_[DacRate / 4] & 0x3FFFu;
    case BitRate:  return regs_[BitRate / 4] & 0xFu;
    default:       return 0;
    }
}

void AudioInterface::write(u32 offset, u32 value) {
    const u32 off = offset & 0x1Cu;

    if (off == Status) {
        // Any write clears AI interrupt.
        if (mi_) {
            mi_->clear(mmio::MiIntr::AI);
        }
        return;
    }

    switch (off) {
    case DramAddr:
        regs_[DramAddr / 4] = value & 0x00FF'FFF8u;
        break;
    case Control:
        regs_[Control / 4] = value & 0x1u;
        dma_enabled_ = (value & ControlDmaOn) != 0;
        break;
    case DacRate: {
        regs_[DacRate / 4] = value & 0x3FFFu;
        const u32 sr = sample_rate();
        cycles_per_frame_ = (sr > 0) ? (kCpuHz / sr) : (kCpuHz / kDefaultSampleRate);
        if (cycles_per_frame_ == 0) {
            cycles_per_frame_ = 1;
        }
        N64_DEBUG("AI DACRATE={} → sample_rate={} Hz, cycles/frame={}",
                  regs_[DacRate / 4], sr, cycles_per_frame_);
        break;
    }
    case BitRate:
        regs_[BitRate / 4] = value & 0xFu;
        break;
    case Len:
        // Writing LEN kicks (or queues) a DMA when DMA is enabled.
        regs_[Len / 4] = value & 0x0003'FFF8u;
        start_or_queue_dma(regs_[Len / 4]);
        break;
    default:
        break;
    }
}

void AudioInterface::start_or_queue_dma(u32 length_reg) {
    const u32 length = align_len(length_reg);
    if (length == 0) {
        return;
    }
    // Hardware can accept a DMA even if CONTROL.DMA_ON is clear on some titles;
    // homebrew typically sets it. We allow the transfer either way but still
    // honour the register for status.
    DmaJob job;
    job.dram_addr = regs_[DramAddr / 4] & 0x00FF'FFF8u;
    job.length = length;

    if (!dma_active_) {
        begin_dma(job);
    } else if (!dma_queued_) {
        queued_ = job;
        dma_queued_ = true;
        update_status_bits();
        N64_DEBUG("AI DMA queued addr={:08X} len={}", job.dram_addr, job.length);
    } else {
        N64_WARN("AI DMA dropped (already full) len={}", length);
    }
}

void AudioInterface::begin_dma(const DmaJob& job) {
    current_ = job;
    dma_active_ = true;
    bytes_remaining_ = job.length;
    read_cursor_ = job.dram_addr;
    regs_[Len / 4] = job.length; // readable remaining-ish
    // Keep DRAM_ADDR as programmed (some games rewrite between DMAs).
    update_status_bits();
    cycle_accum_ = 0;

    const u32 sr = sample_rate();
    cycles_per_frame_ = (sr > 0) ? (kCpuHz / sr) : (kCpuHz / kDefaultSampleRate);
    if (cycles_per_frame_ == 0) {
        cycles_per_frame_ = 1;
    }

    N64_DEBUG("AI DMA start addr={:08X} len={} rate={}Hz",
              current_.dram_addr, current_.length, sr);
}

void AudioInterface::finish_current_dma() {
    dma_active_ = false;
    bytes_remaining_ = 0;
    regs_[Len / 4] = 0;
    ++dmas_completed_;
    update_status_bits();

    if (mi_) {
        mi_->raise(mmio::MiIntr::AI);
    }
    N64_DEBUG("AI DMA complete (total={})", dmas_completed_);

    // Kick queued DMA if any.
    if (dma_queued_) {
        dma_queued_ = false;
        const DmaJob next = queued_;
        queued_ = {};
        begin_dma(next);
    } else {
        update_status_bits();
    }
}

void AudioInterface::complete_dma() {
    // Instant path for tests: drain remaining bytes into the ring, then finish.
    if (!dma_active_) {
        return;
    }
    if (bus_) {
        auto rdram = bus_->rdram();
        while (bytes_remaining_ >= 4) {
            const s16 left  = be_s16_from_rdram(rdram, read_cursor_);
            const s16 right = be_s16_from_rdram(rdram, read_cursor_ + 2);
            push_frame(left, right);
            read_cursor_ += 4;
            bytes_remaining_ -= 4;
        }
    }
    bytes_remaining_ = 0;
    finish_current_dma();
}

void AudioInterface::push_frame(s16 left, s16 right) {
    const std::size_t w = ring_w_.load(std::memory_order_relaxed);
    const std::size_t r = ring_r_.load(std::memory_order_acquire);
    const std::size_t count = w - r;
    ++samples_pushed_;
    if (count >= kRingFrames) {
        // The producer must never advance ring_r_ or overwrite unread data:
        // doing either races the SDL consumer. Reject the newest frame instead.
        ++frames_dropped_;
        return;
    }
    const std::size_t slot = (w & kRingMask) * 2;
    ring_[slot]     = left;
    ring_[slot + 1] = right;
    ring_w_.store(w + 1, std::memory_order_release);
}

void AudioInterface::tick(Cycles cpu_cycles) {
    if (!dma_active_ || bytes_remaining_ == 0 || cycles_per_frame_ == 0) {
        return;
    }
    if (!bus_) {
        return;
    }

    cycle_accum_ += cpu_cycles;
    auto rdram = bus_->rdram();

    while (dma_active_ && cycle_accum_ >= cycles_per_frame_ && bytes_remaining_ >= 4) {
        cycle_accum_ -= cycles_per_frame_;

        const s16 left  = be_s16_from_rdram(rdram, read_cursor_);
        const s16 right = be_s16_from_rdram(rdram, read_cursor_ + 2);
        push_frame(left, right);

        read_cursor_ += 4;
        bytes_remaining_ -= 4;
        regs_[Len / 4] = bytes_remaining_ & 0x0003'FFF8u;
    }

    if (dma_active_ && bytes_remaining_ < 4) {
        bytes_remaining_ = 0;
        finish_current_dma();
    }
}

std::size_t AudioInterface::pull_frames(std::span<s16> out_interleaved_lr) {
    const std::size_t max_frames = out_interleaved_lr.size() / 2;
    if (max_frames == 0) {
        return 0;
    }
    const std::size_t r = ring_r_.load(std::memory_order_relaxed);
    const std::size_t w = ring_w_.load(std::memory_order_acquire);
    if (w <= r) {
        return 0;
    }
    const std::size_t available = w - r;
    const std::size_t n = std::min(max_frames, available);
    for (std::size_t f = 0; f < n; ++f) {
        const std::size_t slot = ((r + f) & kRingMask) * 2;
        out_interleaved_lr[f * 2]     = ring_[slot];
        out_interleaved_lr[f * 2 + 1] = ring_[slot + 1];
    }
    ring_r_.store(r + n, std::memory_order_release);
    return n;
}

std::size_t AudioInterface::buffered_frames() const {
    const std::size_t r = ring_r_.load(std::memory_order_relaxed);
    const std::size_t w = ring_w_.load(std::memory_order_acquire);
    return (w >= r) ? (w - r) : 0;
}

} // namespace n64
