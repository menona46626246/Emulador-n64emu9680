#include <gtest/gtest.h>

#include "n64/bus/bus.hpp"
#include "n64/core/emulator.hpp"
#include "n64/rcp/rsp/insn.hpp"
#include "n64/rcp/rsp/rsp.hpp"

#include <initializer_list>

using namespace n64;
using namespace n64::rsp_insn;

namespace {

void run_vector_program(Rsp& rsp, std::initializer_list<u32> words) {
    u32 offset = 0;
    for (const u32 word : words) {
        store_be32_sp(rsp.imem(), offset, word);
        offset += 4;
    }
    rsp.set_pc(0);
    rsp.set_halted(false);
    rsp.run(static_cast<u32>(words.size()) + 4u);
}

[[nodiscard]] u8 vector_byte(const Rsp& rsp, u32 reg, u32 index) {
    const u16 lane = rsp.vpr(reg, index >> 1);
    return (index & 1u) == 0 ? static_cast<u8>(lane >> 8) : static_cast<u8>(lane);
}

} // namespace

TEST(RspVectorState, ResetClearsRegistersAccumulatorAndFlags) {
    Emulator emulator;
    auto& rsp = emulator.rsp();
    rsp.set_vpr(3, 4, 0xABCD);
    rsp.set_accumulator(4, -123456);
    rsp.set_vco(0xFFFF);
    rsp.set_vcc(0xAAAA);
    rsp.set_vce(0x55);

    rsp.reset();

    EXPECT_EQ(rsp.vpr(3, 4), 0u);
    EXPECT_EQ(rsp.accumulator(4), 0);
    EXPECT_EQ(rsp.vco(), 0u);
    EXPECT_EQ(rsp.vcc(), 0u);
    EXPECT_EQ(rsp.vce(), 0u);
}

TEST(RspVectorTransfers, Mtc2Mfc2UseBigEndianByteElements) {
    Emulator emulator;
    auto& rsp = emulator.rsp();
    rsp.reset();
    rsp.set_gpr(8, 0xAABBu);

    run_vector_program(rsp, {
        mtc2(8, 2, 3),
        mfc2(9, 2, 3),
        brk(),
    });

    EXPECT_EQ(vector_byte(rsp, 2, 3), 0xAAu);
    EXPECT_EQ(vector_byte(rsp, 2, 4), 0xBBu);
    EXPECT_EQ(rsp.gpr(9), 0xFFFF'AABBu);
}

TEST(RspVectorTransfers, Mtc2Element15DoesNotWrapButMfc2Does) {
    Emulator emulator;
    auto& rsp = emulator.rsp();
    rsp.reset();
    rsp.set_vpr(4, 0, 0xCC00);
    rsp.set_gpr(8, 0xAABBu);

    run_vector_program(rsp, {
        mtc2(8, 4, 15),
        mfc2(9, 4, 15),
        brk(),
    });

    EXPECT_EQ(vector_byte(rsp, 4, 15), 0xAAu);
    EXPECT_EQ(vector_byte(rsp, 4, 0), 0xCCu);
    EXPECT_EQ(rsp.gpr(9), 0xFFFF'AACCu);
}

TEST(RspVectorTransfers, ControlRegistersRoundTripAndSignExtend) {
    Emulator emulator;
    auto& rsp = emulator.rsp();
    rsp.reset();
    rsp.set_gpr(8, 0x8001u);
    rsp.set_gpr(9, 0x12F3u);

    run_vector_program(rsp, {
        ctc2(8, 0),
        ctc2(9, 2),
        cfc2(10, 0),
        cfc2(11, 2),
        brk(),
    });

    EXPECT_EQ(rsp.vco(), 0x8001u);
    EXPECT_EQ(rsp.vce(), 0xF3u);
    EXPECT_EQ(rsp.gpr(10), 0xFFFF'8001u);
    EXPECT_EQ(rsp.gpr(11), 0x0000'00F3u);
}

TEST(RspVectorAlu, ElementBroadcastAndSignedSaturation) {
    Emulator emulator;
    auto& rsp = emulator.rsp();
    rsp.reset();
    for (u32 lane = 0; lane < Rsp::kVectorLaneCount; ++lane) {
        rsp.set_vpr(1, lane, static_cast<u16>(lane + 1u));
    }
    rsp.set_vpr(1, 7, 0x7FF8u);
    rsp.set_vpr(2, 3, 10u);

    run_vector_program(rsp, {
        vector(VF_VADD, 3, 1, 2, 11), // vt[3] broadcast to every lane
        brk(),
    });

    EXPECT_EQ(rsp.vpr(3, 0), 11u);
    EXPECT_EQ(rsp.vpr(3, 1), 12u);
    EXPECT_EQ(rsp.vpr(3, 7), 0x7FFFu);
    EXPECT_EQ(rsp.vco(), 0u);
}

TEST(RspVectorAlu, AddCarryFeedsVaddAndThenClearsVco) {
    Emulator emulator;
    auto& rsp = emulator.rsp();
    rsp.reset();
    rsp.set_vpr(1, 0, 0xFFFFu);
    rsp.set_vpr(2, 0, 1u);

    run_vector_program(rsp, {
        vector(VF_VADDC, 3, 1, 2),
        vector(VF_VADD, 4, 1, 2),
        brk(),
    });

    EXPECT_EQ(rsp.vpr(3, 0), 0u);
    EXPECT_EQ(rsp.vpr(4, 0), 1u);
    EXPECT_EQ(rsp.vco(), 0u);
}

TEST(RspVectorAlu, LogicalElementRoutingUsesHardwareTable) {
    Emulator emulator;
    auto& rsp = emulator.rsp();
    rsp.reset();
    for (u32 lane = 0; lane < Rsp::kVectorLaneCount; ++lane) {
        rsp.set_vpr(1, lane, static_cast<u16>(0x1000u + lane));
        rsp.set_vpr(2, lane, static_cast<u16>(lane));
    }

    run_vector_program(rsp, {
        vector(VF_VXOR, 3, 1, 2, 2),
        brk(),
    });

    EXPECT_EQ(rsp.vpr(3, 0), 0x1000u);
    EXPECT_EQ(rsp.vpr(3, 1), 0x1001u); // vt lane 0 is reused
    EXPECT_EQ(rsp.vpr(3, 2), 0x1000u); // vt lane 2
    EXPECT_EQ(rsp.vpr(3, 3), 0x1001u);
    EXPECT_EQ(static_cast<u16>(rsp.accumulator(3)), rsp.vpr(3, 3));
}

TEST(RspVectorSelect, CompareSetsVccAndMergeConsumesIt) {
    Emulator emulator;
    auto& rsp = emulator.rsp();
    rsp.reset();
    rsp.set_vpr(1, 0, static_cast<u16>(-3));
    rsp.set_vpr(1, 1, 9u);
    rsp.set_vpr(2, 0, 2u);
    rsp.set_vpr(2, 1, 4u);

    run_vector_program(rsp, {
        vector(VF_VLT, 3, 1, 2),
        vector(VF_VMRG, 4, 1, 2),
        brk(),
    });

    EXPECT_EQ(rsp.vcc() & 0x3u, 0x1u);
    EXPECT_EQ(rsp.vpr(3, 0), static_cast<u16>(-3));
    EXPECT_EQ(rsp.vpr(3, 1), 4u);
    EXPECT_EQ(rsp.vpr(4, 0), static_cast<u16>(-3));
    EXPECT_EQ(rsp.vpr(4, 1), 4u);
}

TEST(RspVectorMultiply, FractionalMultiplyRoundsAndSaturates) {
    Emulator emulator;
    auto& rsp = emulator.rsp();
    rsp.reset();
    rsp.set_vpr(1, 0, 0x4000u);
    rsp.set_vpr(2, 0, 0x4000u);
    rsp.set_vpr(1, 1, 0x8000u);
    rsp.set_vpr(2, 1, 0x8000u);

    run_vector_program(rsp, {
        vector(VF_VMULF, 3, 1, 2),
        brk(),
    });

    EXPECT_EQ(rsp.vpr(3, 0), 0x2000u);
    EXPECT_EQ(rsp.accumulator(0), 0x2000'8000ll);
    EXPECT_EQ(rsp.vpr(3, 1), 0x7FFFu);
}

TEST(RspVectorMultiply, PartialProductsAccumulateInFortyEightBits) {
    Emulator emulator;
    auto& rsp = emulator.rsp();
    rsp.reset();
    rsp.set_vpr(1, 0, 3u);
    rsp.set_vpr(2, 0, static_cast<u16>(-2));
    rsp.set_vpr(4, 0, 5u);
    rsp.set_vpr(5, 0, 4u);

    run_vector_program(rsp, {
        vector(VF_VMUDN, 3, 1, 2),
        vector(VF_VMADN, 6, 4, 5),
        vector(VF_VSAR, 7, 0, 0, 10),
        brk(),
    });

    EXPECT_EQ(rsp.accumulator(0), 14);
    EXPECT_EQ(rsp.vpr(3, 0), 0xFFFAu);
    EXPECT_EQ(rsp.vpr(6, 0), 14u);
    EXPECT_EQ(rsp.vpr(7, 0), 14u);
}

TEST(RspVectorMultiply, QuantizeAndConditionalRoundOperations) {
    Emulator emulator;
    auto& rsp = emulator.rsp();
    rsp.reset();
    rsp.set_vpr(1, 0, 100u);
    rsp.set_vpr(2, 0, 100u);
    rsp.set_vpr(4, 0, 2u);

    run_vector_program(rsp, {
        vector(VF_VMULQ, 3, 1, 2),
        vector(VF_VRNDP, 5, 1, 4), // odd VS field adds vt << 16 when ACC >= 0
        vector(VF_VMACQ, 6, 0, 0),
        brk(),
    });

    EXPECT_EQ(rsp.vpr(3, 0), 0x1380u);
    EXPECT_EQ(rsp.vpr(5, 0), 0x2712u);
    EXPECT_EQ(rsp.vpr(6, 0), 0x1370u);
    EXPECT_EQ(rsp.accumulator(0), 0x26F2'0000ll);
}

TEST(RspVectorMemory, NormalLoadsRespectScaleElementAndSignedOffset) {
    Emulator emulator;
    auto& rsp = emulator.rsp();
    rsp.reset();
    for (u32 i = 0; i < 8; ++i) {
        rsp.dmem()[0x20u + i] = static_cast<u8>(0xA0u + i);
    }
    rsp.set_gpr(8, 0x28u);

    run_vector_program(rsp, {
        ldv(2, 2, -1, 8), // 0x28 + (-1 * 8) -> 0x20
        brk(),
    });

    EXPECT_EQ(vector_byte(rsp, 2, 0), 0u);
    EXPECT_EQ(vector_byte(rsp, 2, 1), 0u);
    for (u32 i = 0; i < 8; ++i) {
        EXPECT_EQ(vector_byte(rsp, 2, i + 2), static_cast<u8>(0xA0u + i));
    }
}

TEST(RspVectorMemory, QuadLoadStopsAtDmemBoundary) {
    Emulator emulator;
    auto& rsp = emulator.rsp();
    rsp.reset();
    for (u32 i = 0; i < 16; ++i) {
        rsp.dmem()[0x20u + i] = static_cast<u8>(i);
    }
    for (u32 lane = 0; lane < Rsp::kVectorLaneCount; ++lane) {
        rsp.set_vpr(2, lane, 0xEEEEu);
    }
    rsp.set_gpr(8, 0x23u);

    run_vector_program(rsp, {
        lqv(2, 0, 0, 8),
        brk(),
    });

    for (u32 i = 0; i < 13; ++i) {
        EXPECT_EQ(vector_byte(rsp, 2, i), static_cast<u8>(i + 3u));
    }
    EXPECT_EQ(vector_byte(rsp, 2, 13), 0xEEu);
    EXPECT_EQ(vector_byte(rsp, 2, 15), 0xEEu);
}

TEST(RspVectorMemory, ReverseLoadFillsRightSideOfVector) {
    Emulator emulator;
    auto& rsp = emulator.rsp();
    rsp.reset();
    for (u32 i = 0; i < 16; ++i) {
        rsp.dmem()[0x20u + i] = static_cast<u8>(0x40u + i);
    }
    rsp.set_gpr(8, 0x2Cu);

    run_vector_program(rsp, {
        lrv(2, 0, 0, 8),
        brk(),
    });

    for (u32 i = 0; i < 4; ++i) {
        EXPECT_EQ(vector_byte(rsp, 2, i), 0u);
    }
    for (u32 i = 0; i < 12; ++i) {
        EXPECT_EQ(vector_byte(rsp, 2, i + 4u), static_cast<u8>(0x40u + i));
    }
}

TEST(RspVectorMemory, StoresAreImmediatelyVisibleOnBus) {
    Emulator emulator;
    auto& rsp = emulator.rsp();
    rsp.reset();
    rsp.set_vpr(2, 0, 0xAABBu);
    rsp.set_vpr(2, 1, 0xCCDDu);
    rsp.set_gpr(8, 0x30u);

    run_vector_program(rsp, {
        slv(2, 0, 0, 8),
        brk(),
    });

    EXPECT_EQ(rsp.dmem()[0x30], 0xAAu);
    EXPECT_EQ(rsp.dmem()[0x33], 0xDDu);
    EXPECT_EQ(emulator.bus().sp_dmem()[0x30], 0xAAu);
    EXPECT_EQ(emulator.bus().sp_dmem()[0x33], 0xDDu);
}

TEST(RspVectorDivide, ReciprocalUsesHardwareLookupAndPipelineOutput) {
    Emulator emulator;
    auto& rsp = emulator.rsp();
    rsp.reset();
    rsp.set_vpr(2, 0, 3u);

    run_vector_program(rsp, {
        vector(VF_VRCP, 3, 0, 2, 0),
        vector(VF_VRCPH, 4, 1, 2, 0),
        brk(),
    });

    EXPECT_EQ(rsp.vpr(3, 0), 0xA000u);
    EXPECT_EQ(rsp.vpr(4, 1), 0x2AAAu);
}

TEST(RspVectorDivide, ReciprocalCornerCasesMatchRcp) {
    Emulator emulator;
    auto& rsp = emulator.rsp();
    rsp.reset();
    rsp.set_vpr(1, 0, 0u);
    rsp.set_vpr(1, 1, 0x8000u);

    run_vector_program(rsp, {
        vector(VF_VRCP, 2, 0, 1, 0),
        vector(VF_VRCP, 3, 0, 1, 1),
        brk(),
    });

    EXPECT_EQ(rsp.vpr(2, 0), 0xFFFFu);
    EXPECT_EQ(rsp.vpr(3, 0), 0x0000u);
}

TEST(RspVectorDivide, HighLowSequenceBuildsFullPrecisionInput) {
    Emulator emulator;
    auto& rsp = emulator.rsp();
    rsp.reset();
    rsp.set_vpr(1, 0, 0u);
    rsp.set_vpr(1, 1, 2u);

    run_vector_program(rsp, {
        vector(VF_VRCPH, 2, 0, 1, 0),
        vector(VF_VRCPL, 3, 0, 1, 1),
        vector(VF_VRCPH, 4, 0, 1, 0),
        brk(),
    });

    EXPECT_EQ(rsp.vpr(3, 0), 0xE000u);
    EXPECT_EQ(rsp.vpr(4, 0), 0x3FFFu);
}

TEST(RspVectorDivide, ReciprocalSquareRootAndMoveTargetOneLane) {
    Emulator emulator;
    auto& rsp = emulator.rsp();
    rsp.reset();
    rsp.set_vpr(1, 0, 4u);
    rsp.set_vpr(1, 2, 0xBEEFu);
    rsp.set_vpr(5, 0, 0xAAAAu);
    rsp.set_vpr(5, 3, 0xCCCCu);

    run_vector_program(rsp, {
        vector(VF_VRSQ, 2, 0, 1, 0),
        vector(VF_VRSQH, 3, 0, 1, 0),
        vector(VF_VMOV, 5, 3, 1, 2),
        brk(),
    });

    EXPECT_EQ(rsp.vpr(2, 0), 0xE000u);
    EXPECT_EQ(rsp.vpr(3, 0), 0x3FFFu);
    EXPECT_EQ(rsp.vpr(5, 0), 0xAAAAu);
    EXPECT_EQ(rsp.vpr(5, 3), 0xBEEFu);
}

TEST(RspVectorMemory, PackedAndUnsignedPackedTransfers) {
    Emulator emulator;
    auto& rsp = emulator.rsp();
    rsp.reset();
    for (u32 i = 0; i < 8; ++i) {
        rsp.dmem()[0x20u + i] = static_cast<u8>(0x10u + i);
    }
    rsp.set_gpr(8, 0x20u);
    rsp.set_gpr(9, 0x40u);

    run_vector_program(rsp, {
        lpv(2, 0, 0, 8),
        spv(2, 0, 0, 9),
        luv(3, 0, 0, 8),
        suv(3, 0, 1, 9),
        brk(),
    });

    for (u32 lane = 0; lane < 8; ++lane) {
        EXPECT_EQ(rsp.vpr(2, lane), static_cast<u16>((0x10u + lane) << 8));
        EXPECT_EQ(rsp.vpr(3, lane), static_cast<u16>((0x10u + lane) << 7));
        EXPECT_EQ(rsp.dmem()[0x40u + lane], static_cast<u8>(0x10u + lane));
        EXPECT_EQ(rsp.dmem()[0x48u + lane], static_cast<u8>(0x10u + lane));
    }
}

TEST(RspVectorMemory, HalfAndFractionalTransfersUseStrides) {
    Emulator emulator;
    auto& rsp = emulator.rsp();
    rsp.reset();
    for (u32 lane = 0; lane < 8; ++lane) {
        rsp.dmem()[0x20u + lane * 2u] = static_cast<u8>(0x20u + lane);
    }
    rsp.set_gpr(8, 0x20u);
    rsp.set_gpr(9, 0x80u);

    run_vector_program(rsp, {
        lhv(2, 0, 0, 8),
        shv(2, 0, 0, 9),
        lfv(3, 0, 0, 8),
        sfv(3, 0, 1, 9),
        brk(),
    });

    for (u32 lane = 0; lane < 8; ++lane) {
        EXPECT_EQ(rsp.vpr(2, lane), static_cast<u16>((0x20u + lane) << 7));
        EXPECT_EQ(rsp.dmem()[0x80u + lane * 2u], static_cast<u8>(0x20u + lane));
    }
    for (u32 lane = 0; lane < 4; ++lane) {
        EXPECT_EQ(rsp.vpr(3, lane), static_cast<u16>((0x20u + lane * 2u) << 7));
        EXPECT_EQ(rsp.dmem()[0x90u + lane * 4u], static_cast<u8>(0x20u + lane * 2u));
    }
}

TEST(RspVectorMemory, TransposeLoadStoreRoundTripsMatrixDiagonal) {
    Emulator emulator;
    auto& rsp = emulator.rsp();
    rsp.reset();
    for (u32 lane = 0; lane < 8; ++lane) {
        const u16 value = static_cast<u16>(0x1000u + lane);
        rsp.dmem()[0x20u + lane * 2u] = static_cast<u8>(value >> 8);
        rsp.dmem()[0x21u + lane * 2u] = static_cast<u8>(value);
    }
    rsp.set_gpr(8, 0x20u);
    rsp.set_gpr(9, 0x60u);

    run_vector_program(rsp, {
        ltv(8, 0, 0, 8),
        stv(8, 0, 0, 9),
        brk(),
    });

    for (u32 lane = 0; lane < 8; ++lane) {
        EXPECT_EQ(rsp.vpr(8u + lane, lane), static_cast<u16>(0x1000u + lane));
        EXPECT_EQ(rsp.dmem()[0x60u + lane * 2u], 0x10u);
        EXPECT_EQ(rsp.dmem()[0x61u + lane * 2u], static_cast<u8>(lane));
    }
}

TEST(RspVectorProgram, FractionalAudioKernelRunsFromImem) {
    Emulator emulator;
    auto& rsp = emulator.rsp();
    rsp.reset();

    constexpr s16 samples[8] = {1000, -2000, 3000, -4000, 5000, -6000, 7000, -8000};
    for (u32 lane = 0; lane < 8; ++lane) {
        const u16 bits = static_cast<u16>(samples[lane]);
        rsp.dmem()[lane * 2u] = static_cast<u8>(bits >> 8);
        rsp.dmem()[lane * 2u + 1u] = static_cast<u8>(bits);
    }
    rsp.dmem()[0x10] = 0x40; // Q15 coefficient 0.5 in lane 0
    rsp.dmem()[0x11] = 0x00;
    rsp.set_gpr(8, 0x00u);
    rsp.set_gpr(9, 0x10u);
    rsp.set_gpr(10, 0x20u);

    run_vector_program(rsp, {
        lqv(1, 0, 0, 8),
        lqv(2, 0, 0, 9),
        vector(VF_VMULF, 3, 1, 2, 8),
        sqv(3, 0, 0, 10),
        brk(),
    });

    for (u32 lane = 0; lane < 8; ++lane) {
        const u16 output = static_cast<u16>(
            (static_cast<u16>(rsp.dmem()[0x20u + lane * 2u]) << 8) |
            rsp.dmem()[0x21u + lane * 2u]);
        EXPECT_EQ(static_cast<s16>(output), static_cast<s16>(samples[lane] / 2));
    }
    EXPECT_TRUE(rsp.broke());
}
