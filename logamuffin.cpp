
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <cstdio>
#include <exception>

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
    #include <io.h>
#else
    #include <sys/file.h>
    #include <sys/ioctl.h>
    #include <fcntl.h>
    #include <unistd.h>
    #include <cerrno>
#endif

#include "version.hpp"
#include "logamuffin.hpp"

namespace {

using namespace logamuffin;

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

    return 120; // Sensible fallback
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


int run(int argc, char* argv[]) {
    if (argc == 2 && std::string_view(argv[1]) == "--version") {
        std::cout << "logamuffin " << LOGAMUFFIN_VERSION << '\n';
        return 0;
    }
    if (argc < 3) {
        std::cerr
            << "Usage: logamuffin LEVEL MESSAGE [MESSAGE ...]\n"
            << "Levels: TRACE DEBUG INFO WARN WARNING ERROR "
               "CRITICAL FATAL ALWAYS\n";
        return 2;
    }


    Level level{};
    if (!parse_level(argv[1], level) ||
        level == Level::Always) {
        if (uppercase(argv[1]) != "ALWAYS") {
            std::cerr << "Unknown log level: " << argv[1]
                      << ". Valid levels: TRACE, DEBUG, INFO, "
                         "WARNING/WARN, ERROR, CRITICAL/FATAL, ALWAYS\n";
            return 2;
        }
        level = Level::Always;
    }

    Level threshold = Level::Info;
    if (const char* env = std::getenv("LOG_LEVEL")) {
        if (!parse_level(env, threshold) ||
            threshold == Level::Always) {
            std::cerr << "Invalid LOG_LEVEL: " << env << '\n';
            return 2;
        }
    }

    // Join all message arguments with spaces and strip carriage returns.
    const std::string message = join_message(argc, argv, 2);

    // Filter before doing timestamp, formatting, and file I/O.
    if (!should_emit(level, threshold)) {
        return 0;
    }

    const std::string time = timestamp();
    const std::string pc = hostname();
    const std::string source = get_source(argv[0]);

    FILE* stream = level >= Level::Error ? stderr : stdout;
    const bool color = env_enabled("LOG_COLOR", terminal(stream));

    const int width = terminal(stream) ? terminal_width() : 180;

    const std::string output =
        format_console_output(time, pc, level, source, message, color, width);

    std::fwrite(output.data(), 1, output.size(), stream);
    std::fflush(stream);


    // Plain-text file logging, independent of console colors.
    const char* file_env = std::getenv("LOG_FILE");
    const std::filesystem::path file =
        file_env && *file_env ? file_env : ".logs/log.log";

    std::error_code ec;
    if (!file.parent_path().empty()) {
        std::filesystem::create_directories(
            file.parent_path(), ec);

        if (ec) {
            std::cerr << "logamuffin: failed to create log directory: "
                      << ec.message() << '\n';
            return 1;
        }
    }

    // Construct the complete entry before acquiring the lock.
    const std::string entry =
        format_file_entry(time, pc, level, source, message);

    if (!append_locked(file, entry)) {
        std::cerr << "logamuffin: failed to append log file\n";
    }


    return 0;
}

} // namespace

int main(int argc, char* argv[]) {
    try {
        return run(argc, argv);
    } catch (const std::exception& e) {
        std::cerr << "logamuffin: " << e.what() << '\n';
        return 1;
    }
}
