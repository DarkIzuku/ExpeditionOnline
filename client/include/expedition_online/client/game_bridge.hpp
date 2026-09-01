#pragma once

#include <expedition_online/client/config.hpp>
#include <expedition_online/client/logger.hpp>
#include <expedition_online/client/network_client.hpp>
#include <expedition_online/protocol.hpp>

#include <chrono>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <unordered_map>

namespace RC::Unreal
{
class AActor;
class UObject;
}

namespace expedition_online::client
{
class GameBridge
{
  public:
    GameBridge(const ClientConfig& config, NetworkClient& network, Logger& logger);
    ~GameBridge();
    GameBridge(const GameBridge&) = delete;
    auto operator=(const GameBridge&) -> GameBridge& = delete;

    auto tick() -> void;
    auto shutdown() -> void;

  private:
    struct RemotePlayer
    {
        std::string zone;
        std::optional<protocol::AppearanceState> appearance;
        std::deque<protocol::TransformSnapshot> snapshots;
        std::optional<protocol::TransformSnapshot> last_rendered_transform;
        RC::Unreal::AActor* actor{};
        RC::Unreal::UObject* movement_component{};
        float velocity_x{};
        float velocity_y{};
        float velocity_z{};
        float speed{};
        bool appearance_dirty{};
        bool fallback_warning_logged{};
        bool movement_warning_logged{};
        bool character_respawn_pending{};
        bool clock_offset_initialized{};
        double clock_offset_ms{};
        std::uint64_t last_transform_received_ms{};
        std::chrono::steady_clock::time_point next_motion_log{};
        std::chrono::steady_clock::time_point next_interpolation_log{};
    };

    auto process_incoming() -> void;
    auto update_local_player() -> void;
    auto update_remote_players() -> void;
    auto find_local_pawn() -> RC::Unreal::AActor*;
    auto current_zone(RC::Unreal::AActor* pawn) -> std::string;
    auto capture_appearance(RC::Unreal::AActor* pawn) -> protocol::AppearanceState;
    auto ensure_remote_actor(std::uint64_t player_id, RemotePlayer& remote) -> RC::Unreal::AActor*;
    auto apply_remote_transform(std::uint64_t player_id, RemotePlayer& remote) -> void;
    auto apply_remote_appearance(std::uint64_t player_id, RemotePlayer& remote) -> void;
    auto disable_remote_ai(RC::Unreal::AActor* actor) -> void;
    auto destroy_remote(std::uint64_t player_id) -> void;
    auto destroy_all_remotes() -> void;

    const ClientConfig& config_;
    NetworkClient& network_;
    Logger& logger_;
    std::unordered_map<std::uint64_t, RemotePlayer> remotes_;
    std::uint64_t local_player_id_{};
    std::string local_zone_;
    std::optional<protocol::AppearanceState> local_appearance_;
    RC::Unreal::AActor* local_visual_pawn_{};
    RC::Unreal::UObject* local_body_component_{};
    RC::Unreal::UObject* local_hair_component_{};
    std::string last_body_component_log_;
    std::string last_body_mesh_log_;
    bool body_diagnostic_initialized_{};
    std::size_t appearance_failure_count_{};
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
};
} // namespace expedition_online::client
