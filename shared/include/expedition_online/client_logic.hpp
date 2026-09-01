#pragma once

#include <expedition_online/protocol.hpp>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <vector>

namespace expedition_online::client_logic
{
inline constexpr std::size_t kDefaultSnapshotBufferSize{24};

struct InterpolationSample
{
    protocol::TransformSnapshot transform;
    float alpha{};
    bool interpolated{};
};

auto insert_snapshot(std::deque<protocol::TransformSnapshot>& buffer,
                     const protocol::TransformSnapshot& snapshot,
                     std::size_t maximum_size = kDefaultSnapshotBufferSize) -> void;
auto sample_snapshot(const std::deque<protocol::TransformSnapshot>& buffer,
                     std::uint64_t render_timestamp_ms) -> std::optional<InterpolationSample>;
auto lerp_angle_degrees(float from, float to, float alpha) -> float;
auto snapshot_stream_is_stale(std::uint64_t now_ms,
                              std::uint64_t last_received_ms,
                              std::uint64_t stale_after_ms = 750) -> bool;

auto infer_character_from_body_mesh(const std::string& mesh_path) -> std::string;
auto appearance_is_ready(const protocol::AppearanceState& appearance) -> bool;
auto select_effective_appearance(const std::optional<protocol::AppearanceState>& last_valid,
                                 const protocol::AppearanceState& candidate)
    -> std::optional<protocol::AppearanceState>;
auto appearance_retry_delay_ms(std::size_t consecutive_failures) -> int;

struct AppearanceCandidate
{
    std::string component_leaf;
    std::string component_full_name;
    std::string owner_full_name;
    std::string mesh_full_name;
    bool directly_owned_by_pawn{};
    bool owner_is_character_skin{};
    bool related_to_pawn{};
};

// Negative means the candidate is not safe to publish as the current Body.
auto score_appearance_candidate(const AppearanceCandidate& candidate) -> int;
auto select_appearance_candidate(const std::vector<AppearanceCandidate>& candidates)
    -> std::optional<std::size_t>;
} // namespace expedition_online::client_logic
