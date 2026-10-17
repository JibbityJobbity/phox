#include "phox/Log.hpp"

#include <atomic>
#include <cstdio>
#include <string>

namespace phox {

namespace {

std::atomic<LogFn> g_callback{nullptr};

const char *levelName(LogLevel level) {
    switch (level) {
    case LogLevel::Debug: return "debug";
    case LogLevel::Info: return "info";
    case LogLevel::Warning: return "warning";
    case LogLevel::Error: return "error";
    }
    return "?";
}

void defaultSink(LogLevel level, std::string_view message) {
    if (level < LogLevel::Warning) {
        return;
    }
    const std::string text(message);
    std::fprintf(stderr, "[phox:%s] %s\n", levelName(level), text.c_str());
}

} // namespace

void setLogCallback(LogFn callback) { g_callback.store(callback); }

void log(LogLevel level, std::string_view message) {
    if (const LogFn callback = g_callback.load()) {
        callback(level, message);
    } else {
        defaultSink(level, message);
    }
}

} // namespace phox
