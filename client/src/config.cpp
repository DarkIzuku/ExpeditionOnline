#include <expedition_online/client/config.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <stdexcept>

namespace expedition_online::client {
namespace {
auto trim(std::string value) -> std::string {
  const auto not_space = [](unsigned char c) {
    return c != ' ' && c != '\t' && c != '\r' && c != '\n';
  };
  value.erase(value.begin(),
              std::find_if(value.begin(), value.end(), not_space));
  value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(),
              value.end());
  return value;
}

auto parse_bool(std::string value) -> bool {
  std::transform(
      value.begin(), value.end(), value.begin(),
      [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  value = trim(std::move(value));
  if (value == "true" || value == "1" || value == "yes" || value == "on")
    return true;
  if (value == "false" || value == "0" || value == "no" || value == "off")
    return false;
  throw std::runtime_error("invalid boolean value: " + value);
}
} // namespace

auto load_client_config(const std::filesystem::path &path) -> ClientConfig {
  ClientConfig config;
  std::ifstream input(path);
  if (!input) {
    throw std::runtime_error("cannot open client config: " + path.string());
  }

  std::string section;
  std::string line;
  while (std::getline(input, line)) {
    line = trim(line);
    if (line.empty() || line.front() == '#' || line.front() == ';')
      continue;
    if (line.front() == '[' && line.back() == ']') {
      section = trim(line.substr(1, line.size() - 2));
      continue;
    }
    const auto separator = line.find('=');
    if (separator == std::string::npos)
      continue;
    const auto key = trim(line.substr(0, separator));
    const auto value = trim(line.substr(separator + 1));

    if (section == "companions") {
      if (!key.empty() && !value.empty())
        config.companion_by_character[key] = value;
      continue;
    }

    if (key == "host" || key == "ServerHost")
      config.host = value;
    else if (key == "port" || key == "ServerPort") {
      const auto parsed = std::stoi(value);
      if (parsed < 1 || parsed > 65535)
        throw std::runtime_error("port must be in 1..65535");
      config.port = static_cast<std::uint16_t>(parsed);
    } else if (key == "player_name" || key == "PlayerName")
      config.player_name = value;
    else if (key == "reconnect_delay_ms")
      config.reconnect_delay_ms = std::clamp(std::stoi(value), 250, 60000);
    else if (key == "snapshot_hz")
      config.snapshot_hz = std::clamp(std::stoi(value), 1, 60);
    else if (key == "interpolation_delay_ms")
      config.interpolation_delay_ms = std::clamp(std::stoi(value), 0, 2000);
    else if (key == "heartbeat_interval_seconds" || key == "HeartbeatInterval")
      config.heartbeat_interval_seconds = std::clamp(std::stoi(value), 1, 60);
    else if (key == "server_timeout_seconds" || key == "ServerTimeout")
      config.server_timeout_seconds = std::clamp(std::stoi(value), 3, 300);
    else if (key == "remote_network_authority")
      config.remote_network_authority = parse_bool(value);
    else if (key == "remote_use_movement_input")
      config.remote_use_movement_input = parse_bool(value);
    else if (key == "fallback_ai_companion")
      config.fallback_ai_companion = parse_bool(value);
    else if (key == "vanilla_customization")
      config.vanilla_customization = parse_bool(value);
    else if (key == "world_map_remote")
      config.world_map_remote = parse_bool(value);
    else if (key == "sync_locomotion_state")
      config.sync_locomotion_state = parse_bool(value);
    else if (key == "sync_gait")
      config.sync_gait = parse_bool(value);
    else if (key == "sync_crouch")
      config.sync_crouch = parse_bool(value);
    else if (key == "sync_aim")
      config.sync_aim = parse_bool(value);
    else if (key == "legacy_visual_diagnostics")
      config.legacy_visual_diagnostics = parse_bool(value);
    else if (key == "rotation_diagnostics")
      config.rotation_diagnostics = parse_bool(value);
    else if (key == "unsafe_direct_appearance")
      config.unsafe_direct_appearance = parse_bool(value);
    else if (key == "unsafe_direct_hair")
      config.unsafe_direct_hair = parse_bool(value);
    else if (key == "teleport_threshold_units")
      config.teleport_threshold_units =
          std::clamp(std::stof(value), 100.0F, 1000000.0F);
    else if (key == "remote_actor_mode")
      config.remote_actor_mode = value;
    else if (key == "controller_class")
      config.controller_class = value;
    else if (key == "world_map_controller_class")
      config.world_map_controller_class = value;
    else if (key == "world_character_class")
      config.world_character_class = value;
    else if (key == "world_map_character_class")
      config.world_map_character_class = value;
    else if (key == "pawn_property")
      config.pawn_property = value;
    else if (key == "body_component_property")
      config.body_component_property = value;
    else if (key == "hair_component_property")
      config.hair_component_property = value;
    else if (key == "mesh_asset_property")
      config.mesh_asset_property = value;
    else if (key == "controller_property")
      config.controller_property = value;
    else if (key == "brain_component_property")
      config.brain_component_property = value;
    else if (key == "default_companion_class")
      config.default_companion_class = value;
  }
  return config;
}
} // namespace expedition_online::client
