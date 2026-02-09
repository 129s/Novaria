#pragma once

#include <string>
#include <string_view>

namespace novaria::core {

class Logger final {
public:
    static void Info(std::string_view module, std::string_view message);
    static void Warn(std::string_view module, std::string_view message);
    static void Error(std::string_view module, std::string_view message);

private:
    static void Log(std::string_view level, std::string_view module, std::string_view message);
};

}  // namespace novaria::core
