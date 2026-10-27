#ifndef PHOX_CPU_IMAGE_HPP
#define PHOX_CPU_IMAGE_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace phox {

/// An 8-bit RGBA image in host memory: rows top to bottom, 4 bytes per pixel.
struct CpuImage {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> rgba;

    bool empty() const { return rgba.empty(); }
    std::array<std::uint8_t, 4> pixel(std::uint32_t x, std::uint32_t y) const {
        const std::size_t i = (static_cast<std::size_t>(y) * width + x) * 4;
        return {rgba[i], rgba[i + 1], rgba[i + 2], rgba[i + 3]};
    }
};

/// Writes a binary PPM (P6). Alpha is dropped. Creates parent directories.
/// @throws std::runtime_error if the file cannot be written
void writePpm(const std::filesystem::path &path, const CpuImage &image);

/// Reads a binary PPM (P6, maxval 255). Alpha is set to 255.
/// @throws std::runtime_error if the file is missing or not a P6 PPM
CpuImage readPpm(const std::filesystem::path &path);

struct ImageDiff {
    bool sizeMismatch = false;
    std::uint32_t maxChannelDelta = 0;    ///< largest |a - b| over the RGB channels of all pixels
    std::size_t pixelsOverTolerance = 0;  ///< pixels with any RGB channel differing by more than the tolerance
};

/// Compares the RGB channels of two images.
ImageDiff compareImages(const CpuImage &a, const CpuImage &b, std::uint32_t tolerance);

} // namespace phox

#endif // PHOX_CPU_IMAGE_HPP
