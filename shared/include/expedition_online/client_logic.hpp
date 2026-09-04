#pragma once

#include <expedition_online/protocol.hpp>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <vector>

namespace expedition_online::client_logic {
inline constexpr std::size_t kDefaultSnapshotBufferSize{24};

struct InterpolationSample {
  protocol::TransformSnapshot transform;
  float alpha{};
  bool interpolated{};
};

auto insert_snapshot(std::deque<protocol::TransformSnapshot> &buffer,
                     const protocol::TransformSnapshot &snapshot,
                     std::size_t maximum_size = kDefaultSnapshotBufferSize)
    -> void;
auto sample_snapshot(const std::deque<protocol::TransformSnapshot> &buffer,
                     std::uint64_t render_timestamp_ms)
    -> std::optional<InterpolationSample>;
auto lerp_angle_degrees(float from, float to, float alpha) -> float;
auto snapshot_stream_is_stale(std::uint64_t now_ms,
                              std::uint64_t last_received_ms,
                              std::uint64_t stale_after_ms = 750) -> bool;
auto snapshot_exceeds_teleport_threshold(
    const protocol::TransformSnapshot &from,
    const protocol::TransformSnapshot &to, float threshold_units) -> bool;
auto context_requires_actor_reset(protocol::PlayerContext previous,
                                  protocol::PlayerContext next) -> bool;
auto context_supports_remote_actor(protocol::PlayerContext context) -> bool;

auto infer_character_from_body_mesh(const std::string &mesh_path)
    -> std::string;
auto appearance_is_ready(const protocol::AppearanceState &appearance) -> bool;
auto select_effective_appearance(
    const std::optional<protocol::AppearanceState> &last_valid,
    const protocol::AppearanceState &candidate)
    -> std::optional<protocol::AppearanceState>;
auto appearance_retry_delay_ms(std::size_t consecutive_failures) -> int;

enum class AssetResolutionSource {
  cache,
  already_loaded,
  loaded,
  failed,
};

auto asset_resolution_source(bool cached_valid, bool already_loaded_valid,
                             bool loaded_valid) -> AssetResolutionSource;
auto appearance_asset_has_drift(const std::string &expected,
                                const std::string &observed) -> bool;
auto should_reapply_visual_asset(const std::string &expected,
                                 const std::string &observed,
                                 bool component_ready) -> bool;
auto should_write_remote_visual(bool unsafe_write_enabled, bool asset_ready,
                                bool component_ready) -> bool;
auto should_disable_remote_movement_tick(bool network_authority_enabled)
    -> bool;
auto is_customization_skin_mesh(const std::string &mesh_path,
                                std::string_view expected_character = {})
    -> bool;

enum class AppearanceApplyDecision {
  complete,
  retry,
  fail_open,
};

auto appearance_apply_decision(bool body_requested, bool body_ready,
                               bool hair_requested, bool hair_ready,
                               std::size_t attempt,
                               std::size_t maximum_attempts = 10)
    -> AppearanceApplyDecision;

enum class VerticalMovementPhase {
  ground,
  ascend,
  apex,
  descend,
};

auto classify_vertical_movement(bool is_falling, float velocity_z,
                                float apex_threshold = 50.0F)
    -> VerticalMovementPhase;
auto vertical_movement_phase_name(VerticalMovementPhase phase)
    -> std::string_view;
auto jump_demo_movement_mode(std::string_view jump_phase) -> std::uint8_t;

struct AppearanceCandidate {
  std::string component_leaf;
  std::string component_full_name;
  std::string owner_full_name;
  std::string mesh_full_name;
  bool directly_owned_by_pawn{};
  bool owner_is_character_skin{};
  bool related_to_pawn{};
  bool selected_by_mesh_property{};
};

// Negative means the candidate is not safe to publish as the current Body.
auto score_appearance_candidate(const AppearanceCandidate &candidate) -> int;
auto select_appearance_candidate(
    const std::vector<AppearanceCandidate> &candidates)
    -> std::optional<std::size_t>;
} // namespace expedition_online::client_logic
