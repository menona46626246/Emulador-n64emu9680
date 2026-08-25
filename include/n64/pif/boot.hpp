#pragma once

#include "n64/cart/header.hpp"
#include "n64/common/types.hpp"

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace n64 {

class Bus;
class Cpu;
class Pif;
class Rsp;

/// Boot mode.
enum class BootMode : u8 {
    /// Replace IPL1/2/3 with documented HLE (default, no proprietary blobs).
    Hle = 0,
    /// Load user-provided PIF boot ROM into 0x1FC00000 and start at reset
    /// vector 0xBFC00000. Requires a legal dump owned by the user.
    LlePif = 1,
};

/// Boot configuration for cold reset / ROM load.
struct BootConfig {
    BootMode mode = BootMode::Hle;

    /// Bytes of cartridge to copy into RDRAM at phys(entrypoint), from cart
    /// offset 0x1000.  0 = auto (min(1 MiB, cart_size - 0x1000)).
    std::size_t load_size = 0;

    /// If true, skip the cart→RDRAM copy (ROM already placed by the test).
    bool skip_rom_copy = false;

    /// Force a CIC type instead of auto-detect (Unknown = auto).
    CicType force_cic = CicType::Unknown;

    /// Force TV type (nullopt-style: use country code when force_tv_set=false).
    bool force_tv_set = false;
    TvType force_tv = TvType::NTSC;

    /// Optional path to a user-owned PIF boot ROM (typically 2048 bytes).
    /// Loaded at physical 0x1FC00000. Never shipped with this project.
    std::string pif_rom_path;

    /// Optional path to a user-owned standalone IPL3 image (4032 bytes).
    /// When set in HLE mode, copied into SP DMEM @ 0x04000040 for inspection
    /// / future LLE IPL3 stepping. Not executed in pure HLE.
    std::string ipl3_path;

    /// osResetType: 0 = cold, 1 = NMI / soft reset.
    u32 reset_type = 0;
};

struct BootResult {
    bool ok = false;
    CartHeader header{};
    u32 entrypoint = 0;
    std::size_t bytes_copied = 0;
    bool pif_rom_loaded = false;
    bool ipl3_loaded = false;
    BootMode mode = BootMode::Hle;
    const char* error = nullptr;
};

/// High-level IPL3 replacement (or LLE PIF start).
///
/// Real hardware path (simplified):
///   IPL1/2 (PIF ROM @ 0x1FC00000) → copy IPL3 from cart 0x40 to SP DMEM →
///   run IPL3 → DMA game cart 0x1000 → RDRAM → jump to entrypoint.
///
/// HLE path (default):
///   1. Parse cart header + CIC + TV
///   2. Program PI DOM1 timing from header word 0
///   3. Optionally load user PIF ROM / IPL3 blobs (legal, user-provided)
///   4. Copy cart[0x1000 ..] → RDRAM at physical(entrypoint) via PI DMA
///   5. Seed CPU GPRs / COP0 / PIF RAM as post-IPL3 homebrew expects
///   6. Set PC = entrypoint
///
/// LLE PIF path:
///   Load user PIF ROM, seed sideband, PC = 0xBFC00000 (no cart copy).
///
/// No proprietary IPL/PIF firmware is included or required for HLE.
[[nodiscard]] BootResult hle_boot_loaded(Bus& bus,
                                         Cpu& cpu,
                                         Pif& pif,
                                         Rsp& rsp,
                                         const BootConfig& cfg = {});

/// Alias kept for call-sites.
[[nodiscard]] inline BootResult hle_boot(Bus& bus,
                                         Cpu& cpu,
                                         Pif& pif,
                                         Rsp& rsp,
                                         const BootConfig& cfg = {}) {
    return hle_boot_loaded(bus, cpu, pif, rsp, cfg);
}

/// Load a raw binary file into a vector (empty on failure).
[[nodiscard]] std::vector<u8> load_binary_file(std::string_view path);

} // namespace n64
