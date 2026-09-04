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
- Runtime established Ground=`MovementMode 1` and airborne=`MovementMode 3`. Protocol v4 keeps those state changes and adds one transient `JumpEvent` generated only by the local Pawn's real `OnJumped`/`Multicast_OnJumped` flow. The remote invokes `OnJumped` only on the bounded ALS locomotion AnimInstance; it never calls `Jump`, `LaunchCharacter`, impulse or force, so interpolated snapshots remain authoritative. The Probe emits `mode 1 -> JumpEvent -> mode 3 -> mode 1`. Visual success still requires runtime validation.
- `remote_network_authority=true` disables only the remote CharacterMovement component tick. Actor, mesh and AnimBP ticks remain on, while Velocity and MovementMode continue to be written by ExpeditionOnline. `REMOTE_TRANSFORM_DRIFT` compares the post-network position with the next pre-application position; `false` restores rc2 behavior for A/B testing.

## Appearance and character changes

The Probe accepts literal `--character`, `--outfit` and `--hair` values copied from `LOCAL_APPEARANCE`; `--appearance-test` keeps the connection open for 60 seconds by default. Hair uses `Haircut_SkeletalMesh`. Body resolution is ordered: exact `Body`, the actor's reflected `Mesh` component, an owned customization-skin component, `BP_CharacterSkinComponent`, then reachable attached/child actors.

Local capture uses the same runtime evidence: exact `Body`, then `Pawn.Mesh`, then an owned customization-skin component. Dynamic fallback assets must be under `/Game/Characters/Heros/<Character>/Customization/Skin/`; mannequin, face, hair and placeholder assets are rejected. Discovery follows only bounded relationships reachable from that Pawn. The client loads unloaded SkeletalMesh assets through UE4SS's Asset Registry route and caches successful results.

The rc2 direct write proved the remote body is `CharacterMesh0` through `remote.Mesh` and that unloaded Esquie/Hair assets can be loaded, but the game crashed immediately after the verified writes. Therefore `unsafe_direct_appearance=false` and `unsafe_direct_hair=false` are the defaults. The RC resolves and loads requested assets, logs `REMOTE_APPEARANCE_DEFERRED`, and does not write either mesh. It instruments only reached `BP_CharacterSkinComponent` and `CSAP_SwapAssign` objects so a local vanilla outfit change can reveal the safe function and state route. Independent A/A versus B/B remains unresolved until that route is used without a crash.

Changing a verified character replaces only that remote visual actor. Player ID, zone, snapshot buffer, interpolated transform and connection are retained.

## Network safety boundary

The server validates frame size, protocol version, message type, exact payload shape, finite transforms, Hello ordering and a basic per-client message rate. It assigns the authoritative player ID. Heartbeat and timeout remove dead sessions and notify same-zone peers.

This MVP does not provide authentication or encryption. Use it on a LAN or trusted VPN. Direct public exposure of port 7777 is not recommended.

## UE4SS dependency

The pinned compatible UE4SS source is MIT-licensed, but its runtime is not bundled in the client ZIP. ExpeditionOnline depends on an exact runtime/ABI and the official package structure; the installer therefore detects UE4SS and directs the user to the official UE4SS release rather than downloading executables.
