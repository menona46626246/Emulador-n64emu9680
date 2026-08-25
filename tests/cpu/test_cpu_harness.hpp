#pragma once

#include "n64/bus/bus.hpp"
#include "n64/cpu/cpu.hpp"
#include "n64/cpu/insn.hpp"

#include <cstdint>
#include <initializer_list>
#include <vector>

namespace n64::test {

/// Tiny harness: place a MIPS program in RDRAM (via KSEG0) and run the CPU.
class CpuHarness {
public:
    static constexpr u32 kBasePhys = 0x0000'1000;
    static constexpr u32 kBaseVirt = 0x8000'1000; // KSEG0
    static constexpr u32 kDataPhys = 0x0000'8000;
    static constexpr u32 kDataVirt = 0x8000'8000;

    CpuHarness() {
        bus_.reset();
        cpu_.reset();
        cpu_.connect_bus(&bus_);
        // Clear BEV so exceptions go to 0x80000180 (in RDRAM range).
        cpu_.set_cop0(Cop0Reg::Status, 0x2400'0000u); // CU0+CU1, BEV=0
    }

    Cpu& cpu() noexcept { return cpu_; }
    Bus& bus() noexcept { return bus_; }

    void write_code(std::initializer_list<u32> words, u32 phys = kBasePhys) {
        u32 addr = phys;
        for (u32 w : words) {
            bus_.write32(addr, w);
            addr += 4;
        }
    }

    void write_code(const std::vector<u32>& words, u32 phys = kBasePhys) {
        u32 addr = phys;
        for (u32 w : words) {
            bus_.write32(addr, w);
            addr += 4;
        }
    }

    void start(u32 virt = kBaseVirt) {
        cpu_.set_pc(virt);
    }

    /// Run until PC hits `stop_pc` or `max_steps` exhausted.
    Cycles run_until(u64 stop_pc, Cycles max_steps = 10000) {
        Cycles n = 0;
        while (n < max_steps && cpu_.pc() != stop_pc && !cpu_.halted()) {
            cpu_.step();
            ++n;
        }
        return n;
    }

    Cycles run_steps(Cycles n) {
        return cpu_.run(n);
    }

    void poke32(u32 phys, u32 v) { bus_.write32(phys, v); }
    void poke8(u32 phys, u8 v) { bus_.write8(phys, v); }
    [[nodiscard]] u32 peek32(u32 phys) { return bus_.read32(phys); }
    [[nodiscard]] u8 peek8(u32 phys) { return bus_.read8(phys); }

private:
    Bus bus_;
    Cpu cpu_;
};

} // namespace n64::test
