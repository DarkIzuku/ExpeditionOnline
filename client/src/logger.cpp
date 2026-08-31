#include <expedition_online/client/logger.hpp>

#include <chrono>
#include <iomanip>
#include <sstream>

namespace expedition_online::client
{
Logger::Logger(const std::filesystem::path& path) : file_(path, std::ios::app) {}

auto Logger::info(const std::string& message) -> void
{
    write("INFO", message);
}

auto Logger::warning(const std::string& message) -> void
{
    write("WARN", message);
}

auto Logger::error(const std::string& message) -> void
{
    write("ERROR", message);
}

auto Logger::write(const char* level, const std::string& message) -> void
{
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &time);
#else
    localtime_r(&time, &local);
#endif
    std::ostringstream line;
    line << '[' << std::put_time(&local, "%Y-%m-%d %H:%M:%S") << "] [" << level << "] " << message;
    std::lock_guard lock(mutex_);
    if (file_)
    {
        file_ << line.str() << '\n';
        file_.flush();
    }
}
} // namespace expedition_online::client
