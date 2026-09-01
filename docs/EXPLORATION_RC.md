# Exploration Release Candidate scope

This RC synchronizes exploration only: zone, character, outfit, hair, position, rotation and locomotion velocity. Combat, enemies, damage, skills, PvP, shared quests, shared story and shared saves are intentionally disabled. Every player's save and story state remain local.

## Companion classes verified at runtime

| Character | Remote companion class | Status |
|---|---|---|
| Maelle | `BP_Pawn_AICompanion_Maelle_C` | Verified |
| Sciel | `BP_Pawn_AICompanion_Sciel_C` | Verified |
| Verso | `BP_Pawn_AICompanion_Verso_C` | Verified |
| Lune | Unknown | Not verified; safe Maelle fallback and warning |
| Gustave | Unknown | Not verified; safe Maelle fallback and warning |
| Monoco | Unknown | Not verified; safe Maelle fallback and warning |

No unverified Blueprint class is invented. When a character has no mapping, the client logs `REMOTE_CHARACTER_UNKNOWN` and uses the configured fallback.

## Movement status

- Idle, Walk and Run are produced by the existing companion AnimBP from `CharacterMovement.Velocity`; no animation packets are sent.
- Interpolation is applied every GameBridge tick. Use 100–150 ms at the normal 15 Hz snapshot rate and 300 ms for Probe at 4 Hz.
- Jump/Fall/Land is experimental. `--jump-demo` supplies a physical vertical trajectory and velocity, while logs report `movement_mode` and `is_falling`. The RC does not force a movement mode until a real game test proves it is required.

## Appearance and character changes

Remote outfit and hair use only actor-bounded `K2_GetComponentsByClass` enumeration. Exact component names are `Body` and `Haircut_SkeletalMesh`. Missing or not-yet-loaded assets are fail-open: the remote remains usable and the log explains what could not be applied.

Changing a verified character replaces only that remote visual actor. Player ID, zone, snapshot buffer, interpolated transform and connection are retained.

## Network safety boundary

The server validates frame size, protocol version, message type, exact payload shape, finite transforms, Hello ordering and a basic per-client message rate. It assigns the authoritative player ID. Heartbeat and timeout remove dead sessions and notify same-zone peers.

This MVP does not provide authentication or encryption. Use it on a LAN or trusted VPN. Direct public exposure of port 7777 is not recommended.

## UE4SS dependency

The pinned compatible UE4SS source is MIT-licensed, but its runtime is not bundled in the client ZIP. ExpeditionOnline depends on an exact runtime/ABI and the official package structure; the installer therefore detects UE4SS and directs the user to the official UE4SS release rather than downloading executables.
