#include "phox/CpuImage.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <string>

namespace phox {

void writePpm(const std::filesystem::path &path, const CpuImage &image) {
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path());
    }
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        throw std::runtime_error("cannot open " + path.string() + " for writing");
    }
    out << "P6\n" << image.width << " " << image.height << "\n255\n";
    std::vector<char> row(static_cast<std::size_t>(image.width) * 3);
    for (std::uint32_t y = 0; y < image.height; ++y) {
        for (std::uint32_t x = 0; x < image.width; ++x) {
            const std::size_t src = (static_cast<std::size_t>(y) * image.width + x) * 4;
            row[x * 3 + 0] = static_cast<char>(image.rgba[src + 0]);
            row[x * 3 + 1] = static_cast<char>(image.rgba[src + 1]);
            row[x * 3 + 2] = static_cast<char>(image.rgba[src + 2]);
        }
        out.write(row.data(), static_cast<std::streamsize>(row.size()));
    }
    if (!out) {
        throw std::runtime_error("failed writing " + path.string());
    }
}

namespace {

// Reads the next whitespace-separated header token, skipping '#' comments.
std::string headerToken(std::istream &in) {
    std::string token;
    while (in) {
        const int c = in.get();
        if (c == '#') {
            std::string ignored;
            std::getline(in, ignored);
        } else if (std::isspace(c)) {
            if (!token.empty()) break;
        } else if (c != EOF) {
            token.push_back(static_cast<char>(c));
        }
    }
    return token;
}

} // namespace

CpuImage readPpm(const std::filesystem::path &path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("cannot open " + path.string());
    }
    if (headerToken(in) != "P6") {
        throw std::runtime_error(path.string() + " is not a binary PPM (P6)");
    }
    CpuImage image;
    image.width = static_cast<std::uint32_t>(std::stoul(headerToken(in)));
    image.height = static_cast<std::uint32_t>(std::stoul(headerToken(in)));
    if (headerToken(in) != "255") {
        throw std::runtime_error(path.string() + ": only maxval 255 is supported");
    }
    const std::size_t pixels = static_cast<std::size_t>(image.width) * image.height;
    std::vector<char> rgb(pixels * 3);
    in.read(rgb.data(), static_cast<std::streamsize>(rgb.size()));
    if (in.gcount() != static_cast<std::streamsize>(rgb.size())) {
        throw std::runtime_error(path.string() + " is truncated");
    }
    image.rgba.resize(pixels * 4);
    for (std::size_t i = 0; i < pixels; ++i) {
        image.rgba[i * 4 + 0] = static_cast<std::uint8_t>(rgb[i * 3 + 0]);
        image.rgba[i * 4 + 1] = static_cast<std::uint8_t>(rgb[i * 3 + 1]);
        image.rgba[i * 4 + 2] = static_cast<std::uint8_t>(rgb[i * 3 + 2]);
        image.rgba[i * 4 + 3] = 255;
    }
    return image;
}

ImageDiff compareImages(const CpuImage &a, const CpuImage &b, std::uint32_t tolerance) {
    ImageDiff diff;
    if (a.width != b.width || a.height != b.height || a.rgba.size() != b.rgba.size()) {
        diff.sizeMismatch = true;
        return diff;
    }
    const std::size_t pixels = static_cast<std::size_t>(a.width) * a.height;
    for (std::size_t i = 0; i < pixels; ++i) {
        std::uint32_t worst = 0;
        for (std::size_t c = 0; c < 3; ++c) {
            const int delta = std::abs(int(a.rgba[i * 4 + c]) - int(b.rgba[i * 4 + c]));
            worst = std::max(worst, static_cast<std::uint32_t>(delta));
        }
        diff.maxChannelDelta = std::max(diff.maxChannelDelta, worst);
        if (worst > tolerance) {
            ++diff.pixelsOverTolerance;
        }
    }
    return diff;
}

} // namespace phox
