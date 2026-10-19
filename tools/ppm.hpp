#pragma once
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

// Minimal P6 RGB PPM files for the preview tools: no header comments,
// dimensions 1..8192, rows top-down.
namespace glyphshift::tools {
inline std::vector<unsigned char> read_ppm(const char* path, unsigned& width, unsigned& height) {
    std::ifstream input(path, std::ios::binary);
    std::string magic;
    unsigned maximum = 0;
    input >> magic >> width >> height >> maximum;
    if (magic != "P6" || maximum != 255 || !width || !height || width > 8192 || height > 8192)
        throw std::runtime_error("Expected P6 RGB PPM, no comments, dimensions 1..8192");
    if (input.get() != '\n')
        throw std::runtime_error("Expected newline before PPM pixels");
    std::vector<unsigned char> pixels(static_cast<std::size_t>(width) * height * 3);
    if (!input.read(reinterpret_cast<char*>(pixels.data()),
            static_cast<std::streamsize>(pixels.size())))
        throw std::runtime_error("Cannot read input pixels");
    return pixels;
}

inline void write_ppm(const char* path,
    const std::vector<unsigned char>& pixels,
    unsigned width,
    unsigned height) {
    std::ofstream destination(path, std::ios::binary);
    destination << "P6\n" << width << ' ' << height << "\n255\n";
    destination.write(reinterpret_cast<const char*>(pixels.data()),
        static_cast<std::streamsize>(pixels.size()));
    if (!destination)
        throw std::runtime_error("Cannot write output");
}
}  // namespace glyphshift::tools
