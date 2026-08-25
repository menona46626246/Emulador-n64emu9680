#pragma once

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <memory>
#include <string_view>

namespace n64::log {

/// Initialize the global logger. Safe to call once at process start.
void init(std::string_view pattern = "[%H:%M:%S.%e] [%^%l%$] %v",
          spdlog::level::level_enum level = spdlog::level::info);

/// Access the shared logger (creates a default one if init was never called).
std::shared_ptr<spdlog::logger>& get();

// Convenience macros — prefer these over bare spdlog calls so we can redirect later.
#define N64_TRACE(...)    ::spdlog::trace(__VA_ARGS__)
#define N64_DEBUG(...)    ::spdlog::debug(__VA_ARGS__)
#define N64_INFO(...)     ::spdlog::info(__VA_ARGS__)
#define N64_WARN(...)     ::spdlog::warn(__VA_ARGS__)
#define N64_ERROR(...)    ::spdlog::error(__VA_ARGS__)
#define N64_CRITICAL(...) ::spdlog::critical(__VA_ARGS__)

} // namespace n64::log
