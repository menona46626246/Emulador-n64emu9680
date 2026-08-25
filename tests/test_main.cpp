#include <gtest/gtest.h>

#include "n64/common/log.hpp"

int main(int argc, char** argv) {
    // Quiet logs during tests unless a test opts into verbose output.
    n64::log::init("[%H:%M:%S.%e] [%^%l%$] %v", spdlog::level::err);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
