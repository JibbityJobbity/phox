#include "phox/metal/ShaderLibrary.hpp"

#include "phox/Log.hpp"
#include "phox/metal/Device.hpp"

#include <algorithm>
#include <fstream>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace phox::mtl {

namespace {

std::string readFile(const std::filesystem::path &path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("cannot open Metal shader source " + path.string());
    }
    std::ostringstream text;
    text << file.rdbuf();
    return text.str();
}

// Replaces each `#include "name"` line with the file it names (once per file), keeping <system> includes.
std::string expandIncludes(const std::filesystem::path &path, std::vector<std::string> &included) {
    static const std::regex includeLine(R"(^\s*#\s*include\s*"([^"]+)\"\s*$)");
    std::istringstream lines(readFile(path));
    std::ostringstream out;
    std::string line;
    while (std::getline(lines, line)) {
        std::smatch match;
        if (std::regex_match(line, match, includeLine)) {
            const std::filesystem::path header = path.parent_path() / match[1].str();
            if (std::find(included.begin(), included.end(), header.string()) == included.end()) {
                included.push_back(header.string());
                out << expandIncludes(header, included) << '\n';
            }
            continue;
        }
        out << line << '\n';
    }
    return out.str();
}

} // namespace

ShaderLibrary::ShaderLibrary(Device &device, std::filesystem::path directory)
    : m_device(device), m_directory(std::move(directory)) {}

ShaderLibrary::~ShaderLibrary() = default;

id<MTLLibrary> ShaderLibrary::library(const std::string &stem) {
    if (auto it = m_libraries.find(stem); it != m_libraries.end()) {
        return it->second;
    }
    const std::filesystem::path path = m_directory / (stem + ".metal");
    std::vector<std::string> included;
    const std::string source = expandIncludes(path, included);

    MTLCompileOptions *options = [MTLCompileOptions new];
    // Keep IEEE semantics so results track the Vulkan backend and the goldens.
    options.fastMathEnabled = NO;
    NSError *error = nil;
    id<MTLLibrary> library = [m_device.handle() newLibraryWithSource:[NSString stringWithUTF8String:source.c_str()]
                                                             options:options
                                                               error:&error];
    if (library == nil) {
        const std::string message = error != nil ? error.localizedDescription.UTF8String : "unknown error";
        throw std::runtime_error("failed to compile " + path.string() + ": " + message);
    }
    if (error != nil) {
        log::warn("compiling {}: {}", path.string(), error.localizedDescription.UTF8String); // warnings only
    }
    library.label = [NSString stringWithUTF8String:stem.c_str()];
    log::debug("shader library: compiled {}", path.string());
    m_libraries.emplace(stem, library);
    return library;
}

id<MTLFunction> ShaderLibrary::function(const std::string &name) {
    const std::size_t dot = name.rfind('.');
    if (dot == std::string::npos || dot == 0 || dot + 1 == name.size()) {
        throw std::runtime_error("shader name '" + name + "' is not <stem>.<stage>");
    }
    const std::string stem = name.substr(0, dot);
    const std::string functionName = stem + "_" + name.substr(dot + 1);
    id<MTLFunction> function =
        [library(stem) newFunctionWithName:[NSString stringWithUTF8String:functionName.c_str()]];
    if (function == nil) {
        throw std::runtime_error("no function " + functionName + " in " + (m_directory / (stem + ".metal")).string());
    }
    return function;
}

} // namespace phox::mtl
