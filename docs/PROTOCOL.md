# ExpeditionOnline protocol v5

Protocol 5 is a deliberate binary break from protocol 4. Client, Probe and
server must come from the same build; a v4 peer receives error 1001 and is
disconnected cleanly.

The transport remains one TCP stream per client. Integers and floats use
network byte order. Strings are UTF-8 prefixed by a big-endian `uint16` byte
length.

## Frame header (20 bytes)

| Offset | Size | Field |
|---:|---:|---|
| 0 | 4 | Magic `EXON` |
| 4 | 2 | Protocol version (`5`) |
| 6 | 2 | Message type |
| 8 | 4 | Payload byte count, maximum 1 MiB |
| 12 | 8 | Sender sequence number |

## Messages

- `Hello` (1): player name and client build; it must be first.
- `Welcome` (2): server-assigned `uint64` player ID.
- `ZoneState` (3): player ID and exact outermost package/full-name string.
- `AppearanceState` (4): player ID plus the vanilla `character_id`,
  `customization_skin` and `customization_face` IDs. These are not mesh paths.
- `TransformSnapshot` (5): player ID, Unix milliseconds and XYZ plus
  Pitch/Yaw/Roll as six `float32` values.
- `PlayerLeft` (6), `Error` (7), empty `Ping`/`Pong` (8/9), and
  `PlayerJoined` (10).
- `MovementState` (11): player ID, Unreal `MovementMode` and
  `CustomMovementMode` bytes. This preserves the runtime-validated
  ground/air pipeline.
- `JumpEvent` (12): player ID and monotonic `uint64` jump sequence. It is an
  explicit transient event and is never inferred from AIR state.
- `PlayerContextState` (13): player ID and one byte: unavailable=0,
  exploration=1, world-map=2, combat=3.
- `PlayerLocomotionState` (14): player ID, movement mode, locomotion state,
  gait and stance bytes; one bitfield for sprinting/crouching/aiming; and
  `float32 aim_pitch`.

The enum bytes in locomotion state preserve reflected game values when those
properties exist. Probe uses a deterministic synthetic vocabulary for tests:
locomotion 0=idle/ground, 1=ground movement, 2=air; gait 0=none, 1=walk,
2=run, 3=sprint; stance 0=standing, 1=crouching. The client uses speed only as
a fallback when a vanilla property is not available.

## Authority and replay rules

Client-supplied player IDs are never trusted. The server overwrites them with
the ID assigned to the connection. Transform snapshots remain the only
position authority. Locomotion state controls presentation only and is sent
on change; it never invokes movement input. Jump sequences are monotonic and
duplicates/stale values are ignored.

State is relayed only to sessions with an identical latest zone. On late join,
the relay sends player identity, zone, context, appearance, latest transform,
MovementState and PlayerLocomotionState. `JumpEvent` is never replayed.

Context transitions between exploration and world-map require clients to
destroy the old visual actor and discard the incompatible interpolation
buffer. A same-context transform discontinuity is handled client-side as a
teleport: clear the buffer, snap once and temporarily zero Velocity.

The server rejects oversized frames, unknown/server-only client messages,
malformed payloads, invalid context/movement values, non-finite transforms or
aim pitch, messages before `Hello`, and basic floods. Heartbeats default to a
5-second interval with a 15-second inactivity timeout.

No story, quest, inventory, save, combat, damage, PvP or traversal/montage
state is synchronized in protocol 5.
