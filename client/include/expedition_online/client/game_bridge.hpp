#pragma once

#include <expedition_online/client/config.hpp>
#include <expedition_online/client/logger.hpp>
#include <expedition_online/client/network_client.hpp>
#include <expedition_online/protocol.hpp>

#include <chrono>
#include <cstdint>
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
        std::optional<protocol::TransformSnapshot> transform;
        RC::Unreal::AActor* actor{};
        bool appearance_dirty{};
    };

    auto process_incoming() -> void;
    auto update_local_player() -> void;
    auto update_remote_players() -> void;
    auto find_local_pawn() -> RC::Unreal::AActor*;
    auto current_zone(RC::Unreal::AActor* pawn) -> std::string;
    auto capture_appearance(RC::Unreal::AActor* pawn) -> protocol::AppearanceState;
    auto ensure_remote_actor(std::uint64_t player_id, RemotePlayer& remote) -> RC::Unreal::AActor*;
    auto apply_remote_transform(RemotePlayer& remote) -> void;
    auto apply_remote_appearance(RemotePlayer& remote) -> void;
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
    bool resync_requested_{};
    bool exploration_available_{};
    bool shutdown_{};
    std::chrono::steady_clock::time_point next_bridge_tick_{};
    std::chrono::steady_clock::time_point next_snapshot_{};
};
} // namespace expedition_online::client
