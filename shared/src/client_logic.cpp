#include <expedition_online/client_logic.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string_view>
#include <utility>

namespace expedition_online::client_logic
{
namespace
{
auto lerp(float from, float to, float alpha) -> float
{
    return from + (to - from) * alpha;
}

auto normalized_degrees(float value) -> float
{
    value = std::fmod(value + 180.0F, 360.0F);
    if (value < 0.0F) value += 360.0F;
    return value - 180.0F;
}

auto contains(const std::string& value, std::string_view token) -> bool
{
    return value.find(token) != std::string::npos;
}
} // namespace

auto insert_snapshot(std::deque<protocol::TransformSnapshot>& buffer,
                     const protocol::TransformSnapshot& snapshot,
                     std::size_t maximum_size) -> void
{
    if (maximum_size == 0)
    {
        buffer.clear();
        return;
    }

    const auto position = std::lower_bound(
        buffer.begin(), buffer.end(), snapshot.timestamp_ms,
        [](const protocol::TransformSnapshot& value, std::uint64_t timestamp) {
            return value.timestamp_ms < timestamp;
        });
    if (position != buffer.end() && position->timestamp_ms == snapshot.timestamp_ms)
    {
        *position = snapshot;
    }
    else
    {
        buffer.insert(position, snapshot);
    }
    while (buffer.size() > maximum_size) buffer.pop_front();
}

auto lerp_angle_degrees(float from, float to, float alpha) -> float
{
    const auto shortest_delta = normalized_degrees(to - from);
    return normalized_degrees(from + shortest_delta * std::clamp(alpha, 0.0F, 1.0F));
}

auto sample_snapshot(const std::deque<protocol::TransformSnapshot>& buffer,
                     std::uint64_t render_timestamp_ms) -> std::optional<InterpolationSample>
{
    if (buffer.empty()) return std::nullopt;
    if (buffer.size() == 1 || render_timestamp_ms <= buffer.front().timestamp_ms)
    {
        return InterpolationSample{buffer.front(), 0.0F, false};
    }
    if (render_timestamp_ms >= buffer.back().timestamp_ms)
    {
        return InterpolationSample{buffer.back(), 1.0F, false};
    }

    for (std::size_t index = 1; index < buffer.size(); ++index)
    {
        const auto& before = buffer[index - 1];
        const auto& after = buffer[index];
        if (render_timestamp_ms > after.timestamp_ms) continue;
        const auto span = after.timestamp_ms - before.timestamp_ms;
        if (span == 0) return InterpolationSample{after, 1.0F, false};
        const auto alpha = std::clamp(
            static_cast<float>(render_timestamp_ms - before.timestamp_ms) / static_cast<float>(span),
            0.0F, 1.0F);
        auto result = before;
        result.timestamp_ms = render_timestamp_ms;
        result.x = lerp(before.x, after.x, alpha);
        result.y = lerp(before.y, after.y, alpha);
        result.z = lerp(before.z, after.z, alpha);
        result.pitch = lerp_angle_degrees(before.pitch, after.pitch, alpha);
        result.yaw = lerp_angle_degrees(before.yaw, after.yaw, alpha);
        result.roll = lerp_angle_degrees(before.roll, after.roll, alpha);
        return InterpolationSample{result, alpha, true};
    }
    return InterpolationSample{buffer.back(), 1.0F, false};
}

auto snapshot_stream_is_stale(std::uint64_t now_ms,
                              std::uint64_t last_received_ms,
                              std::uint64_t stale_after_ms) -> bool
{
    return now_ms >= last_received_ms && now_ms - last_received_ms > stale_after_ms;
}

auto infer_character_from_body_mesh(const std::string& mesh_path) -> std::string
{
    std::string folded = mesh_path;
    std::transform(folded.begin(), folded.end(), folded.begin(),
                   [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    constexpr std::pair<std::string_view, std::string_view> characters[]{
        {"/maelle/", "Maelle"},
        {"/lune/", "Lune"},
        {"/sciel/", "Sciel"},
        {"/verso/", "Verso"},
        {"/gustave/", "Gustave"},
        {"/monoco/", "Monoco"},
    };
    for (const auto& [token, character] : characters)
    {
        if (folded.find(token) != std::string::npos) return std::string(character);
    }
    return "Unknown";
}

auto appearance_is_ready(const protocol::AppearanceState& appearance) -> bool
{
    return !appearance.outfit_mesh.empty() && appearance.character_class != "Unknown" &&
           !appearance.character_class.empty();
}

auto select_effective_appearance(const std::optional<protocol::AppearanceState>& last_valid,
                                 const protocol::AppearanceState& candidate)
    -> std::optional<protocol::AppearanceState>
{
    if (appearance_is_ready(candidate)) return candidate;
    return last_valid;
}

auto appearance_retry_delay_ms(std::size_t consecutive_failures) -> int
{
    if (consecutive_failures <= 2) return 500;
    if (consecutive_failures <= 5) return 1000;
    return 2000;
}

auto score_appearance_candidate(const AppearanceCandidate& candidate) -> int
{
    if (candidate.component_leaf != "Body" || candidate.mesh_full_name.empty()) return -1;
    if (contains(candidate.mesh_full_name, "SKM_Quinn") || contains(candidate.mesh_full_name, "None.None")) return -1;
    if (!contains(candidate.mesh_full_name, "/Game/Characters/Heros/")) return -1;
    if (infer_character_from_body_mesh(candidate.mesh_full_name) == "Unknown") return -1;
    if (!candidate.directly_owned_by_pawn &&
        !(candidate.owner_is_character_skin && candidate.related_to_pawn))
    {
        return -1;
    }

    int score{170}; // Hero asset + exact Body leaf + recognized character.
    if (candidate.directly_owned_by_pawn) score += 30;
    if (candidate.owner_is_character_skin && candidate.related_to_pawn) score += 40;
    return score;
}

auto select_appearance_candidate(const std::vector<AppearanceCandidate>& candidates)
    -> std::optional<std::size_t>
{
    std::optional<std::size_t> best_index;
    int best_score{-1};
    for (std::size_t index = 0; index < candidates.size(); ++index)
    {
        const auto score = score_appearance_candidate(candidates[index]);
        if (score >= 0 && score >= best_score)
        {
            best_index = index;
            best_score = score;
        }
    }
    return best_index;
}
} // namespace expedition_online::client_logic
