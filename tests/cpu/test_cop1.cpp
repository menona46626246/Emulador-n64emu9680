#include <gtest/gtest.h>

#include "n64/cpu/insn.hpp"
#include "n64/cpu/block_cache.hpp"
#include "tests/cpu/test_cpu_harness.hpp"

#include <bit>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

using namespace n64;
using namespace n64::insn;
using n64::test::CpuHarness;

namespace {

constexpr u32 kFcrCondition = 1u << 23;
constexpr u32 kFcrFlushSubnormals = 1u << 24;
constexpr u32 kFcrCauseUnimplemented = 1u << 17;
constexpr u32 kFcrCauseInvalid = 1u << 16;
constexpr u32 kFcrCauseDivZero = 1u << 15;
constexpr u32 kFcrCauseOverflow = 1u << 14;
constexpr u32 kFcrCauseUnderflow = 1u << 13;
constexpr u32 kFcrCauseInexact = 1u << 12;
constexpr u32 kFcrEnableInvalid = 1u << 11;
constexpr u32 kFcrEnableDivZero = 1u << 10;
constexpr u32 kFcrFlagInvalid = 1u << 6;
constexpr u32 kFcrFlagDivZero = 1u << 5;
constexpr u32 kFcrFlagOverflow = 1u << 4;
constexpr u32 kFcrFlagUnderflow = 1u << 3;
constexpr u32 kFcrFlagInexact = 1u << 2;

u32 bits(float value) {
    return std::bit_cast<u32>(value);
}

u64 bits(double value) {
    return std::bit_cast<u64>(value);
}

float float_value(u32 value) {
    return std::bit_cast<float>(value);
}

double double_value(u64 value) {
    return std::bit_cast<double>(value);
}

u32 exception_code(const Cpu& cpu) {
    return (cpu.cop0(Cop0Reg::Cause) >> 2) & 0x1Fu;
}

} // namespace

TEST(CpuCop1, ResetAndTransferInstructionsExposeFcr0) {
    CpuHarness h;
    h.cpu().set_gpr(4, 0xFFFF'FFFF'8000'0001ull);
    h.cpu().set_gpr(6, 0x0123'4567'89AB'CDEFull);
    h.write_code({
        mtc1(4, 2),
        mfc1(5, 2),
        dmtc1(6, 4),
        dmfc1(7, 4),
        cfc1(8, 0),
    });
    h.start();
    h.run_steps(5);

    EXPECT_EQ(h.cpu().gpr(5), 0xFFFF'FFFF'8000'0001ull);
    EXPECT_EQ(h.cpu().gpr(7), 0x0123'4567'89AB'CDEFull);
    EXPECT_EQ(h.cpu().gpr(8), 0x0000'0B00u);
    EXPECT_EQ(h.cpu().fpr(2) & 0xFFFF'FFFFu, 0x8000'0001u);
    EXPECT_EQ(h.cpu().fcr31(), 0u);
}

TEST(CpuCop1, DisabledCoprocessorRaisesCpuWithCeOne) {
    CpuHarness h;
    h.cpu().set_cop0(Cop0Reg::Status,
                     h.cpu().cop0(Cop0Reg::Status) & ~(1u << 29));
    h.cpu().set_fpr(0, bits(1.0f));
    h.cpu().set_fpr(1, bits(2.0f));
    h.cpu().set_fpr(2, 0xDEAD'BEEFu);
    h.write_code({add_s(2, 0, 1)});
    h.start();
    h.run_steps(1);

    EXPECT_EQ(exception_code(h.cpu()), ExcCode::CpU);
    EXPECT_EQ((h.cpu().cop0(Cop0Reg::Cause) >> 28) & 3u, 1u);
    EXPECT_EQ(h.cpu().fpr(2), 0xDEAD'BEEFu);
}

TEST(CpuCop1, LoadsAndStoresSingleAndDoubleValues) {
    CpuHarness h;
    constexpr u64 kDoubleBits = 0x4009'21FB'5444'2D18ull;
    h.cpu().set_gpr(4, CpuHarness::kDataVirt);
    h.poke32(CpuHarness::kDataPhys, bits(1.25f));
    h.bus().write64(CpuHarness::kDataPhys + 8, kDoubleBits);
    h.write_code({
        lwc1(2, 4, 0),
        ldc1(4, 4, 8),
        swc1(2, 4, 0x10),
        sdc1(4, 4, 0x18),
    });
    h.start();
    h.run_steps(4);

    EXPECT_EQ(static_cast<u32>(h.cpu().fpr(2)), bits(1.25f));
    EXPECT_EQ(h.cpu().fpr(4), kDoubleBits);
    EXPECT_EQ(h.peek32(CpuHarness::kDataPhys + 0x10), bits(1.25f));
    EXPECT_EQ(h.bus().read64(CpuHarness::kDataPhys + 0x18), kDoubleBits);
}

TEST(CpuCop1, SingleArithmeticAndUnaryOperations) {
    CpuHarness h;
    h.cpu().set_fpr(0, bits(7.5f));
    h.cpu().set_fpr(1, bits(2.5f));
    h.write_code({
        add_s(2, 0, 1),
        sub_s(3, 0, 1),
        mul_s(4, 0, 1),
        div_s(5, 0, 1),
        sqrt_s(6, 0),
        neg_s(7, 1),
        abs_s(8, 7),
        mov_s(9, 0),
    });
    h.start();
    h.run_steps(8);

    EXPECT_FLOAT_EQ(float_value(static_cast<u32>(h.cpu().fpr(2))), 10.0f);
    EXPECT_FLOAT_EQ(float_value(static_cast<u32>(h.cpu().fpr(3))), 5.0f);
    EXPECT_FLOAT_EQ(float_value(static_cast<u32>(h.cpu().fpr(4))), 18.75f);
    EXPECT_FLOAT_EQ(float_value(static_cast<u32>(h.cpu().fpr(5))), 3.0f);
    EXPECT_FLOAT_EQ(float_value(static_cast<u32>(h.cpu().fpr(6))), std::sqrt(7.5f));
    EXPECT_FLOAT_EQ(float_value(static_cast<u32>(h.cpu().fpr(7))), -2.5f);
    EXPECT_FLOAT_EQ(float_value(static_cast<u32>(h.cpu().fpr(8))), 2.5f);
    EXPECT_EQ(static_cast<u32>(h.cpu().fpr(9)), bits(7.5f));
}

TEST(CpuCop1, DoubleArithmeticUsesFullRegistersInFrOneMode) {
    CpuHarness h;
    h.cpu().set_fpr(0, bits(9.0));
    h.cpu().set_fpr(1, bits(4.0));
    h.write_code({
        add_d(2, 0, 1),
        sub_d(3, 0, 1),
        mul_d(4, 0, 1),
        div_d(5, 0, 1),
        sqrt_d(6, 0),
    });
    h.start();
    h.run_steps(5);

    EXPECT_DOUBLE_EQ(double_value(h.cpu().fpr(2)), 13.0);
    EXPECT_DOUBLE_EQ(double_value(h.cpu().fpr(3)), 5.0);
    EXPECT_DOUBLE_EQ(double_value(h.cpu().fpr(4)), 36.0);
    EXPECT_DOUBLE_EQ(double_value(h.cpu().fpr(5)), 2.25);
    EXPECT_DOUBLE_EQ(double_value(h.cpu().fpr(6)), 3.0);
}

TEST(CpuCop1, ExplicitIntegerRoundingInstructions) {
    CpuHarness h;
    h.cpu().set_fpr(0, bits(2.5f));
    h.cpu().set_fpr(1, bits(-2.5f));
    h.write_code({
        round_w(FMT_S, 2, 0),
        trunc_w(FMT_S, 3, 1),
        ceil_w(FMT_S, 4, 0),
        floor_w(FMT_S, 5, 1),
    });
    h.start();
    h.run_steps(4);

    EXPECT_EQ(static_cast<s32>(h.cpu().fpr(2)), 2);
    EXPECT_EQ(static_cast<s32>(h.cpu().fpr(3)), -2);
    EXPECT_EQ(static_cast<s32>(h.cpu().fpr(4)), 3);
    EXPECT_EQ(static_cast<s32>(h.cpu().fpr(5)), -3);
}

TEST(CpuCop1, ConversionsHonorFcr31RoundingMode) {
    CpuHarness h;
    h.cpu().set_fpr(0, bits(3.75));
    h.cpu().set_fpr(2, static_cast<u32>(static_cast<s32>(-7)));
    h.cpu().set_fpr(4, static_cast<u64>(static_cast<s64>(9)));
    h.cpu().set_gpr(4, 1u); // RM=RZ
    h.write_code({
        ctc1(4, 31),
        cvt_w(FMT_D, 6, 0),
        cvt_s(FMT_W, 7, 2),
        cvt_d(FMT_L, 8, 4),
        cvt_s(FMT_D, 10, 0),
        cvt_d(FMT_S, 11, 10),
    });
    h.start();
    h.run_steps(6);

    EXPECT_EQ(static_cast<s32>(h.cpu().fpr(6)), 3);
    EXPECT_FLOAT_EQ(float_value(static_cast<u32>(h.cpu().fpr(7))), -7.0f);
    EXPECT_DOUBLE_EQ(double_value(h.cpu().fpr(8)), 9.0);
    EXPECT_FLOAT_EQ(float_value(static_cast<u32>(h.cpu().fpr(10))), 3.75f);
    EXPECT_DOUBLE_EQ(double_value(h.cpu().fpr(11)), 3.75);
}

TEST(CpuCop1, CompareControlsBranchAndExecutesDelaySlot) {
    CpuHarness h;
    h.cpu().set_fpr(0, bits(1.0f));
    h.cpu().set_fpr(1, bits(2.0f));
    h.write_code({
        c_cond_s(0, 1, 0x4), // C.OLT.S
        bc1t(2),
        addiu(8, 0, 1),      // delay slot
        addiu(9, 0, 99),     // skipped
        addiu(9, 0, 7),
    });
    h.start();
    h.run_steps(4);

    EXPECT_NE(h.cpu().fcr31() & kFcrCondition, 0u);
    EXPECT_EQ(h.cpu().gpr(8), 1u);
    EXPECT_EQ(h.cpu().gpr(9), 7u);
}

TEST(CpuCop1, FalseLikelyBranchNullifiesDelaySlot) {
    CpuHarness h;
    h.cpu().set_fpr(0, bits(3.0f));
    h.cpu().set_fpr(1, bits(2.0f));
    h.write_code({
        c_cond_s(0, 1, 0x4), // false
        bc1tl(1),
        addiu(8, 0, 55),     // nullified
        addiu(9, 0, 7),
    });
    h.start();
    h.run_steps(3);

    EXPECT_EQ(h.cpu().fcr31() & kFcrCondition, 0u);
    EXPECT_EQ(h.cpu().gpr(8), 0u);
    EXPECT_EQ(h.cpu().gpr(9), 7u);
}

TEST(CpuCop1, DisabledDivideByZeroStoresInfinityAndStatus) {
    CpuHarness h;
    h.cpu().set_fpr(0, bits(1.0f));
    h.cpu().set_fpr(1, bits(0.0f));
    h.write_code({div_s(2, 0, 1)});
    h.start();
    h.run_steps(1);

    EXPECT_TRUE(std::isinf(float_value(static_cast<u32>(h.cpu().fpr(2)))));
    EXPECT_NE(h.cpu().fcr31() & kFcrCauseDivZero, 0u);
    EXPECT_NE(h.cpu().fcr31() & kFcrFlagDivZero, 0u);
    EXPECT_EQ(h.cpu().exception_count(), 0u);
}

TEST(CpuCop1, EnabledExceptionPreservesDestinationAndRaisesFpe) {
    CpuHarness h;
    h.cpu().set_fpr(0, bits(1.0f));
    h.cpu().set_fpr(1, bits(0.0f));
    h.cpu().set_fpr(2, 0xDEAD'BEEFu);
    h.cpu().set_gpr(4, kFcrEnableDivZero);
    h.write_code({ctc1(4, 31), div_s(2, 0, 1)});
    h.start();
    h.run_steps(2);

    EXPECT_EQ(exception_code(h.cpu()), ExcCode::FPE);
    EXPECT_NE(h.cpu().fcr31() & kFcrCauseDivZero, 0u);
    EXPECT_EQ(h.cpu().fcr31() & kFcrFlagDivZero, 0u);
    EXPECT_EQ(h.cpu().fpr(2), 0xDEAD'BEEFu);
}

TEST(CpuCop1, InvalidSquareRootProducesQuietNanAndFlags) {
    CpuHarness h;
    h.cpu().set_fpr(0, bits(-1.0f));
    h.write_code({sqrt_s(2, 0)});
    h.start();
    h.run_steps(1);

    EXPECT_TRUE(std::isnan(float_value(static_cast<u32>(h.cpu().fpr(2)))));
    EXPECT_NE(h.cpu().fcr31() & kFcrCauseInvalid, 0u);
    EXPECT_NE(h.cpu().fcr31() & kFcrFlagInvalid, 0u);
}

TEST(CpuCop1, FrZeroPairsEvenLowAndOddHighWords) {
    CpuHarness h;
    h.cpu().set_cop0(Cop0Reg::Status,
                     h.cpu().cop0(Cop0Reg::Status) & ~(1u << 26));
    h.cpu().set_gpr(4, bits(1.5));
    h.write_code({
        dmtc1(4, 2),
        add_d(4, 2, 2),
        dmfc1(5, 4),
    });
    h.start();
    h.run_steps(3);

    const u64 source = bits(1.5);
    EXPECT_EQ(static_cast<u32>(h.cpu().fpr(2)), static_cast<u32>(source));
    EXPECT_EQ(static_cast<u32>(h.cpu().fpr(3)), static_cast<u32>(source >> 32));
    EXPECT_DOUBLE_EQ(double_value(h.cpu().gpr(5)), 3.0);
}

TEST(CpuCop1, DenormalOperandRequestsSoftwareEmulation) {
    CpuHarness h;
    h.cpu().set_fpr(0, 1u); // smallest positive single denormal
    h.cpu().set_fpr(1, bits(1.0f));
    h.cpu().set_fpr(2, 0xABCD'1234u);
    h.write_code({add_s(2, 0, 1)});
    h.start();
    h.run_steps(1);

    EXPECT_EQ(exception_code(h.cpu()), ExcCode::FPE);
    EXPECT_NE(h.cpu().fcr31() & kFcrCauseUnimplemented, 0u);
    EXPECT_EQ(h.cpu().fpr(2), 0xABCD'1234u);
}

TEST(CpuCop1, ControlRegisterRoundTripsWritableFields) {
    CpuHarness h;
    constexpr u32 kValue =
        kFcrFlushSubnormals | kFcrCondition | kFcrEnableDivZero |
        kFcrFlagInvalid | kFcrFlagInexact | 3u;
    h.cpu().set_gpr(4, kValue);
    h.write_code({ctc1(4, 31), cfc1(5, 31)});
    h.start();
    h.run_steps(2);

    EXPECT_EQ(h.cpu().fcr31(), kValue);
    EXPECT_EQ(h.cpu().gpr(5), kValue);
    EXPECT_EQ(h.cpu().exception_count(), 0u);
}

TEST(CpuCop1, ControlWriteWithEnabledCauseRaisesFpeImmediately) {
    CpuHarness h;
    h.cpu().set_gpr(4, kFcrCauseDivZero | kFcrEnableDivZero);
    h.write_code({ctc1(4, 31)});
    h.start();
    h.run_steps(1);

    EXPECT_EQ(exception_code(h.cpu()), ExcCode::FPE);
    EXPECT_NE(h.cpu().fcr31() & kFcrCauseDivZero, 0u);
}

TEST(CpuCop1, EnabledInvalidOperationPreservesDestinationAndFlags) {
    CpuHarness h;
    h.cpu().set_fpr(0, bits(-4.0f));
    h.cpu().set_fpr(2, 0xCAFE'BABEu);
    h.cpu().set_gpr(4, kFcrEnableInvalid);
    h.write_code({ctc1(4, 31), sqrt_s(2, 0)});
    h.start();
    h.run_steps(2);

    EXPECT_EQ(exception_code(h.cpu()), ExcCode::FPE);
    EXPECT_NE(h.cpu().fcr31() & kFcrCauseInvalid, 0u);
    EXPECT_EQ(h.cpu().fcr31() & kFcrFlagInvalid, 0u);
    EXPECT_EQ(h.cpu().fpr(2), 0xCAFE'BABEu);
}

TEST(CpuCop1, MoveCopiesQuietNanWithoutChangingCauseBits) {
    CpuHarness h;
    constexpr u32 kQuietNan = 0x7FBF'FFFFu;
    h.cpu().set_fpr(0, kQuietNan);
    h.cpu().set_gpr(4, kFcrCauseInvalid);
    h.write_code({ctc1(4, 31), mov_s(2, 0)});
    h.start();
    h.run_steps(2);

    EXPECT_EQ(static_cast<u32>(h.cpu().fpr(2)), kQuietNan);
    EXPECT_NE(h.cpu().fcr31() & kFcrCauseInvalid, 0u);
    EXPECT_EQ(h.cpu().exception_count(), 0u);
}

TEST(CpuCop1, QuietNanArithmeticRequestsSoftwareEmulation) {
    CpuHarness h;
    h.cpu().set_fpr(0, 0x7FBF'FFFFu);
    h.cpu().set_fpr(1, bits(1.0f));
    h.cpu().set_fpr(2, 0x1234'5678u);
    h.write_code({add_s(2, 0, 1)});
    h.start();
    h.run_steps(1);

    EXPECT_EQ(exception_code(h.cpu()), ExcCode::FPE);
    EXPECT_NE(h.cpu().fcr31() & kFcrCauseUnimplemented, 0u);
    EXPECT_EQ(h.cpu().fpr(2), 0x1234'5678u);
}

TEST(CpuCop1, QuietNanCompareCanReportUnordered) {
    CpuHarness h;
    h.cpu().set_fpr(0, 0x7FBF'FFFFu);
    h.cpu().set_fpr(1, bits(1.0f));
    h.write_code({c_cond_s(0, 1, 0x1)}); // C.UN.S
    h.start();
    h.run_steps(1);

    EXPECT_NE(h.cpu().fcr31() & kFcrCondition, 0u);
    EXPECT_EQ(h.cpu().fcr31() & kFcrCauseInvalid, 0u);
    EXPECT_EQ(h.cpu().exception_count(), 0u);
}

TEST(CpuCop1, LongConversionsUse64BitDestinations) {
    CpuHarness h;
    h.cpu().set_fpr(0, bits(3.75));
    h.cpu().set_fpr(2, bits(-2.5f));
    h.cpu().set_gpr(4, 1u); // RM=RZ
    h.write_code({
        ctc1(4, 31),
        cvt_l(FMT_D, 6, 0),
        round_l(FMT_S, 8, 2),
        dmfc1(5, 6),
        dmfc1(7, 8),
    });
    h.start();
    h.run_steps(5);

    EXPECT_EQ(static_cast<s64>(h.cpu().gpr(5)), 3);
    EXPECT_EQ(static_cast<s64>(h.cpu().gpr(7)), -2);
}

TEST(CpuCop1, UnderflowWithoutFlushRequestsSoftwareEmulation) {
    CpuHarness h;
    h.cpu().set_fpr(0, 0x0080'0000u); // minimum positive normal
    h.cpu().set_fpr(1, bits(0.5f));
    h.cpu().set_fpr(2, 0xDEAD'BEEFu);
    h.write_code({mul_s(2, 0, 1)});
    h.start();
    h.run_steps(1);

    EXPECT_EQ(exception_code(h.cpu()), ExcCode::FPE);
    EXPECT_NE(h.cpu().fcr31() & kFcrCauseUnimplemented, 0u);
    EXPECT_EQ(h.cpu().fpr(2), 0xDEAD'BEEFu);
}

TEST(CpuCop1, FlushModeUsesDirectedMinimumNormalAndRecordsUnderflow) {
    CpuHarness h;
    h.cpu().set_fpr(0, 0x0080'0000u);
    h.cpu().set_fpr(1, bits(0.5f));
    h.cpu().set_gpr(4, kFcrFlushSubnormals | 2u); // RM=RP
    h.write_code({ctc1(4, 31), mul_s(2, 0, 1)});
    h.start();
    h.run_steps(2);

    EXPECT_EQ(static_cast<u32>(h.cpu().fpr(2)), 0x0080'0000u);
    EXPECT_NE(h.cpu().fcr31() & kFcrCauseUnderflow, 0u);
    EXPECT_NE(h.cpu().fcr31() & kFcrCauseInexact, 0u);
    EXPECT_NE(h.cpu().fcr31() & kFcrFlagUnderflow, 0u);
    EXPECT_NE(h.cpu().fcr31() & kFcrFlagInexact, 0u);
}

TEST(CpuCop1, OverflowRespectsRoundTowardZero) {
    CpuHarness h;
    h.cpu().set_fpr(0, 0x7F7F'FFFFu);
    h.cpu().set_fpr(1, bits(2.0f));
    h.cpu().set_gpr(4, 1u); // RM=RZ
    h.write_code({ctc1(4, 31), mul_s(2, 0, 1)});
    h.start();
    h.run_steps(2);

    EXPECT_EQ(static_cast<u32>(h.cpu().fpr(2)), 0x7F7F'FFFFu);
    EXPECT_NE(h.cpu().fcr31() & kFcrCauseOverflow, 0u);
    EXPECT_NE(h.cpu().fcr31() & kFcrCauseInexact, 0u);
    EXPECT_NE(h.cpu().fcr31() & kFcrFlagOverflow, 0u);
    EXPECT_NE(h.cpu().fcr31() & kFcrFlagInexact, 0u);
}

TEST(CpuCop1, ArithmeticHonorsFcr31RoundingMode) {
    const float half_ulp_at_one = std::ldexp(1.0f, -24);

    CpuHarness toward_positive;
    toward_positive.cpu().set_fpr(0, bits(1.0f));
    toward_positive.cpu().set_fpr(1, bits(half_ulp_at_one));
    toward_positive.cpu().set_gpr(4, 2u); // RM=RP
    toward_positive.write_code({ctc1(4, 31), add_s(2, 0, 1)});
    toward_positive.start();
    toward_positive.run_steps(2);

    EXPECT_EQ(static_cast<u32>(toward_positive.cpu().fpr(2)),
              bits(std::nextafter(1.0f, 2.0f)));
    EXPECT_NE(toward_positive.cpu().fcr31() & kFcrCauseInexact, 0u);

    CpuHarness toward_zero;
    toward_zero.cpu().set_fpr(0, bits(1.0f));
    toward_zero.cpu().set_fpr(1, bits(half_ulp_at_one));
    toward_zero.cpu().set_gpr(4, 1u); // RM=RZ
    toward_zero.write_code({ctc1(4, 31), add_s(2, 0, 1)});
    toward_zero.start();
    toward_zero.run_steps(2);

    EXPECT_EQ(static_cast<u32>(toward_zero.cpu().fpr(2)), bits(1.0f));
    EXPECT_NE(toward_zero.cpu().fcr31() & kFcrCauseInexact, 0u);
}

TEST(CpuCop1, FrZeroRejectsOddDoubleRegisters) {
    CpuHarness h;
    h.cpu().set_cop0(Cop0Reg::Status,
                     h.cpu().cop0(Cop0Reg::Status) & ~(1u << 26));
    h.cpu().set_fpr(1, bits(1.0));
    h.cpu().set_fpr(2, bits(2.0));
    h.cpu().set_fpr(4, 0xAA55'AA55u);
    h.write_code({add_d(4, 1, 2)});
    h.start();
    h.run_steps(1);

    EXPECT_EQ(exception_code(h.cpu()), ExcCode::FPE);
    EXPECT_NE(h.cpu().fcr31() & kFcrCauseUnimplemented, 0u);
    EXPECT_EQ(h.cpu().fpr(4), 0xAA55'AA55u);
}

TEST(CpuCop1, BlockCacheEndsCop1BranchAfterItsDelaySlot) {
    CpuHarness h;
    h.cpu().set_fpr(0, bits(1.0f));
    h.cpu().set_fpr(1, bits(2.0f));
    h.write_code({
        c_cond_s(0, 1, 0x4),
        bc1t(1),
        addiu(8, 0, 3),
        addiu(9, 0, 7),
    });
    h.start();

    BlockCache cache;
    h.cpu().set_block_cache(&cache);
    const BasicBlock* block = cache.compile(CpuHarness::kBaseVirt,
                                            h.bus(), h.cpu());
    ASSERT_NE(block, nullptr);
    EXPECT_TRUE(block->ends_with_branch);
    EXPECT_EQ(block->insns.size(), 3u);

    h.run_steps(4);
    EXPECT_EQ(h.cpu().gpr(8), 3u);
    EXPECT_EQ(h.cpu().gpr(9), 7u);
}

TEST(CpuCop1, FiniteArithmeticMatchesHostReference) {
    {
        CpuHarness h;
        constexpr std::array<std::array<float, 2>, 4> kInputs{{
            {{1.5f, 2.25f}},
            {{-7.0f, 0.75f}},
            {{123.125f, -4.5f}},
            {{0.03125f, 16.0f}},
        }};
        std::vector<u32> code;
        for (u32 i = 0; i < kInputs.size(); ++i) {
            const u32 fs = i * 2u;
            const u32 ft = fs + 1u;
            const u32 fd = 8u + i * 4u;
            h.cpu().set_fpr(fs, bits(kInputs[i][0]));
            h.cpu().set_fpr(ft, bits(kInputs[i][1]));
            code.push_back(add_s(fd, fs, ft));
            code.push_back(sub_s(fd + 1u, fs, ft));
            code.push_back(mul_s(fd + 2u, fs, ft));
            code.push_back(div_s(fd + 3u, fs, ft));
        }
        h.write_code(code);
        h.start();
        h.run_steps(code.size());

        for (u32 i = 0; i < kInputs.size(); ++i) {
            const float left = kInputs[i][0];
            const float right = kInputs[i][1];
            const u32 fd = 8u + i * 4u;
            EXPECT_FLOAT_EQ(float_value(static_cast<u32>(h.cpu().fpr(fd))),
                            left + right);
            EXPECT_FLOAT_EQ(float_value(static_cast<u32>(h.cpu().fpr(fd + 1u))),
                            left - right);
            EXPECT_FLOAT_EQ(float_value(static_cast<u32>(h.cpu().fpr(fd + 2u))),
                            left * right);
            EXPECT_FLOAT_EQ(float_value(static_cast<u32>(h.cpu().fpr(fd + 3u))),
                            left / right);
        }
    }

    {
        CpuHarness h;
        constexpr std::array<std::array<double, 2>, 3> kInputs{{
            {{1.5, 2.25}},
            {{-19.0, 0.125}},
            {{1024.5, -3.0}},
        }};
        std::vector<u32> code;
        for (u32 i = 0; i < kInputs.size(); ++i) {
            const u32 fs = i * 2u;
            const u32 ft = fs + 1u;
            const u32 fd = 8u + i * 4u;
            h.cpu().set_fpr(fs, bits(kInputs[i][0]));
            h.cpu().set_fpr(ft, bits(kInputs[i][1]));
            code.push_back(add_d(fd, fs, ft));
            code.push_back(sub_d(fd + 1u, fs, ft));
            code.push_back(mul_d(fd + 2u, fs, ft));
            code.push_back(div_d(fd + 3u, fs, ft));
        }
        h.write_code(code);
        h.start();
        h.run_steps(code.size());

        for (u32 i = 0; i < kInputs.size(); ++i) {
            const double left = kInputs[i][0];
            const double right = kInputs[i][1];
            const u32 fd = 8u + i * 4u;
            EXPECT_DOUBLE_EQ(double_value(h.cpu().fpr(fd)), left + right);
            EXPECT_DOUBLE_EQ(double_value(h.cpu().fpr(fd + 1u)), left - right);
            EXPECT_DOUBLE_EQ(double_value(h.cpu().fpr(fd + 2u)), left * right);
            EXPECT_DOUBLE_EQ(double_value(h.cpu().fpr(fd + 3u)), left / right);
        }
    }
}
