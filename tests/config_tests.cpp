#include <expedition_online/client/config.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace client = expedition_online::client;

auto main() -> int {
  const auto suffix =
      std::chrono::steady_clock::now().time_since_epoch().count();
  const auto path =
      std::filesystem::temp_directory_path() /
      ("ExpeditionOnline-config-" + std::to_string(suffix) + ".ini");
  try {
    {
      std::ofstream output(path);
      output << "[network]\nServerHost=127.0.0.1\nServerPort=7777\n"
                "[game]\nremote_network_authority=false\n"
                "unsafe_direct_appearance=true\nunsafe_direct_hair=on\n";
    }
    const auto parsed = client::load_client_config(path);
    if (parsed.remote_network_authority || !parsed.unsafe_direct_appearance ||
        !parsed.unsafe_direct_hair) {
      throw std::runtime_error(
          "experimental bool config values were not parsed");
    }
    std::filesystem::remove(path);

    const auto defaults_path = path.string() + ".defaults";
    {
      std::ofstream output(defaults_path);
      output << "[network]\nServerHost=127.0.0.1\n";
    }
    const auto defaults = client::load_client_config(defaults_path);
    std::filesystem::remove(defaults_path);
    if (!defaults.remote_network_authority ||
        defaults.unsafe_direct_appearance || defaults.unsafe_direct_hair) {
      throw std::runtime_error("crash-safe config defaults changed");
    }
    std::cout << "Client config tests passed\n";
    return 0;
  } catch (const std::exception &exception) {
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    std::filesystem::remove(path.string() + ".defaults", ignored);
    std::cerr << "CONFIG TEST FAILURE: " << exception.what() << '\n';
    return 1;
  }
}
