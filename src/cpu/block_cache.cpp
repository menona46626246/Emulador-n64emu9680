#include "n64/cpu/block_cache.hpp"

#include "n64/bus/bus.hpp"
#include "n64/cpu/cpu.hpp"

namespace n64 {
namespace {

inline bool is_branch_or_jump(u32 insn) noexcept {
    const u32 op = insn >> 26;
    switch (op) {
    case 0x00: { // SPECIAL
        const u32 fn = insn & 63;
        return (fn == 0x08 /* JR */ || fn == 0x09 /* JALR */);
    }
    case 0x01: { // REGIMM
        const u32 rt = (insn >> 16) & 31;
        return (rt == 0x00 /* BLTZ */ || rt == 0x01 /* BGEZ */ ||
                rt == 0x02 /* BLTZL */ || rt == 0x03 /* BGEZL */ ||
                rt == 0x10 /* BLTZAL */ || rt == 0x11 /* BGEZAL */ ||
                rt == 0x12 /* BLTZALL */ || rt == 0x13 /* BGEZALL */);
    }
    case 0x02: // J
    case 0x03: // JAL
    case 0x04: // BEQ
    case 0x05: // BNE
    case 0x06: // BLEZ
    case 0x07: // BGTZ
    case 0x14: // BEQL
    case 0x15: // BNEL
    case 0x16: // BLEZL
    case 0x17: // BGTZL
        return true;
    default:
        return false;
    }
}

inline bool is_terminal_no_delay(u32 insn) noexcept {
    const u32 op = insn >> 26;
    if (op == 0x00) { // SPECIAL
        const u32 fn = insn & 63;
        return (fn == 0x0C /* SYSCALL */ || fn == 0x0D /* BREAK */);
    }
    if (op == 0x10) { // COP0
        const u32 rs = (insn >> 21) & 31;
        const u32 fn = insn & 63;
        if (rs == 0x10) {
            // TLB operations can change the physical mapping of subsequent
            // instructions; ERET redirects immediately.
            return fn == 0x01 || fn == 0x02 || fn == 0x06 ||
                   fn == 0x08 || fn == 0x18;
        }
        if (rs == 0x04) { // MTC0
            const u32 rd = (insn >> 11) & 31;
            // Status, Cause, Compare, EntryHi, Config
            if (rd == 12 || rd == 13 || rd == 11 || rd == 10 || rd == 16) {
                return true;
            }
        }
    }
    return false;
}

} // namespace

BlockCache::BlockCache()
    : table_(kDirectTableSize) {}

BlockCache::~BlockCache() = default;

void BlockCache::mark_physically_aliased(std::size_t slot_index) {
    Slot& slot = table_[slot_index];
    if (slot.aliased_position != kNotAliased) return;
    slot.aliased_position = physically_aliased_slots_.size();
    physically_aliased_slots_.push_back(slot_index);
}

void BlockCache::unmark_physically_aliased(std::size_t slot_index) noexcept {
    Slot& slot = table_[slot_index];
    if (slot.aliased_position == kNotAliased) return;

    const std::size_t position = slot.aliased_position;
    const std::size_t replacement = physically_aliased_slots_.back();
    physically_aliased_slots_[position] = replacement;
    table_[replacement].aliased_position = position;
    physically_aliased_slots_.pop_back();
    slot.aliased_position = kNotAliased;
}

void BlockCache::invalidate_slot(std::size_t slot_index) noexcept {
    Slot& slot = table_[slot_index];
    if (!slot.valid) return;
    unmark_physically_aliased(slot_index);
    slot.valid = false;
    slot.tag_pc = ~0ull;
    if (count_ > 0) --count_;
}

const BasicBlock* BlockCache::lookup(u64 vaddr) const noexcept {
    const std::size_t idx = static_cast<std::size_t>((vaddr >> 2) & kDirectTableMask);
    const Slot& slot = table_[idx];
    if (slot.valid && slot.tag_pc == vaddr) {
        ++hits_;
        return &slot.block;
    }
    ++misses_;
    return nullptr;
}

const BasicBlock* BlockCache::compile(u64 vaddr, const Bus& bus, const Cpu& cpu) {
    PhysicalAddress first_paddr = 0;
    if (!cpu.translate(vaddr, false, first_paddr)) {
        return nullptr;
    }

    BasicBlock blk;
    blk.start_pc = vaddr;
    blk.paddr = first_paddr;
    blk.insns.reserve(kMaxBlockInsns);
    blk.ends_with_branch = false;

    u64 curr_pc = vaddr;
    for (std::size_t i = 0; i < kMaxBlockInsns; ++i) {
        PhysicalAddress paddr = 0;
        if (!cpu.translate(curr_pc, false, paddr)) {
            break;
        }
        const u32 insn = bus.read32_const(paddr);
        blk.insns.push_back(insn);

        if (is_branch_or_jump(insn)) {
            // Fetch delay slot instruction
            const u64 delay_pc = curr_pc + 4;
            PhysicalAddress delay_paddr = 0;
            if (cpu.translate(delay_pc, false, delay_paddr)) {
                const u32 delay_insn = bus.read32_const(delay_paddr);
                blk.insns.push_back(delay_insn);
            }
            blk.ends_with_branch = true;
            break;
        }

        if (is_terminal_no_delay(insn)) {
            break;
        }

        curr_pc += 4;
        // Stop at 4KB page boundary
        if ((curr_pc & 0xFFFu) == 0) {
            break;
        }
    }

    if (blk.insns.empty()) {
        return nullptr;
    }

    const std::size_t idx = static_cast<std::size_t>((vaddr >> 2) & kDirectTableMask);
    Slot& slot = table_[idx];
    unmark_physically_aliased(idx);
    if (!slot.valid) {
        ++count_;
    }
    slot.tag_pc = vaddr;
    slot.block = std::move(blk);
    slot.valid = true;
    const u32 virtual32 = static_cast<u32>(vaddr);
    const bool direct_mapped =
        virtual32 >= 0x8000'0000u && virtual32 <= 0xBFFF'FFFFu &&
        first_paddr == (virtual32 & 0x1FFF'FFFFu);
    if (!direct_mapped) {
        mark_physically_aliased(idx);
    }
    return &slot.block;
}

void BlockCache::invalidate(PhysicalAddress paddr, u32 size) {
    if (size == 0) {
        clear();
        return;
    }
    if (size >= kDirectTableSize * 4u) {
        clear();
        return;
    }
    const u64 p_end = static_cast<u64>(paddr) + size;
    const u64 first_word = static_cast<u64>(paddr) >> 2;
    const u64 last_word = (p_end - 1) >> 2;
    const u64 first_candidate = first_word > kMaxBlockInsns
                                    ? first_word - kMaxBlockInsns
                                    : 0;
    for (u64 word = first_candidate; word <= last_word; ++word) {
        Slot& slot = table_[static_cast<std::size_t>(word & kDirectTableMask)];
        if (!slot.valid) continue;
        const u64 blk_start = slot.block.paddr;
        const u64 blk_end = blk_start + static_cast<u64>(slot.block.insns.size() * 4);
        if (static_cast<u64>(paddr) < blk_end && p_end > blk_start) {
            invalidate_slot(static_cast<std::size_t>(word & kDirectTableMask));
        }
    }

    // TLB mappings can alias an unrelated physical page, so their virtual
    // direct-table index cannot be derived from `paddr`.
    std::size_t aliased = 0;
    while (aliased < physically_aliased_slots_.size()) {
        const std::size_t slot_index = physically_aliased_slots_[aliased];
        const Slot& slot = table_[slot_index];
        const u64 blk_start = slot.block.paddr;
        const u64 blk_end = blk_start + static_cast<u64>(slot.block.insns.size() * 4);
        if (static_cast<u64>(paddr) < blk_end && p_end > blk_start) {
            invalidate_slot(slot_index);
        } else {
            ++aliased;
        }
    }
}

void BlockCache::clear() noexcept {
    for (auto& slot : table_) {
        slot.valid = false;
        slot.tag_pc = ~0ull;
        slot.aliased_position = kNotAliased;
    }
    physically_aliased_slots_.clear();
    count_ = 0;
}

} // namespace n64
