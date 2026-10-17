#ifndef PHOX_TEST_IMAGE_COMPARE_HPP
#define PHOX_TEST_IMAGE_COMPARE_HPP

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace phox::test {

struct Image {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> rgb; // width * height * 3
};

// Loads a binary PPM (P6, maxval 255). Throws std::runtime_error on any malformed input.
inline Image loadPpm(const std::filesystem::path &path) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        throw std::runtime_error("Failed to open image: " + path.string());
    }
    const std::vector<char> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    std::size_t pos = 0;
    const auto token = [&]() {
        while (pos < data.size() && (data[pos] == ' ' || data[pos] == '\n' || data[pos] == '\r' || data[pos] == '\t')) {
            ++pos;
        }
        const std::size_t start = pos;
        while (pos < data.size() && data[pos] != ' ' && data[pos] != '\n' && data[pos] != '\r' && data[pos] != '\t') {
            ++pos;
        }
        return std::string(data.data() + start, pos - start);
    };

    if (token() != "P6") {
        throw std::runtime_error("Not a binary PPM (P6): " + path.string());
    }
    Image image;
    image.width = static_cast<std::uint32_t>(std::stoul(token()));
    image.height = static_cast<std::uint32_t>(std::stoul(token()));
    if (token() != "255") {
        throw std::runtime_error("Unsupported PPM maxval: " + path.string());
    }
    ++pos; // exactly one whitespace byte separates the header from the pixels

    const std::size_t bytes = static_cast<std::size_t>(image.width) * image.height * 3;
    if (data.size() - pos != bytes) {
        throw std::runtime_error("PPM pixel data has the wrong size: " + path.string());
    }
    image.rgb.assign(reinterpret_cast<const std::uint8_t *>(data.data()) + pos,
                     reinterpret_cast<const std::uint8_t *>(data.data()) + pos + bytes);
    return image;
}

// Number of pixels where any channel differs by more than `tolerance`.
// Throws std::runtime_error if the dimensions differ.
inline std::size_t countDifferingPixels(const Image &a, const Image &b, int tolerance = 2) {
    if (a.width != b.width || a.height != b.height) {
        throw std::runtime_error("Image dimensions differ");
    }
    std::size_t differing = 0;
    for (std::size_t i = 0; i < static_cast<std::size_t>(a.width) * a.height; ++i) {
        for (std::size_t c = 0; c < 3; ++c) {
            if (std::abs(static_cast<int>(a.rgb[i * 3 + c]) - static_cast<int>(b.rgb[i * 3 + c])) > tolerance) {
                ++differing;
                break;
            }
        }
    }
    return differing;
}

} // namespace phox::test

#endif // PHOX_TEST_IMAGE_COMPARE_HPP
