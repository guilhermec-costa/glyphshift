#include <glyphshift/platform/process.hpp>
#include <stdexcept>

namespace glyphshift::platform {
std::filesystem::path executable_path() {
    std::error_code error;
    auto path = std::filesystem::read_symlink("/proc/self/exe", error);
    if (error || path.empty())
        throw std::runtime_error("cannot locate the running executable");
    return path;
}
}  // namespace glyphshift::platform
