#include "Check.hpp"
#include "ImageCompare.hpp"

#include <array>

#ifndef PHOX_GOLDEN_DIR
#error "PHOX_GOLDEN_DIR must be defined by the build"
#endif

namespace {

std::array<int, 3> pixel(const phox::test::Image &image, std::uint32_t x, std::uint32_t y) {
    const std::size_t i = (static_cast<std::size_t>(y) * image.width + x) * 3;
    return {image.rgb[i], image.rgb[i + 1], image.rgb[i + 2]};
}

} // namespace

int main() {
    const auto golden = phox::test::loadPpm(std::filesystem::path(PHOX_GOLDEN_DIR) / "triangle_640x480.ppm");
    CHECK(golden.width == 640 && golden.height == 480);

    // The golden is the baseline the refactor must reproduce: clear colour (0.1, 0.2, 0.3) with the
    // albedo (1.0, 0.2, 0.4) shape covering exactly the top-right quadrant.
    const std::array<int, 3> background{26, 51, 76};
    const std::array<int, 3> albedo{255, 51, 102};
    CHECK(pixel(golden, 100, 100) == background);
    CHECK(pixel(golden, 100, 400) == background);
    CHECK(pixel(golden, 500, 400) == background);
    CHECK(pixel(golden, 500, 100) == albedo);
    CHECK(pixel(golden, 320, 0) == albedo);
    CHECK(pixel(golden, 319, 0) == background);
    CHECK(pixel(golden, 639, 239) == albedo);
    CHECK(pixel(golden, 639, 240) == background);

    // Identical images: zero differing pixels.
    CHECK(phox::test::countDifferingPixels(golden, golden) == 0);

    // Changing one pixel by more than the tolerance is detected, and by no more than it is not.
    auto changed = golden;
    changed.rgb[0] = static_cast<std::uint8_t>(changed.rgb[0] + 3);
    CHECK(phox::test::countDifferingPixels(golden, changed) == 1);
    CHECK(phox::test::countDifferingPixels(golden, changed, 3) == 0);
    changed.rgb[3 * 640 + 1] = static_cast<std::uint8_t>(changed.rgb[3 * 640 + 1] + 40);
    CHECK(phox::test::countDifferingPixels(golden, changed) == 2);

    // Mismatched dimensions and a missing file are errors, not silent passes.
    auto smaller = golden;
    smaller.height = 479;
    bool threw = false;
    try {
        phox::test::countDifferingPixels(golden, smaller);
    } catch (const std::runtime_error &) {
        threw = true;
    }
    CHECK(threw);

    threw = false;
    try {
        phox::test::loadPpm("does-not-exist.ppm");
    } catch (const std::runtime_error &) {
        threw = true;
    }
    CHECK(threw);

    std::cout << "image compare test passed" << std::endl;
    return 0;
}
