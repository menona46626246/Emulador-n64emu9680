#include "n64/pif/boot.hpp"

#include "n64/bus/bus.hpp"
#include "n64/bus/mmio.hpp"
#include "n64/bus/pi.hpp"
#include "n64/bus/sp_regs.hpp"
#include "n64/common/log.hpp"
#include "n64/cpu/cpu.hpp"
#include "n64/pif/pif.hpp"
#include "n64/rcp/rsp/rsp.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>

namespace n64 {
namespace {

[[nodiscard]] u32 load_phys_from_entry(u32 entry) noexcept {
    return entry & 0x1FFF'FFFFu;
}

void apply_pi_dom1_from_header(Bus& bus, u32 pi_word) {
    // Header word0 (big-endian on cart):
    //   [31:24] = 0x80 endian probe
    //   [23:20] = DOM1_RLS
    //   [19:16] = DOM1_PGS
    //   [15:8]  = DOM1_PWD
    //   [7:0]   = DOM1_LAT
    const u32 lat =  pi_word        & 0xFFu;
    const u32 pwd = (pi_word >> 8)  & 0xFFu;
    const u32 pgs = (pi_word >> 16) & 0x0Fu;
    const u32 rls = (pi_word >> 20) & 0x0Fu;

    bus.write32(mmio::PI_BASE + PeripheralInterface::BsdDom1Lat, lat);
    bus.write32(mmio::PI_BASE + PeripheralInterface::BsdDom1Pwd, pwd);
    bus.write32(mmio::PI_BASE + PeripheralInterface::BsdDom1Pgs, pgs);
    bus.write32(mmio::PI_BASE + PeripheralInterface::BsdDom1Rls, rls);

    N64_DEBUG("PI DOM1 from header: lat={:02X} pwd={:02X} pgs={:X} rls={:X}",
              lat, pwd, pgs, rls);
}

void seed_cpu_after_ipl3(Cpu& cpu, const CartHeader& hdr, u32 sp_top, u32 reset_type) {
    // Post-IPL3 GPR state used by libultra / libdragon / homebrew.
    // Drawn from public HLE boot notes (cen64, ares, mupen documentation).
    for (std::size_t i = 0; i < 32; ++i) {
        cpu.set_gpr(i, 0);
    }

    // Some homebrew reads these "os*" seeds:
    //   s3 (r19) osRomType   — 0 = cart
    //   s4 (r20) osTvType    — 0 PAL, 1 NTSC, 2 MPAL
    //   s5 (r21) osResetType — 0 cold, 1 NMI
    //   s6 (r22) osCicId     — CIC seed
    //   s7 (r23) osVersion   — often 0
    cpu.set_gpr(6,  0xFFFF'FFFFu);                              // a2
    cpu.set_gpr(11, 0xFFFF'FFFFu);                              // t3
    cpu.set_gpr(19, 0x0000'0000u);                              // s3 osRomType=cart
    cpu.set_gpr(20, static_cast<u64>(static_cast<u8>(hdr.tv))); // s4 osTvType
    cpu.set_gpr(21, reset_type & 1u);                           // s5 osResetType
    cpu.set_gpr(22, static_cast<u64>(hdr.cic_seed));            // s6 CIC seed
    cpu.set_gpr(23, 0x0000'0000u);                              // s7 osVersion
    cpu.set_gpr(24, 0x0000'0003u);                              // t8
    cpu.set_gpr(25, 0xFFFF'FFFFu);                              // t9
    cpu.set_gpr(29, sp_top);                                    // sp
    cpu.set_gpr(31, 0xA400'1550ull);                            // ra (IPL3 stub)

    // COP0 after IPL: BEV=0 so game handlers in RDRAM work; CU0+CU1+FR.
    cpu.set_cop0(Cop0Reg::Status,  0x3400'0000u);
    cpu.set_cop0(Cop0Reg::Config,  0x7006'E463u);
    cpu.set_cop0(Cop0Reg::Count,   0);
    cpu.set_cop0(Cop0Reg::Compare, 0xFFFF'FFFFu);
    cpu.set_cop0(Cop0Reg::Cause,   0);
    cpu.set_cop0(Cop0Reg::EPC,     0);
    cpu.set_cop0(Cop0Reg::PRId,    0x0000'0B22u);
    cpu.set_cop0(Cop0Reg::Context, 0);
    cpu.set_cop0(Cop0Reg::Wired,   0);
    cpu.set_cop0(Cop0Reg::BadVAddr,0);
    cpu.set_cop0(Cop0Reg::Random,  31);

    cpu.set_pc(hdr.entrypoint);
    cpu.set_halted(false);
}

void seed_pif_ram(Pif& pif, const CartHeader& hdr, u32 reset_type) {
    auto ram = pif.ram();
    std::fill(ram.begin(), ram.end(), static_cast<u8>(0));

    // Boot sideband (public HLE conventions used by several emulators):
    //   [0x24..0x27] sometimes hold CIC challenge remnants — leave 0
    //   [0x26]       CIC seed (alternate location used by some HLE)
    //   [0x27]       CIC seed copy
    //   [0x3C]       reset type in low bits of a status word region
    //   [0x3F]       Joybus control; keep clear until software requests a transaction
    ram[0x26] = hdr.cic_seed;
    ram[0x27] = hdr.cic_seed;
    ram[0x3C] = static_cast<u8>(reset_type & 0xFFu);
    ram[0x3F] = 0;
}

std::size_t copy_rom_to_rdram(Bus& bus, u32 dest_phys, std::size_t nbytes) {
    const auto cart = bus.cartridge();
    if (cart.size() <= 0x1000) {
        return 0;
    }
    const std::size_t available = cart.size() - 0x1000;
    const std::size_t n = std::min(nbytes, available);
    if (n == 0 || bus.rdram().empty()) {
        return 0;
    }

    bus.write32(mmio::PI_BASE + PeripheralInterface::DramAddr, dest_phys);
    bus.write32(mmio::PI_BASE + PeripheralInterface::CartAddr,
                mmio::CART_DOM1_BASE + 0x1000);
    bus.write32(mmio::PI_BASE + PeripheralInterface::RdLen,
                static_cast<u32>(n - 1));
    // Clear PI interrupt from boot DMA so the game starts clean.
    bus.write32(mmio::PI_BASE + PeripheralInterface::Status,
                PeripheralInterface::WrClrIntr);
    return n;
}

bool install_pif_rom(Bus& bus, std::span<const u8> data) {
    if (data.empty()) {
        return false;
    }
    // PIF ROM window: physical 0x1FC00000, typically 2048 bytes.
    // Bus currently only maps PIF RAM at 0x1FC007C0. For LLE we write into a
    // dedicated cart-like region via bus helper if available; otherwise copy
    // into the low PIF ROM area through write8 to open storage.
    //
    // Phase 3 LLE support: store in bus cartridge-adjacent PIF ROM buffer.
    return bus.load_pif_rom(data);
}

bool install_ipl3_to_dmem(Bus& bus, Rsp& rsp, std::span<const u8> ipl3) {
    // Real IPL2 copies cart[0x40..0x1000) into SP DMEM starting at 0x40.
    // We mirror that layout for inspection / future stepping.
    if (ipl3.size() < 16) {
        return false;
    }
    const std::size_t n = std::min<std::size_t>(ipl3.size(), 0x1000 - 0x40);
    auto dmem = bus.sp_dmem();
    auto rdmem = rsp.dmem();
    for (std::size_t i = 0; i < n; ++i) {
        const std::size_t dst = 0x40 + i;
        if (dst < dmem.size()) {
            dmem[dst] = ipl3[i];
        }
        if (dst < rdmem.size()) {
            rdmem[dst] = ipl3[i];
        }
    }
    // Also place a copy at DMEM 0 for convenience of some tooling.
    N64_INFO("Installed user IPL3 ({} bytes) into SP DMEM @ +0x40", n);
    return true;
}

} // namespace

std::vector<u8> load_binary_file(std::string_view path) {
    std::ifstream in(std::string(path), std::ios::binary | std::ios::ate);
    if (!in) {
        N64_ERROR("Cannot open binary: {}", path);
        return {};
    }
    const auto size = in.tellg();
    if (size <= 0) {
        N64_ERROR("Binary is empty: {}", path);
        return {};
    }
    in.seekg(0, std::ios::beg);
    std::vector<u8> data(static_cast<std::size_t>(size));
    if (!in.read(reinterpret_cast<char*>(data.data()), size)) {
        N64_ERROR("Failed reading binary: {}", path);
        return {};
    }
    return data;
}

BootResult hle_boot_loaded(Bus& bus, Cpu& cpu, Pif& pif, Rsp& rsp, const BootConfig& cfg) {
    BootResult result{};
    result.mode = cfg.mode;

    if (!bus.has_cartridge() && cfg.mode == BootMode::Hle) {
        result.error = "no cartridge loaded";
        N64_ERROR("boot: {}", result.error);
        return result;
    }

    CartHeader hdr{};
    if (bus.has_cartridge()) {
        hdr = parse_cart_header(bus.cartridge());
    }

    if (cfg.force_cic != CicType::Unknown) {
        hdr.cic = cfg.force_cic;
        hdr.cic_seed = cic_seed(hdr.cic);
        hdr.entrypoint = fixup_entrypoint(hdr.cic, hdr.pc);
        if (hdr.entrypoint == 0) {
            hdr.entrypoint = 0x8000'0400u;
        }
    }
    if (cfg.force_tv_set) {
        hdr.tv = cfg.force_tv;
    }

    if (bus.has_cartridge() && !hdr.valid_magic()) {
        N64_WARN("Cart header magic {:08X} is unusual (expected 0x80......)",
                 hdr.pi_bsd_dom1);
    }

    result.header = hdr;
    result.entrypoint = hdr.entrypoint;

    // Optional user PIF ROM (legal dump only — never bundled).
    if (!cfg.pif_rom_path.empty()) {
        const auto blob = load_binary_file(cfg.pif_rom_path);
        if (blob.empty()) {
            result.error = "failed to load PIF ROM";
            return result;
        }
        if (blob.size() < 0x100 || blob.size() > 0x2000) {
            N64_WARN("PIF ROM size {} is unusual (expected ~2048 bytes)", blob.size());
        }
        if (!install_pif_rom(bus, blob)) {
            result.error = "bus rejected PIF ROM";
            return result;
        }
        result.pif_rom_loaded = true;
        N64_INFO("Loaded user PIF ROM ({} bytes) from {}", blob.size(), cfg.pif_rom_path);
    }

    // Optional user IPL3 bytes (installed after RSP reset so they survive).
    std::vector<u8> ipl3_blob;
    if (!cfg.ipl3_path.empty()) {
        ipl3_blob = load_binary_file(cfg.ipl3_path);
        if (ipl3_blob.empty()) {
            result.error = "failed to load IPL3 file";
            return result;
        }
    } else if (bus.has_cartridge() && bus.cartridge().size() >= 0x1000) {
        const auto cart = bus.cartridge();
        ipl3_blob.assign(cart.begin() + 0x40, cart.begin() + 0x1000);
    }

    // ---------- LLE PIF path ----------
    if (cfg.mode == BootMode::LlePif) {
        if (!result.pif_rom_loaded) {
            result.error = "LLE PIF boot requires --pif-rom PATH (user-owned dump)";
            N64_ERROR("boot: {}", result.error);
            return result;
        }
        if (bus.has_cartridge()) {
            apply_pi_dom1_from_header(bus, hdr.pi_bsd_dom1);
        }
        seed_pif_ram(pif, hdr, cfg.reset_type);
        auto br = bus.pif_ram();
        auto pr = pif.ram();
        std::copy(pr.begin(), pr.end(), br.begin());

        for (std::size_t i = 0; i < 32; ++i) {
            cpu.set_gpr(i, 0);
        }
        // BEV=1 so boot exception vectors stay in KSEG1 while PIF runs.
        cpu.set_cop0(Cop0Reg::Status, 0x3440'0000u);
        cpu.set_cop0(Cop0Reg::Config, 0x7006'E463u);
        cpu.set_pc(0xBFC0'0000ull);
        cpu.set_halted(false);
        rsp.reset();
        if (!ipl3_blob.empty()) {
            result.ipl3_loaded = install_ipl3_to_dmem(bus, rsp, ipl3_blob);
        }
        result.entrypoint = 0xBFC0'0000u;
        result.header.entrypoint = result.entrypoint;
        result.ok = true;
        N64_INFO("Boot LLE-PIF: PC=BFC00000 (user PIF ROM), cart={}",
                 bus.has_cartridge() ? "yes" : "no");
        return result;
    }

    // ---------- HLE path ----------
    N64_INFO("Boot HLE: title=\"{}\" CIC={} TV={} entry={:08X} crc1={:08X} ipl3_crc={:08X}",
             hdr.title(), cic_name(hdr.cic), tv_name(hdr.tv), hdr.entrypoint,
             hdr.crc1, hdr.ipl3_crc32);

    if (bus.has_cartridge()) {
        apply_pi_dom1_from_header(bus, hdr.pi_bsd_dom1);
    }

    rsp.reset();
    bus.write32(mmio::SP_REGS_BASE + SpRegisters::Status,
                SpRegisters::WrSetHalt | SpRegisters::WrClearIntr);

    // Mirror IPL3 into DMEM after reset (tooling / future LLE IPL3).
    if (!ipl3_blob.empty()) {
        result.ipl3_loaded = install_ipl3_to_dmem(bus, rsp, ipl3_blob);
    }

    const u32 dest = load_phys_from_entry(hdr.entrypoint);
    std::size_t load_size = cfg.load_size;
    if (load_size == 0 && bus.has_cartridge()) {
        const auto cart = bus.cartridge();
        const std::size_t avail = cart.size() > 0x1000 ? cart.size() - 0x1000 : 0;
        load_size = std::min<std::size_t>(avail, 1u * 1024u * 1024u);
    }

    if (!cfg.skip_rom_copy && bus.has_cartridge()) {
        result.bytes_copied = copy_rom_to_rdram(bus, dest, load_size);
        N64_INFO("Boot HLE: copied {} bytes cart+0x1000 → RDRAM {:08X}",
                 result.bytes_copied, dest);
    } else {
        result.bytes_copied = 0;
    }

    seed_pif_ram(pif, hdr, cfg.reset_type);
    auto br = bus.pif_ram();
    auto pr = pif.ram();
    std::copy(pr.begin(), pr.end(), br.begin());

    const u32 sp_top = static_cast<u32>(bus.rdram_size() - 0x10) | 0xA000'0000u;
    seed_cpu_after_ipl3(cpu, hdr, sp_top, cfg.reset_type);

    result.ok = true;
    return result;
}

} // namespace n64
