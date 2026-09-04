#include <expedition_online/protocol.hpp>

#include <algorithm>
#include <bit>
#include <cstring>
#include <limits>
#include <utility>

namespace expedition_online::protocol {
namespace {
class Writer {
public:
  auto u8(std::uint8_t value) -> void { data_.push_back(value); }

  auto u16(std::uint16_t value) -> void {
    data_.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xffU));
    data_.push_back(static_cast<std::uint8_t>(value & 0xffU));
  }

  auto u32(std::uint32_t value) -> void {
    for (int shift = 24; shift >= 0; shift -= 8) {
      data_.push_back(static_cast<std::uint8_t>((value >> shift) & 0xffU));
    }
  }

  auto u64(std::uint64_t value) -> void {
    for (int shift = 56; shift >= 0; shift -= 8) {
      data_.push_back(static_cast<std::uint8_t>((value >> shift) & 0xffU));
    }
  }

  auto f32(float value) -> void { u32(std::bit_cast<std::uint32_t>(value)); }

  auto string(const std::string &value) -> void {
    if (value.size() > kMaxStringSize ||
        value.size() > std::numeric_limits<std::uint16_t>::max()) {
      throw ProtocolError("string exceeds protocol limit");
    }
    u16(static_cast<std::uint16_t>(value.size()));
    data_.insert(data_.end(), value.begin(), value.end());
  }

  auto bytes(std::span<const std::uint8_t> value) -> void {
    data_.insert(data_.end(), value.begin(), value.end());
  }

  auto take() && -> std::vector<std::uint8_t> { return std::move(data_); }

private:
  std::vector<std::uint8_t> data_;
};

class Reader {
public:
  explicit Reader(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}

  auto u8() -> std::uint8_t {
    require(1);
    return bytes_[offset_++];
  }

  auto u16() -> std::uint16_t {
    require(2);
    const auto value = static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(bytes_[offset_]) << 8U) |
        static_cast<std::uint16_t>(bytes_[offset_ + 1]));
    offset_ += 2;
    return value;
  }

  auto u32() -> std::uint32_t {
    require(4);
    std::uint32_t value{};
    for (int index = 0; index < 4; ++index) {
      value = (value << 8U) | bytes_[offset_ + index];
    }
    offset_ += 4;
    return value;
  }

  auto u64() -> std::uint64_t {
    require(8);
    std::uint64_t value{};
    for (int index = 0; index < 8; ++index) {
      value = (value << 8U) | bytes_[offset_ + index];
    }
    offset_ += 8;
    return value;
  }

  auto f32() -> float { return std::bit_cast<float>(u32()); }

  auto string() -> std::string {
    const auto size = static_cast<std::size_t>(u16());
    if (size > kMaxStringSize) {
      throw ProtocolError("string exceeds protocol limit");
    }
    require(size);
    std::string value(reinterpret_cast<const char *>(bytes_.data() + offset_),
                      size);
    offset_ += size;
    return value;
  }

  auto finish() const -> void {
    if (offset_ != bytes_.size()) {
      throw ProtocolError("unexpected trailing payload bytes");
    }
  }

private:
  auto require(std::size_t size) const -> void {
    if (size > bytes_.size() - offset_) {
      throw ProtocolError("truncated protocol payload");
    }
  }

  std::span<const std::uint8_t> bytes_;
  std::size_t offset_{};
};

auto read_u16_at(std::span<const std::uint8_t> bytes, std::size_t offset)
    -> std::uint16_t {
  return static_cast<std::uint16_t>(
      (static_cast<std::uint16_t>(bytes[offset]) << 8U) | bytes[offset + 1]);
}

auto read_u32_at(std::span<const std::uint8_t> bytes, std::size_t offset)
    -> std::uint32_t {
  std::uint32_t value{};
  for (std::size_t index = 0; index < 4; ++index) {
    value = (value << 8U) | bytes[offset + index];
  }
  return value;
}

auto read_u64_at(std::span<const std::uint8_t> bytes, std::size_t offset)
    -> std::uint64_t {
  std::uint64_t value{};
  for (std::size_t index = 0; index < 8; ++index) {
    value = (value << 8U) | bytes[offset + index];
  }
  return value;
}
} // namespace

auto encode(const Hello &value) -> std::vector<std::uint8_t> {
  Writer writer;
  writer.string(value.player_name);
  writer.string(value.client_build);
  return std::move(writer).take();
}

auto encode(const Welcome &value) -> std::vector<std::uint8_t> {
  Writer writer;
  writer.u64(value.player_id);
  return std::move(writer).take();
}

auto encode(const ZoneState &value) -> std::vector<std::uint8_t> {
  Writer writer;
  writer.u64(value.player_id);
  writer.string(value.zone);
  return std::move(writer).take();
}

auto encode(const AppearanceState &value) -> std::vector<std::uint8_t> {
  Writer writer;
  writer.u64(value.player_id);
  writer.string(value.character_id);
  writer.string(value.customization_skin);
  writer.string(value.customization_face);
  return std::move(writer).take();
}

auto encode(const PlayerContextState &value) -> std::vector<std::uint8_t> {
  Writer writer;
  writer.u64(value.player_id);
  writer.u8(static_cast<std::uint8_t>(value.context));
  return std::move(writer).take();
}

auto encode(const PlayerJoined &value) -> std::vector<std::uint8_t> {
  Writer writer;
  writer.u64(value.player_id);
  writer.string(value.player_name);
  return std::move(writer).take();
}

auto encode(const TransformSnapshot &value) -> std::vector<std::uint8_t> {
  Writer writer;
  writer.u64(value.player_id);
  writer.u64(value.timestamp_ms);
  writer.f32(value.x);
  writer.f32(value.y);
  writer.f32(value.z);
  writer.f32(value.pitch);
  writer.f32(value.yaw);
  writer.f32(value.roll);
  return std::move(writer).take();
}

auto encode(const MovementState &value) -> std::vector<std::uint8_t> {
  Writer writer;
  writer.u64(value.player_id);
  writer.u8(value.movement_mode);
  writer.u8(value.custom_movement_mode);
  return std::move(writer).take();
}

auto encode(const PlayerLocomotionState &value) -> std::vector<std::uint8_t> {
  Writer writer;
  writer.u64(value.player_id);
  writer.u8(value.movement_mode);
  writer.u8(value.locomotion_state);
  writer.u8(value.gait);
  writer.u8(value.stance);
  std::uint8_t flags{};
  if (value.sprinting)
    flags |= 0x01U;
  if (value.crouching)
    flags |= 0x02U;
  if (value.aiming)
    flags |= 0x04U;
  writer.u8(flags);
  writer.f32(value.aim_pitch);
  return std::move(writer).take();
}

auto encode(const JumpEvent &value) -> std::vector<std::uint8_t> {
  Writer writer;
  writer.u64(value.player_id);
  writer.u64(value.sequence);
  return std::move(writer).take();
}

auto encode(const PlayerLeft &value) -> std::vector<std::uint8_t> {
  Writer writer;
  writer.u64(value.player_id);
  return std::move(writer).take();
}

auto encode(const ErrorMessage &value) -> std::vector<std::uint8_t> {
  Writer writer;
  writer.u16(value.code);
  writer.string(value.message);
  return std::move(writer).take();
}

auto decode_hello(std::span<const std::uint8_t> bytes) -> Hello {
  Reader reader(bytes);
  Hello value{reader.string(), reader.string()};
  reader.finish();
  return value;
}

auto decode_welcome(std::span<const std::uint8_t> bytes) -> Welcome {
  Reader reader(bytes);
  Welcome value{reader.u64()};
  reader.finish();
  return value;
}

auto decode_zone_state(std::span<const std::uint8_t> bytes) -> ZoneState {
  Reader reader(bytes);
  ZoneState value{reader.u64(), reader.string()};
  reader.finish();
  return value;
}

auto decode_appearance_state(std::span<const std::uint8_t> bytes)
    -> AppearanceState {
  Reader reader(bytes);
  AppearanceState value{reader.u64(), reader.string(), reader.string(),
                        reader.string()};
  reader.finish();
  return value;
}

auto decode_player_context_state(std::span<const std::uint8_t> bytes)
    -> PlayerContextState {
  Reader reader(bytes);
  PlayerContextState value{reader.u64(),
                           static_cast<PlayerContext>(reader.u8())};
  reader.finish();
  return value;
}

auto decode_player_joined(std::span<const std::uint8_t> bytes) -> PlayerJoined {
  Reader reader(bytes);
  PlayerJoined value{reader.u64(), reader.string()};
  reader.finish();
  return value;
}

auto decode_transform_snapshot(std::span<const std::uint8_t> bytes)
    -> TransformSnapshot {
  Reader reader(bytes);
  TransformSnapshot value;
  value.player_id = reader.u64();
  value.timestamp_ms = reader.u64();
  value.x = reader.f32();
  value.y = reader.f32();
  value.z = reader.f32();
  value.pitch = reader.f32();
  value.yaw = reader.f32();
  value.roll = reader.f32();
  reader.finish();
  return value;
}

auto decode_movement_state(std::span<const std::uint8_t> bytes)
    -> MovementState {
  Reader reader(bytes);
  MovementState value{reader.u64(), reader.u8(), reader.u8()};
  reader.finish();
  return value;
}

auto decode_player_locomotion_state(std::span<const std::uint8_t> bytes)
    -> PlayerLocomotionState {
  Reader reader(bytes);
  PlayerLocomotionState value;
  value.player_id = reader.u64();
  value.movement_mode = reader.u8();
  value.locomotion_state = reader.u8();
  value.gait = reader.u8();
  value.stance = reader.u8();
  const auto flags = reader.u8();
  value.sprinting = (flags & 0x01U) != 0;
  value.crouching = (flags & 0x02U) != 0;
  value.aiming = (flags & 0x04U) != 0;
  value.aim_pitch = reader.f32();
  reader.finish();
  return value;
}

auto decode_jump_event(std::span<const std::uint8_t> bytes) -> JumpEvent {
  Reader reader(bytes);
  JumpEvent value{reader.u64(), reader.u64()};
  reader.finish();
  return value;
}

auto decode_player_left(std::span<const std::uint8_t> bytes) -> PlayerLeft {
  Reader reader(bytes);
  PlayerLeft value{reader.u64()};
  reader.finish();
  return value;
}

auto decode_error(std::span<const std::uint8_t> bytes) -> ErrorMessage {
  Reader reader(bytes);
  ErrorMessage value{reader.u16(), reader.string()};
  reader.finish();
  return value;
}

auto encode_frame(const Frame &frame) -> std::vector<std::uint8_t> {
  if (frame.payload.size() > kMaxPayloadSize) {
    throw ProtocolError("frame payload exceeds protocol limit");
  }

  Writer writer;
  writer.bytes(kMagic);
  writer.u16(frame.version);
  writer.u16(static_cast<std::uint16_t>(frame.type));
  writer.u32(static_cast<std::uint32_t>(frame.payload.size()));
  writer.u64(frame.sequence);
  writer.bytes(frame.payload);
  return std::move(writer).take();
}

auto FrameDecoder::push(std::span<const std::uint8_t> bytes) -> void {
  buffer_.insert(buffer_.end(), bytes.begin(), bytes.end());

  std::size_t consumed{};
  while (buffer_.size() - consumed >= kHeaderSize) {
    const std::span<const std::uint8_t> remaining(buffer_.data() + consumed,
                                                  buffer_.size() - consumed);
    if (!std::equal(kMagic.begin(), kMagic.end(), remaining.begin())) {
      throw ProtocolError("invalid frame magic");
    }

    const auto payload_size = read_u32_at(remaining, 8);
    if (payload_size > kMaxPayloadSize) {
      throw ProtocolError("frame payload exceeds protocol limit");
    }
    const auto frame_size =
        kHeaderSize + static_cast<std::size_t>(payload_size);
    if (remaining.size() < frame_size) {
      break;
    }

    Frame frame;
    frame.version = read_u16_at(remaining, 4);
    frame.type = static_cast<MessageType>(read_u16_at(remaining, 6));
    frame.sequence = read_u64_at(remaining, 12);
    frame.payload.assign(
        remaining.begin() + static_cast<std::ptrdiff_t>(kHeaderSize),
        remaining.begin() + static_cast<std::ptrdiff_t>(frame_size));
    ready_.push_back(std::move(frame));
    consumed += frame_size;
  }

  if (consumed != 0) {
    buffer_.erase(buffer_.begin(),
                  buffer_.begin() + static_cast<std::ptrdiff_t>(consumed));
  }
}

auto FrameDecoder::take_frames() -> std::vector<Frame> {
  auto frames = std::move(ready_);
  ready_.clear();
  return frames;
}

auto FrameDecoder::buffered_bytes() const noexcept -> std::size_t {
  return buffer_.size();
}

auto message_type_name(MessageType type) -> const char * {
  switch (type) {
  case MessageType::hello:
    return "Hello";
  case MessageType::welcome:
    return "Welcome";
  case MessageType::zone_state:
    return "ZoneState";
  case MessageType::appearance_state:
    return "AppearanceState";
  case MessageType::transform_snapshot:
    return "TransformSnapshot";
  case MessageType::player_left:
    return "PlayerLeft";
  case MessageType::error:
    return "Error";
  case MessageType::ping:
    return "Ping";
  case MessageType::pong:
    return "Pong";
  case MessageType::player_joined:
    return "PlayerJoined";
  case MessageType::movement_state:
    return "MovementState";
  case MessageType::jump_event:
    return "JumpEvent";
  case MessageType::player_context_state:
    return "PlayerContextState";
  case MessageType::player_locomotion_state:
    return "PlayerLocomotionState";
  default:
    return "Unknown";
  }
}

auto is_known_message_type(MessageType type) noexcept -> bool {
  switch (type) {
  case MessageType::hello:
  case MessageType::welcome:
  case MessageType::zone_state:
  case MessageType::appearance_state:
  case MessageType::transform_snapshot:
  case MessageType::player_left:
  case MessageType::error:
  case MessageType::ping:
  case MessageType::pong:
  case MessageType::player_joined:
    return true;
  case MessageType::movement_state:
    return true;
  case MessageType::jump_event:
    return true;
  case MessageType::player_context_state:
    return true;
  case MessageType::player_locomotion_state:
    return true;
  default:
    return false;
  }
}

auto is_empty_payload_message(MessageType type) noexcept -> bool {
  return type == MessageType::ping || type == MessageType::pong;
}

auto is_valid_player_context(PlayerContext context) noexcept -> bool {
  return context == PlayerContext::unavailable ||
         context == PlayerContext::exploration ||
         context == PlayerContext::world_map ||
         context == PlayerContext::combat;
}

auto player_context_name(PlayerContext context) noexcept -> const char * {
  switch (context) {
  case PlayerContext::unavailable:
    return "unavailable";
  case PlayerContext::exploration:
    return "exploration";
  case PlayerContext::world_map:
    return "world_map";
  case PlayerContext::combat:
    return "combat";
  }
  return "invalid";
}
} // namespace expedition_online::protocol
