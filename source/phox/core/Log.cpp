#include "phox/Log.hpp"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>

namespace phox::log {

namespace {

Level levelFromEnvironment() {
    const char *value = std::getenv("PHOX_LOG");
    if (value == nullptr) return Level::Info;
    if (std::strcmp(value, "debug") == 0) return Level::Debug;
    if (std::strcmp(value, "warn") == 0) return Level::Warn;
    if (std::strcmp(value, "error") == 0) return Level::Error;
    if (std::strcmp(value, "off") == 0) return Level::Off;
    return Level::Info;
}

std::atomic<Level> &currentLevel() {
    static std::atomic<Level> level{levelFromEnvironment()};
    return level;
}

const char *prefix(Level level) {
    switch (level) {
    case Level::Debug: return "debug";
    case Level::Info: return "info";
    case Level::Warn: return "warn";
    case Level::Error: return "error";
    case Level::Off: break;
    }
    return "";
}

} // namespace

void setLevel(Level level) { currentLevel().store(level); }

Level level() { return currentLevel().load(); }

void write(Level level, std::string_view message) {
    static std::mutex mutex;
    const std::lock_guard<std::mutex> lock(mutex);
    std::fprintf(stderr, "[phox] %s: %.*s\n", prefix(level), static_cast<int>(message.size()), message.data());
}

} // namespace phox::log
