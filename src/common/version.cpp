#include "n64/common/version.hpp"

#include <sstream>

namespace n64 {

std::string Version::to_string() const {
    std::ostringstream oss;
    oss << major << '.' << minor << '.' << patch;
    return oss.str();
}

Version version() {
    return Version{};
}

std::string_view project_name() noexcept {
    return "n64emu";
}

} // namespace n64
