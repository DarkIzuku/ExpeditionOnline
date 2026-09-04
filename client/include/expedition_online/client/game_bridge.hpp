#pragma once

#include <expedition_online/client/config.hpp>
#include <expedition_online/client/logger.hpp>
#include <expedition_online/client/network_client.hpp>
#include <expedition_online/protocol.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <unordered_map>

namespace RC::Unreal {
class AActor;
class UObject;
class UFunction;
} // namespace RC::Unreal

namespace expedition_online::client {
class GameBridge {
public:
  GameBridge(const ClientConfig &config, NetworkClient &network,
             Logger &logger);
  ~GameBridge();
  GameBridge(const GameBridge &) = delete;
  auto operator=(const GameBridge &) -> GameBridge & = delete;

  auto tick() -> void;
  auto observe_process_event(RC::Unreal::UObject *object,
                             RC::Unreal::UFunction *function) -> void;
  auto shutdown() -> void;

private:
  struct RemotePlayer {
    std::string zone;
    protocol::PlayerContext context{protocol::PlayerContext::unavailable};
    std::optional<protocol::AppearanceState> appearance;
    std::optional<protocol::MovementState> movement_state;
    std::optional<protocol::PlayerLocomotionState> locomotion_state;
    std::deque<protocol::JumpEvent> jump_events;
    std::deque<protocol::TransformSnapshot> snapshots;
    std::optional<protocol::TransformSnapshot> last_rendered_transform;
    RC::Unreal::AActor *actor{};
    RC::Unreal::UObject *movement_component{};
    RC::Unreal::UObject *body_component{};
    RC::Unreal::UObject *hair_component{};
    RC::Unreal::UObject *expected_body_mesh{};
    RC::Unreal::UObject *expected_hair_mesh{};
    RC::Unreal::UObject *locomotion_anim_instance{};
    RC::Unreal::UObject *skin_component{};
    std::string body_route;
    std::string hair_route;
    std::string spawned_character;
    std::string backend;
    std::unordered_map<std::string, std::string> visual_mesh_snapshot;
    float velocity_x{};
    float velocity_y{};
    float velocity_z{};
    float speed{};
    bool appearance_dirty{};
    bool fallback_warning_logged{};
    bool movement_warning_logged{};
    bool character_respawn_pending{};
    bool skeletal_diagnostic_logged{};
    bool visual_snapshot_initialized{};
    bool movement_state_dirty{};
    bool locomotion_state_dirty{};
    bool locomotion_warning_logged{};
    bool hair_verification_pending{};
    bool body_verification_pending{};
    bool clock_offset_initialized{};
    bool network_authority_configured{};
    bool transform_drift_initialized{};
    bool jump_target_warning_logged{};
    std::uint64_t last_jump_sequence_received{};
    std::uint64_t last_jump_sequence_applied{};
    float last_network_x{};
    float last_network_y{};
    float last_network_z{};
    float last_before_x{};
    float last_before_y{};
    float last_before_z{};
    float last_after_x{};
    float last_after_y{};
    float last_after_z{};
    std::size_t appearance_attempt_count{};
    double clock_offset_ms{};
    std::uint64_t last_transform_received_ms{};
    std::chrono::steady_clock::time_point next_motion_log{};
    std::chrono::steady_clock::time_point next_interpolation_log{};
    std::chrono::steady_clock::time_point next_appearance_retry{};
    std::chrono::steady_clock::time_point next_visual_verification{};
    std::chrono::steady_clock::time_point next_transform_drift_log{};
    std::chrono::steady_clock::time_point next_movement_component_state_log{};
  };

  auto process_incoming() -> void;
  auto update_local_player() -> void;
  auto update_remote_players() -> void;
  auto find_local_pawn() -> RC::Unreal::AActor *;
  auto current_zone(RC::Unreal::AActor *pawn) -> std::string;
  auto capture_appearance(RC::Unreal::AActor *pawn)
      -> protocol::AppearanceState;
  auto ensure_remote_actor(std::uint64_t player_id, RemotePlayer &remote)
      -> RC::Unreal::AActor *;
  auto apply_remote_transform(std::uint64_t player_id, RemotePlayer &remote)
      -> void;
  auto apply_remote_appearance(std::uint64_t player_id, RemotePlayer &remote)
      -> void;
  auto apply_remote_movement_state(std::uint64_t player_id,
                                   RemotePlayer &remote) -> void;
  auto apply_remote_locomotion_state(std::uint64_t player_id,
                                     RemotePlayer &remote) -> void;
  auto apply_remote_jump_events(std::uint64_t player_id, RemotePlayer &remote)
      -> void;
  auto configure_remote_network_authority(std::uint64_t player_id,
                                          RemotePlayer &remote) -> void;
  auto verify_remote_visual_state(std::uint64_t player_id, RemotePlayer &remote)
      -> void;
  auto update_local_jump_diagnostics(RC::Unreal::AActor *pawn) -> void;
  auto disable_remote_ai(RC::Unreal::AActor *actor) -> void;
  auto destroy_remote_actor(std::uint64_t player_id, RemotePlayer &remote,
                            bool reset_interpolation) -> void;
  auto destroy_remote(std::uint64_t player_id) -> void;
  auto destroy_all_remotes() -> void;

  const ClientConfig &config_;
  NetworkClient &network_;
  Logger &logger_;
  std::unordered_map<std::uint64_t, RemotePlayer> remotes_;
  std::uint64_t local_player_id_{};
  std::string local_zone_;
  std::optional<protocol::AppearanceState> local_appearance_;
  std::optional<protocol::MovementState> local_movement_state_;
  std::optional<protocol::PlayerLocomotionState> local_locomotion_state_;
  protocol::PlayerContext detected_context_{
      protocol::PlayerContext::unavailable};
  protocol::PlayerContext local_context_{protocol::PlayerContext::unavailable};
  std::optional<protocol::PlayerContext> last_sent_context_;
  RC::Unreal::AActor *local_visual_pawn_{};
  RC::Unreal::UObject *local_body_component_{};
  RC::Unreal::UObject *local_hair_component_{};
  RC::Unreal::UObject *local_movement_component_{};
  std::string last_body_component_log_;
  std::string last_body_mesh_log_;
  std::string last_body_source_log_;
  bool body_diagnostic_initialized_{};
  bool local_visual_route_diagnostic_logged_{};
  bool local_movement_state_initialized_{};
  std::string last_local_movement_signature_;
  std::unordered_map<std::string, std::string> local_jump_signals_;
  std::unordered_map<std::string, std::chrono::steady_clock::time_point>
      local_jump_event_times_;
  std::unordered_map<RC::Unreal::UObject *, std::string> local_jump_objects_;
  std::unordered_map<RC::Unreal::UObject *, std::string> local_skin_objects_;
  std::unordered_map<std::string, std::string> local_skin_signals_;
  std::unordered_map<std::string, RC::Unreal::UObject *> asset_cache_;
  std::size_t appearance_failure_count_{};
  std::uint64_t local_jump_sequence_{};
  std::chrono::steady_clock::time_point last_local_jump_started_{};
  bool resync_requested_{};
  bool exploration_available_{};
  bool network_was_connected_{};
  bool shutdown_{};
  std::chrono::steady_clock::time_point next_bridge_tick_{};
  std::chrono::steady_clock::time_point next_appearance_capture_{};
  std::chrono::steady_clock::time_point next_appearance_pending_log_{};
  std::chrono::steady_clock::time_point next_appearance_scan_log_{};
  std::chrono::steady_clock::time_point next_snapshot_{};
  std::chrono::steady_clock::time_point next_local_transform_log_{};
  std::chrono::steady_clock::time_point next_local_jump_scan_{};
};
} // namespace expedition_online::client
