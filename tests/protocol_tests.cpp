#include <expedition_online/client_logic.hpp>
#include <expedition_online/protocol.hpp>

#include <cmath>
#include <cstdint>
#include <deque>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace proto = expedition_online::protocol;
namespace logic = expedition_online::client_logic;

namespace {
auto check(bool condition, const std::string &message) -> void {
  if (!condition)
    throw std::runtime_error(message);
}

auto test_payloads() -> void {
  const proto::Hello hello{"Maelle", "client-0.1.0"};
  const auto decoded_hello = proto::decode_hello(proto::encode(hello));
  check(decoded_hello.player_name == hello.player_name, "Hello player_name");
  check(decoded_hello.client_build == hello.client_build, "Hello client_build");

  const proto::AppearanceState appearance{42, "Maelle", "Maelle_ActeIII",
                                          "Maelle_Face_02"};
  const auto decoded_appearance =
      proto::decode_appearance_state(proto::encode(appearance));
  check(decoded_appearance.player_id == 42, "Appearance player_id");
  check(decoded_appearance.character_id == appearance.character_id,
        "Appearance character");
  check(decoded_appearance.customization_skin == appearance.customization_skin,
        "Appearance customization skin");
  check(decoded_appearance.customization_face == appearance.customization_face,
        "Appearance customization face");

  const proto::PlayerContextState context{42, proto::PlayerContext::world_map};
  const auto decoded_context =
      proto::decode_player_context_state(proto::encode(context));
  check(decoded_context.player_id == 42, "PlayerContextState player_id");
  check(decoded_context.context == proto::PlayerContext::world_map,
        "PlayerContextState context");

  const proto::PlayerJoined joined{77, "Verso"};
  const auto decoded_joined =
      proto::decode_player_joined(proto::encode(joined));
  check(decoded_joined.player_id == joined.player_id, "PlayerJoined player_id");
  check(decoded_joined.player_name == joined.player_name,
        "PlayerJoined player_name");

  const proto::TransformSnapshot transform{9,     123456, 1.25F, -2.5F,
                                           3.75F, 10.0F,  20.0F, 30.0F};
  const auto decoded_transform =
      proto::decode_transform_snapshot(proto::encode(transform));
  check(decoded_transform.player_id == transform.player_id,
        "Transform player_id");
  check(decoded_transform.timestamp_ms == transform.timestamp_ms,
        "Transform timestamp");
  check(std::fabs(decoded_transform.x - transform.x) < 0.0001F, "Transform x");
  check(std::fabs(decoded_transform.yaw - transform.yaw) < 0.0001F,
        "Transform yaw");

  const proto::MovementState movement{91, 3, 7};
  const auto decoded_movement =
      proto::decode_movement_state(proto::encode(movement));
  check(decoded_movement.player_id == movement.player_id,
        "MovementState player_id");
  check(decoded_movement.movement_mode == 3, "MovementState movement_mode");
  check(decoded_movement.custom_movement_mode == 7,
        "MovementState custom_movement_mode");

  const proto::PlayerLocomotionState locomotion{91,   3,    4,    2,     1,
                                                true, true, true, -27.5F};
  const auto decoded_locomotion =
      proto::decode_player_locomotion_state(proto::encode(locomotion));
  check(decoded_locomotion.player_id == 91, "PlayerLocomotionState player_id");
  check(decoded_locomotion.movement_mode == 3 &&
            decoded_locomotion.locomotion_state == 4 &&
            decoded_locomotion.gait == 2 && decoded_locomotion.stance == 1,
        "PlayerLocomotionState compact enums");
  check(decoded_locomotion.sprinting && decoded_locomotion.crouching &&
            decoded_locomotion.aiming &&
            std::fabs(decoded_locomotion.aim_pitch + 27.5F) < 0.0001F,
        "PlayerLocomotionState flags and aim pitch");

  const proto::JumpEvent jump{1234, 17};
  const auto decoded_jump = proto::decode_jump_event(proto::encode(jump));
  check(decoded_jump.player_id == jump.player_id, "JumpEvent player_id");
  check(decoded_jump.sequence == jump.sequence, "JumpEvent sequence");
}

auto test_fragmented_frames() -> void {
  const auto first = proto::encode_frame(
      proto::make_frame(proto::MessageType::zone_state, 100,
                        proto::ZoneState{8, "PersistentLevel-A"}));
  const auto second = proto::encode_frame(proto::make_frame(
      proto::MessageType::player_left, 101, proto::PlayerLeft{8}));
  std::vector<std::uint8_t> stream = first;
  stream.insert(stream.end(), second.begin(), second.end());

  proto::FrameDecoder decoder;
  for (const auto byte : stream) {
    decoder.push(std::span<const std::uint8_t>(&byte, 1));
  }
  const auto frames = decoder.take_frames();
  check(frames.size() == 2, "fragmented decoder frame count");
  check(frames[0].sequence == 100, "first sequence");
  check(proto::decode_zone_state(frames[0].payload).zone == "PersistentLevel-A",
        "first payload");
  check(frames[1].sequence == 101, "second sequence");
  check(proto::decode_player_left(frames[1].payload).player_id == 8,
        "second payload");
  check(decoder.buffered_bytes() == 0, "decoder buffer drained");
}

auto test_rejections() -> void {
  check(proto::is_known_message_type(proto::MessageType::hello),
        "known Hello type");
  check(proto::is_known_message_type(proto::MessageType::pong),
        "known Pong type");
  check(!proto::is_known_message_type(static_cast<proto::MessageType>(65535)),
        "unknown type rejection helper");
  check(proto::is_empty_payload_message(proto::MessageType::ping),
        "Ping empty payload helper");
  check(!proto::is_empty_payload_message(proto::MessageType::hello),
        "Hello is not empty payload");
  check(proto::is_known_message_type(proto::MessageType::jump_event),
        "known JumpEvent type");
  check(proto::is_known_message_type(proto::MessageType::player_context_state),
        "known PlayerContextState type");
  check(
      proto::is_known_message_type(proto::MessageType::player_locomotion_state),
      "known PlayerLocomotionState type");
  check(proto::is_valid_player_context(proto::PlayerContext::combat),
        "valid combat context");
  check(!proto::is_valid_player_context(static_cast<proto::PlayerContext>(255)),
        "invalid player context");
  check(proto::kProtocolVersion == 5, "protocol v5 capability boundary");

  bool rejected{};
  try {
    (void)proto::decode_welcome(std::vector<std::uint8_t>{0, 1});
  } catch (const proto::ProtocolError &) {
    rejected = true;
  }
  check(rejected, "truncated payload rejection");

  rejected = false;
  try {
    proto::FrameDecoder decoder;
    std::vector<std::uint8_t> invalid(proto::kHeaderSize, 0);
    decoder.push(invalid);
  } catch (const proto::ProtocolError &) {
    rejected = true;
  }
  check(rejected, "invalid magic rejection");
}

auto snapshot(std::uint64_t timestamp, float x, float yaw = 0.0F)
    -> proto::TransformSnapshot {
  return proto::TransformSnapshot{7,        timestamp, x,   x * 2.0F,
                                  x * 3.0F, 0.0F,      yaw, 0.0F};
}

auto test_snapshot_interpolation() -> void {
  std::deque<proto::TransformSnapshot> buffer;
  logic::insert_snapshot(buffer, snapshot(200, 10.0F, -170.0F));
  logic::insert_snapshot(buffer, snapshot(100, 0.0F, 170.0F));
  logic::insert_snapshot(buffer, snapshot(300, 20.0F));
  check(buffer.size() == 3, "snapshot buffer size");
  check(buffer[0].timestamp_ms == 100 && buffer[1].timestamp_ms == 200 &&
            buffer[2].timestamp_ms == 300,
        "snapshot buffer ordering");

  logic::insert_snapshot(buffer, snapshot(200, 12.0F, -170.0F));
  check(buffer.size() == 3 && std::fabs(buffer[1].x - 12.0F) < 0.0001F,
        "duplicate snapshot replacement");

  const auto interpolated = logic::sample_snapshot(buffer, 150);
  check(interpolated && interpolated->interpolated,
        "interpolation sample available");
  check(std::fabs(interpolated->transform.x - 6.0F) < 0.0001F, "position lerp");
  check(std::fabs(std::fabs(interpolated->transform.yaw) - 180.0F) < 0.0001F,
        "yaw shortest path interpolation");

  const auto beyond_latest = logic::sample_snapshot(buffer, 999);
  check(beyond_latest && !beyond_latest->interpolated &&
            beyond_latest->transform.timestamp_ms == buffer.back().timestamp_ms,
        "no indefinite snapshot extrapolation");

  check(!logic::snapshot_stream_is_stale(1750, 1000),
        "snapshot not stale at threshold");
  check(logic::snapshot_stream_is_stale(1751, 1000),
        "snapshot stale after threshold");
  check(logic::snapshot_exceeds_teleport_threshold(
            snapshot(100, 0.0F), snapshot(200, 100.0F), 50.0F),
        "large same-context transform is a teleport");
  check(!logic::snapshot_exceeds_teleport_threshold(
            snapshot(100, 0.0F), snapshot(200, 10.0F), 50.0F),
        "normal transform stays interpolated");
  check(logic::context_requires_actor_reset(proto::PlayerContext::exploration,
                                            proto::PlayerContext::world_map),
        "context transition resets remote actor");
  check(!logic::context_requires_actor_reset(proto::PlayerContext::exploration,
                                             proto::PlayerContext::exploration),
        "stable context preserves remote actor");
  check(
      logic::context_supports_remote_actor(proto::PlayerContext::exploration) &&
          logic::context_supports_remote_actor(
              proto::PlayerContext::world_map) &&
          !logic::context_supports_remote_actor(proto::PlayerContext::combat),
      "only exploration contexts render remote actors");
}

auto test_appearance_selection() -> void {
  logic::AppearanceCandidate direct{
      "Body",
      "SkeletalMeshComponent /Game/Map.Pawn.Body",
      "BP_jRPG_Character_World_C /Game/Map.Pawn",
      "SkeletalMesh "
      "/Game/Characters/Heros/Lune/Outfits/SK_Lune_Chic_Skin.SK_Lune_Chic_Skin",
      true,
      false,
      true,
  };
  check(logic::score_appearance_candidate(direct) > 0,
        "direct pawn Body candidate");

  auto skin = direct;
  skin.owner_full_name = "BP_CharacterSkin_ScielOutfit_Chic_C /Game/Map.Skin";
  skin.mesh_full_name = "SkeletalMesh "
                        "/Game/Characters/Heros/Sciel/Outfits/"
                        "SK_Sciel_Chic_Skin.SK_Sciel_Chic_Skin";
  skin.directly_owned_by_pawn = false;
  skin.owner_is_character_skin = true;
  skin.related_to_pawn = true;
  check(logic::score_appearance_candidate(skin) > 0,
        "related character skin Body candidate accepted");

  auto generic = direct;
  generic.component_leaf = "SkeletalMeshComponent_2147473671";
  check(logic::score_appearance_candidate(generic) < 0,
        "generic non-customization component rejected");
  auto quinn = direct;
  quinn.mesh_full_name =
      "SkeletalMesh /Game/Characters/Mannequins/Meshes/SKM_Quinn.SKM_Quinn";
  check(logic::score_appearance_candidate(quinn) < 0, "Quinn body rejected");
  auto unrelated = skin;
  unrelated.related_to_pawn = false;
  check(logic::score_appearance_candidate(unrelated) < 0,
        "unrelated skin rejected");

  check(!logic::select_appearance_candidate({}),
        "empty bounded component set is immediately not ready");
  const std::vector<logic::AppearanceCandidate> bounded_candidates{quinn,
                                                                   direct};
  const auto selected = logic::select_appearance_candidate(bounded_candidates);
  check(selected && *selected == 1,
        "bounded component resolver selects valid Body");

  logic::AppearanceCandidate pawn_mesh{
      "SkeletalMeshComponent_2147470943",
      "SkeletalMeshComponent /Game/Map.Pawn.SkeletalMeshComponent_2147470943",
      "BP_jRPG_Character_World_C /Game/Map.Pawn",
      "SkeletalMesh /Game/Characters/Heros/Maelle/Customization/Skin/"
      "SK_Maelle_Esquie.SK_Maelle_Esquie",
      true,
      false,
      true,
      true,
  };
  check(logic::is_customization_skin_mesh(pawn_mesh.mesh_full_name, "Maelle"),
        "Pawn.Mesh customization skin accepted");
  check(logic::score_appearance_candidate(pawn_mesh) > 0,
        "local resolver accepts Pawn.Mesh dynamic component");
  const auto dynamic_only = logic::select_appearance_candidate({pawn_mesh});
  check(dynamic_only && *dynamic_only == 0,
        "remote resolver can select a dynamic Mesh property component");
  const auto body_first =
      logic::select_appearance_candidate({pawn_mesh, direct});
  check(body_first && *body_first == 1,
        "exact Body retains priority over dynamic Mesh property");
  auto face = pawn_mesh;
  face.mesh_full_name =
      "SkeletalMesh /Game/Characters/Heros/Maelle/Customization/Skin/FaceMesh";
  check(logic::score_appearance_candidate(face) < 0, "Face mesh rejected");
  auto hair = pawn_mesh;
  hair.mesh_full_name = "SkeletalMesh /Game/Characters/Hair/Heroes/Maelle/Hair";
  check(logic::score_appearance_candidate(hair) < 0, "Hair mesh rejected");
  auto placeholder = pawn_mesh;
  placeholder.mesh_full_name =
      "SkeletalMesh /Game/Characters/Heros/Maelle/SK_Maelle_Placeholder";
  check(logic::score_appearance_candidate(placeholder) < 0,
        "Placeholder mesh rejected");

  const proto::AppearanceState valid{1, "Lune", "Lune_Chic", "Lune_Face_01"};
  const proto::AppearanceState pending{1, "Unknown", "", "Sciel_Face_01"};
  const auto retained = logic::select_effective_appearance(valid, pending);
  check(retained && retained->character_id == "Lune" &&
            retained->customization_skin == valid.customization_skin,
        "pending Unknown does not replace last valid appearance");
  check(!logic::select_effective_appearance(std::nullopt, pending),
        "initial Unknown remains pending");

  check(logic::appearance_retry_delay_ms(1) == 500 &&
            logic::appearance_retry_delay_ms(2) == 500,
        "initial appearance retry delay");
  check(logic::appearance_retry_delay_ms(3) == 1000 &&
            logic::appearance_retry_delay_ms(5) == 1000,
        "middle appearance retry delay");
  check(logic::appearance_retry_delay_ms(6) == 2000 &&
            logic::appearance_retry_delay_ms(100) == 2000,
        "appearance retry delay backoff cap");

  using Decision = logic::AppearanceApplyDecision;
  check(logic::appearance_apply_decision(true, false, true, true, 1) ==
            Decision::retry,
        "remote appearance retries while Body is not ready");
  check(
      logic::appearance_apply_decision(true, true, true, true, 1) ==
          Decision::complete,
      "remote appearance completes when requested visual components are ready");
  check(logic::appearance_apply_decision(true, false, false, false, 10) ==
            Decision::fail_open,
        "remote appearance fails open after bounded retries");
  check(logic::appearance_apply_decision(false, false, false, false, 1) ==
            Decision::complete,
        "empty optional Probe meshes do not trigger retries");
}

auto test_runtime_correction_logic() -> void {
  const proto::AppearanceState known{7, "Maelle", "Maelle_Alicia",
                                     "Maelle_Face_02"};
  const proto::AppearanceState temporarily_empty{7, "Maelle", "", ""};
  const auto preserved =
      logic::select_effective_appearance(known, temporarily_empty);
  check(preserved && preserved->customization_skin == "Maelle_Alicia" &&
            preserved->customization_face == "Maelle_Face_02",
        "last-known-good customization survives an empty capture");
  const proto::AppearanceState switched{7, "Lune", "", ""};
  const auto switched_result = logic::select_effective_appearance(known, switched);
  check(switched_result && switched_result->character_id == "Lune" &&
            switched_result->customization_skin.empty(),
        "character switch does not inherit previous customization IDs");

  check(!logic::customization_capture_is_due(false, 500, 100),
        "clean customization does not capture");
  check(!logic::customization_capture_is_due(true, 99, 100),
        "customization debounce is honored");
  check(logic::customization_capture_is_due(true, 100, 100),
        "dirty customization captures at debounce deadline");

  check(!logic::should_run_legacy_visual_diagnostics("world_character", false),
        "world-character skips legacy appearance scans");
  check(logic::should_run_legacy_visual_diagnostics("world_character", true) &&
            logic::should_run_legacy_visual_diagnostics(
                "ai_companion_legacy", false),
        "legacy diagnostics remain explicitly available");
  check(logic::should_suppress_remote_companions("world_character", false),
        "remote world-character requests companion suppression");
  check(!logic::should_suppress_remote_companions("world_character", true) &&
            !logic::should_suppress_remote_companions("world_map", false),
        "companion suppression never targets local or world-map actors");

  check(std::fabs(logic::rotation_drift_degrees(179.0F, -179.0F) - 2.0F) <
            0.001F,
        "rotation drift follows the shortest angular path");
  for (int distance = 0; distance <= 10000; distance += 37) {
    check(std::fabs(logic::bounded_demo_offset(static_cast<float>(distance),
                                               250.0F)) <= 250.001F,
          "Probe demo offset stays inside configured radius");
  }
}

auto test_local_movement_state_classification() -> void {
  using Phase = logic::VerticalMovementPhase;
  check(logic::classify_vertical_movement(false, 600.0F) == Phase::ground,
        "MovementMode/IsFalling wins over vertical velocity on ground");
  check(logic::classify_vertical_movement(true, 600.0F) == Phase::ascend,
        "falling positive Z is ascent");
  check(logic::classify_vertical_movement(true, 25.0F) == Phase::apex,
        "falling low Z is apex");
  check(logic::classify_vertical_movement(true, -400.0F) == Phase::descend,
        "falling negative Z is descent");
  check(logic::vertical_movement_phase_name(Phase::ground) == "GROUND" &&
            logic::vertical_movement_phase_name(Phase::ascend) == "ASCEND" &&
            logic::vertical_movement_phase_name(Phase::apex) == "APEX" &&
            logic::vertical_movement_phase_name(Phase::descend) == "DESCEND",
        "movement phase log names");
  check(logic::jump_demo_movement_mode("GROUND") == 1 &&
            logic::jump_demo_movement_mode("ASCEND") == 3 &&
            logic::jump_demo_movement_mode("APEX") == 3 &&
            logic::jump_demo_movement_mode("DESCEND") == 3 &&
            logic::jump_demo_movement_mode("LAND") == 1,
        "Probe jump transitions are 1->3->1");
}

auto test_remote_asset_safety() -> void {
  using Source = logic::AssetResolutionSource;
  check(logic::asset_resolution_source(true, false, false) == Source::cache,
        "valid asset cache wins");
  check(logic::asset_resolution_source(false, true, false) ==
            Source::already_loaded,
        "StaticFindObject result wins before loader");
  check(logic::asset_resolution_source(false, false, true) == Source::loaded,
        "asset loader fallback is selected");
  check(logic::asset_resolution_source(false, false, false) == Source::failed,
        "failed asset lookup remains fail-open");
  check(logic::appearance_asset_has_drift("HairA", "HairB"),
        "hair drift requests reapply");
  check(!logic::appearance_asset_has_drift("HairA", "HairA"),
        "matching hair does not reapply");
  check(!logic::appearance_asset_has_drift("OutfitA", "OutfitA"),
        "outfit verification accepts observed requested asset");
  check(logic::should_reapply_visual_asset("OutfitA", "OutfitB", true),
        "outfit drift requests reapply when component is ready");
  check(logic::should_reapply_visual_asset("HairA", "HairB", true),
        "hair drift requests reapply when component is ready");
  check(!logic::should_reapply_visual_asset("OutfitA", "OutfitB", false),
        "missing component is resolved before drift reapply");
  check(!logic::should_write_remote_visual(false, true, true),
        "direct appearance write is disabled by default");
  check(logic::should_write_remote_visual(true, true, true),
        "unsafe appearance write requires explicit opt-in and ready inputs");
  check(logic::should_disable_remote_movement_tick(true) &&
            !logic::should_disable_remote_movement_tick(false),
        "network authority controls only the remote movement tick policy");
}
} // namespace

auto main() -> int {
  try {
    test_payloads();
    test_fragmented_frames();
    test_rejections();
    test_snapshot_interpolation();
    test_appearance_selection();
    test_runtime_correction_logic();
    test_local_movement_state_classification();
    test_remote_asset_safety();
    std::cout << "All protocol tests passed\n";
    return 0;
  } catch (const std::exception &exception) {
    std::cerr << "TEST FAILURE: " << exception.what() << '\n';
    return 1;
  }
}
