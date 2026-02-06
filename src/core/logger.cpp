#include "core/logger.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>

namespace novaria::core {
namespace {

std::mutex& LogMutex() {
    static std::mutex mutex;
    return mutex;
}

std::string BuildTimestamp() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);

    std::tm local_tm{};
#if defined(_WIN32)
    localtime_s(&local_tm, &time);
#else
    localtime_r(&time, &local_tm);
#endif

    std::ostringstream stream;
    stream << std::put_time(&local_tm, "%Y-%m-%d %H:%M:%S");
    return stream.str();
}

}  // namespace

void Logger::Info(std::string_view module, std::string_view message) {
    Log("INFO", module, message);
}

void Logger::Warn(std::string_view module, std::string_view message) {
    Log("WARN", module, message);
}

void Logger::Error(std::string_view module, std::string_view message) {
    Log("ERROR", module, message);
}

void Logger::Log(
    std::string_view level,
    std::string_view module,
    std::string_view message) {
    std::lock_guard<std::mutex> lock(LogMutex());
    std::cout << '[' << BuildTimestamp() << "] [" << level << "] [" << module << "] "
              << message << '\n';
}

}  // namespace novaria::core
