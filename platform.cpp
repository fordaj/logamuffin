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

// Width assumed when the terminal size cannot be determined. This is
// independent of console format widths (kMediumFormatWidth in
// logamuffin.hpp): it is only the TTY fallback, not a layout rule.
constexpr int kTerminalWidthFallback = 120;

// POSIX hostname buffer; 256 covers DNS names without truncation for
// typical hosts.
constexpr std::size_t kHostnameBufferSize = 256;

// Write all bytes, retrying partial writes. POSIX retries EINTR;
// Windows WriteFile has no EINTR equivalent (immediate failure).
#ifdef _WIN32
bool write_all(HANDLE file, const std::string& entry) {
    std::size_t written = 0;
    while (written < entry.size()) {
        DWORD chunk = static_cast<DWORD>(
            std::min<std::size_t>(
                entry.size() - written,
                static_cast<std::size_t>(MAXDWORD)));
        DWORD count = 0;
        if (!WriteFile(file, entry.data() + written, chunk, &count, nullptr) ||
            count == 0) {
            return false;
        }
        written += count;
    }
    return true;
}

// Windows locked append: open -> lock -> seek-end -> write -> unlock -> close.
bool append_locked_windows(
    const std::filesystem::path& path,
    const std::string& entry) {
    HANDLE file = CreateFileW(
        path.c_str(), GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
        OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;
    OVERLAPPED overlap{};
    if (!LockFileEx(file, LOCKFILE_EXCLUSIVE_LOCK, 0, 1, 0, &overlap)) {
        CloseHandle(file);
        return false;
    }
    LARGE_INTEGER end{};
    bool success = SetFilePointerEx(file, end, nullptr, FILE_END) != 0 &&
                   write_all(file, entry);
    if (!UnlockFileEx(file, 0, 1, 0, &overlap))
        success = false;
    CloseHandle(file);
    return success;
}
#else
bool write_all(int fd, const std::string& entry) {
    std::size_t written = 0;
    while (written < entry.size()) {
        const ssize_t count = write(
            fd, entry.data() + written, entry.size() - written);
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0)
            return false;
        written += static_cast<std::size_t>(count);
    }
    return true;
}

// POSIX locked append: open -> flock -> write -> unlock -> close.
bool append_locked_posix(
    const std::filesystem::path& path,
    const std::string& entry) {
    const int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_APPEND,
                        static_cast<mode_t>(0666));
    if (fd == -1)
        return false;
    if (flock(fd, LOCK_EX) != 0) {
        close(fd);
        return false;
    }
    bool success = write_all(fd, entry);
    if (flock(fd, LOCK_UN) != 0)
        success = false;
    if (close(fd) != 0)
        success = false;
    return success;
}
#endif

} // namespace

std::string hostname() {
    std::string result;

#ifdef _WIN32
    char buffer[MAX_COMPUTERNAME_LENGTH + 1]{};
    DWORD size = sizeof(buffer);

    if (GetComputerNameA(buffer, &size))
        result.assign(buffer, size);
#else
    char buffer[kHostnameBufferSize]{};

    if (gethostname(buffer, sizeof(buffer) - 1) == 0)
        result = buffer;
#endif

    // Strip mDNS suffix so output reads "my-mac" not "my-mac.local".
    constexpr std::string_view kSuffix = ".local";
    if (result.size() >= kSuffix.size() &&
        result.compare(result.size() - kSuffix.size(),
                       kSuffix.size(), kSuffix) == 0) {
        result.resize(result.size() - kSuffix.size());
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

    return kTerminalWidthFallback; // Sensible fallback
}

bool append_locked(
    const std::filesystem::path& path,
    const std::string& entry) {
#ifdef _WIN32
    return append_locked_windows(path, entry);
#else
    return append_locked_posix(path, entry);
#endif
}

} // namespace logamuffin
