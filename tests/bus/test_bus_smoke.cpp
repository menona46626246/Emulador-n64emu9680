#include <gtest/gtest.h>

#include "n64/bus/bus.hpp"

#include <vector>

using namespace n64;

TEST(BusSmoke, RdramReadWrite32) {
    Bus bus;
    bus.reset();

    bus.write32(0x0000'0100, 0xDEADBEEFu);
    EXPECT_EQ(bus.read32(0x0000'0100), 0xDEADBEEFu);

    bus.write8(0x0000'0200, 0xABu);
    bus.write8(0x0000'0201, 0xCDu);
    bus.write8(0x0000'0202, 0xEFu);
    bus.write8(0x0000'0203, 0x01u);
    EXPECT_EQ(bus.read32(0x0000'0200), 0xABCDEF01u);
}

TEST(BusSmoke, SpDmemImem) {
    Bus bus;
    bus.write32(0x0400'0000, 0x11223344u);
    EXPECT_EQ(bus.read32(0x0400'0000), 0x11223344u);

    bus.write32(0x0400'1000, 0x55667788u);
    EXPECT_EQ(bus.read32(0x0400'1000), 0x55667788u);
}

TEST(BusSmoke, CartridgeLoad) {
    Bus bus;
    // Minimal fake .z64 header magic + padding.
    std::vector<u8> rom(0x1000, 0);
    rom[0] = 0x80;
    rom[1] = 0x37;
    rom[2] = 0x12;
    rom[3] = 0x40;
    rom[0x10] = 0xCA;
    ASSERT_TRUE(bus.load_cartridge(rom));
    EXPECT_TRUE(bus.has_cartridge());
    EXPECT_EQ(bus.read8(0x1000'0000), 0x80);
    EXPECT_EQ(bus.read8(0x1000'0010), 0xCA);
    EXPECT_EQ(bus.read32(0x1000'0000), 0x80371240u);
}

TEST(BusSmoke, CartridgeEndianConvertV64) {
    Bus bus;
    // .v64 magic (byte-swapped pairs of .z64)
    std::vector<u8> rom = {0x37, 0x80, 0x40, 0x12, 0x00, 0x00, 0x00, 0x00};
    ASSERT_TRUE(bus.load_cartridge(rom));
    EXPECT_EQ(bus.read32(0x1000'0000), 0x80371240u);
}

TEST(BusSmoke, PhysicalMask) {
    EXPECT_EQ(Bus::physical(0xA000'0100u), 0x0000'0100u);
    EXPECT_EQ(Bus::physical(0x8000'0000u), 0x0000'0000u);
    EXPECT_EQ(Bus::physical(0xBFC0'0000u), 0x1FC0'0000u);
}
