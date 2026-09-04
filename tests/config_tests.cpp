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
      output
          << "[network]\nServerHost=127.0.0.1\nServerPort=7777\n"
             "[game]\nremote_network_authority=false\n"
             "remote_use_movement_input=true\n"
             "fallback_ai_companion=false\nvanilla_customization=false\n"
             "world_map_remote=false\nremote_actor_mode=ai_companion_legacy\n"
             "sync_locomotion_state=false\nsync_gait=false\n"
             "sync_crouch=false\nsync_aim=false\n"
             "teleport_threshold_units=1234\n"
             "unsafe_direct_appearance=true\nunsafe_direct_hair=on\n";
    }
    const auto parsed = client::load_client_config(path);
    if (parsed.remote_network_authority || !parsed.remote_use_movement_input ||
        parsed.fallback_ai_companion || parsed.vanilla_customization ||
        parsed.world_map_remote || parsed.sync_locomotion_state ||
        parsed.sync_gait || parsed.sync_crouch || parsed.sync_aim ||
        parsed.remote_actor_mode != "ai_companion_legacy" ||
        parsed.teleport_threshold_units != 1234.0F ||
        !parsed.unsafe_direct_appearance || !parsed.unsafe_direct_hair) {
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
        defaults.remote_use_movement_input || !defaults.fallback_ai_companion ||
        !defaults.vanilla_customization || !defaults.world_map_remote ||
        !defaults.sync_locomotion_state || !defaults.sync_gait ||
        !defaults.sync_crouch || !defaults.sync_aim ||
        defaults.remote_actor_mode != "world_character" ||
        defaults.teleport_threshold_units != 5000.0F ||
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
