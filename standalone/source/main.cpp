// Renders a triangle with the forward technique on the Vulkan backend and writes it as a PPM image.

#include "phox/forward/ForwardTechnique.hpp"
#include "phox/graph/RenderGraph.hpp"
#include "phox/rhi/Backend.hpp"
#include "phox/scene/Camera.hpp"
#include "phox/scene/Scene.hpp"

#include <cxxopts.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>

namespace {

constexpr uint32_t kWidth = 640;
constexpr uint32_t kHeight = 480;

// `rgba` is tightly packed 8-bit RGBA; the PPM keeps only RGB.
void writePpm(const std::filesystem::path &path, uint32_t width, uint32_t height, std::span<const std::byte> rgba) {
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path());
    }
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        throw std::runtime_error("cannot write " + path.string());
    }
    out << "P6\n" << width << " " << height << "\n255\n";
    for (size_t i = 0; i < size_t{width} * height; ++i) {
        out.write(reinterpret_cast<const char *>(rgba.data() + i * 4), 3);
    }
}

} // namespace

int main(int argc, char **argv) {
    cxxopts::Options options("PhoxSample", "Renders a triangle with the forward technique and writes a PPM image");
    options.add_options()("o,out", "output image", cxxopts::value<std::string>()->default_value("./rendered_triangle.ppm"))(
        "v,validation", "enable Vulkan validation layers")("h,help", "print this help");

    try {
        const auto args = options.parse(argc, argv);
        if (args.count("help") != 0) {
            std::cout << options.help() << std::endl;
            return 0;
        }

        phox::rhi::DeviceOptions deviceOptions;
        deviceOptions.validation = args.count("validation") != 0;
        const auto device = phox::rhi::createDevice(phox::rhi::Backend::Vulkan, deviceOptions);

        phox::forward::ForwardTechnique technique;
        if (!technique.isSupported(device->capabilities())) {
            throw std::runtime_error("this GPU does not support the forward technique");
        }
        technique.init(*device);

        phox::scene::Scene scene;
        scene.addObject(phox::scene::ObjectFactory::createTriangle("triangle", phox::scene::Material{"basic", "default"}));
        const phox::scene::Camera camera = phox::scene::Camera::unitOrtho();

        const phox::rhi::TextureHandle target = device->createTexture(
            {kWidth, kHeight, 1, phox::rhi::Format::RGBA8Unorm,
             phox::rhi::TextureUsage::ColorAttachment | phox::rhi::TextureUsage::TransferSrc, "target"});

        {
            phox::graph::RenderGraph graph(*device);
            phox::graph::GraphBuilder &builder = graph.begin();
            const phox::graph::GraphTexture imported =
                builder.importTexture(target, phox::rhi::ResourceState::Undefined, phox::rhi::ResourceState::TransferSrc);
            technique.setFrame(scene, camera, imported);
            technique.build(builder);
            graph.compile();
            device->wait(graph.execute());

            const auto pixels = device->map(graph.resolve(technique.readbackBuffer()));
            const std::string outPath = args["out"].as<std::string>();
            writePpm(outPath, kWidth, kHeight, pixels);
            std::cout << "Rendered " << scene.objectCount() << " object(s) with the '" << technique.name()
                      << "' technique on " << device->capabilities().deviceName << "; wrote " << outPath << std::endl;
        }

        device->destroy(target);
        technique.shutdown(*device);
    } catch (const std::exception &error) {
        std::cerr << "PhoxSample failed: " << error.what() << std::endl;
        return 1;
    }
    return 0;
}
