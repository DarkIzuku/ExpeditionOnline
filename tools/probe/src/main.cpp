#include <expedition_online/build_info.hpp>
#include <expedition_online/client_logic.hpp>
#include <expedition_online/protocol.hpp>
#include <expedition_online/socket.hpp>

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace eo = expedition_online;
namespace proto = expedition_online::protocol;
namespace logic = expedition_online::client_logic;

namespace {
struct Options {
  std::string host{"127.0.0.1"};
  std::uint16_t port{7777};
  std::string name{"Probe"};
  std::string zone{"ProbeZone"};
  std::string character{"ProbeCharacter"};
  std::string customization_skin;
  std::string customization_face;
  proto::PlayerContext context{proto::PlayerContext::exploration};
  int duration_seconds{10};
  float x{};
  float y{};
  float z{};
  float yaw{};
  float radius{300.0F};
  float angular_speed{1.0F};
  int snapshot_hz{4};
  bool duration_explicit{};
  bool movement_demo{};
  bool jump_demo{};
  bool idle_demo{};
  bool appearance_test{};
  bool teleport_demo{};
  bool full_exploration_demo{};
  bool crouch_demo{};
  bool show_help{};
};

auto print_usage() -> void {
  std::cout
      << "ExpeditionOnlineProbe - simulated exploration client\n\n"
      << "Usage:\n"
      << "  ExpeditionOnlineProbe.exe [options]\n\n"
      << "Options:\n"
      << "  --host <address>    Server address (default: 127.0.0.1)\n"
      << "  --port <port>       Server TCP port (default: 7777)\n"
      << "  --name <name>       Simulated player name (default: Probe)\n"
      << "  --zone <zone>       Exact LOCAL_ZONE value (default: ProbeZone)\n"
      << "  --char <id>          Literal vanilla character ID (default: "
         "ProbeCharacter)\n"
      << "  --customization-skin <id>  Vanilla customization skin ID\n"
      << "  --customization-face <id>  Vanilla customization face ID\n"
      << "  --context <value>   unavailable|exploration|world_map|combat\n"
      << "  --duration <sec>    Test duration in seconds (default: 10)\n"
      << "  --x <float>         Circle center X from LOCAL_TRANSFORM (default: "
         "0)\n"
      << "  --y <float>         Circle center Y from LOCAL_TRANSFORM (default: "
         "0)\n"
      << "  --z <float>         Fixed Z from LOCAL_TRANSFORM (default: 0)\n"
      << "  --yaw <float>       Fixed yaw in degrees (default: 0)\n"
      << "  --radius <float>    Circular movement radius (default: 300)\n"
      << "  --angular-speed <float>  Circle speed in radians/sec (default: 1)\n"
      << "  --snapshot-hz <rate> Snapshot send rate in Hz (default: 4)\n"
      << "  --movement-demo      Idle 5s, walk 10s, run 10s, stop 5s\n"
      << "  --jump-demo          Ground, ascend, apex, descend and land "
         "trajectory\n"
      << "  --idle-demo          Hold one exact transform for 15 seconds "
         "unless "
         "--duration is set\n"
      << "  --appearance-test    Hold the literal customization IDs for 60 "
         "seconds "
         "unless --duration is set\n"
      << "  --teleport-demo      Emit one transform beyond the teleport "
         "threshold\n"
      << "  --full-exploration-demo  Idle/walk/run/sprint/stop/jump/stop\n"
      << "  --crouch-demo        Add a standing/crouching/standing transition\n"
      << "  --help              Show this help\n";
}

auto parse_options(int argc, char **argv) -> Options {
  Options options;
  for (int index = 1; index < argc; ++index) {
    const std::string argument = argv[index];
    const auto next_value = [&]() -> std::string {
      if (index + 1 >= argc)
        throw std::runtime_error("missing value for argument: " + argument);
      return argv[++index];
    };
    if (argument == "--help" || argument == "-h")
      options.show_help = true;
    else if (argument == "--host")
      options.host = next_value();
    else if (argument == "--port") {
      const auto port = std::stoi(next_value());
      if (port < 1 || port > 65535)
        throw std::runtime_error("port must be in 1..65535");
      options.port = static_cast<std::uint16_t>(port);
    } else if (argument == "--name")
      options.name = next_value();
    else if (argument == "--zone")
      options.zone = next_value();
    else if (argument == "--char" || argument == "--character")
      options.character = next_value();
    else if (argument == "--customization-skin" || argument == "--outfit")
      options.customization_skin = next_value();
    else if (argument == "--customization-face" || argument == "--hair")
      options.customization_face = next_value();
    else if (argument == "--context") {
      const auto value = next_value();
      if (value == "unavailable" || value == "0")
        options.context = proto::PlayerContext::unavailable;
      else if (value == "exploration" || value == "1")
        options.context = proto::PlayerContext::exploration;
      else if (value == "world_map" || value == "world-map" || value == "2")
        options.context = proto::PlayerContext::world_map;
      else if (value == "combat" || value == "3")
        options.context = proto::PlayerContext::combat;
      else
        throw std::runtime_error("invalid context: " + value);
    } else if (argument == "--duration") {
      options.duration_seconds = std::stoi(next_value());
      options.duration_explicit = true;
    } else if (argument == "--x")
      options.x = std::stof(next_value());
    else if (argument == "--y")
      options.y = std::stof(next_value());
    else if (argument == "--z")
      options.z = std::stof(next_value());
    else if (argument == "--yaw")
      options.yaw = std::stof(next_value());
    else if (argument == "--radius")
      options.radius = std::stof(next_value());
    else if (argument == "--angular-speed")
      options.angular_speed = std::stof(next_value());
    else if (argument == "--snapshot-hz")
      options.snapshot_hz = std::stoi(next_value());
    else if (argument == "--movement-demo")
      options.movement_demo = true;
    else if (argument == "--jump-demo")
      options.jump_demo = true;
    else if (argument == "--idle-demo")
      options.idle_demo = true;
    else if (argument == "--appearance-test")
      options.appearance_test = true;
    else if (argument == "--teleport-demo")
      options.teleport_demo = true;
    else if (argument == "--full-exploration-demo")
      options.full_exploration_demo = true;
    else if (argument == "--crouch-demo")
      options.crouch_demo = true;
    else
      throw std::runtime_error("unknown or incomplete argument: " + argument);
  }
  if (options.duration_seconds < 1)
    throw std::runtime_error("duration must be at least 1 second");
  if (!std::isfinite(options.x) || !std::isfinite(options.y) ||
      !std::isfinite(options.z) || !std::isfinite(options.yaw) ||
      !std::isfinite(options.radius) || !std::isfinite(options.angular_speed)) {
    throw std::runtime_error("position, yaw and radius must be finite numbers");
  }
  if (options.radius < 0.0F)
    throw std::runtime_error("radius must be zero or greater");
  if (options.snapshot_hz < 1 || options.snapshot_hz > 60) {
    throw std::runtime_error("snapshot-hz must be in 1..60");
  }
  if (options.movement_demo && !options.duration_explicit)
    options.duration_seconds = 30;
  if (options.jump_demo && !options.movement_demo && !options.duration_explicit)
    options.duration_seconds = 10;
  if (options.idle_demo && !options.duration_explicit)
    options.duration_seconds = 15;
  if (options.idle_demo) {
    options.radius = 0.0F;
    options.angular_speed = 0.0F;
  }
  if (options.appearance_test && !options.duration_explicit)
    options.duration_seconds = 60;
  if (options.full_exploration_demo && !options.duration_explicit)
    options.duration_seconds = 30;
  return options;
}

struct DemoTransform {
  float x{};
  float y{};
  float z{};
  float speed{};
  float velocity_z{};
  std::string movement_phase;
  std::string jump_phase;
};

auto demo_transform(const Options &options, float elapsed) -> DemoTransform {
  DemoTransform result{options.x, options.y, options.z, 0.0F,
                       0.0F,      "CIRCLE",  "GROUND"};
  if (options.full_exploration_demo) {
    float distance{};
    if (elapsed < 3.0F) {
      result.movement_phase = "IDLE";
    } else if (elapsed < 8.0F) {
      result.movement_phase = "WALK";
      result.speed = 180.0F;
      distance = (elapsed - 3.0F) * result.speed;
    } else if (elapsed < 13.0F) {
      result.movement_phase = "RUN";
      result.speed = 450.0F;
      distance = 900.0F + (elapsed - 8.0F) * result.speed;
    } else if (elapsed < 18.0F) {
      result.movement_phase = "SPRINT";
      result.speed = 700.0F;
      distance = 3150.0F + (elapsed - 13.0F) * result.speed;
    } else {
      result.movement_phase = "STOP";
      distance = 6650.0F;
    }
    const auto yaw_radians = options.yaw * 3.14159265358979323846F / 180.0F;
    result.x = options.x + std::cos(yaw_radians) * distance;
    result.y = options.y + std::sin(yaw_radians) * distance;
  } else if (options.movement_demo) {
    float distance{};
    if (elapsed < 5.0F)
      result.movement_phase = "IDLE";
    else if (elapsed < 15.0F) {
      result.movement_phase = "WALK";
      result.speed = 180.0F;
      distance = (elapsed - 5.0F) * result.speed;
    } else if (elapsed < 25.0F) {
      result.movement_phase = "RUN";
      result.speed = 500.0F;
      distance = 1800.0F + (elapsed - 15.0F) * result.speed;
    } else {
      result.movement_phase = "STOP";
      distance = 6800.0F;
    }
    const auto yaw_radians = options.yaw * 3.14159265358979323846F / 180.0F;
    result.x = options.x + std::cos(yaw_radians) * distance;
    result.y = options.y + std::sin(yaw_radians) * distance;
  } else if (!options.jump_demo) {
    const auto angle = elapsed * options.angular_speed;
    result.x = options.x + std::cos(angle) * options.radius;
    result.y = options.y + std::sin(angle) * options.radius;
    result.speed = std::abs(options.angular_speed * options.radius);
  } else
    result.movement_phase = "IDLE";

  if (options.jump_demo || options.full_exploration_demo) {
    const auto jump_start = options.full_exploration_demo ? 22.0F : 5.0F;
    constexpr float launch_velocity = 600.0F;
    constexpr float gravity = -980.0F;
    const auto jump_time = elapsed - jump_start;
    const auto flight_duration = -2.0F * launch_velocity / gravity;
    if (jump_time < 0.0F) {
      result.jump_phase = "GROUND";
    } else if (jump_time < flight_duration) {
      result.velocity_z = launch_velocity + gravity * jump_time;
      result.z = options.z + launch_velocity * jump_time +
                 0.5F * gravity * jump_time * jump_time;
      if (result.velocity_z > 150.0F)
        result.jump_phase = "ASCEND";
      else if (result.velocity_z < -150.0F)
        result.jump_phase = "DESCEND";
      else
        result.jump_phase = "APEX";
    } else {
      result.jump_phase = "LAND";
      result.z = options.z;
    }
  }
  return result;
}

auto send_frame(eo::net::SocketHandle socket, const proto::Frame &frame)
    -> void {
  const auto bytes = proto::encode_frame(frame);
  std::string error;
  if (!eo::net::send_all(socket, bytes, error))
    throw std::runtime_error(error);
}

auto show_frame(const proto::Frame &frame) -> void {
  std::cout << "RECV " << proto::message_type_name(frame.type)
            << " seq=" << frame.sequence;
  switch (frame.type) {
  case proto::MessageType::welcome:
    std::cout << " player=" << proto::decode_welcome(frame.payload).player_id;
    break;
  case proto::MessageType::zone_state: {
    const auto value = proto::decode_zone_state(frame.payload);
    std::cout << " player=" << value.player_id << " zone=" << value.zone;
    break;
  }
  case proto::MessageType::player_joined: {
    const auto value = proto::decode_player_joined(frame.payload);
    std::cout << " player=" << value.player_id << " name=" << value.player_name;
    break;
  }
  case proto::MessageType::appearance_state: {
    const auto value = proto::decode_appearance_state(frame.payload);
    std::cout << " player=" << value.player_id
              << " character_id=" << value.character_id
              << " customization_skin=" << value.customization_skin
              << " customization_face=" << value.customization_face;
    break;
  }
  case proto::MessageType::player_context_state: {
    const auto value = proto::decode_player_context_state(frame.payload);
    std::cout << " player=" << value.player_id
              << " context=" << proto::player_context_name(value.context);
    break;
  }
  case proto::MessageType::transform_snapshot: {
    const auto value = proto::decode_transform_snapshot(frame.payload);
    std::cout << " player=" << value.player_id << " xyz=" << value.x << ','
              << value.y << ',' << value.z << " yaw=" << value.yaw;
    break;
  }
  case proto::MessageType::movement_state: {
    const auto value = proto::decode_movement_state(frame.payload);
    std::cout << " player=" << value.player_id
              << " movement_mode=" << static_cast<int>(value.movement_mode)
              << " custom_movement_mode="
              << static_cast<int>(value.custom_movement_mode);
    break;
  }
  case proto::MessageType::player_locomotion_state: {
    const auto value = proto::decode_player_locomotion_state(frame.payload);
    std::cout << " player=" << value.player_id
              << " movement_mode=" << static_cast<int>(value.movement_mode)
              << " locomotion_state="
              << static_cast<int>(value.locomotion_state)
              << " gait=" << static_cast<int>(value.gait)
              << " stance=" << static_cast<int>(value.stance)
              << " sprinting=" << (value.sprinting ? "true" : "false")
              << " crouching=" << (value.crouching ? "true" : "false")
              << " aiming=" << (value.aiming ? "true" : "false")
              << " aim_pitch=" << value.aim_pitch;
    break;
  }
  case proto::MessageType::jump_event: {
    const auto value = proto::decode_jump_event(frame.payload);
    std::cout << " player=" << value.player_id
              << " jump_sequence=" << value.sequence;
    break;
  }
  case proto::MessageType::player_left:
    std::cout << " player="
              << proto::decode_player_left(frame.payload).player_id;
    break;
  case proto::MessageType::error: {
    const auto value = proto::decode_error(frame.payload);
    std::cout << " code=" << value.code << " message=" << value.message;
    break;
  }
  default:
    break;
  }
  std::cout << '\n';
}
} // namespace

auto main(int argc, char **argv) -> int {
  eo::net::SocketHandle socket = eo::net::kInvalidSocket;
  try {
    const auto options = parse_options(argc, argv);
    if (options.show_help) {
      print_usage();
      return 0;
    }
    eo::net::SocketRuntime sockets;
    std::string error;
    socket = eo::net::connect_tcp(options.host, options.port, error);
    if (socket == eo::net::kInvalidSocket)
      throw std::runtime_error(error);
    eo::net::set_no_delay(socket, true, error);

    std::uint64_t sequence{1};
    send_frame(
        socket,
        proto::make_frame(
            proto::MessageType::hello, sequence++,
            proto::Hello{options.name, eo::build_info::identity(
                                           "Probe", proto::kProtocolVersion)}));
    send_frame(socket,
               proto::make_frame(proto::MessageType::zone_state, sequence++,
                                 proto::ZoneState{0, options.zone}));
    send_frame(socket, proto::make_frame(
                           proto::MessageType::player_context_state, sequence++,
                           proto::PlayerContextState{0, options.context}));
    send_frame(socket, proto::make_frame(
                           proto::MessageType::appearance_state, sequence++,
                           proto::AppearanceState{0, options.character,
                                                  options.customization_skin,
                                                  options.customization_face}));
    send_frame(socket,
               proto::make_frame(proto::MessageType::movement_state, sequence++,
                                 proto::MovementState{0, 1, 0}));
    send_frame(socket,
               proto::make_frame(
                   proto::MessageType::player_locomotion_state, sequence++,
                   proto::PlayerLocomotionState{0, 1, 0, 0, 0, false, false,
                                                false, 0.0F}));
    if (options.jump_demo)
      std::cout << "DEMO_MOVEMENT_STATE mode=1 custom=0\n";
    std::cout << "CONNECTED name=" << options.name << " zone=" << options.zone
              << " character=" << options.character
              << " customization_skin=" << options.customization_skin
              << " customization_face=" << options.customization_face
              << " context=" << proto::player_context_name(options.context)
              << " base=" << options.x << ',' << options.y << ',' << options.z
              << " yaw=" << options.yaw << " radius=" << options.radius
              << " angular_speed=" << options.angular_speed
              << " snapshot_hz=" << options.snapshot_hz
              << " movement_demo=" << (options.movement_demo ? "true" : "false")
              << " jump_demo=" << (options.jump_demo ? "true" : "false")
              << " idle_demo=" << (options.idle_demo ? "true" : "false")
              << " appearance_test="
              << (options.appearance_test ? "true" : "false")
              << " teleport_demo=" << (options.teleport_demo ? "true" : "false")
              << " full_exploration_demo="
              << (options.full_exploration_demo ? "true" : "false")
              << " crouch_demo=" << (options.crouch_demo ? "true" : "false")
              << '\n';

    proto::FrameDecoder decoder;
    std::array<std::uint8_t, 64U * 1024U> buffer{};
    const auto start = std::chrono::steady_clock::now();
    auto next_snapshot = start;
    std::string last_movement_phase;
    std::string last_jump_phase;
    std::uint8_t last_movement_mode{1};
    std::uint64_t jump_sequence{};
    bool jump_event_sent{};
    bool teleport_sent{};
    std::string last_locomotion_signature;
    while (std::chrono::steady_clock::now() - start <
           std::chrono::seconds(options.duration_seconds)) {
      const auto now = std::chrono::steady_clock::now();
      if (now >= next_snapshot) {
        const auto elapsed = std::chrono::duration<float>(now - start).count();
        const auto demo = demo_transform(options, elapsed);
        auto output = demo;
        if (options.teleport_demo && !teleport_sent && elapsed >= 3.0F) {
          output.x += 10000.0F;
          teleport_sent = true;
          std::cout << "DEMO_TELEPORT delta=10000\n";
        }
        if (demo.movement_phase != last_movement_phase ||
            demo.jump_phase != last_jump_phase) {
          std::cout << "DEMO_PHASE movement=" << demo.movement_phase
                    << " jump=" << demo.jump_phase << " speed=" << demo.speed
                    << " velocityZ=" << demo.velocity_z << '\n';
          last_movement_phase = demo.movement_phase;
          last_jump_phase = demo.jump_phase;
        }
        const auto movement_mode =
            logic::jump_demo_movement_mode(demo.jump_phase);
        if ((options.jump_demo || options.full_exploration_demo) &&
            !jump_event_sent && movement_mode == 3) {
          ++jump_sequence;
          send_frame(socket, proto::make_frame(
                                 proto::MessageType::jump_event, sequence++,
                                 proto::JumpEvent{0, jump_sequence}));
          std::cout << "DEMO_JUMP_EVENT sequence=" << jump_sequence << '\n';
          jump_event_sent = true;
        }
        if (movement_mode != last_movement_mode) {
          send_frame(socket, proto::make_frame(
                                 proto::MessageType::movement_state, sequence++,
                                 proto::MovementState{0, movement_mode, 0}));
          std::cout << "DEMO_MOVEMENT_STATE mode="
                    << static_cast<int>(movement_mode) << " custom=0\n";
          last_movement_mode = movement_mode;
        }
        const auto gait = demo.movement_phase == "WALK"     ? std::uint8_t{1}
                          : demo.movement_phase == "RUN"    ? std::uint8_t{2}
                          : demo.movement_phase == "SPRINT" ? std::uint8_t{3}
                                                            : std::uint8_t{0};
        const auto airborne = movement_mode == 3;
        const auto crouching =
            options.crouch_demo && elapsed >= 18.0F && elapsed < 21.0F;
        const proto::PlayerLocomotionState locomotion{
            0,
            movement_mode,
            airborne ? std::uint8_t{2}
                     : (demo.speed > 0.0F ? std::uint8_t{1} : std::uint8_t{0}),
            gait,
            crouching ? std::uint8_t{1} : std::uint8_t{0},
            gait == 3,
            crouching,
            false,
            0.0F};
        const auto locomotion_signature =
            std::to_string(locomotion.movement_mode) + '|' +
            std::to_string(locomotion.locomotion_state) + '|' +
            std::to_string(locomotion.gait) + '|' +
            std::to_string(locomotion.stance) + '|' +
            (locomotion.sprinting ? "1" : "0") + '|' +
            (locomotion.crouching ? "1" : "0");
        if (locomotion_signature != last_locomotion_signature) {
          send_frame(socket, proto::make_frame(
                                 proto::MessageType::player_locomotion_state,
                                 sequence++, locomotion));
          std::cout << "DEMO_LOCOMOTION movement_mode="
                    << static_cast<int>(locomotion.movement_mode)
                    << " locomotion_state="
                    << static_cast<int>(locomotion.locomotion_state)
                    << " gait=" << static_cast<int>(locomotion.gait)
                    << " stance=" << static_cast<int>(locomotion.stance)
                    << " sprinting="
                    << (locomotion.sprinting ? "true" : "false")
                    << " crouching="
                    << (locomotion.crouching ? "true" : "false") << '\n';
          last_locomotion_signature = locomotion_signature;
        }
        const auto timestamp = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch())
                .count());
        send_frame(
            socket,
            proto::make_frame(
                proto::MessageType::transform_snapshot, sequence++,
                proto::TransformSnapshot{0, timestamp, output.x, output.y,
                                         output.z, 0.0F, options.yaw, 0.0F}));
        next_snapshot =
            now + std::chrono::milliseconds(1000 / options.snapshot_hz);
      }

      if (eo::net::wait_readable(socket, 50, error)) {
        const auto received = eo::net::receive_some(socket, buffer, error);
        if (received <= 0)
          throw std::runtime_error(error);
        decoder.push(std::span<const std::uint8_t>(
            buffer.data(), static_cast<std::size_t>(received)));
        for (const auto &frame : decoder.take_frames()) {
          if (frame.version != proto::kProtocolVersion)
            throw std::runtime_error("server protocol version mismatch");
          if (frame.type == proto::MessageType::ping)
            send_frame(socket, proto::Frame{proto::kProtocolVersion,
                                            proto::MessageType::pong,
                                            frame.sequence,
                                            {}});
          show_frame(frame);
        }
      } else if (!error.empty()) {
        throw std::runtime_error(error);
      }
    }

    eo::net::shutdown_socket(socket);
    eo::net::close_socket(socket);
    std::cout << "DONE\n";
    return 0;
  } catch (const std::exception &exception) {
    eo::net::shutdown_socket(socket);
    eo::net::close_socket(socket);
    std::cerr << "FATAL: " << exception.what() << '\n';
    return 1;
  }
}
