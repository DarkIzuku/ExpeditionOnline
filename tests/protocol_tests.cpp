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

namespace
{
auto check(bool condition, const std::string& message) -> void
{
    if (!condition) throw std::runtime_error(message);
}

auto test_payloads() -> void
{
    const proto::Hello hello{"Maelle", "client-0.1.0"};
    const auto decoded_hello = proto::decode_hello(proto::encode(hello));
    check(decoded_hello.player_name == hello.player_name, "Hello player_name");
    check(decoded_hello.client_build == hello.client_build, "Hello client_build");

    const proto::AppearanceState appearance{42, "BP_Maelle_C", "/Game/Body", "/Game/Hair"};
    const auto decoded_appearance = proto::decode_appearance_state(proto::encode(appearance));
    check(decoded_appearance.player_id == 42, "Appearance player_id");
    check(decoded_appearance.character_class == appearance.character_class, "Appearance character");
    check(decoded_appearance.outfit_mesh == appearance.outfit_mesh, "Appearance outfit");
    check(decoded_appearance.hair_mesh == appearance.hair_mesh, "Appearance hair");

    const proto::PlayerJoined joined{77, "Verso"};
    const auto decoded_joined = proto::decode_player_joined(proto::encode(joined));
    check(decoded_joined.player_id == joined.player_id, "PlayerJoined player_id");
    check(decoded_joined.player_name == joined.player_name, "PlayerJoined player_name");

    const proto::TransformSnapshot transform{9, 123456, 1.25F, -2.5F, 3.75F, 10.0F, 20.0F, 30.0F};
    const auto decoded_transform = proto::decode_transform_snapshot(proto::encode(transform));
    check(decoded_transform.player_id == transform.player_id, "Transform player_id");
    check(decoded_transform.timestamp_ms == transform.timestamp_ms, "Transform timestamp");
    check(std::fabs(decoded_transform.x - transform.x) < 0.0001F, "Transform x");
    check(std::fabs(decoded_transform.yaw - transform.yaw) < 0.0001F, "Transform yaw");
}

auto test_fragmented_frames() -> void
{
    const auto first = proto::encode_frame(proto::make_frame(proto::MessageType::zone_state,
                                                             100,
                                                             proto::ZoneState{8, "PersistentLevel-A"}));
    const auto second = proto::encode_frame(proto::make_frame(proto::MessageType::player_left,
                                                              101,
                                                              proto::PlayerLeft{8}));
    std::vector<std::uint8_t> stream = first;
    stream.insert(stream.end(), second.begin(), second.end());

    proto::FrameDecoder decoder;
    for (const auto byte : stream)
    {
        decoder.push(std::span<const std::uint8_t>(&byte, 1));
    }
    const auto frames = decoder.take_frames();
    check(frames.size() == 2, "fragmented decoder frame count");
    check(frames[0].sequence == 100, "first sequence");
    check(proto::decode_zone_state(frames[0].payload).zone == "PersistentLevel-A", "first payload");
    check(frames[1].sequence == 101, "second sequence");
    check(proto::decode_player_left(frames[1].payload).player_id == 8, "second payload");
    check(decoder.buffered_bytes() == 0, "decoder buffer drained");
}

auto test_rejections() -> void
{
    bool rejected{};
    try
    {
        (void)proto::decode_welcome(std::vector<std::uint8_t>{0, 1});
    }
    catch (const proto::ProtocolError&)
    {
        rejected = true;
    }
    check(rejected, "truncated payload rejection");

    rejected = false;
    try
    {
        proto::FrameDecoder decoder;
        std::vector<std::uint8_t> invalid(proto::kHeaderSize, 0);
        decoder.push(invalid);
    }
    catch (const proto::ProtocolError&)
    {
        rejected = true;
    }
    check(rejected, "invalid magic rejection");
}

auto snapshot(std::uint64_t timestamp, float x, float yaw = 0.0F) -> proto::TransformSnapshot
{
    return proto::TransformSnapshot{7, timestamp, x, x * 2.0F, x * 3.0F, 0.0F, yaw, 0.0F};
}

auto test_snapshot_interpolation() -> void
{
    std::deque<proto::TransformSnapshot> buffer;
    logic::insert_snapshot(buffer, snapshot(200, 10.0F, -170.0F));
    logic::insert_snapshot(buffer, snapshot(100, 0.0F, 170.0F));
    logic::insert_snapshot(buffer, snapshot(300, 20.0F));
    check(buffer.size() == 3, "snapshot buffer size");
    check(buffer[0].timestamp_ms == 100 && buffer[1].timestamp_ms == 200 && buffer[2].timestamp_ms == 300,
          "snapshot buffer ordering");

    logic::insert_snapshot(buffer, snapshot(200, 12.0F, -170.0F));
    check(buffer.size() == 3 && std::fabs(buffer[1].x - 12.0F) < 0.0001F,
          "duplicate snapshot replacement");

    const auto interpolated = logic::sample_snapshot(buffer, 150);
    check(interpolated && interpolated->interpolated, "interpolation sample available");
    check(std::fabs(interpolated->transform.x - 6.0F) < 0.0001F, "position lerp");
    check(std::fabs(std::fabs(interpolated->transform.yaw) - 180.0F) < 0.0001F,
          "yaw shortest path interpolation");

    const auto beyond_latest = logic::sample_snapshot(buffer, 999);
    check(beyond_latest && !beyond_latest->interpolated &&
              beyond_latest->transform.timestamp_ms == buffer.back().timestamp_ms,
          "no indefinite snapshot extrapolation");

    check(!logic::snapshot_stream_is_stale(1750, 1000), "snapshot not stale at threshold");
    check(logic::snapshot_stream_is_stale(1751, 1000), "snapshot stale after threshold");
}

auto test_appearance_selection() -> void
{
    logic::AppearanceCandidate direct{
        "Body",
        "SkeletalMeshComponent /Game/Map.Pawn.Body",
        "BP_jRPG_Character_World_C /Game/Map.Pawn",
        "SkeletalMesh /Game/Characters/Heros/Lune/Outfits/SK_Lune_Chic_Skin.SK_Lune_Chic_Skin",
        true,
        false,
        true,
    };
    check(logic::score_appearance_candidate(direct) > 0, "direct pawn Body candidate");

    auto skin = direct;
    skin.owner_full_name = "BP_CharacterSkin_ScielOutfit_Chic_C /Game/Map.Skin";
    skin.mesh_full_name = "SkeletalMesh /Game/Characters/Heros/Sciel/Outfits/SK_Sciel_Chic_Skin.SK_Sciel_Chic_Skin";
    skin.directly_owned_by_pawn = false;
    skin.owner_is_character_skin = true;
    skin.related_to_pawn = true;
    check(logic::score_appearance_candidate(skin) > 0,
          "related character skin Body candidate accepted");

    auto generic = direct;
    generic.component_leaf = "SkeletalMeshComponent_2147473671";
    check(logic::score_appearance_candidate(generic) < 0, "generic component rejected");
    auto quinn = direct;
    quinn.mesh_full_name = "SkeletalMesh /Game/Characters/Mannequins/Meshes/SKM_Quinn.SKM_Quinn";
    check(logic::score_appearance_candidate(quinn) < 0, "Quinn body rejected");
    auto unrelated = skin;
    unrelated.related_to_pawn = false;
    check(logic::score_appearance_candidate(unrelated) < 0, "unrelated skin rejected");

    const proto::AppearanceState valid{1, "Lune", direct.mesh_full_name, "/Game/Characters/Hair/LuneHair"};
    const proto::AppearanceState pending{1, "Unknown", "", "/Game/Characters/Hair/ScielHair"};
    const auto retained = logic::select_effective_appearance(valid, pending);
    check(retained && retained->character_class == "Lune" && retained->outfit_mesh == valid.outfit_mesh,
          "pending Unknown does not replace last valid appearance");
    check(!logic::select_effective_appearance(std::nullopt, pending),
          "initial Unknown remains pending");
}
} // namespace

auto main() -> int
{
    try
    {
        test_payloads();
        test_fragmented_frames();
        test_rejections();
        test_snapshot_interpolation();
        test_appearance_selection();
        std::cout << "All protocol tests passed\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "TEST FAILURE: " << exception.what() << '\n';
        return 1;
    }
}
