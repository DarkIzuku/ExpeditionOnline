# ExpeditionOnline protocol v3

The MVP uses one TCP stream per client. Every integer and float is encoded in network byte order. Strings are UTF-8 with a big-endian `uint16` byte length.

## Frame header (20 bytes)

| Offset | Size | Field |
|---:|---:|---|
| 0 | 4 | Magic: `EXON` |
| 4 | 2 | Protocol version (`3`) |
| 6 | 2 | Message type |
| 8 | 4 | Payload byte count, maximum 1 MiB |
| 12 | 8 | Sender sequence number |

## Messages

- `Hello` (1): player name, client build. Must be first.
- `Welcome` (2): server-assigned `uint64` player id.
- `ZoneState` (3): player id, zone string.
- `AppearanceState` (4): player id, Character/class, Outfit mesh, Hair mesh.
- `TransformSnapshot` (5): player id, Unix milliseconds, XYZ and Pitch/Yaw/Roll as six `float32` values.
- `PlayerLeft` (6): player id.
- `Error` (7): error code and text.
- `Ping` / `Pong` (8/9): empty payload; the sequence is echoed.
- `PlayerJoined` (10): player id and display name. It is emitted only when two sessions share a zone.
- `MovementState` (11): player id, `uint8` MovementMode and `uint8` CustomMovementMode. Clients send it only when either mode changes.

Client-supplied player ids are never trusted. The server overwrites them with the id assigned to that connection. State is relayed only to sessions whose latest `ZoneState.zone` is identical. On entry into a zone, the server sends cached zone, appearance, latest transform and latest movement state so late joiners can create existing remote players.

Both sides send lightweight heartbeats. Defaults are a 5-second interval and a 15-second inactivity timeout. The server also rejects oversized frames, unknown/server-only client messages, malformed or truncated payloads, non-finite transforms, messages before `Hello`, and basic per-client floods. Only the offending connection is removed.

No story, quest, inventory, save, combat, damage, or PvP data exists in protocol v3.

## UDP extension point

`TransformSnapshot` is isolated from control messages and already has player, sequence, and timestamp fields. A later protocol version can move only that message to UDP while keeping `Hello`, `Welcome`, zone, appearance, movement mode, errors, and lifecycle on TCP. Version 3 deliberately remains TCP-only.
