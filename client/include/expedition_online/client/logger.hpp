#pragma once

#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>

namespace expedition_online::client
{
class Logger
{
  public:
    explicit Logger(const std::filesystem::path& path);
    auto info(const std::string& message) -> void;
    auto warning(const std::string& message) -> void;
    auto error(const std::string& message) -> void;

  private:
    auto write(const char* level, const std::string& message) -> void;

    std::mutex mutex_;
    std::ofstream file_;
};
} // namespace expedition_online::client
