cmake_minimum_required(VERSION 3.16)
project(n64emu
    VERSION 0.1.0
    DESCRIPTION "Modular Nintendo 64 emulator (homebrew-first)"
    LANGUAGES CXX C
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(N64EMU_BUILD_TESTS "Build unit tests" ON)
option(N64EMU_BUILD_FRONTEND "Build SDL2 frontend" ON)
option(N64EMU_HEADLESS "Prefer dummy/offscreen video (CI / no display)" OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

# Warnings
if(MSVC)
    add_compile_options(/W4 /permissive- /EHsc)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------
# spdlog (header-only / vendored)
set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_BENCH OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/spdlog EXCLUDE_FROM_ALL)

# SDL2 — prefer system / CMAKE_PREFIX_PATH install, then bundled package
if(N64EMU_BUILD_FRONTEND)
    find_package(SDL2 CONFIG QUIET)
    if(NOT SDL2_FOUND AND NOT TARGET SDL2::SDL2)
        file(GLOB N64EMU_BUNDLED_SDL_DIRS LIST_DIRECTORIES TRUE
            "${CMAKE_CURRENT_SOURCE_DIR}/third_party/SDL2-*"
        )
        list(SORT N64EMU_BUNDLED_SDL_DIRS COMPARE NATURAL ORDER DESCENDING)
        foreach(N64EMU_BUNDLED_SDL_DIR IN LISTS N64EMU_BUNDLED_SDL_DIRS)
            if(EXISTS "${N64EMU_BUNDLED_SDL_DIR}/cmake/sdl2-config.cmake")
                find_package(SDL2 CONFIG QUIET
                    PATHS "${N64EMU_BUNDLED_SDL_DIR}/cmake"
                    NO_DEFAULT_PATH
                )
                if(SDL2_FOUND OR TARGET SDL2::SDL2)
                    message(STATUS "Found bundled SDL2: ${N64EMU_BUNDLED_SDL_DIR}")
                    break()
                endif()
            endif()
        endforeach()
    endif()
    if(NOT SDL2_FOUND)
        find_package(SDL2 QUIET)
    endif()
    if(NOT SDL2_FOUND AND NOT TARGET SDL2::SDL2)
        # Fall back to pkg-config / sdl2-config style variables
        find_path(SDL2_INCLUDE_DIR SDL.h
            PATH_SUFFIXES SDL2
            HINTS ENV SDL2_DIR
            PATHS /usr/include /usr/local/include
                  ${CMAKE_SOURCE_DIR}/../deps/sdl2-install/include
        )
        find_library(SDL2_LIBRARY
            NAMES SDL2
            HINTS ENV SDL2_DIR
            PATHS /usr/lib /usr/local/lib
                  ${CMAKE_SOURCE_DIR}/../deps/sdl2-install/lib
                  /home/user/deps/sdl2-install/lib
        )
        if(SDL2_INCLUDE_DIR AND SDL2_LIBRARY)
            add_library(SDL2::SDL2 UNKNOWN IMPORTED)
            set_target_properties(SDL2::SDL2 PROPERTIES
                IMPORTED_LOCATION "${SDL2_LIBRARY}"
                INTERFACE_INCLUDE_DIRECTORIES "${SDL2_INCLUDE_DIR}"
            )
            # Also provide SDL2::SDL2main if present
            find_library(SDL2MAIN_LIBRARY
                NAMES SDL2main
                HINTS ENV SDL2_DIR
                PATHS /usr/lib /usr/local/lib
                      /home/user/deps/sdl2-install/lib
            )
            if(SDL2MAIN_LIBRARY)
                add_library(SDL2::SDL2main UNKNOWN IMPORTED)
                set_target_properties(SDL2::SDL2main PROPERTIES
                    IMPORTED_LOCATION "${SDL2MAIN_LIBRARY}"
                )
            endif()
            set(SDL2_FOUND TRUE)
            message(STATUS "Found SDL2 (manual): ${SDL2_LIBRARY}")
        else()
            message(FATAL_ERROR
                "SDL2 not found. Install libsdl2-dev or set CMAKE_PREFIX_PATH "
                "to an SDL2 install prefix (e.g. -DCMAKE_PREFIX_PATH=/path/to/sdl2-install).")
        endif()
    else()
        message(STATUS "Found SDL2 via CMake package")
    endif()
endif()

# ---------------------------------------------------------------------------
# Common library (logging, types, result helpers)
# ---------------------------------------------------------------------------
add_library(n64_common
    src/common/log.cpp
    src/common/version.cpp
)
add_library(n64::common ALIAS n64_common)
target_include_directories(n64_common
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(n64_common
    PUBLIC
        spdlog::spdlog_header_only
)
target_compile_definitions(n64_common
    PUBLIC
        N64EMU_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
        N64EMU_VERSION_MINOR=${PROJECT_VERSION_MINOR}
        N64EMU_VERSION_PATCH=${PROJECT_VERSION_PATCH}
)

# ---------------------------------------------------------------------------
# Core emulator library (stubs in Phase 0; filled in later phases)
# ---------------------------------------------------------------------------
add_library(n64_core
    src/core/emulator.cpp
    src/core/scheduler.cpp
    src/cpu/cpu.cpp
    src/cpu/cop1.cpp
    src/cpu/block_cache.cpp
    src/bus/bus.cpp
    src/bus/mi.cpp
    src/bus/pi.cpp
    src/bus/si.cpp
    src/bus/ri.cpp
    src/bus/sp_regs.cpp
    src/bus/dp_regs.cpp
    src/cart/header.cpp
    src/vi/vi.cpp
    src/ai/ai.cpp
    src/pif/pif.cpp
    src/pif/boot.cpp
    src/rcp/rsp/rsp.cpp
    src/rcp/rsp/rsp_vector.cpp
    src/rcp/rdp/rdp.cpp
    src/debug/debugger.cpp
)
add_library(n64::core ALIAS n64_core)
if(MSVC)
    set_source_files_properties(src/cpu/cop1.cpp PROPERTIES COMPILE_OPTIONS "/fp:strict")
else()
    set_source_files_properties(src/cpu/cop1.cpp PROPERTIES COMPILE_OPTIONS "-frounding-math")
endif()
target_include_directories(n64_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(n64_core
    PUBLIC
        n64::common
)

# ---------------------------------------------------------------------------
# Frontend library
# ---------------------------------------------------------------------------
if(N64EMU_BUILD_FRONTEND)
    add_library(n64_frontend
        src/frontend/window.cpp
        src/frontend/application.cpp
        src/frontend/input.cpp
        src/frontend/audio_output.cpp
    )
    add_library(n64::frontend ALIAS n64_frontend)
    target_include_directories(n64_frontend
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(n64_frontend
        PUBLIC
            n64::core
            n64::common
            SDL2::SDL2
    )
    if(TARGET SDL2::SDL2main)
        target_link_libraries(n64_frontend PUBLIC SDL2::SDL2main)
    endif()
    if(N64EMU_HEADLESS)
        target_compile_definitions(n64_frontend PUBLIC N64EMU_HEADLESS=1)
    endif()

    # Main executable
    add_executable(n64emu src/main.cpp)
    target_link_libraries(n64emu PRIVATE n64::frontend n64::core n64::common)
    if(WIN32 AND TARGET SDL2::SDL2)
        add_custom_command(TARGET n64emu POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:SDL2::SDL2>"
                "$<TARGET_FILE_DIR:n64emu>"
            COMMENT "Copying SDL2 runtime next to n64emu"
            VERBATIM
        )
    endif()
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(N64EMU_BUILD_TESTS)
    enable_testing()
    set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    add_subdirectory(third_party/googletest EXCLUDE_FROM_ALL)

    add_executable(n64emu_tests
        tests/test_main.cpp
        tests/core/test_version.cpp
        tests/core/test_emulator_smoke.cpp
        tests/bus/test_bus_smoke.cpp
        tests/bus/test_mmio_dma.cpp
        tests/cpu/test_cpu_smoke.cpp
        tests/cpu/test_cpu_interpreter.cpp
        tests/cpu/test_tlb.cpp
        tests/cpu/test_cop1.cpp
        tests/core/test_boot.cpp
        tests/core/test_timing.cpp
        tests/vi/test_vi.cpp
        tests/pif/test_input.cpp
        tests/rdp/test_rdp.cpp
        tests/rsp/test_rsp.cpp
        tests/rsp/test_rsp_vector.cpp
        tests/ai/test_ai.cpp
        tests/debug/test_debugger.cpp
        tests/core/test_optimization.cpp
        tests/core/test_block_cache.cpp
    )
    target_include_directories(n64emu_tests
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
    )
    target_link_libraries(n64emu_tests
        PRIVATE
            n64::core
            n64::common
            GTest::gtest
    )
    if(N64EMU_BUILD_FRONTEND)
        target_sources(n64emu_tests PRIVATE
            tests/frontend/test_input.cpp
            src/frontend/input.cpp
        )
        target_link_libraries(n64emu_tests PRIVATE SDL2::SDL2)
        if(WIN32)
            add_custom_command(TARGET n64emu_tests POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "$<TARGET_FILE:SDL2::SDL2>"
                    "$<TARGET_FILE_DIR:n64emu_tests>"
                COMMENT "Copying SDL2 runtime next to n64emu_tests"
                VERBATIM
            )
        endif()
    endif()
    include(GoogleTest)
    gtest_discover_tests(n64emu_tests
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        DISCOVERY_TIMEOUT 30
    )

    # Convenience target: ctest via `cmake --build . --target check`
    add_custom_target(check
        COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
        DEPENDS n64emu_tests
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    )
endif()

# ---------------------------------------------------------------------------
# Install (minimal)
# ---------------------------------------------------------------------------
if(N64EMU_BUILD_FRONTEND)
    install(TARGETS n64emu RUNTIME DESTINATION bin)
endif()

message(STATUS "=== n64emu ${PROJECT_VERSION} ===")
message(STATUS "Build type : ${CMAKE_BUILD_TYPE}")
message(STATUS "Tests      : ${N64EMU_BUILD_TESTS}")
message(STATUS "Frontend   : ${N64EMU_BUILD_FRONTEND}")
message(STATUS "Headless   : ${N64EMU_HEADLESS}")
