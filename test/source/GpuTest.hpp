#ifndef PHOX_TEST_GPU_TEST_HPP
#define PHOX_TEST_GPU_TEST_HPP

#include "Check.hpp"
#include "ImageCompare.hpp"

#include "phox/Log.hpp"
#include "phox/rhi/Backend.hpp"

#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace phox::test {

inline bool validationRequested() {
    const char *value = std::getenv("PHOX_VALIDATION");
    return value != nullptr && value[0] == '1';
}

// Golden images are exact on lavapipe. Drivers on real hardware round differently (the clear colour
// 0.1 * 255 = 25.5 comes out as 25 on some), so PHOX_GOLDEN_TOLERANCE=2 allows a per-channel difference
// of 2/255 for at most 0.5% of the pixels. The default is exact.
inline int goldenTolerance() {
    const char *value = std::getenv("PHOX_GOLDEN_TOLERANCE");
    return value != nullptr ? std::atoi(value) : 0;
}

inline bool matchesGolden(const Image &actual, const Image &golden) {
    const int tolerance = goldenTolerance();
    const size_t differing = countDifferingPixels(actual, golden, tolerance);
    const size_t allowed = tolerance == 0 ? 0 : size_t{actual.width} * actual.height / 200;
    if (differing > allowed) {
        std::cerr << differing << " pixels differ from the golden image (tolerance " << tolerance << ", allowed "
                  << allowed << ")\n";
    }
    return differing <= allowed;
}

// Warning and Error messages from the library (including validation layer output) end up here.
inline std::vector<std::string> &capturedLog() {
    static std::vector<std::string> messages;
    return messages;
}

inline void installLogCapture() {
    setLogCallback([](LogLevel level, std::string_view message) {
        if (level >= LogLevel::Warning) {
            capturedLog().emplace_back(message);
        }
    });
}

// Fails the test if anything was logged at Warning or above.
inline void checkLogClean() {
    if (!capturedLog().empty()) {
        for (const std::string &message : capturedLog()) {
            std::cerr << "unexpected log message: " << message << "\n";
        }
        std::exit(1);
    }
}

// GPU tests must fail when no device can be created. Only PHOX_SKIP_GPU_TESTS=1 turns that into a skip
// (exit code 77, registered as SKIP_RETURN_CODE).
inline std::unique_ptr<rhi::Device> createGpuDevice() {
    try {
        rhi::DeviceOptions options;
        options.validation = validationRequested();
        return rhi::createDevice(rhi::Backend::Vulkan, options);
    } catch (const std::exception &error) {
        const char *skip = std::getenv("PHOX_SKIP_GPU_TESTS");
        if (skip != nullptr && skip[0] == '1') {
            std::cout << "SKIPPED: " << error.what() << std::endl;
            std::exit(77);
        }
        std::cerr << "cannot create a Vulkan device: " << error.what() << "\n";
        std::exit(1);
    }
}

inline std::vector<uint32_t> readSpirv(const std::string &path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in.is_open()) {
        throw std::runtime_error("cannot open shader " + path);
    }
    const std::streamsize bytes = in.tellg();
    if (bytes <= 0 || bytes % 4 != 0) {
        throw std::runtime_error("not a SPIR-V file: " + path);
    }
    std::vector<uint32_t> words(static_cast<size_t>(bytes) / 4);
    in.seekg(0);
    in.read(reinterpret_cast<char *>(words.data()), bytes);
    return words;
}

} // namespace phox::test

#endif // PHOX_TEST_GPU_TEST_HPP
