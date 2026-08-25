#pragma once

#include <string>
#include <string_view>

namespace n64 {

struct Version {
    int major = N64EMU_VERSION_MAJOR;
    int minor = N64EMU_VERSION_MINOR;
    int patch = N64EMU_VERSION_PATCH;

    [[nodiscard]] std::string to_string() const;
};

[[nodiscard]] Version version();
[[nodiscard]] std::string_view project_name() noexcept;

} // namespace n64
