#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

#include "window.hpp"

// Fixture helpers shared by every window system: options, shader files and
// frame pacing.
namespace fixture {
glyphshift::FixtureOptions options(int argc, char** argv, bool vulkan) {
    std::vector<std::string_view> args;
    for (int i = 1; i < argc; ++i)
        args.emplace_back(argv[i]);
    const auto result = glyphshift::parse_fixture_options(args, vulkan);
    if (result.help) {
        std::cout << "Usage: " << argv[0] << " [--frames N] [--fixed-time SECONDS]"
                  << (vulkan ? " [--validation]" : "")
                  << "\nEscape or the window close button exits. Animation "
                     "advances at 1/60 s per frame.\n";
    }
    return result;
}

std::string read_text(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input)
        throw std::runtime_error("Cannot open shader: " + path);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

std::vector<std::uint32_t> read_spirv(const std::string& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input)
        throw std::runtime_error("Cannot open SPIR-V: " + path);
    const auto size = input.tellg();
    if (size <= 0 || size % 4 != 0)
        throw std::runtime_error("Invalid SPIR-V size: " + path);
    std::vector<std::uint32_t> code(static_cast<std::size_t>(size) / 4);
    input.seekg(0);
    if (!input.read(reinterpret_cast<char*>(code.data()), size))
        throw std::runtime_error("Cannot read SPIR-V: " + path);
    return code;
}

void pace(std::chrono::steady_clock::time_point start, std::uint64_t frame) {
    std::this_thread::sleep_until(
        start +
        std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::duration<double>(static_cast<double>(frame) / glyphshift::fixture_rate)));
}
}  // namespace fixture
