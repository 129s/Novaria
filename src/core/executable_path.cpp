#include "core/executable_path.h"

#include <system_error>

#if defined(_WIN32)
#include <Windows.h>
#endif

namespace novaria::core {

std::filesystem::path GetExecutablePath() {
#if defined(_WIN32)
    std::wstring buffer;
    buffer.resize(32 * 1024);

    const DWORD size =
        ::GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (size == 0) {
        return std::filesystem::current_path();
    }

    buffer.resize(size);
    return std::filesystem::path(buffer);
#else
    std::error_code ec;
    const std::filesystem::path proc_path = "/proc/self/exe";
    if (std::filesystem::exists(proc_path, ec)) {
        const std::filesystem::path resolved = std::filesystem::read_symlink(proc_path, ec);
        if (!ec && !resolved.empty()) {
            return resolved;
        }
    }
    return std::filesystem::current_path();
#endif
}

}  // namespace novaria::core

