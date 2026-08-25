#pragma once

#include "n64/common/types.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace n64 {

class Bus;
class Cpu;

/// A sequence of instructions executed together as a basic block.
struct BasicBlock {
    u64 start_pc = 0;
    PhysicalAddress paddr = 0;
    std::vector<u32> insns;
    bool ends_with_branch = false;
};

/// Cache of pre-compiled/pre-fetched basic blocks to eliminate fetch/translate overhead.
class BlockCache {
public:
    static constexpr std::size_t kMaxBlockInsns = 32;
    static constexpr std::size_t kDirectTableSize = 65536; // 64K direct-mapped slots
    static constexpr u64 kDirectTableMask = kDirectTableSize - 1;

    BlockCache();
    ~BlockCache();

    BlockCache(const BlockCache&) = delete;
    BlockCache& operator=(const BlockCache&) = delete;

    /// Look up a cached block by virtual PC.
    [[nodiscard]] const BasicBlock* lookup(u64 vaddr) const noexcept;

    /// Compile a new basic block starting at `vaddr`.
    const BasicBlock* compile(u64 vaddr, const Bus& bus, const Cpu& cpu);

    /// Invalidate blocks that overlap the modified physical memory range.
    void invalidate(PhysicalAddress paddr, u32 size);

    /// Clear all cached blocks.
    void clear() noexcept;

    [[nodiscard]] std::size_t size() const noexcept { return count_; }
    [[nodiscard]] u64 hits() const noexcept { return hits_; }
    [[nodiscard]] u64 misses() const noexcept { return misses_; }

private:
    struct Slot {
        u64 tag_pc = ~0ull;
        BasicBlock block;
        bool valid = false;
    };

    std::vector<Slot> table_;
    std::size_t count_ = 0;
    mutable u64 hits_ = 0;
    mutable u64 misses_ = 0;
};

} // namespace n64