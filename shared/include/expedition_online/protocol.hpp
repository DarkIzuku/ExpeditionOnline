#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace expedition_online::protocol {
inline constexpr std::array<std::uint8_t, 4> kMagic{'E', 'X', 'O', 'N'};
inline constexpr std::uint16_t kProtocolVersion = 5;
inline constexpr std::size_t kHeaderSize = 20;
inline constexpr std::uint32_t kMaxPayloadSize = 1024U * 1024U;
inline constexpr std::size_t kMaxStringSize = 16U * 1024U;

enum class MessageType : std::uint16_t {
  hello = 1,
  welcome = 2,
  zone_state = 3,
  appearance_state = 4,
  transform_snapshot = 5,
  player_left = 6,
  error = 7,
  ping = 8,
  pong = 9,
  player_joined = 10,
  movement_state = 11,
  jump_event = 12,
  player_context_state = 13,
  player_locomotion_state = 14,
};

enum class PlayerContext : std::uint8_t {
  unavailable = 0,
  exploration = 1,
  world_map = 2,
  combat = 3,
};

struct Hello {
  std::string player_name;
  std::string client_build;
};

struct Welcome {
  std::uint64_t player_id{};
};

struct ZoneState {
  std::uint64_t player_id{};
  std::string zone;
};

struct AppearanceState {
  std::uint64_t player_id{};
  std::string character_id;
  std::string customization_skin;
  std::string customization_face;
};

struct PlayerContextState {
  std::uint64_t player_id{};
  PlayerContext context{PlayerContext::unavailable};
};

struct PlayerJoined {
  std::uint64_t player_id{};
  std::string player_name;
};

struct TransformSnapshot {
  std::uint64_t player_id{};
  std::uint64_t timestamp_ms{};
  float x{};
  float y{};
  float z{};
  float pitch{};
  float yaw{};
  float roll{};
};

struct MovementState {
  std::uint64_t player_id{};
  std::uint8_t movement_mode{};
  std::uint8_t custom_movement_mode{};
};

struct PlayerLocomotionState {
  std::uint64_t player_id{};
  std::uint8_t movement_mode{};
  std::uint8_t locomotion_state{};
  std::uint8_t gait{};
  std::uint8_t stance{};
  bool sprinting{};
  bool crouching{};
  bool aiming{};
  float aim_pitch{};
};

struct JumpEvent {
  std::uint64_t player_id{};
  std::uint64_t sequence{};
};

struct PlayerLeft {
  std::uint64_t player_id{};
};

struct ErrorMessage {
  std::uint16_t code{};
  std::string message;
};

struct Frame {
  std::uint16_t version{kProtocolVersion};
  MessageType type{};
  std::uint64_t sequence{};
  std::vector<std::uint8_t> payload;
};

class ProtocolError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

auto encode(const Hello &value) -> std::vector<std::uint8_t>;
auto encode(const Welcome &value) -> std::vector<std::uint8_t>;
auto encode(const ZoneState &value) -> std::vector<std::uint8_t>;
auto encode(const AppearanceState &value) -> std::vector<std::uint8_t>;
auto encode(const PlayerContextState &value) -> std::vector<std::uint8_t>;
auto encode(const PlayerJoined &value) -> std::vector<std::uint8_t>;
auto encode(const TransformSnapshot &value) -> std::vector<std::uint8_t>;
auto encode(const MovementState &value) -> std::vector<std::uint8_t>;
auto encode(const PlayerLocomotionState &value) -> std::vector<std::uint8_t>;
auto encode(const JumpEvent &value) -> std::vector<std::uint8_t>;
auto encode(const PlayerLeft &value) -> std::vector<std::uint8_t>;
auto encode(const ErrorMessage &value) -> std::vector<std::uint8_t>;

auto decode_hello(std::span<const std::uint8_t> bytes) -> Hello;
auto decode_welcome(std::span<const std::uint8_t> bytes) -> Welcome;
auto decode_zone_state(std::span<const std::uint8_t> bytes) -> ZoneState;
auto decode_appearance_state(std::span<const std::uint8_t> bytes)
    -> AppearanceState;
auto decode_player_context_state(std::span<const std::uint8_t> bytes)
    -> PlayerContextState;
auto decode_player_joined(std::span<const std::uint8_t> bytes) -> PlayerJoined;
auto decode_transform_snapshot(std::span<const std::uint8_t> bytes)
    -> TransformSnapshot;
auto decode_movement_state(std::span<const std::uint8_t> bytes)
    -> MovementState;
auto decode_player_locomotion_state(std::span<const std::uint8_t> bytes)
    -> PlayerLocomotionState;
auto decode_jump_event(std::span<const std::uint8_t> bytes) -> JumpEvent;
auto decode_player_left(std::span<const std::uint8_t> bytes) -> PlayerLeft;
auto decode_error(std::span<const std::uint8_t> bytes) -> ErrorMessage;

auto encode_frame(const Frame &frame) -> std::vector<std::uint8_t>;

template <typename T>
auto make_frame(MessageType type, std::uint64_t sequence, const T &value)
    -> Frame {
  return Frame{kProtocolVersion, type, sequence, encode(value)};
}

class FrameDecoder {
public:
  auto push(std::span<const std::uint8_t> bytes) -> void;
  auto take_frames() -> std::vector<Frame>;
  auto buffered_bytes() const noexcept -> std::size_t;

private:
  std::vector<std::uint8_t> buffer_;
  std::vector<Frame> ready_;
};

auto message_type_name(MessageType type) -> const char *;
auto is_known_message_type(MessageType type) noexcept -> bool;
auto is_empty_payload_message(MessageType type) noexcept -> bool;
auto is_valid_player_context(PlayerContext context) noexcept -> bool;
auto player_context_name(PlayerContext context) noexcept -> const char *;
} // namespace expedition_online::protocol
