#include <expedition_online/client/game_bridge.hpp>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <string_view>
#include <utility>
#include <vector>

#include <Windows.h>

#include <Unreal/AActor.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/World.hpp>

namespace expedition_online::client
{
namespace
{
using RC::Unreal::AActor;
using RC::Unreal::FHitResult;
using RC::Unreal::FRotator;
using RC::Unreal::FVector;
using RC::Unreal::UClass;
using RC::Unreal::UObject;
namespace UObjectGlobals = RC::Unreal::UObjectGlobals;

thread_local bool in_bridge_tick{};

auto object_is_valid(UObject* object) -> bool
{
    return object != nullptr && !object->IsUnreachable();
}

auto widen(std::string_view value) -> std::wstring
{
    if (value.empty()) return {};
    const auto count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (count <= 0) return std::wstring(value.begin(), value.end());
    std::wstring result(static_cast<std::size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), count);
    return result;
}

auto narrow(std::wstring_view value) -> std::string
{
    if (value.empty()) return {};
    const auto count = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (count <= 0) return {};
    std::string result(static_cast<std::size_t>(count), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), count, nullptr, nullptr);
    return result;
}

auto object_name(UObject* object) -> std::string
{
    return object_is_valid(object) ? narrow(object->GetFullName()) : std::string{};
}

auto object_leaf_name(UObject* object) -> std::string
{
    return object_is_valid(object) ? narrow(object->GetName()) : std::string{};
}

auto object_property(UObject* object, const std::string& property_name) -> UObject*
{
    if (!object_is_valid(object)) return nullptr;
    const auto wide_name = widen(property_name);
    auto** value = object->GetValuePtrByPropertyNameInChain<UObject*>(wide_name.c_str());
    return value != nullptr && object_is_valid(*value) ? *value : nullptr;
}

auto set_object_property(UObject* object, const std::string& property_name, UObject* value) -> bool
{
    if (!object_is_valid(object) || !object_is_valid(value)) return false;
    const auto wide_name = widen(property_name);
    auto** target = object->GetValuePtrByPropertyNameInChain<UObject*>(wide_name.c_str());
    if (!target) return false;
    *target = value;
    return true;
}

auto call_no_args(UObject* object, const std::string& function_name) -> bool
{
    if (!object_is_valid(object)) return false;
    const auto wide_name = widen(function_name);
    auto* function = object->GetFunctionByNameInChain(wide_name.c_str());
    if (!function) return false;
    object->ProcessEvent(function, nullptr);
    return true;
}

auto normalized_object_path(std::string full_name) -> std::string
{
    const auto space = full_name.find(' ');
    if (space != std::string::npos) full_name.erase(0, space + 1);
    return full_name;
}

auto class_leaf(std::string full_name) -> std::string
{
    full_name = normalized_object_path(std::move(full_name));
    const auto dot = full_name.find_last_of('.');
    if (dot != std::string::npos) return full_name.substr(dot + 1);
    const auto slash = full_name.find_last_of('/');
    return slash == std::string::npos ? full_name : full_name.substr(slash + 1);
}

auto same_appearance(const protocol::AppearanceState& left, const protocol::AppearanceState& right) -> bool
{
    return left.character_class == right.character_class && left.outfit_mesh == right.outfit_mesh && left.hair_mesh == right.hair_mesh;
}

auto resolve_object(const std::string& full_name) -> UObject*
{
    const auto path = widen(normalized_object_path(full_name));
    if (path.empty()) return nullptr;
    return UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, path.c_str());
}

auto is_skeletal_mesh_component(UObject* object) -> bool
{
    if (!object_is_valid(object)) return false;
    auto* object_class = object->GetClassPrivate();
    if (!object_is_valid(object_class)) return false;

    auto* skeletal_mesh_component_class = UObjectGlobals::StaticFindObject<UClass*>(
        nullptr, nullptr, L"/Script/Engine.SkeletalMeshComponent");
    if (object_is_valid(skeletal_mesh_component_class))
    {
        return object->IsA(skeletal_mesh_component_class);
    }

    // The reflected Engine class should always be available. Retain a name
    // check so appearance capture still works on unusual stripped builds.
    return object_name(object_class).find("SkeletalMeshComponent") != std::string::npos;
}

auto is_owned_by(UObject* object, UObject* expected_owner) -> bool
{
    if (!object_is_valid(object) || !object_is_valid(expected_owner)) return false;
    for (auto* outer = object->GetOuterPrivate(); object_is_valid(outer); outer = outer->GetOuterPrivate())
    {
        if (outer == expected_owner) return true;
    }
    return false;
}

auto find_owned_skeletal_component(AActor* owner, const std::string& component_name) -> UObject*
{
    if (!object_is_valid(owner)) return nullptr;
    std::vector<UObject*> components;
    UObjectGlobals::FindAllOf(L"SkeletalMeshComponent", components);
    for (auto* component : components)
    {
        if (is_skeletal_mesh_component(component) && is_owned_by(component, owner) &&
            object_leaf_name(component) == component_name)
        {
            return component;
        }
    }
    return nullptr;
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
} // namespace

GameBridge::GameBridge(const ClientConfig& config, NetworkClient& network, Logger& logger)
    : config_(config), network_(network), logger_(logger)
{
}

GameBridge::~GameBridge()
{
    shutdown();
}

auto GameBridge::tick() -> void
{
    if (shutdown_ || in_bridge_tick) return;
    const auto now = std::chrono::steady_clock::now();
    if (now < next_bridge_tick_) return;
    next_bridge_tick_ = now + std::chrono::milliseconds(16);

    in_bridge_tick = true;
    try
    {
        process_incoming();
        update_local_player();
        update_remote_players();
    }
    catch (const std::exception& exception)
    {
        logger_.error(std::string("GAME_BRIDGE_EXCEPTION ") + exception.what());
    }
    in_bridge_tick = false;
}

auto GameBridge::shutdown() -> void
{
    if (shutdown_) return;
    shutdown_ = true;
    destroy_all_remotes();
}

auto GameBridge::process_incoming() -> void
{
    for (const auto& frame : network_.drain_incoming())
    {
        switch (frame.type)
        {
        case protocol::MessageType::welcome:
        {
            local_player_id_ = protocol::decode_welcome(frame.payload).player_id;
            resync_requested_ = true;
            logger_.info("WELCOME player_id=" + std::to_string(local_player_id_));
            break;
        }
        case protocol::MessageType::zone_state:
        {
            const auto state = protocol::decode_zone_state(frame.payload);
            if (state.player_id == local_player_id_) break;
            auto& remote = remotes_[state.player_id];
            remote.zone = state.zone;
            if (!local_zone_.empty() && state.zone != local_zone_) destroy_remote(state.player_id);
            break;
        }
        case protocol::MessageType::player_joined:
        {
            const auto joined = protocol::decode_player_joined(frame.payload);
            if (joined.player_id != local_player_id_)
            {
                remotes_.try_emplace(joined.player_id);
                logger_.info("REMOTE_PLAYER_JOINED player=" + std::to_string(joined.player_id) + " name=" + joined.player_name);
            }
            break;
        }
        case protocol::MessageType::appearance_state:
        {
            const auto state = protocol::decode_appearance_state(frame.payload);
            if (state.player_id == local_player_id_) break;
            auto& remote = remotes_[state.player_id];
            remote.appearance = state;
            remote.appearance_dirty = true;
            remote.fallback_warning_logged = false;
            break;
        }
        case protocol::MessageType::transform_snapshot:
        {
            const auto state = protocol::decode_transform_snapshot(frame.payload);
            if (state.player_id == local_player_id_) break;
            remotes_[state.player_id].transform = state;
            break;
        }
        case protocol::MessageType::player_left:
            destroy_remote(protocol::decode_player_left(frame.payload).player_id);
            break;
        case protocol::MessageType::error:
        {
            const auto error = protocol::decode_error(frame.payload);
            logger_.error("SERVER_ERROR code=" + std::to_string(error.code) + " message=" + error.message);
            break;
        }
        default:
            break;
        }
    }
}

auto GameBridge::update_local_player() -> void
{
    auto* pawn = find_local_pawn();
    if (!pawn)
    {
        local_visual_pawn_ = nullptr;
        local_body_component_ = nullptr;
        local_hair_component_ = nullptr;
        if (exploration_available_)
        {
            exploration_available_ = false;
            destroy_all_remotes();
            logger_.info("EXPLORATION_UNAVAILABLE Pawn=nil; combat synchronization is intentionally disabled");
        }
        return;
    }

    if (!exploration_available_)
    {
        exploration_available_ = true;
        logger_.info("EXPLORATION_READY pawn=" + object_name(pawn));
    }

    const auto zone = current_zone(pawn);
    if (zone.empty()) return;
    if (zone != local_zone_)
    {
        destroy_all_remotes();
        local_zone_ = zone;
        local_appearance_.reset();
        resync_requested_ = true;
        logger_.info("LOCAL_ZONE " + local_zone_);
    }

    if (network_.connected() && resync_requested_)
    {
        network_.enqueue(protocol::make_frame(protocol::MessageType::zone_state,
                                              network_.next_sequence(),
                                              protocol::ZoneState{local_player_id_, local_zone_}));
    }

    const auto now = std::chrono::steady_clock::now();
    auto appearance = local_appearance_.value_or(protocol::AppearanceState{});
    bool appearance_changed{};
    if (!local_appearance_ || now >= next_appearance_capture_)
    {
        next_appearance_capture_ = now + std::chrono::seconds(1);
        appearance = capture_appearance(pawn);
        appearance_changed = !local_appearance_ || !same_appearance(*local_appearance_, appearance);
        if (appearance_changed) local_appearance_ = appearance;
    }
    if (network_.connected() && (resync_requested_ || appearance_changed))
    {
        network_.enqueue(protocol::make_frame(protocol::MessageType::appearance_state,
                                              network_.next_sequence(),
                                              appearance));
        logger_.info("LOCAL_APPEARANCE character=" + appearance.character_class +
                     " outfit=" + appearance.outfit_mesh + " hair=" + appearance.hair_mesh);
    }
    resync_requested_ = false;

    const auto location = pawn->K2_GetActorLocation();
    const auto rotation = pawn->K2_GetActorRotation();
    if (now >= next_local_transform_log_)
    {
        next_local_transform_log_ = now + std::chrono::seconds(2);
        logger_.info("LOCAL_TRANSFORM x=" + std::to_string(location.X()) +
                     " y=" + std::to_string(location.Y()) +
                     " z=" + std::to_string(location.Z()) +
                     " yaw=" + std::to_string(rotation.GetYaw()));
    }

    if (!network_.connected() || now < next_snapshot_) return;
    next_snapshot_ = now + std::chrono::milliseconds(1000 / config_.snapshot_hz);

    const auto timestamp = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    protocol::TransformSnapshot snapshot;
    snapshot.player_id = local_player_id_;
    snapshot.timestamp_ms = timestamp;
    snapshot.x = location.X();
    snapshot.y = location.Y();
    snapshot.z = location.Z();
    snapshot.pitch = rotation.GetPitch();
    snapshot.yaw = rotation.GetYaw();
    snapshot.roll = rotation.GetRoll();
    network_.enqueue(protocol::make_frame(protocol::MessageType::transform_snapshot,
                                          network_.next_sequence(),
                                          snapshot));
}

auto GameBridge::update_remote_players() -> void
{
    if (!exploration_available_ || local_zone_.empty()) return;
    for (auto& [player_id, remote] : remotes_)
    {
        if (remote.zone != local_zone_ || !remote.transform) continue;
        if (!ensure_remote_actor(player_id, remote)) continue;
        if (remote.appearance_dirty) apply_remote_appearance(remote);
        apply_remote_transform(remote);
    }
}

auto GameBridge::find_local_pawn() -> AActor*
{
    const auto controller_name = widen(config_.controller_class);
    auto* controller = UObjectGlobals::FindFirstOf(controller_name.c_str());
    auto* pawn = object_property(controller, config_.pawn_property);
    return object_is_valid(pawn) ? static_cast<AActor*>(pawn) : nullptr;
}

auto GameBridge::current_zone(AActor* pawn) -> std::string
{
    if (!object_is_valid(pawn)) return {};
    if (auto* level = pawn->GetLevel(); object_is_valid(level)) return narrow(level->GetFullName());
    if (auto* world = pawn->GetWorld(); object_is_valid(world)) return narrow(world->GetFullName());
    return {};
}

auto GameBridge::capture_appearance(AActor* pawn) -> protocol::AppearanceState
{
    if (local_visual_pawn_ != pawn)
    {
        local_visual_pawn_ = pawn;
        local_body_component_ = nullptr;
        local_hair_component_ = nullptr;
    }

    if (!object_is_valid(local_body_component_))
    {
        auto* mesh_property = object_property(pawn, "Mesh");
        local_body_component_ = is_skeletal_mesh_component(mesh_property)
                                    ? mesh_property
                                    : find_owned_skeletal_component(pawn, config_.body_component_property);
    }
    if (!object_is_valid(local_hair_component_))
    {
        auto* hair_property = object_property(pawn, config_.hair_component_property);
        local_hair_component_ = is_skeletal_mesh_component(hair_property)
                                    ? hair_property
                                    : find_owned_skeletal_component(pawn, config_.hair_component_property);
    }

    protocol::AppearanceState appearance;
    appearance.player_id = local_player_id_;
    appearance.outfit_mesh = object_name(object_property(local_body_component_, config_.mesh_asset_property));
    appearance.hair_mesh = object_name(object_property(local_hair_component_, config_.mesh_asset_property));
    appearance.character_class = infer_character_from_body_mesh(appearance.outfit_mesh);
    return appearance;
}

auto GameBridge::ensure_remote_actor(std::uint64_t player_id, RemotePlayer& remote) -> AActor*
{
    if (object_is_valid(remote.actor)) return remote.actor;
    auto* local_pawn = find_local_pawn();
    if (!local_pawn || !remote.transform) return nullptr;

    std::string class_spec = config_.default_companion_class;
    if (remote.appearance)
    {
        const auto character = class_leaf(remote.appearance->character_class);
        bool mapped{};
        if (const auto found = config_.companion_by_character.find(character); found != config_.companion_by_character.end())
        {
            class_spec = found->second;
            mapped = true;
        }
        else
        {
            const auto visual_signature = remote.appearance->character_class + ' ' +
                                          remote.appearance->outfit_mesh + ' ' +
                                          remote.appearance->hair_mesh;
            for (const auto& [token, companion_class] : config_.companion_by_character)
            {
                if (visual_signature.find(token) != std::string::npos)
                {
                    class_spec = companion_class;
                    mapped = true;
                    break;
                }
            }
        }
        if (!mapped && !remote.fallback_warning_logged)
        {
            remote.fallback_warning_logged = true;
            logger_.warning("REMOTE_CHARACTER_UNKNOWN player=" + std::to_string(player_id) +
                            " character=" + character +
                            " fallback=" + config_.default_companion_class);
        }
    }

    UClass* actor_class{};
    if (!class_spec.empty() && class_spec.front() == '/')
    {
        const auto class_path = widen(class_spec);
        actor_class = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, class_path.c_str());
    }
    else
    {
        const auto short_name = widen(class_spec);
        if (auto* template_actor = UObjectGlobals::FindFirstOf(short_name.c_str()); object_is_valid(template_actor))
        {
            actor_class = template_actor->GetClassPrivate();
        }
    }
    if (!actor_class)
    {
        logger_.warning("REMOTE_SPAWN_WAIT player=" + std::to_string(player_id) + " class=" + class_spec);
        return nullptr;
    }

    auto* world = local_pawn->GetWorld();
    if (!object_is_valid(world)) return nullptr;
    const auto& state = *remote.transform;
    FVector location(state.x, state.y, state.z);
    FRotator rotation(state.pitch, state.yaw, state.roll);
    remote.actor = world->SpawnActor(actor_class, &location, &rotation);
    if (!object_is_valid(remote.actor))
    {
        remote.actor = nullptr;
        logger_.warning("REMOTE_SPAWN_FAILED player=" + std::to_string(player_id) + " class=" + class_spec);
        return nullptr;
    }

    disable_remote_ai(remote.actor);
    remote.appearance_dirty = true;
    logger_.info("REMOTE_SPAWNED player=" + std::to_string(player_id) + " actor=" + object_name(remote.actor));
    return remote.actor;
}

auto GameBridge::apply_remote_transform(RemotePlayer& remote) -> void
{
    if (!object_is_valid(remote.actor) || !remote.transform) return;
    const auto& state = *remote.transform;
    FVector location(state.x, state.y, state.z);
    FRotator rotation(state.pitch, state.yaw, state.roll);
    FHitResult hit_result{};
    remote.actor->K2_SetActorLocationAndRotation(location, rotation, false, hit_result, true);
}

auto GameBridge::apply_remote_appearance(RemotePlayer& remote) -> void
{
    if (!object_is_valid(remote.actor) || !remote.appearance) return;
    auto* body_component = object_property(remote.actor, config_.body_component_property);
    auto* hair_component = object_property(remote.actor, config_.hair_component_property);
    bool changed{};
    if (auto* body_mesh = resolve_object(remote.appearance->outfit_mesh))
    {
        changed |= set_object_property(body_component, config_.mesh_asset_property, body_mesh);
        call_no_args(body_component, "MarkRenderStateDirty");
    }
    if (auto* hair_mesh = resolve_object(remote.appearance->hair_mesh))
    {
        changed |= set_object_property(hair_component, config_.mesh_asset_property, hair_mesh);
        call_no_args(hair_component, "MarkRenderStateDirty");
    }
    remote.appearance_dirty = false;
    if (changed) logger_.info("REMOTE_APPEARANCE_APPLIED actor=" + object_name(remote.actor));
}

auto GameBridge::disable_remote_ai(AActor* actor) -> void
{
    if (!object_is_valid(actor)) return;
    actor->SetActorEnableCollision(false);
    actor->SetActorTickEnabled(false);
    auto* controller = object_property(actor, config_.controller_property);
    call_no_args(controller, "StopMovement");
    if (object_is_valid(controller))
    {
        static_cast<AActor*>(controller)->SetActorTickEnabled(false);
        auto* brain = object_property(controller, config_.brain_component_property);
        // Deactivate is the no-argument Blueprint-safe path for stopping the
        // BrainComponent; StopLogic itself requires an FString parameter.
        call_no_args(brain, "Deactivate");
    }
}

auto GameBridge::destroy_remote(std::uint64_t player_id) -> void
{
    const auto found = remotes_.find(player_id);
    if (found == remotes_.end()) return;
    if (object_is_valid(found->second.actor))
    {
        found->second.actor->K2_DestroyActor();
        logger_.info("REMOTE_DESTROYED player=" + std::to_string(player_id));
    }
    remotes_.erase(found);
}

auto GameBridge::destroy_all_remotes() -> void
{
    for (auto& [player_id, remote] : remotes_)
    {
        (void)player_id;
        if (object_is_valid(remote.actor)) remote.actor->K2_DestroyActor();
    }
    remotes_.clear();
}
} // namespace expedition_online::client
