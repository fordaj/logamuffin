#include "platform.hpp"

#include <algorithm>

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
    #include <io.h>
#else
    #include <cerrno>
    #include <fcntl.h>
    #include <sys/file.h>
    #include <sys/ioctl.h>
    #include <unistd.h>
#endif

#include <string_view>

namespace logamuffin {

namespace {

// Width assumed when the terminal size cannot be determined.
constexpr int kFallbackTerminalWidth = 120;

} // namespace

std::string hostname() {
    std::string result;

#ifdef _WIN32
    char buffer[MAX_COMPUTERNAME_LENGTH + 1]{};
    DWORD size = sizeof(buffer);

    if (GetComputerNameA(buffer, &size))
        result.assign(buffer, size);
#else
    char buffer[256]{};

    if (gethostname(buffer, sizeof(buffer) - 1) == 0)
        result = buffer;
#endif

    constexpr std::string_view suffix = ".local";

    if (result.size() >= suffix.size() &&
        result.compare(result.size() - suffix.size(),
                       suffix.size(), suffix) == 0) {
        result.resize(result.size() - suffix.size());
    }

    return result.empty() ? "unknown" : result;
}

bool terminal(FILE* stream) {
#ifdef _WIN32
    const int fd = stream == stderr ? _fileno(stderr) : _fileno(stdout);
    return _isatty(fd) != 0;
#else
    const int fd = stream == stderr ? STDERR_FILENO : STDOUT_FILENO;
    return isatty(fd) != 0;
#endif
}


int terminal_width() {
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO info;
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);

    if (GetConsoleScreenBufferInfo(out, &info)) {
        return info.srWindow.Right - info.srWindow.Left + 1;
    }
#else
    struct winsize ws {};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 &&
        ws.ws_col > 0) {
        return ws.ws_col;
    }
#endif

    return kFallbackTerminalWidth; // Sensible fallback
}

bool append_locked(
    const std::filesystem::path& path,
    const std::string& entry)
{

#ifdef _WIN32
    HANDLE file = CreateFileW(
        path.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);

    if (file == INVALID_HANDLE_VALUE)
        return false;

    OVERLAPPED overlap{};
    if (!LockFileEx(
            file, LOCKFILE_EXCLUSIVE_LOCK,
            0, 1, 0, &overlap)) {
        CloseHandle(file);
        return false;
    }

    LARGE_INTEGER end{};
    bool success = SetFilePointerEx(
        file, end, nullptr, FILE_END) != 0;

    std::size_t written = 0;
    while (success && written < entry.size()) {
        DWORD chunk = static_cast<DWORD>(
            std::min<std::size_t>(
                entry.size() - written,
                static_cast<std::size_t>(MAXDWORD)));

        DWORD count = 0;
        if (!WriteFile(
                file, entry.data() + written,
                chunk, &count, nullptr) || count == 0) {
            success = false;
            break;
        }

        written += count;
    }

    if (!UnlockFileEx(file, 0, 1, 0, &overlap))
        success = false;

    CloseHandle(file);
    return success;

    #else
    const int fd = open(
        path.c_str(),
        O_WRONLY | O_CREAT | O_APPEND,
        0666);

    if (fd == -1)
        return false;

    bool success = false;

    if (flock(fd, LOCK_EX) == 0) {
        std::size_t written = 0;
        success = true;

        while (written < entry.size()) {
            const ssize_t count = write(
                fd,
                entry.data() + written,
                entry.size() - written);

            if (count < 0 && errno == EINTR)
                continue;

            if (count <= 0) {
                success = false;
                break;
            }

            written += static_cast<std::size_t>(count);
        }

        if (flock(fd, LOCK_UN) != 0)
            success = false;
    }

    if (close(fd) != 0)
        success = false;

    return success;
#endif
}

} // namespace logamuffin
