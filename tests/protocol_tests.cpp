#include <expedition_online/protocol.hpp>

#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace proto = expedition_online::protocol;

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
} // namespace

auto main() -> int
{
    try
    {
        test_payloads();
        test_fragmented_frames();
        test_rejections();
        std::cout << "All protocol tests passed\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "TEST FAILURE: " << exception.what() << '\n';
        return 1;
    }
}
