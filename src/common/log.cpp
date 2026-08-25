#include "n64/common/log.hpp"

#include <mutex>

namespace n64::log {
namespace {

std::shared_ptr<spdlog::logger> g_logger;
std::once_flag g_once;

void ensure_default() {
    std::call_once(g_once, [] {
        if (!g_logger) {
            g_logger = spdlog::stdout_color_mt("n64emu");
            g_logger->set_level(spdlog::level::info);
            g_logger->set_pattern("[%H:%M:%S.%e] [%^%l%$] %v");
            spdlog::set_default_logger(g_logger);
        }
    });
}

} // namespace

void init(std::string_view pattern, spdlog::level::level_enum level) {
    auto sink_logger = spdlog::stdout_color_mt("n64emu");
    sink_logger->set_level(level);
    sink_logger->set_pattern(std::string(pattern));
    g_logger = sink_logger;
    spdlog::set_default_logger(g_logger);
    // Mark once_flag consumed so ensure_default becomes a no-op.
    std::call_once(g_once, [] {});
}

std::shared_ptr<spdlog::logger>& get() {
    ensure_default();
    return g_logger;
}

} // namespace n64::log
