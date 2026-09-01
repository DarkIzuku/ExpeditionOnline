#include <expedition_online/client/game_bridge.hpp>
#include <expedition_online/client_logic.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <string_view>
#include <utility>
#include <vector>

#include <Windows.h>

#include <Unreal/AActor.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/World.hpp>

namespace expedition_online::client {
namespace {
using RC::Unreal::AActor;
using RC::Unreal::FHitResult;
using RC::Unreal::FRotator;
using RC::Unreal::FVector;
using RC::Unreal::TArray;
using RC::Unreal::UClass;
using RC::Unreal::UObject;
namespace UObjectGlobals = RC::Unreal::UObjectGlobals;
namespace logic = expedition_online::client_logic;

thread_local bool in_bridge_tick{};

auto object_is_valid(UObject *object) -> bool {
  return object != nullptr && !object->IsUnreachable();
}

auto widen(std::string_view value) -> std::wstring {
  if (value.empty())
    return {};
  const auto count =
      MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                          static_cast<int>(value.size()), nullptr, 0);
  if (count <= 0)
    return std::wstring(value.begin(), value.end());
  std::wstring result(static_cast<std::size_t>(count), L'\0');
  MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                      static_cast<int>(value.size()), result.data(), count);
  return result;
}

auto narrow(std::wstring_view value) -> std::string {
  if (value.empty())
    return {};
  const auto count = WideCharToMultiByte(CP_UTF8, 0, value.data(),
                                         static_cast<int>(value.size()),
                                         nullptr, 0, nullptr, nullptr);
  if (count <= 0)
    return {};
  std::string result(static_cast<std::size_t>(count), '\0');
  WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                      result.data(), count, nullptr, nullptr);
  return result;
}

auto object_name(UObject *object) -> std::string {
  return object_is_valid(object) ? narrow(object->GetFullName())
                                 : std::string{};
}

auto object_leaf_name(UObject *object) -> std::string {
  return object_is_valid(object) ? narrow(object->GetName()) : std::string{};
}

auto wall_clock_ms() -> std::uint64_t {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count());
}

auto object_property(UObject *object, const std::string &property_name)
    -> UObject * {
  if (!object_is_valid(object))
    return nullptr;
  const auto wide_name = widen(property_name);
  auto **value =
      object->GetValuePtrByPropertyNameInChain<UObject *>(wide_name.c_str());
  return value != nullptr && object_is_valid(*value) ? *value : nullptr;
}

auto vector_property(UObject *object, const std::string &property_name)
    -> FVector * {
  if (!object_is_valid(object))
    return nullptr;
  const auto wide_name = widen(property_name);
  return object->GetValuePtrByPropertyNameInChain<FVector>(wide_name.c_str());
}

auto byte_property(UObject *object, const std::string &property_name)
    -> std::uint8_t * {
  if (!object_is_valid(object))
    return nullptr;
  const auto wide_name = widen(property_name);
  return object->GetValuePtrByPropertyNameInChain<std::uint8_t>(
      wide_name.c_str());
}

auto set_object_property(UObject *object, const std::string &property_name,
                         UObject *value) -> bool {
  if (!object_is_valid(object) || !object_is_valid(value))
    return false;
  const auto wide_name = widen(property_name);
  auto **target =
      object->GetValuePtrByPropertyNameInChain<UObject *>(wide_name.c_str());
  if (!target)
    return false;
  *target = value;
  return true;
}

auto call_no_args(UObject *object, const std::string &function_name) -> bool {
  if (!object_is_valid(object))
    return false;
  const auto wide_name = widen(function_name);
  auto *function = object->GetFunctionByNameInChain(wide_name.c_str());
  if (!function)
    return false;
  object->ProcessEvent(function, nullptr);
  return true;
}

auto call_object_return(UObject *object, const std::string &function_name)
    -> UObject * {
  if (!object_is_valid(object))
    return nullptr;
  const auto wide_name = widen(function_name);
  auto *function = object->GetFunctionByNameInChain(wide_name.c_str());
  if (!function)
    return nullptr;
  struct Params {
    UObject *ReturnValue{};
  } params;
  object->ProcessEvent(function, &params);
  return object_is_valid(params.ReturnValue) ? params.ReturnValue : nullptr;
}

auto call_vector_return(UObject *object, const std::string &function_name,
                        FVector &result) -> bool {
  if (!object_is_valid(object))
    return false;
  const auto wide_name = widen(function_name);
  auto *function = object->GetFunctionByNameInChain(wide_name.c_str());
  if (!function)
    return false;
  struct Params {
    FVector ReturnValue{};
  } params;
  object->ProcessEvent(function, &params);
  result = params.ReturnValue;
  return true;
}

auto call_bool_return(UObject *object, const std::string &function_name,
                      bool &result) -> bool {
  if (!object_is_valid(object))
    return false;
  const auto wide_name = widen(function_name);
  auto *function = object->GetFunctionByNameInChain(wide_name.c_str());
  if (!function)
    return false;
  struct Params {
    bool ReturnValue{};
  } params;
  object->ProcessEvent(function, &params);
  result = params.ReturnValue;
  return true;
}

auto normalized_object_path(std::string full_name) -> std::string {
  const auto space = full_name.find(' ');
  if (space != std::string::npos)
    full_name.erase(0, space + 1);
  return full_name;
}

auto class_leaf(std::string full_name) -> std::string {
  full_name = normalized_object_path(std::move(full_name));
  const auto dot = full_name.find_last_of('.');
  if (dot != std::string::npos)
    return full_name.substr(dot + 1);
  const auto slash = full_name.find_last_of('/');
  return slash == std::string::npos ? full_name : full_name.substr(slash + 1);
}

auto same_appearance(const protocol::AppearanceState &left,
                     const protocol::AppearanceState &right) -> bool {
  return left.character_class == right.character_class &&
         left.outfit_mesh == right.outfit_mesh &&
         left.hair_mesh == right.hair_mesh;
}

auto resolve_object(const std::string &full_name) -> UObject * {
  const auto path = widen(normalized_object_path(full_name));
  if (path.empty())
    return nullptr;
  return UObjectGlobals::StaticFindObject<UObject *>(nullptr, nullptr,
                                                     path.c_str());
}

auto is_skeletal_mesh_component(UObject *object) -> bool {
  if (!object_is_valid(object))
    return false;
  auto *object_class = object->GetClassPrivate();
  if (!object_is_valid(object_class))
    return false;

  auto *skeletal_mesh_component_class =
      UObjectGlobals::StaticFindObject<UClass *>(
          nullptr, nullptr, L"/Script/Engine.SkeletalMeshComponent");
  if (object_is_valid(skeletal_mesh_component_class)) {
    return object->IsA(skeletal_mesh_component_class);
  }

  // The reflected Engine class should always be available. Retain a name
  // check so appearance capture still works on unusual stripped builds.
  return object_name(object_class).find("SkeletalMeshComponent") !=
         std::string::npos;
}

auto is_instance_of(UObject *object, const wchar_t *class_path,
                    std::string_view class_name_fallback) -> bool {
  if (!object_is_valid(object))
    return false;
  auto *expected_class =
      UObjectGlobals::StaticFindObject<UClass *>(nullptr, nullptr, class_path);
  if (object_is_valid(expected_class))
    return object->IsA(expected_class);
  auto *object_class = object->GetClassPrivate();
  return object_is_valid(object_class) &&
         object_name(object_class).find(class_name_fallback) !=
             std::string::npos;
}

auto find_remote_movement_component(AActor *owner) -> UObject * {
  if (!object_is_valid(owner))
    return nullptr;
  auto *component = object_property(owner, "CharacterMovement");
  if (is_instance_of(component, L"/Script/Engine.CharacterMovementComponent",
                     "CharacterMovementComponent"))
    return component;

  component = object_property(owner, "MovementComponent");
  if (is_instance_of(component, L"/Script/Engine.MovementComponent",
                     "MovementComponent"))
    return component;

  auto *movement_component_class = UObjectGlobals::StaticFindObject<UClass *>(
      nullptr, nullptr, L"/Script/Engine.CharacterMovementComponent");
  if (!object_is_valid(movement_component_class))
    return nullptr;
  const auto &components =
      owner->K2_GetComponentsByClass(movement_component_class);
  for (auto *candidate : components) {
    if (is_instance_of(candidate, L"/Script/Engine.CharacterMovementComponent",
                       "CharacterMovementComponent")) {
      return candidate;
    }
  }
  return nullptr;
}

auto horizontal_speed(float x, float y, float z) -> float {
  return std::sqrt(x * x + y * y + z * z);
}

auto find_owned_skeletal_component(AActor *owner,
                                   const std::string &component_name)
    -> UObject * {
  if (!object_is_valid(owner))
    return nullptr;
  auto *skeletal_mesh_component_class =
      UObjectGlobals::StaticFindObject<UClass *>(
          nullptr, nullptr, L"/Script/Engine.SkeletalMeshComponent");
  if (!object_is_valid(skeletal_mesh_component_class))
    return nullptr;
  const auto &components =
      owner->K2_GetComponentsByClass(skeletal_mesh_component_class);
  for (auto *component : components) {
    if (is_skeletal_mesh_component(component) &&
        object_leaf_name(component) == component_name) {
      return component;
    }
  }
  return nullptr;
}

auto is_actor(UObject *object) -> bool {
  return is_instance_of(object, L"/Script/Engine.Actor", "Actor");
}

auto is_character_skin_actor(AActor *actor) -> bool {
  if (!object_is_valid(actor))
    return false;
  return object_name(actor).find("BP_CharacterSkin_") != std::string::npos ||
         object_name(actor->GetClassPrivate()).find("BP_CharacterSkin_") !=
             std::string::npos;
}

struct ReachableActor {
  AActor *actor{};
  std::string route;
};

auto add_reachable_actor(std::vector<ReachableActor> &actors, UObject *object,
                         std::string route) -> void {
  if (!object_is_valid(object) || !is_actor(object))
    return;
  auto *actor = static_cast<AActor *>(object);
  const auto duplicate = std::find_if(
      actors.begin(), actors.end(),
      [actor](const ReachableActor &value) { return value.actor == actor; });
  if (duplicate == actors.end())
    actors.push_back(ReachableActor{actor, std::move(route)});
}

auto append_actor_array_function(AActor *owner,
                                 const std::string &function_name,
                                 std::vector<ReachableActor> &actors,
                                 const std::string &route) -> void {
  if (!object_is_valid(owner))
    return;
  const auto wide_name = widen(function_name);
  auto *function = owner->GetFunctionByNameInChain(wide_name.c_str());
  if (!function)
    return;

  if (function_name == "GetAttachedActors") {
    struct Params {
      TArray<AActor *> OutActors{};
      bool bResetArray{true};
      bool bRecursivelyIncludeAttachedActors{true};
    } params;
    owner->ProcessEvent(function, &params);
    for (auto *actor : params.OutActors)
      add_reachable_actor(actors, actor, route);
  } else {
    struct Params {
      TArray<AActor *> ChildActors{};
      bool bIncludeDescendants{true};
    } params;
    owner->ProcessEvent(function, &params);
    for (auto *actor : params.ChildActors)
      add_reachable_actor(actors, actor, route);
  }
}

constexpr std::string_view kVisualActorProperties[]{
    "CharacterSkinActor",
    "CurrentCharacterSkinActor",
    "ActiveCharacterSkinActor",
    "CharacterSkin",
    "CurrentCharacterSkin",
    "ActiveCharacterSkin",
    "SkinActor",
    "VisualActor",
    "CharacterVisualActor",
};

auto collect_reachable_visual_actors(AActor *root)
    -> std::vector<ReachableActor> {
  std::vector<ReachableActor> actors;
  if (!object_is_valid(root))
    return actors;
  add_reachable_actor(actors, root, "pawn");

  for (const auto property_name : kVisualActorProperties) {
    if (auto *value = object_property(root, std::string(property_name));
        object_is_valid(value)) {
      add_reachable_actor(actors, value,
                          "pawn_property:" + std::string(property_name));
    }
  }

  auto *actor_component_class = UObjectGlobals::StaticFindObject<UClass *>(
      nullptr, nullptr, L"/Script/Engine.ActorComponent");
  if (object_is_valid(actor_component_class)) {
    const auto &components =
        root->K2_GetComponentsByClass(actor_component_class);
    for (auto *component : components) {
      if (!object_is_valid(component))
        continue;
      if (auto *child_actor = object_property(component, "ChildActor");
          object_is_valid(child_actor)) {
        add_reachable_actor(actors, child_actor,
                            "child_actor_component:" +
                                object_leaf_name(component));
      }
      for (const auto property_name : kVisualActorProperties) {
        if (auto *value =
                object_property(component, std::string(property_name));
            object_is_valid(value)) {
          add_reachable_actor(
              actors, value,
              "component_property:" + object_leaf_name(component) + "." +
                  std::string(property_name));
        }
      }
    }
  }

  // Both calls are bounded to actors related to this Pawn. No global UObject
  // enumeration is used, including while levels or customization actors stream.
  append_actor_array_function(root, "GetAttachedActors", actors,
                              "attached_actor");
  append_actor_array_function(root, "GetAllChildActors", actors, "child_actor");
  return actors;
}

struct ComponentSelection {
  UObject *component{};
  AActor *owner{};
  std::string route;
};

auto find_reachable_skeletal_component(
    const std::vector<ReachableActor> &actors,
    const std::string &component_name) -> ComponentSelection {
  const auto select_from = [&](bool character_skin_only) -> ComponentSelection {
    for (const auto &reachable : actors) {
      if (!object_is_valid(reachable.actor))
        continue;
      if (character_skin_only != is_character_skin_actor(reachable.actor))
        continue;
      if (auto *component =
              find_owned_skeletal_component(reachable.actor, component_name)) {
        return ComponentSelection{component, reachable.actor, reachable.route};
      }
    }
    return {};
  };

  if (!actors.empty()) {
    if (auto *direct = find_owned_skeletal_component(actors.front().actor,
                                                     component_name)) {
      return ComponentSelection{direct, actors.front().actor,
                                actors.front().route};
    }
  }
  if (auto selected = select_from(true); object_is_valid(selected.component))
    return selected;
  return select_from(false);
}

struct BodySelection {
  UObject *component{};
  UObject *mesh{};
  std::size_t candidates{};
  std::string source{"none"};
  std::vector<ReachableActor> reachable_actors;
};

auto select_body_component(AActor *pawn) -> BodySelection {
  BodySelection selection;
  if (!object_is_valid(pawn))
    return selection;
  auto *skeletal_mesh_component_class =
      UObjectGlobals::StaticFindObject<UClass *>(
          nullptr, nullptr, L"/Script/Engine.SkeletalMeshComponent");
  if (!object_is_valid(skeletal_mesh_component_class))
    return selection;

  selection.reachable_actors = collect_reachable_visual_actors(pawn);
  std::vector<UObject *> component_objects;
  std::vector<logic::AppearanceCandidate> candidates;
  std::vector<std::string> component_sources;
  for (const auto &reachable : selection.reachable_actors) {
    if (!object_is_valid(reachable.actor))
      continue;
    const auto &components =
        reachable.actor->K2_GetComponentsByClass(skeletal_mesh_component_class);
    for (auto *component : components) {
      ++selection.candidates;
      if (!is_skeletal_mesh_component(component))
        continue;
      auto *mesh = object_property(component, "SkeletalMesh");
      logic::AppearanceCandidate candidate;
      candidate.component_leaf = object_leaf_name(component);
      candidate.component_full_name = object_name(component);
      candidate.owner_full_name = object_name(reachable.actor);
      candidate.mesh_full_name = object_name(mesh);
      candidate.directly_owned_by_pawn = reachable.actor == pawn;
      candidate.owner_is_character_skin =
          is_character_skin_actor(reachable.actor);
      candidate.related_to_pawn = true;
      component_objects.push_back(component);
      component_sources.push_back(reachable.route);
      candidates.push_back(std::move(candidate));
    }
  }

  const auto selected = logic::select_appearance_candidate(candidates);
  if (!selected)
    return selection;
  selection.component = component_objects[*selected];
  selection.mesh = object_property(selection.component, "SkeletalMesh");
  selection.source = component_sources[*selected];
  return selection;
}

auto log_remote_skeletal_inventory(Logger &logger, std::uint64_t player_id,
                                   const std::vector<ReachableActor> &actors)
    -> void {
  auto *skeletal_mesh_component_class =
      UObjectGlobals::StaticFindObject<UClass *>(
          nullptr, nullptr, L"/Script/Engine.SkeletalMeshComponent");
  if (!object_is_valid(skeletal_mesh_component_class))
    return;
  std::size_t logged{};
  for (const auto &reachable : actors) {
    if (!object_is_valid(reachable.actor))
      continue;
    logger.info("REMOTE_VISUAL_ACTOR player=" + std::to_string(player_id) +
                " route=" + reachable.route +
                " actor=" + object_name(reachable.actor) +
                " class=" + object_name(reachable.actor->GetClassPrivate()));
    const auto &components =
        reachable.actor->K2_GetComponentsByClass(skeletal_mesh_component_class);
    for (auto *component : components) {
      if (!is_skeletal_mesh_component(component))
        continue;
      logger.info(
          "REMOTE_SKELETAL_COMPONENT player=" + std::to_string(player_id) +
          " leaf=" + object_leaf_name(component) +
          " full_name=" + object_name(component) +
          " mesh=" + object_name(object_property(component, "SkeletalMesh")) +
          " owner=" + object_name(reachable.actor) +
          " route=" + reachable.route);
      if (++logged >= 64) {
        logger.warning("REMOTE_SKELETAL_COMPONENT_TRUNCATED player=" +
                       std::to_string(player_id) + " limit=64");
        return;
      }
    }
  }
  if (logged == 0) {
    logger.info("REMOTE_SKELETAL_COMPONENT player=" +
                std::to_string(player_id) + " leaf=nil full_name=nil mesh=nil");
  }
}

auto log_local_visual_diagnostic(Logger &logger, AActor *pawn,
                                 const BodySelection &selection) -> void {
  logger.info("LOCAL_VISUAL_DIAGNOSTIC pawn=" + object_name(pawn) +
              " reason=Body_not_reachable");
  for (const auto property_name : kVisualActorProperties) {
    const auto wide_name = widen(property_name);
    auto **slot =
        pawn->GetValuePtrByPropertyNameInChain<UObject *>(wide_name.c_str());
    if (slot != nullptr) {
      logger.info(
          "LOCAL_SKIN_PROPERTY property=" + std::string(property_name) +
          " value=" + (object_is_valid(*slot) ? object_name(*slot) : "nil"));
    }
  }

  auto *actor_component_class = UObjectGlobals::StaticFindObject<UClass *>(
      nullptr, nullptr, L"/Script/Engine.ActorComponent");
  if (object_is_valid(actor_component_class)) {
    const auto &components =
        pawn->K2_GetComponentsByClass(actor_component_class);
    std::size_t logged{};
    for (auto *component : components) {
      if (!object_is_valid(component))
        continue;
      logger.info(
          "LOCAL_COMPONENT_DIAGNOSTIC leaf=" + object_leaf_name(component) +
          " full_name=" + object_name(component) + " class=" +
          object_name(component->GetClassPrivate()) + " child_actor=" +
          object_name(object_property(component, "ChildActor")));
      if (++logged >= 64) {
        logger.warning("LOCAL_COMPONENT_DIAGNOSTIC_TRUNCATED limit=64");
        break;
      }
    }
  }

  for (const auto &reachable : selection.reachable_actors) {
    logger.info("LOCAL_VISUAL_ROUTE route=" + reachable.route +
                " actor=" + object_name(reachable.actor) + " class=" +
                object_name(object_is_valid(reachable.actor)
                                ? reachable.actor->GetClassPrivate()
                                : nullptr));
  }
}

} // namespace

GameBridge::GameBridge(const ClientConfig &config, NetworkClient &network,
                       Logger &logger)
    : config_(config), network_(network), logger_(logger) {}

GameBridge::~GameBridge() { shutdown(); }

auto GameBridge::tick() -> void {
  if (shutdown_ || in_bridge_tick)
    return;
  const auto now = std::chrono::steady_clock::now();
  if (now < next_bridge_tick_)
    return;
  next_bridge_tick_ = now + std::chrono::milliseconds(16);

  in_bridge_tick = true;
  try {
    const auto connected = network_.connected();
    if (network_was_connected_ && !connected) {
      (void)network_.drain_incoming();
      destroy_all_remotes();
      local_player_id_ = 0;
      resync_requested_ = true;
      logger_.warning("CONNECTION_STATE_RESET remotes_destroyed=true "
                      "reconnect_pending=true");
    }
    network_was_connected_ = connected;
    process_incoming();
    update_local_player();
    update_remote_players();
  } catch (const std::exception &exception) {
    logger_.error(std::string("GAME_BRIDGE_EXCEPTION ") + exception.what());
  }
  in_bridge_tick = false;
}

auto GameBridge::shutdown() -> void {
  if (shutdown_)
    return;
  shutdown_ = true;
  destroy_all_remotes();
}

auto GameBridge::process_incoming() -> void {
  for (const auto &frame : network_.drain_incoming()) {
    switch (frame.type) {
    case protocol::MessageType::welcome: {
      destroy_all_remotes();
      local_player_id_ = protocol::decode_welcome(frame.payload).player_id;
      resync_requested_ = true;
      logger_.info("WELCOME player_id=" + std::to_string(local_player_id_));
      break;
    }
    case protocol::MessageType::zone_state: {
      const auto state = protocol::decode_zone_state(frame.payload);
      if (state.player_id == local_player_id_)
        break;
      auto &remote = remotes_[state.player_id];
      remote.zone = state.zone;
      if (!local_zone_.empty() && state.zone != local_zone_)
        destroy_remote(state.player_id);
      break;
    }
    case protocol::MessageType::player_joined: {
      const auto joined = protocol::decode_player_joined(frame.payload);
      if (joined.player_id != local_player_id_) {
        remotes_.try_emplace(joined.player_id);
        logger_.info(
            "REMOTE_PLAYER_JOINED player=" + std::to_string(joined.player_id) +
            " name=" + joined.player_name);
      }
      break;
    }
    case protocol::MessageType::appearance_state: {
      const auto state = protocol::decode_appearance_state(frame.payload);
      if (state.player_id == local_player_id_)
        break;
      auto &remote = remotes_[state.player_id];
      logger_.info("REMOTE_APPEARANCE_RECEIVED player=" +
                   std::to_string(state.player_id) +
                   " character=" + state.character_class +
                   " outfit=" + state.outfit_mesh + " hair=" + state.hair_mesh);
      const auto old_character =
          remote.appearance ? class_leaf(remote.appearance->character_class)
                            : std::string{};
      const auto new_character = class_leaf(state.character_class);
      if (!old_character.empty() && old_character != new_character) {
        logger_.info("REMOTE_CHARACTER_CHANGED player=" +
                     std::to_string(state.player_id) + " old=" + old_character +
                     " new=" + new_character);
        if (object_is_valid(remote.actor))
          remote.actor->K2_DestroyActor();
        remote.actor = nullptr;
        remote.movement_component = nullptr;
        remote.body_component = nullptr;
        remote.hair_component = nullptr;
        remote.body_route.clear();
        remote.hair_route.clear();
        remote.character_respawn_pending = true;
      }
      remote.appearance = state;
      remote.appearance_dirty = true;
      remote.appearance_attempt_count = 0;
      remote.next_appearance_retry = {};
      remote.fallback_warning_logged = false;
      break;
    }
    case protocol::MessageType::transform_snapshot: {
      const auto state = protocol::decode_transform_snapshot(frame.payload);
      if (state.player_id == local_player_id_)
        break;
      auto &remote = remotes_[state.player_id];
      const auto advances_stream =
          remote.snapshots.empty() ||
          state.timestamp_ms >= remote.snapshots.back().timestamp_ms;
      logic::insert_snapshot(remote.snapshots, state);
      if (!advances_stream)
        break;
      const auto received_at_ms = wall_clock_ms();
      remote.last_transform_received_ms = received_at_ms;
      const auto offset_sample = static_cast<double>(received_at_ms) -
                                 static_cast<double>(state.timestamp_ms);
      if (!remote.clock_offset_initialized) {
        remote.clock_offset_ms = offset_sample;
        remote.clock_offset_initialized = true;
      } else {
        remote.clock_offset_ms =
            remote.clock_offset_ms * 0.9 + offset_sample * 0.1;
      }
      remote.velocity_x = 0.0F;
      remote.velocity_y = 0.0F;
      remote.velocity_z = 0.0F;
      remote.speed = 0.0F;
      if (remote.snapshots.size() >= 2) {
        const auto &previous = remote.snapshots[remote.snapshots.size() - 2];
        const auto &current = remote.snapshots.back();
        const auto delta_seconds =
            static_cast<float>(current.timestamp_ms - previous.timestamp_ms) /
            1000.0F;
        if (delta_seconds > 0.0F && delta_seconds <= 2.0F) {
          remote.velocity_x = (current.x - previous.x) / delta_seconds;
          remote.velocity_y = (current.y - previous.y) / delta_seconds;
          remote.velocity_z = (current.z - previous.z) / delta_seconds;
          remote.speed = horizontal_speed(remote.velocity_x, remote.velocity_y,
                                          remote.velocity_z);
          if (remote.speed < 1.0F) {
            remote.velocity_x = 0.0F;
            remote.velocity_y = 0.0F;
            remote.velocity_z = 0.0F;
            remote.speed = 0.0F;
          }
        }
      }
      break;
    }
    case protocol::MessageType::player_left:
      destroy_remote(protocol::decode_player_left(frame.payload).player_id);
      break;
    case protocol::MessageType::error: {
      const auto error = protocol::decode_error(frame.payload);
      logger_.error("SERVER_ERROR code=" + std::to_string(error.code) +
                    " message=" + error.message);
      break;
    }
    default:
      break;
    }
  }
}

auto GameBridge::update_local_player() -> void {
  auto *pawn = find_local_pawn();
  if (!pawn) {
    local_visual_pawn_ = nullptr;
    local_body_component_ = nullptr;
    local_hair_component_ = nullptr;
    local_movement_component_ = nullptr;
    local_visual_route_diagnostic_logged_ = false;
    local_movement_state_initialized_ = false;
    last_local_movement_signature_.clear();
    if (exploration_available_) {
      exploration_available_ = false;
      destroy_all_remotes();
      logger_.info("EXPLORATION_UNAVAILABLE Pawn=nil; combat synchronization "
                   "is intentionally disabled");
    }
    return;
  }

  if (!exploration_available_) {
    exploration_available_ = true;
    logger_.info("EXPLORATION_READY pawn=" + object_name(pawn));
  }

  const auto zone = current_zone(pawn);
  if (zone.empty())
    return;
  if (zone != local_zone_) {
    destroy_all_remotes();
    local_zone_ = zone;
    resync_requested_ = true;
    logger_.info("LOCAL_ZONE " + local_zone_);
  }

  if (network_.connected() && resync_requested_) {
    network_.enqueue(protocol::make_frame(
        protocol::MessageType::zone_state, network_.next_sequence(),
        protocol::ZoneState{local_player_id_, local_zone_}));
  }

  const auto now = std::chrono::steady_clock::now();
  if (local_visual_pawn_ != pawn) {
    appearance_failure_count_ = 0;
    next_appearance_capture_ = {};
    local_movement_component_ = nullptr;
    local_movement_state_initialized_ = false;
    last_local_movement_signature_.clear();
  }
  bool appearance_changed{};
  if (now >= next_appearance_capture_) {
    const auto candidate = capture_appearance(pawn);
    const auto effective =
        logic::select_effective_appearance(local_appearance_, candidate);
    if (logic::appearance_is_ready(candidate)) {
      appearance_failure_count_ = 0;
      next_appearance_capture_ = now + std::chrono::seconds(1);
      appearance_changed =
          !local_appearance_ || !same_appearance(*local_appearance_, candidate);
      local_appearance_ = effective;
    } else {
      ++appearance_failure_count_;
      next_appearance_capture_ =
          now + std::chrono::milliseconds(logic::appearance_retry_delay_ms(
                    appearance_failure_count_));
      if (now >= next_appearance_pending_log_) {
        next_appearance_pending_log_ = now + std::chrono::seconds(2);
        logger_.info("LOCAL_APPEARANCE_PENDING reason=body_not_ready");
      }
    }
  }
  if (network_.connected() && local_appearance_ &&
      (resync_requested_ || appearance_changed)) {
    network_.enqueue(
        protocol::make_frame(protocol::MessageType::appearance_state,
                             network_.next_sequence(), *local_appearance_));
    logger_.info(
        "LOCAL_APPEARANCE character=" + local_appearance_->character_class +
        " outfit=" + local_appearance_->outfit_mesh +
        " hair=" + local_appearance_->hair_mesh);
  }
  resync_requested_ = false;

  const auto location = pawn->K2_GetActorLocation();
  const auto rotation = pawn->K2_GetActorRotation();

  if (!object_is_valid(local_movement_component_)) {
    local_movement_component_ = find_remote_movement_component(pawn);
  }
  FVector local_velocity{};
  if (!call_vector_return(pawn, "GetVelocity", local_velocity)) {
    if (auto *value = vector_property(local_movement_component_, "Velocity"))
      local_velocity = *value;
  }
  const auto *local_movement_mode =
      byte_property(local_movement_component_, "MovementMode");
  bool local_is_falling{};
  const auto has_local_is_falling = call_bool_return(
      local_movement_component_, "IsFalling", local_is_falling);
  const auto vertical_phase = logic::classify_vertical_movement(
      has_local_is_falling && local_is_falling, local_velocity.Z());
  const auto movement_mode_text = local_movement_mode
                                      ? std::to_string(*local_movement_mode)
                                      : "unavailable";
  const auto is_falling_text = has_local_is_falling
                                   ? (local_is_falling ? "true" : "false")
                                   : "unavailable";
  const auto movement_signature =
      movement_mode_text + '|' + is_falling_text + '|' +
      std::string(logic::vertical_movement_phase_name(vertical_phase));
  if (!local_movement_state_initialized_ ||
      movement_signature != last_local_movement_signature_) {
    local_movement_state_initialized_ = true;
    last_local_movement_signature_ = movement_signature;
    logger_.info(
        "LOCAL_MOVEMENT_STATE movement_mode=" + movement_mode_text +
        " is_falling=" + is_falling_text + " phase=" +
        std::string(logic::vertical_movement_phase_name(vertical_phase)) +
        " velocity=" + std::to_string(local_velocity.X()) + "," +
        std::to_string(local_velocity.Y()) + "," +
        std::to_string(local_velocity.Z()) + " speed=" +
        std::to_string(horizontal_speed(local_velocity.X(), local_velocity.Y(),
                                        local_velocity.Z())));
  }
  if (now >= next_local_transform_log_) {
    next_local_transform_log_ = now + std::chrono::seconds(2);
    logger_.info("LOCAL_TRANSFORM x=" + std::to_string(location.X()) +
                 " y=" + std::to_string(location.Y()) +
                 " z=" + std::to_string(location.Z()) +
                 " yaw=" + std::to_string(rotation.GetYaw()));
  }

  if (!network_.connected() || now < next_snapshot_)
    return;
  next_snapshot_ = now + std::chrono::milliseconds(1000 / config_.snapshot_hz);

  const auto timestamp = static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count());
  protocol::TransformSnapshot snapshot;
  snapshot.player_id = local_player_id_;
  snapshot.timestamp_ms = timestamp;
  snapshot.x = location.X();
  snapshot.y = location.Y();
  snapshot.z = location.Z();
  snapshot.pitch = rotation.GetPitch();
  snapshot.yaw = rotation.GetYaw();
  snapshot.roll = rotation.GetRoll();
  network_.enqueue(
      protocol::make_frame(protocol::MessageType::transform_snapshot,
                           network_.next_sequence(), snapshot));
}

auto GameBridge::update_remote_players() -> void {
  if (!exploration_available_ || local_zone_.empty())
    return;
  for (auto &[player_id, remote] : remotes_) {
    if (remote.zone != local_zone_ || remote.snapshots.empty())
      continue;
    if (!ensure_remote_actor(player_id, remote))
      continue;
    if (remote.appearance_dirty)
      apply_remote_appearance(player_id, remote);
    apply_remote_transform(player_id, remote);
  }
}

auto GameBridge::find_local_pawn() -> AActor * {
  const auto controller_name = widen(config_.controller_class);
  auto *controller = UObjectGlobals::FindFirstOf(controller_name.c_str());
  auto *pawn = object_property(controller, config_.pawn_property);
  return object_is_valid(pawn) ? static_cast<AActor *>(pawn) : nullptr;
}

auto GameBridge::current_zone(AActor *pawn) -> std::string {
  if (!object_is_valid(pawn))
    return {};
  if (auto *level = pawn->GetLevel(); object_is_valid(level))
    return narrow(level->GetFullName());
  if (auto *world = pawn->GetWorld(); object_is_valid(world))
    return narrow(world->GetFullName());
  return {};
}

auto GameBridge::capture_appearance(AActor *pawn) -> protocol::AppearanceState {
  const auto scan_started = std::chrono::steady_clock::now();
  if (local_visual_pawn_ != pawn) {
    local_visual_pawn_ = pawn;
    local_body_component_ = nullptr;
    local_hair_component_ = nullptr;
    last_body_component_log_.clear();
    last_body_mesh_log_.clear();
    last_body_source_log_.clear();
    body_diagnostic_initialized_ = false;
    local_visual_route_diagnostic_logged_ = false;
  }

  // This is intentionally bounded to components owned by the local Pawn.
  // Global UObject scans during level streaming can stall the game thread.
  const auto body_selection = select_body_component(pawn);
  local_body_component_ = body_selection.component;
  if (!object_is_valid(local_hair_component_)) {
    auto *hair_property =
        object_property(pawn, config_.hair_component_property);
    local_hair_component_ = is_skeletal_mesh_component(hair_property)
                                ? hair_property
                                : find_owned_skeletal_component(
                                      pawn, config_.hair_component_property);
  }

  protocol::AppearanceState appearance;
  appearance.player_id = local_player_id_;
  auto *body_mesh = body_selection.mesh;
  appearance.outfit_mesh = object_name(body_mesh);
  appearance.hair_mesh = object_name(
      object_property(local_hair_component_, config_.mesh_asset_property));
  appearance.character_class =
      logic::infer_character_from_body_mesh(appearance.outfit_mesh);

  const auto body_component_log = object_is_valid(local_body_component_)
                                      ? object_name(local_body_component_)
                                      : "nil";
  const auto body_mesh_log =
      object_is_valid(body_mesh) ? object_name(body_mesh) : "nil";
  if (!body_diagnostic_initialized_ ||
      body_component_log != last_body_component_log_) {
    logger_.info("APPEARANCE_BODY_COMPONENT component=" + body_component_log);
    last_body_component_log_ = body_component_log;
  }
  if (!body_diagnostic_initialized_ || body_mesh_log != last_body_mesh_log_) {
    logger_.info("APPEARANCE_BODY_MESH mesh=" + body_mesh_log);
    last_body_mesh_log_ = body_mesh_log;
  }
  body_diagnostic_initialized_ = true;

  if (!object_is_valid(body_selection.component)) {
    last_body_source_log_.clear();
    if (!local_visual_route_diagnostic_logged_) {
      local_visual_route_diagnostic_logged_ = true;
      log_local_visual_diagnostic(logger_, pawn, body_selection);
    }
  } else {
    local_visual_route_diagnostic_logged_ = false;
    if (body_selection.source != "pawn" &&
        body_selection.source != last_body_source_log_) {
      logger_.info("APPEARANCE_VISUAL_ROUTE source=" + body_selection.source +
                   " component=" + object_name(body_selection.component));
    }
    last_body_source_log_ = body_selection.source;
  }

  const auto scan_finished = std::chrono::steady_clock::now();
  const auto duration_us =
      std::chrono::duration_cast<std::chrono::microseconds>(scan_finished -
                                                            scan_started)
          .count();
  if (scan_finished >= next_appearance_scan_log_) {
    next_appearance_scan_log_ = scan_finished + std::chrono::seconds(2);
    const auto message =
        "APPEARANCE_SCAN duration_us=" + std::to_string(duration_us) +
        " candidates=" + std::to_string(body_selection.candidates) +
        " source=" + body_selection.source;
    if (duration_us > 5000) {
      logger_.warning(message);
    } else {
      logger_.info(message);
    }
  }
  return appearance;
}

auto GameBridge::ensure_remote_actor(std::uint64_t player_id,
                                     RemotePlayer &remote) -> AActor * {
  if (object_is_valid(remote.actor))
    return remote.actor;
  auto *local_pawn = find_local_pawn();
  if (!local_pawn || remote.snapshots.empty())
    return nullptr;

  std::string class_spec = config_.default_companion_class;
  if (remote.appearance) {
    const auto character = class_leaf(remote.appearance->character_class);
    bool mapped{};
    if (const auto found = config_.companion_by_character.find(character);
        found != config_.companion_by_character.end()) {
      class_spec = found->second;
      mapped = true;
    } else {
      const auto visual_signature = remote.appearance->character_class + ' ' +
                                    remote.appearance->outfit_mesh + ' ' +
                                    remote.appearance->hair_mesh;
      for (const auto &[token, companion_class] :
           config_.companion_by_character) {
        if (visual_signature.find(token) != std::string::npos) {
          class_spec = companion_class;
          mapped = true;
          break;
        }
      }
    }
    if (!mapped && !remote.fallback_warning_logged) {
      remote.fallback_warning_logged = true;
      logger_.warning("REMOTE_CHARACTER_UNKNOWN player=" +
                      std::to_string(player_id) + " character=" + character +
                      " fallback=" + config_.default_companion_class);
    }
  }

  UClass *actor_class{};
  if (!class_spec.empty() && class_spec.front() == '/') {
    const auto class_path = widen(class_spec);
    actor_class = UObjectGlobals::StaticFindObject<UClass *>(
        nullptr, nullptr, class_path.c_str());
  } else {
    const auto short_name = widen(class_spec);
    if (auto *template_actor = UObjectGlobals::FindFirstOf(short_name.c_str());
        object_is_valid(template_actor)) {
      actor_class = template_actor->GetClassPrivate();
    }
  }
  if (!actor_class) {
    logger_.warning("REMOTE_SPAWN_WAIT player=" + std::to_string(player_id) +
                    " class=" + class_spec);
    return nullptr;
  }

  auto *world = local_pawn->GetWorld();
  if (!object_is_valid(world))
    return nullptr;
  const auto &state = remote.last_rendered_transform
                          ? *remote.last_rendered_transform
                          : remote.snapshots.back();
  FVector location(state.x, state.y, state.z);
  FRotator rotation(state.pitch, state.yaw, state.roll);
  remote.actor = world->SpawnActor(actor_class, &location, &rotation);
  if (!object_is_valid(remote.actor)) {
    remote.actor = nullptr;
    logger_.warning("REMOTE_SPAWN_FAILED player=" + std::to_string(player_id) +
                    " class=" + class_spec);
    return nullptr;
  }

  disable_remote_ai(remote.actor);
  remote.movement_component = find_remote_movement_component(remote.actor);
  remote.body_component = nullptr;
  remote.hair_component = nullptr;
  remote.body_route.clear();
  remote.hair_route.clear();
  remote.movement_warning_logged = false;
  remote.skeletal_diagnostic_logged = false;
  remote.appearance_attempt_count = 0;
  remote.next_appearance_retry = {};
  auto *mesh_component = object_property(remote.actor, "Mesh");
  if (!is_skeletal_mesh_component(mesh_component)) {
    mesh_component = find_owned_skeletal_component(
        remote.actor, config_.body_component_property);
  }
  auto *anim_instance = call_object_return(mesh_component, "GetAnimInstance");
  logger_.info("REMOTE_MOTION_SETUP player=" + std::to_string(player_id) +
               " movement_component=" + object_name(remote.movement_component) +
               " movement_class=" +
               object_name(object_is_valid(remote.movement_component)
                               ? remote.movement_component->GetClassPrivate()
                               : nullptr) +
               " anim_instance_class=" +
               object_name(object_is_valid(anim_instance)
                               ? anim_instance->GetClassPrivate()
                               : nullptr));
  remote.appearance_dirty = true;
  logger_.info("REMOTE_SPAWNED player=" + std::to_string(player_id) +
               " actor=" + object_name(remote.actor));
  if (remote.character_respawn_pending) {
    remote.character_respawn_pending = false;
    logger_.info(
        "REMOTE_CHARACTER_RESPAWN player=" + std::to_string(player_id) +
        " character=" +
        (remote.appearance ? remote.appearance->character_class : "Unknown") +
        " actor=" + object_name(remote.actor));
  }
  return remote.actor;
}

auto GameBridge::apply_remote_transform(std::uint64_t player_id,
                                        RemotePlayer &remote) -> void {
  if (!object_is_valid(remote.actor) || remote.snapshots.empty())
    return;

  const auto current_wall_ms = wall_clock_ms();
  const auto estimated_remote_now =
      remote.clock_offset_initialized
          ? static_cast<double>(current_wall_ms) - remote.clock_offset_ms
          : static_cast<double>(remote.snapshots.back().timestamp_ms);
  const auto delayed_render_time =
      estimated_remote_now -
      static_cast<double>(config_.interpolation_delay_ms);
  const auto render_timestamp_ms =
      delayed_render_time > 0.0
          ? static_cast<std::uint64_t>(delayed_render_time)
          : std::uint64_t{};
  const auto sample =
      logic::sample_snapshot(remote.snapshots, render_timestamp_ms);
  if (!sample)
    return;
  const auto &state = sample->transform;
  remote.last_rendered_transform = state;
  const auto &target = remote.snapshots.back();
  FVector location(state.x, state.y, state.z);
  FRotator rotation(state.pitch, state.yaw, state.roll);
  FHitResult hit_result{};
  remote.actor->K2_SetActorLocationAndRotation(location, rotation, false,
                                               hit_result, true);

  if (!object_is_valid(remote.movement_component)) {
    remote.movement_component = find_remote_movement_component(remote.actor);
  }

  const auto now = std::chrono::steady_clock::now();
  const auto snapshot_stale = logic::snapshot_stream_is_stale(
      current_wall_ms, remote.last_transform_received_ms);
  const auto velocity_x = snapshot_stale ? 0.0F : remote.velocity_x;
  const auto velocity_y = snapshot_stale ? 0.0F : remote.velocity_y;
  const auto velocity_z = snapshot_stale ? 0.0F : remote.velocity_z;
  const auto speed = snapshot_stale ? 0.0F : remote.speed;
  if (auto *velocity = vector_property(remote.movement_component, "Velocity")) {
    *velocity = FVector(velocity_x, velocity_y, velocity_z);
  } else if (!remote.movement_warning_logged) {
    remote.movement_warning_logged = true;
    logger_.warning("REMOTE_MOVEMENT_COMPONENT_MISSING player=" +
                    std::to_string(player_id));
  }

  if (now >= remote.next_motion_log) {
    remote.next_motion_log = now + std::chrono::seconds(2);
    FVector observed_velocity{};
    if (!call_vector_return(remote.actor, "GetVelocity", observed_velocity)) {
      if (auto *observed =
              vector_property(remote.movement_component, "Velocity"))
        observed_velocity = *observed;
    }
    const auto *movement_mode =
        byte_property(remote.movement_component, "MovementMode");
    bool is_falling{};
    const auto has_is_falling =
        call_bool_return(remote.movement_component, "IsFalling", is_falling);
    logger_.info(
        "REMOTE_MOTION player=" + std::to_string(player_id) + " speed=" +
        std::to_string(speed) + " velocity=" + std::to_string(velocity_x) +
        "," + std::to_string(velocity_y) + "," + std::to_string(velocity_z) +
        " observed=" + std::to_string(observed_velocity.X()) + "," +
        std::to_string(observed_velocity.Y()) + "," +
        std::to_string(observed_velocity.Z()) + " movement_mode=" +
        (movement_mode ? std::to_string(*movement_mode) : "unavailable") +
        " is_falling=" +
        (has_is_falling ? (is_falling ? "true" : "false") : "unavailable"));
  }

  if (now >= remote.next_interpolation_log) {
    remote.next_interpolation_log = now + std::chrono::seconds(2);
    logger_.info("REMOTE_INTERPOLATION player=" + std::to_string(player_id) +
                 " buffer=" + std::to_string(remote.snapshots.size()) +
                 " delay_ms=" + std::to_string(config_.interpolation_delay_ms) +
                 " alpha=" + std::to_string(sample->alpha) +
                 " render_xyz=" + std::to_string(state.x) + "," +
                 std::to_string(state.y) + "," + std::to_string(state.z) +
                 " target_xyz=" + std::to_string(target.x) + "," +
                 std::to_string(target.y) + "," + std::to_string(target.z));
  }
}

auto GameBridge::apply_remote_appearance(std::uint64_t player_id,
                                         RemotePlayer &remote) -> void {
  if (!object_is_valid(remote.actor) || !remote.appearance)
    return;
  const auto now = std::chrono::steady_clock::now();
  if (now < remote.next_appearance_retry)
    return;

  const auto reachable_actors = collect_reachable_visual_actors(remote.actor);
  if (!object_is_valid(remote.body_component) ||
      object_leaf_name(remote.body_component) !=
          config_.body_component_property) {
    const auto selection = find_reachable_skeletal_component(
        reachable_actors, config_.body_component_property);
    remote.body_component = selection.component;
    remote.body_route = selection.route;
  }
  if (!object_is_valid(remote.hair_component) ||
      object_leaf_name(remote.hair_component) !=
          config_.hair_component_property) {
    const auto selection = find_reachable_skeletal_component(
        reachable_actors, config_.hair_component_property);
    remote.hair_component = selection.component;
    remote.hair_route = selection.route;
  }

  const auto first_attempt = remote.appearance_attempt_count == 0;
  if (first_attempt || object_is_valid(remote.body_component)) {
    logger_.info(
        "REMOTE_BODY_COMPONENT player=" + std::to_string(player_id) +
        " component=" +
        (object_is_valid(remote.body_component)
             ? object_name(remote.body_component)
             : "nil") +
        " route=" + (remote.body_route.empty() ? "none" : remote.body_route));
  }
  if (first_attempt || object_is_valid(remote.hair_component)) {
    logger_.info(
        "REMOTE_HAIR_COMPONENT player=" + std::to_string(player_id) +
        " component=" +
        (object_is_valid(remote.hair_component)
             ? object_name(remote.hair_component)
             : "nil") +
        " route=" + (remote.hair_route.empty() ? "none" : remote.hair_route));
  }
  if (!object_is_valid(remote.body_component) &&
      !remote.skeletal_diagnostic_logged &&
      remote.appearance_attempt_count >= 2) {
    remote.skeletal_diagnostic_logged = true;
    log_remote_skeletal_inventory(logger_, player_id, reachable_actors);
  }

  bool changed{};
  const auto body_requested = !remote.appearance->outfit_mesh.empty();
  const auto hair_requested = !remote.appearance->hair_mesh.empty();
  auto *body_mesh =
      body_requested ? resolve_object(remote.appearance->outfit_mesh) : nullptr;
  auto *hair_mesh =
      hair_requested ? resolve_object(remote.appearance->hair_mesh) : nullptr;
  auto body_ready = !body_requested;
  auto hair_ready = !hair_requested;

  if (body_requested && object_is_valid(body_mesh) &&
      object_is_valid(remote.body_component)) {
    if (object_property(remote.body_component, config_.mesh_asset_property) ==
        body_mesh) {
      body_ready = true;
    } else if (set_object_property(remote.body_component,
                                   config_.mesh_asset_property, body_mesh)) {
      changed = true;
      body_ready = true;
      call_no_args(remote.body_component, "MarkRenderStateDirty");
      logger_.info("REMOTE_OUTFIT_APPLIED player=" + std::to_string(player_id) +
                   " mesh=" + object_name(body_mesh));
    }
  }
  if (hair_requested && object_is_valid(hair_mesh) &&
      object_is_valid(remote.hair_component)) {
    if (object_property(remote.hair_component, config_.mesh_asset_property) ==
        hair_mesh) {
      hair_ready = true;
    } else if (set_object_property(remote.hair_component,
                                   config_.mesh_asset_property, hair_mesh)) {
      changed = true;
      hair_ready = true;
      call_no_args(remote.hair_component, "MarkRenderStateDirty");
      logger_.info("REMOTE_HAIR_APPLIED player=" + std::to_string(player_id) +
                   " mesh=" + object_name(hair_mesh));
    }
  }

  ++remote.appearance_attempt_count;
  const auto decision = logic::appearance_apply_decision(
      body_requested, body_ready, hair_requested, hair_ready,
      remote.appearance_attempt_count);
  if (decision == logic::AppearanceApplyDecision::retry) {
    const auto delay_ms =
        logic::appearance_retry_delay_ms(remote.appearance_attempt_count);
    remote.next_appearance_retry = now + std::chrono::milliseconds(delay_ms);
    if (first_attempt) {
      if (body_requested && !body_ready) {
        logger_.warning(
            "REMOTE_OUTFIT_FAIL_OPEN player=" + std::to_string(player_id) +
            " reason=" +
            (object_is_valid(body_mesh) ? "Body_component_not_ready"
                                        : "asset_not_loaded") +
            " mesh=" + remote.appearance->outfit_mesh + " retrying=true");
      }
      if (hair_requested && !hair_ready) {
        logger_.warning(
            "REMOTE_HAIR_FAIL_OPEN player=" + std::to_string(player_id) +
            " reason=" +
            (object_is_valid(hair_mesh)
                 ? "Haircut_SkeletalMesh_component_not_ready"
                 : "asset_not_loaded") +
            " mesh=" + remote.appearance->hair_mesh + " retrying=true");
      }
    }
    logger_.info("REMOTE_APPEARANCE_RETRY player=" + std::to_string(player_id) +
                 " attempt=" + std::to_string(remote.appearance_attempt_count) +
                 " delay_ms=" + std::to_string(delay_ms) +
                 " body_ready=" + (body_ready ? "true" : "false") +
                 " hair_ready=" + (hair_ready ? "true" : "false"));
    return;
  }

  remote.appearance_dirty = false;
  if (decision == logic::AppearanceApplyDecision::fail_open) {
    logger_.warning(
        "REMOTE_APPEARANCE_FAIL_OPEN player=" + std::to_string(player_id) +
        " attempts=" + std::to_string(remote.appearance_attempt_count) +
        " body_ready=" + (body_ready ? "true" : "false") +
        " hair_ready=" + (hair_ready ? "true" : "false"));
  }
  logger_.info("REMOTE_APPEARANCE_APPLIED player=" + std::to_string(player_id) +
               " changed=" + (changed ? "true" : "false") + " complete=" +
               (decision == logic::AppearanceApplyDecision::complete
                    ? "true"
                    : "false") +
               " actor=" + object_name(remote.actor));
}

auto GameBridge::disable_remote_ai(AActor *actor) -> void {
  if (!object_is_valid(actor))
    return;
  actor->SetActorEnableCollision(false);
  auto *controller = object_property(actor, config_.controller_property);
  call_no_args(controller, "StopMovement");
  if (object_is_valid(controller)) {
    static_cast<AActor *>(controller)->SetActorTickEnabled(false);
    auto *brain = object_property(controller, config_.brain_component_property);
    // Deactivate is the no-argument Blueprint-safe path for stopping the
    // BrainComponent; StopLogic itself requires an FString parameter.
    call_no_args(brain, "Deactivate");
  }
}

auto GameBridge::destroy_remote(std::uint64_t player_id) -> void {
  const auto found = remotes_.find(player_id);
  if (found == remotes_.end())
    return;
  if (object_is_valid(found->second.actor)) {
    found->second.actor->K2_DestroyActor();
    logger_.info("REMOTE_DESTROYED player=" + std::to_string(player_id));
  }
  remotes_.erase(found);
}

auto GameBridge::destroy_all_remotes() -> void {
  for (auto &[player_id, remote] : remotes_) {
    (void)player_id;
    if (object_is_valid(remote.actor))
      remote.actor->K2_DestroyActor();
  }
  remotes_.clear();
}
} // namespace expedition_online::client
