#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>

namespace expedition_online::client {
struct ClientConfig {
  std::string host{"127.0.0.1"};
  std::uint16_t port{7777};
  std::string player_name{"Expeditioner"};
  int reconnect_delay_ms{2000};
  int snapshot_hz{15};
  int interpolation_delay_ms{100};
  int heartbeat_interval_seconds{5};
  int server_timeout_seconds{15};
  bool remote_network_authority{true};
  bool remote_use_movement_input{false};
  bool fallback_ai_companion{true};
  bool vanilla_customization{true};
  bool world_map_remote{true};
  bool sync_locomotion_state{true};
  bool sync_gait{true};
  bool sync_crouch{true};
  bool sync_aim{true};
  bool legacy_visual_diagnostics{false};
  bool rotation_diagnostics{false};
  bool unsafe_direct_appearance{false};
  bool unsafe_direct_hair{false};
  float teleport_threshold_units{5000.0F};

  std::string remote_actor_mode{"world_character"};
  std::string controller_class{"BP_jRPG_Controller_World_C"};
  std::string world_map_controller_class{"BP_PlayerController_WorldMap_C"};
  std::string world_character_class{
      "/Game/jRPGTemplate/Blueprints/Basics/"
      "BP_jRPG_Character_World.BP_jRPG_Character_World_C"};
  std::string world_map_character_class{
      "/Game/Gameplay/WorldMap/"
      "BP_WorldMapCharacter.BP_WorldMapCharacter_C"};
  std::string pawn_property{"Pawn"};
  std::string body_component_property{"Body"};
  std::string hair_component_property{"Haircut_SkeletalMesh"};
  std::string mesh_asset_property{"SkeletalMesh"};
  std::string controller_property{"Controller"};
  std::string brain_component_property{"BrainComponent"};
  std::string default_companion_class{"BP_Pawn_AICompanion_Maelle_C"};
  std::unordered_map<std::string, std::string> companion_by_character;
};

auto load_client_config(const std::filesystem::path &path) -> ClientConfig;
} // namespace expedition_online::client
