#include <gtest/gtest.h>

#include "n64/cpu/insn.hpp"
#include "tests/cpu/test_cpu_harness.hpp"

#include <cstdio>
#include <string>
#include <vector>

using namespace n64;
using namespace n64::insn;
using n64::test::CpuHarness;

// =============================================================================
// ALU immediate / register
// =============================================================================

TEST(CpuInterpreter, AddiuLuiOri) {
    CpuHarness h;
    // li $t0, 0x12345678  => lui + ori
    h.write_code({
        lui(8, 0x1234),          // $t0
        ori(8, 8, 0x5678),
        nop(),
    });
    h.start();
    h.run_steps(2);
    // 0x12345678 is positive as s32 → zero-extended into 64-bit GPR via sext32.
    EXPECT_EQ(h.cpu().gpr(8), 0x00000000'12345678ull);
    // Negative LUI result is sign-extended:
    h.write_code({ lui(9, 0x8000), nop() });
    h.start();
    h.run_steps(1);
    EXPECT_EQ(h.cpu().gpr(9), 0xFFFFFFFF'80000000ull);
}

TEST(CpuInterpreter, AdduSubuAndOrXorNor) {
    CpuHarness h;
    h.cpu().set_gpr(8, 0x0000'00F0);  // t0
    h.cpu().set_gpr(9, 0x0000'000F);  // t1
    h.write_code({
        addu(10, 8, 9),   // t2 = t0+t1 = 0xFF
        subu(11, 8, 9),   // t3 = 0xE1
        and_(12, 8, 9),   // t4 = 0
        or_(13, 8, 9),    // t5 = 0xFF
        xor_(14, 8, 9),   // t6 = 0xFF
        nor(15, 8, 9),    // t7 = ~0xFF
        nop(),
    });
    h.start();
    h.run_steps(6);
    EXPECT_EQ(h.cpu().gpr(10) & 0xFFFFFFFF, 0xFFu);
    EXPECT_EQ(h.cpu().gpr(11) & 0xFFFFFFFF, 0xE1u);
    EXPECT_EQ(h.cpu().gpr(12) & 0xFFFFFFFF, 0x00u);
    EXPECT_EQ(h.cpu().gpr(13) & 0xFFFFFFFF, 0xFFu);
    EXPECT_EQ(h.cpu().gpr(14) & 0xFFFFFFFF, 0xFFu);
    EXPECT_EQ(h.cpu().gpr(15) & 0xFFFFFFFF, 0xFFFFFF00u);
}

TEST(CpuInterpreter, SltSlti) {
    CpuHarness h;
    h.cpu().set_gpr(4, static_cast<u64>(static_cast<s64>(-5)));
    h.cpu().set_gpr(5, 3);
    h.write_code({
        slt(6, 4, 5),     // -5 < 3 → 1
        sltu(7, 4, 5),    // unsigned big < 3 → 0
        slti(8, 4, 0),    // -5 < 0 → 1
        sltiu(9, 5, 100), // 3 < 100 → 1
        nop(),
    });
    h.start();
    h.run_steps(4);
    EXPECT_EQ(h.cpu().gpr(6), 1u);
    EXPECT_EQ(h.cpu().gpr(7), 0u);
    EXPECT_EQ(h.cpu().gpr(8), 1u);
    EXPECT_EQ(h.cpu().gpr(9), 1u);
}

TEST(CpuInterpreter, Shifts) {
    CpuHarness h;
    h.cpu().set_gpr(4, 0x8000'0000u);
    h.cpu().set_gpr(5, 4);
    h.write_code({
        sll(6, 4, 1),     // 0x00000000 (shift out)
        srl(7, 4, 1),     // 0x40000000
        sra(8, 4, 1),     // 0xC0000000 sign-ext
        sllv(9, 4, 5),    // << 4
        nop(),
    });
    h.start();
    h.run_steps(4);
    EXPECT_EQ(static_cast<u32>(h.cpu().gpr(6)), 0x0000'0000u);
    EXPECT_EQ(static_cast<u32>(h.cpu().gpr(7)), 0x4000'0000u);
    EXPECT_EQ(static_cast<u32>(h.cpu().gpr(8)), 0xC000'0000u);
    EXPECT_EQ(static_cast<u32>(h.cpu().gpr(9)), 0x0000'0000u);
}

TEST(CpuInterpreter, AddOverflowRaises) {
    CpuHarness h;
    // Install a trivial exception handler at 0x80000180 that just spins (ERET-less).
    // We'll only check that exception_count increments and EPC is set.
    h.cpu().set_gpr(4, 0x7FFF'FFFF);
    h.cpu().set_gpr(5, 1);
    h.write_code({
        add(6, 4, 5), // overflow
        nop(),
    });
    h.start();
    const u64 before = h.cpu().exception_count();
    h.run_steps(1);
    EXPECT_EQ(h.cpu().exception_count(), before + 1);
    EXPECT_EQ(h.cpu().cop0(Cop0Reg::EPC), CpuHarness::kBaseVirt);
    const u32 cause = h.cpu().cop0(Cop0Reg::Cause);
    EXPECT_EQ((cause >> 2) & 0x1F, ExcCode::Ov);
    // EXL set
    EXPECT_NE(h.cpu().cop0(Cop0Reg::Status) & 0x2u, 0u);
}

// =============================================================================
// Multiply / divide
// =============================================================================

TEST(CpuInterpreter, MultMfloMfhi) {
    CpuHarness h;
    h.cpu().set_gpr(4, 1000);
    h.cpu().set_gpr(5, 2000);
    h.write_code({
        mult(4, 5),
        mflo(6),
        mfhi(7),
        nop(),
    });
    h.start();
    h.run_steps(3);
    EXPECT_EQ(static_cast<u32>(h.cpu().gpr(6)), 2'000'000u);
    EXPECT_EQ(static_cast<u32>(h.cpu().gpr(7)), 0u);
}

TEST(CpuInterpreter, MultNegative) {
    CpuHarness h;
    h.cpu().set_gpr(4, static_cast<u64>(static_cast<s32>(-3)));
    h.cpu().set_gpr(5, static_cast<u64>(static_cast<s32>(5)));
    h.write_code({
        mult(4, 5),
        mflo(6),
        mfhi(7),
        nop(),
    });
    h.start();
    h.run_steps(3);
    EXPECT_EQ(static_cast<s32>(h.cpu().gpr(6)), -15);
    EXPECT_EQ(static_cast<s32>(h.cpu().gpr(7)), -1); // sign extend of high
}

TEST(CpuInterpreter, Div) {
    CpuHarness h;
    h.cpu().set_gpr(4, 100);
    h.cpu().set_gpr(5, 7);
    h.write_code({
        div_(4, 5),
        mflo(6), // quot 14
        mfhi(7), // rem 2
        nop(),
    });
    h.start();
    h.run_steps(3);
    EXPECT_EQ(static_cast<s32>(h.cpu().gpr(6)), 14);
    EXPECT_EQ(static_cast<s32>(h.cpu().gpr(7)), 2);
}

TEST(CpuInterpreter, Divu) {
    CpuHarness h;
    h.cpu().set_gpr(4, 0xFFFF'FFFFu);
    h.cpu().set_gpr(5, 2);
    h.write_code({
        divu(4, 5),
        mflo(6),
        mfhi(7),
        nop(),
    });
    h.start();
    h.run_steps(3);
    EXPECT_EQ(static_cast<u32>(h.cpu().gpr(6)), 0x7FFF'FFFFu);
    EXPECT_EQ(static_cast<u32>(h.cpu().gpr(7)), 1u);
}

// =============================================================================
// Load / store
// =============================================================================

TEST(CpuInterpreter, LwSw) {
    CpuHarness h;
    h.poke32(CpuHarness::kDataPhys, 0xDEADBEEFu);
    h.cpu().set_gpr(4, CpuHarness::kDataVirt); // base
    h.write_code({
        lw(5, 4, 0),
        addiu(5, 5, 1),          // 0xDEADBEF0
        sw(5, 4, 4),
        lw(6, 4, 4),
        nop(),
    });
    h.start();
    h.run_steps(4);
    EXPECT_EQ(static_cast<u32>(h.cpu().gpr(5)), 0xDEADBEF0u);
    EXPECT_EQ(static_cast<u32>(h.cpu().gpr(6)), 0xDEADBEF0u);
    EXPECT_EQ(h.peek32(CpuHarness::kDataPhys + 4), 0xDEADBEF0u);
}

TEST(CpuInterpreter, LbLbuSb) {
    CpuHarness h;
    h.poke8(CpuHarness::kDataPhys, 0xFE);
    h.cpu().set_gpr(4, CpuHarness::kDataVirt);
    h.write_code({
        lb(5, 4, 0),    // sign-ext 0xFF..FE
        lbu(6, 4, 0),   // zero-ext 0xFE
        addiu(7, 0, 0x7F),
        sb(7, 4, 1),
        lbu(8, 4, 1),
        nop(),
    });
    h.start();
    h.run_steps(5);
    EXPECT_EQ(h.cpu().gpr(5), 0xFFFFFFFF'FFFFFFFEull);
    EXPECT_EQ(h.cpu().gpr(6), 0xFEu);
    EXPECT_EQ(h.cpu().gpr(8), 0x7Fu);
    EXPECT_EQ(h.peek8(CpuHarness::kDataPhys + 1), 0x7Fu);
}

TEST(CpuInterpreter, LhLhuSh) {
    CpuHarness h;
    h.bus().write16(CpuHarness::kDataPhys, 0xABCD);
    h.cpu().set_gpr(4, CpuHarness::kDataVirt);
    h.write_code({
        lh(5, 4, 0),
        lhu(6, 4, 0),
        addiu(7, 0, static_cast<s16>(0x1234)),
        sh(7, 4, 2),
        lhu(8, 4, 2),
        nop(),
    });
    h.start();
    h.run_steps(5);
    EXPECT_EQ(h.cpu().gpr(5), 0xFFFFFFFF'FFFFABCDull);
    EXPECT_EQ(h.cpu().gpr(6), 0xABCDu);
    EXPECT_EQ(h.cpu().gpr(8), 0x1234u);
}

TEST(CpuInterpreter, LdSd) {
    CpuHarness h;
    h.bus().write64(CpuHarness::kDataPhys, 0x0123456789ABCDEFull);
    h.cpu().set_gpr(4, CpuHarness::kDataVirt);
    h.write_code({
        ld(5, 4, 0),
        sd(5, 4, 8),
        ld(6, 4, 8),
        nop(),
    });
    h.start();
    h.run_steps(3);
    EXPECT_EQ(h.cpu().gpr(5), 0x0123456789ABCDEFull);
    EXPECT_EQ(h.cpu().gpr(6), 0x0123456789ABCDEFull);
}

TEST(CpuInterpreter, UnalignedLwRaisesAdEL) {
    CpuHarness h;
    h.cpu().set_gpr(4, CpuHarness::kDataVirt + 1);
    h.cpu().set_gpr(5, 0xCAFE'BABE'1234'5678ull);
    h.write_code({
        lw(5, 4, 0),
        nop(),
    });
    h.start();
    const u64 before = h.cpu().exception_count();
    h.run_steps(1);
    EXPECT_EQ(h.cpu().exception_count(), before + 1);
    EXPECT_EQ((h.cpu().cop0(Cop0Reg::Cause) >> 2) & 0x1F, ExcCode::AdEL);
    EXPECT_EQ(h.cpu().cop0(Cop0Reg::BadVAddr), CpuHarness::kDataVirt + 1);
    EXPECT_EQ(h.cpu().gpr(5), 0xCAFE'BABE'1234'5678ull);
}

TEST(CpuInterpreter, LlScRequiresMatchingPhysicalAddress) {
    CpuHarness h;
    h.poke32(CpuHarness::kDataPhys, 41);
    h.poke32(CpuHarness::kDataPhys + 4, 0xDEAD'BEEFu);
    h.cpu().set_gpr(4, CpuHarness::kDataVirt);
    h.write_code({
        ll(5, 4, 0),
        addiu(5, 5, 1),
        sc(5, 4, 4),
        nop(),
    });
    h.start();
    h.run_steps(3);
    EXPECT_EQ(h.cpu().gpr(5), 0u);
    EXPECT_EQ(h.peek32(CpuHarness::kDataPhys + 4), 0xDEAD'BEEFu);
}

TEST(CpuInterpreter, LlScMatchingAddressStoresAndReportsSuccess) {
    CpuHarness h;
    h.poke32(CpuHarness::kDataPhys, 41);
    h.cpu().set_gpr(4, CpuHarness::kDataVirt);
    h.write_code({
        ll(5, 4, 0),
        addiu(5, 5, 1),
        sc(5, 4, 0),
        nop(),
    });
    h.start();
    h.run_steps(3);
    EXPECT_EQ(h.cpu().gpr(5), 1u);
    EXPECT_EQ(h.peek32(CpuHarness::kDataPhys), 42u);
}

TEST(CpuInterpreter, FaultingScPreservesSourceRegister) {
    CpuHarness h;
    h.poke32(CpuHarness::kDataPhys, 41);
    h.cpu().set_gpr(4, CpuHarness::kDataVirt);
    h.write_code({
        ll(5, 4, 0),
        addiu(5, 0, 0x55),
        sc(5, 4, 1),
        nop(),
    });
    h.start();
    h.run_steps(3);
    EXPECT_EQ((h.cpu().cop0(Cop0Reg::Cause) >> 2) & 0x1F, ExcCode::AdES);
    EXPECT_EQ(h.cpu().gpr(5), 0x55u);
}

// =============================================================================
// Branches / jumps / delay slots
// =============================================================================

TEST(CpuInterpreter, BeqTakenDelaySlot) {
    CpuHarness h;
    // PC layout @ 0x80001000:
    // 0: beq $0,$0, +2     -> target = 0x80001000+4 + 2*4 = 0x8000100C
    // 1: addiu $t0,$0,1    delay slot — MUST execute
    // 2: addiu $t1,$0,2    skipped
    // 3: addiu $t2,$0,3    landing
    // 4: nop
    h.write_code({
        beq(0, 0, 2),
        addiu(8, 0, 1),   // delay
        addiu(9, 0, 2),   // skipped
        addiu(10, 0, 3),  // land
        nop(),
    });
    h.start();
    h.run_steps(3); // branch + delay + land
    EXPECT_EQ(h.cpu().gpr(8), 1u);
    EXPECT_EQ(h.cpu().gpr(9), 0u);
    EXPECT_EQ(h.cpu().gpr(10), 3u);
    EXPECT_EQ(h.cpu().pc(), CpuHarness::kBaseVirt + 0x10); // after land insn
}

TEST(CpuInterpreter, BneNotTaken) {
    CpuHarness h;
    h.cpu().set_gpr(4, 1);
    h.cpu().set_gpr(5, 1);
    h.write_code({
        bne(4, 5, 2),
        addiu(8, 0, 1),  // delay always runs
        addiu(9, 0, 2),  // falls through
        addiu(10, 0, 3),
        nop(),
    });
    h.start();
    h.run_steps(3);
    EXPECT_EQ(h.cpu().gpr(8), 1u);
    EXPECT_EQ(h.cpu().gpr(9), 2u);
}

TEST(CpuInterpreter, BeqlNotTakenSkipsDelay) {
    CpuHarness h;
    h.cpu().set_gpr(4, 1);
    h.cpu().set_gpr(5, 2);
    // beql not taken → skip delay slot
    h.write_code({
        beql(4, 5, 2),
        addiu(8, 0, 1),  // should be skipped
        addiu(9, 0, 2),
        nop(),
    });
    h.start();
    h.run_steps(2);
    EXPECT_EQ(h.cpu().gpr(8), 0u);
    EXPECT_EQ(h.cpu().gpr(9), 2u);
}

TEST(CpuInterpreter, JAndJal) {
    CpuHarness h;
    // 0: jal target (0x80001010)
    // 1: addiu t0,0,1   delay
    // 2: addiu t1,0,2   (return point after jal)
    // 3: nop
    // 4: target: addiu t2,0,3
    // 5: jr ra
    // 6: nop delay
    const u32 base = CpuHarness::kBaseVirt;
    const u32 target = base + 0x10;
    h.write_code({
        jal(target),
        addiu(8, 0, 1),
        addiu(9, 0, 2),
        nop(),
        addiu(10, 0, 3), // target
        jr(31),
        nop(),
        nop(),
    });
    h.start();
    // jal + delay + body + jr + delay + return addiu
    h.run_steps(6);
    EXPECT_EQ(h.cpu().gpr(8), 1u);
    EXPECT_EQ(h.cpu().gpr(10), 3u);
    EXPECT_EQ(h.cpu().gpr(31), base + 8); // link = insn after delay
    EXPECT_EQ(h.cpu().gpr(9), 2u); // executed after return
}

TEST(CpuInterpreter, BgezalLink) {
    CpuHarness h;
    h.cpu().set_gpr(4, 5); // >= 0
    const u32 base = CpuHarness::kBaseVirt;
    h.write_code({
        bgezal(4, 2),     // link, branch to +2
        addiu(8, 0, 1),   // delay
        addiu(9, 0, 2),   // skipped
        addiu(10, 0, 3),  // land
        nop(),
    });
    h.start();
    h.run_steps(3);
    EXPECT_EQ(h.cpu().gpr(31), base + 8);
    EXPECT_EQ(h.cpu().gpr(8), 1u);
    EXPECT_EQ(h.cpu().gpr(10), 3u);
}

// =============================================================================
// COP0 / exceptions
// =============================================================================

TEST(CpuInterpreter, Mtc0Mfc0) {
    CpuHarness h;
    h.cpu().set_gpr(4, 0x1234'5678);
    h.write_code({
        mtc0(4, Cop0Reg::TagLo),
        mfc0(5, Cop0Reg::TagLo),
        nop(),
    });
    h.start();
    h.run_steps(2);
    EXPECT_EQ(static_cast<u32>(h.cpu().gpr(5)), 0x1234'5678u);
}

TEST(CpuInterpreter, SyscallAndEret) {
    CpuHarness h;
    // Program at 0x80001000:
    //   syscall
    //   addiu t0, 0, 0x11   ; should run after ERET
    //   nop
    // Handler at 0x80000180:
    //   addiu t1, 0, 0x22
    //   mfc0 t2, EPC
    //   addiu t2, t2, 4     ; skip syscall
    //   mtc0 t2, EPC
    //   eret
    //   nop
    h.write_code({
        syscall(),
        addiu(8, 0, 0x11),
        nop(),
    });

    // Exception handler (BEV=0 → 0x80000180 → phys 0x180)
    const u32 handler_phys = 0x180;
    h.write_code({
        addiu(9, 0, 0x22),
        mfc0(10, Cop0Reg::EPC),
        addiu(10, 10, 4),
        mtc0(10, Cop0Reg::EPC),
        eret(),
        nop(),
    }, handler_phys);

    h.start();
    // syscall → handler (5 insns) → eret → addiu t0
    h.run_steps(8);
    EXPECT_EQ(h.cpu().gpr(9), 0x22u);
    EXPECT_EQ(h.cpu().gpr(8), 0x11u);
    EXPECT_EQ(h.cpu().exception_count(), 1u);
    // EXL cleared after ERET
    EXPECT_EQ(h.cpu().cop0(Cop0Reg::Status) & 0x2u, 0u);
}

TEST(CpuInterpreter, BreakException) {
    CpuHarness h;
    h.write_code({
        brk(0x42),
        nop(),
    });
    h.start();
    h.run_steps(1);
    EXPECT_EQ((h.cpu().cop0(Cop0Reg::Cause) >> 2) & 0x1F, ExcCode::Bp);
}

TEST(CpuInterpreter, BranchDelayExceptionSetsBD) {
    CpuHarness h;
    // beq taken; delay slot is BREAK
    h.write_code({
        beq(0, 0, 4),
        brk(1),
        nop(),
    });
    // minimal handler that just ERETs back (will re-break — only check Cause.BD)
    h.write_code({
        eret(),
        nop(),
    }, 0x180);

    h.start();
    h.run_steps(2); // branch + break in delay
    EXPECT_NE(h.cpu().cop0(Cop0Reg::Cause) & (1u << 31), 0u); // BD
    // EPC points to the branch, not the break
    EXPECT_EQ(h.cpu().cop0(Cop0Reg::EPC), CpuHarness::kBaseVirt);
}

TEST(CpuInterpreter, NotTakenBranchDelayExceptionSetsBD) {
    CpuHarness h;
    h.cpu().set_gpr(4, 7);
    h.cpu().set_gpr(5, 7);
    h.write_code({
        bne(4, 5, 4), // not taken, but the following instruction is still a delay slot
        brk(1),
        nop(),
    });
    h.start();
    h.run_steps(2);
    EXPECT_NE(h.cpu().cop0(Cop0Reg::Cause) & (1u << 31), 0u);
    EXPECT_EQ(h.cpu().cop0(Cop0Reg::EPC), CpuHarness::kBaseVirt);
}

TEST(CpuInterpreter, DaddAndDsubOverflowPreserveDestination) {
    CpuHarness h;
    h.cpu().set_gpr(4, 0x7FFF'FFFF'FFFF'FFFFull);
    h.cpu().set_gpr(5, 1);
    h.cpu().set_gpr(6, 0x1234);
    h.write_code({
        dadd(6, 4, 5),
        nop(),
    });
    h.start();
    h.run_steps(1);
    EXPECT_EQ((h.cpu().cop0(Cop0Reg::Cause) >> 2) & 0x1F, ExcCode::Ov);
    EXPECT_EQ(h.cpu().gpr(6), 0x1234u);

    CpuHarness h2;
    h2.cpu().set_gpr(4, 0x8000'0000'0000'0000ull);
    h2.cpu().set_gpr(5, 1);
    h2.cpu().set_gpr(6, 0x5678);
    h2.write_code({
        dsub(6, 4, 5),
        nop(),
    });
    h2.start();
    h2.run_steps(1);
    EXPECT_EQ((h2.cpu().cop0(Cop0Reg::Cause) >> 2) & 0x1F, ExcCode::Ov);
    EXPECT_EQ(h2.cpu().gpr(6), 0x5678u);
}

TEST(CpuInterpreter, DmultHandlesInt64MinMagnitude) {
    CpuHarness h;
    h.cpu().set_gpr(4, 0x8000'0000'0000'0000ull);
    h.cpu().set_gpr(5, 0xFFFF'FFFF'FFFF'FFFFull);
    h.write_code({
        dmult(4, 5),
        mfhi(6),
        mflo(7),
        nop(),
    });
    h.start();
    h.run_steps(3);
    EXPECT_EQ(h.cpu().gpr(6), 0u);
    EXPECT_EQ(h.cpu().gpr(7), 0x8000'0000'0000'0000ull);
}

// =============================================================================
// Synthetic program: sum 1..N
// =============================================================================

TEST(CpuInterpreter, ProgramSum1To10) {
    CpuHarness h;
    // t0 = counter (1..10), t1 = sum, t2 = limit 10
    // 0: addiu t0, zero, 1
    // 1: addiu t1, zero, 0
    // 2: addiu t2, zero, 10
    // 3: loop: addu t1, t1, t0
    // 4: addiu t0, t0, 1
    // 5: slt  t3, t2, t0     ; t3 = (10 < t0)
    // 6: beq  t3, zero, -4   ; while !t3 → loop (back to addu)
    // 7: nop
    // 8: nop  ; done
    h.write_code({
        addiu(8, 0, 1),
        addiu(9, 0, 0),
        addiu(10, 0, 10),
        addu(9, 9, 8),       // loop
        addiu(8, 8, 1),
        slt(11, 10, 8),
        beq(11, 0, -4),      // back 4 insns to addu
        nop(),
        nop(),               // done PC
    });
    h.start();
    const u64 done = CpuHarness::kBaseVirt + 8 * 4;
    const Cycles steps = h.run_until(done, 200);
    EXPECT_LT(steps, 200u);
    EXPECT_EQ(h.cpu().gpr(9), 55u); // 1+..+10
    EXPECT_EQ(h.cpu().gpr(8), 11u);
}

TEST(CpuInterpreter, ProgramMemoryCopy) {
    CpuHarness h;
    // Copy 4 words from src to dst using a loop.
    const u32 src = CpuHarness::kDataPhys;
    const u32 dst = CpuHarness::kDataPhys + 0x100;
    for (u32 i = 0; i < 4; ++i) {
        h.poke32(src + i * 4, 0xA0000000u + i);
    }
    // a0=src_va, a1=dst_va, a2=4
    h.cpu().set_gpr(4, CpuHarness::kDataVirt);
    h.cpu().set_gpr(5, CpuHarness::kDataVirt + 0x100);
    h.cpu().set_gpr(6, 4);
    // 0: beq a2,0, +5     done
    // 1: nop
    // 2: lw t0, 0(a0)
    // 3: sw t0, 0(a1)
    // 4: addiu a0, a0, 4
    // 5: addiu a1, a1, 4
    // 6: addiu a2, a2, -1
    // 7: beq zero, zero, -8  ; back to top
    // 8: nop
    // 9: nop done
    h.write_code({
        beq(6, 0, 5),
        nop(),
        lw(8, 4, 0),
        sw(8, 5, 0),
        addiu(4, 4, 4),
        addiu(5, 5, 4),
        addiu(6, 6, -1),
        beq(0, 0, -8),
        nop(),
        nop(),
    });
    h.start();
    h.run_until(CpuHarness::kBaseVirt + 9 * 4, 500);
    for (u32 i = 0; i < 4; ++i) {
        EXPECT_EQ(h.peek32(dst + i * 4), 0xA0000000u + i) << "word " << i;
    }
}

// =============================================================================
// Trace determinism
// =============================================================================

TEST(CpuInterpreter, TraceIsDeterministic) {
    auto run_once = []() {
        CpuHarness h;
        h.write_code({
            addiu(4, 0, 1),
            addiu(5, 0, 2),
            addu(6, 4, 5),
            nop(),
        });
        h.start();
        std::vector<std::string> lines;
        h.cpu().set_trace([&](u64 pc, u32 insn, const std::string& d) {
            char buf[128];
            std::snprintf(buf, sizeof(buf), "%08llX %08X %s",
                          static_cast<unsigned long long>(pc), insn, d.c_str());
            lines.emplace_back(buf);
        });
        h.run_steps(3);
        return lines;
    };
    const auto a = run_once();
    const auto b = run_once();
    ASSERT_EQ(a.size(), 3u);
    EXPECT_EQ(a, b);
}

// =============================================================================
// Zero register hardwired under real ops
// =============================================================================

TEST(CpuInterpreter, ZeroRegisterStaysZero) {
    CpuHarness h;
    h.write_code({
        addiu(0, 0, 0x1234),
        lui(0, 0xFFFF),
        addu(0, 0, 0),
        nop(),
    });
    h.start();
    h.run_steps(3);
    EXPECT_EQ(h.cpu().gpr(0), 0u);
}

TEST(CpuInterpreter, Kseg0Kseg1Translate) {
    Cpu cpu;
    PhysicalAddress p = 0;
    EXPECT_TRUE(cpu.translate(0x8000'1234ull, false, p));
    EXPECT_EQ(p, 0x0000'1234u);
    EXPECT_TRUE(cpu.translate(0xA000'5678ull, false, p));
    EXPECT_EQ(p, 0x0000'5678u);
    EXPECT_FALSE(cpu.translate(0x0000'0100ull, false, p));
}
