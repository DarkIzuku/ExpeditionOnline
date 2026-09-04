# Exploration vNext scope and validation

Exploration vNext keeps the parts already validated in ExpeditionOnline:
timestamped transform interpolation, Velocity derived from real snapshots,
MovementState, explicit JumpEvent, network-authority movement tick policy,
TCP relay, heartbeat, reconnect, zone isolation and authoritative player IDs.

It adopts from the OnlineExpedition 0.6.0 analysis only the pieces with a
clear advantage: independent `BP_jRPG_Character_World_C` ghosts,
`BP_WorldMapCharacter_C`, temporary/restored `CurrentCharacterWorld`, vanilla
CharacterCustomization, companion cleanup, context identity and richer visual
locomotion state.

It deliberately does not adopt WebSocket/Steam networking, AddMovementInput,
jump inference, direct mesh writes, raw 0x50-byte assumptions, or the other
mod's interpolation. Traversal, mantle, grapple, ladder/rope and montage
position are documented in the reverse-engineering report but remain deferred.

## Validation boundary

Standalone tests validate protocol 5 serialization/rejection, authoritative
IDs, same-zone relay, late-join caches, context transitions, discrete
locomotion, natural AIR without JumpEvent, crouch transitions, character
identity, reconnect, timeout, Probe demos and teleport threshold logic.

The native DLL build validates that the code compiles against the pinned UE4SS
ABI and exports the required lifecycle functions. It cannot prove the game
build's reflected property names or visual result. Until an in-game A/B test,
the following remain runtime pending: successful world-character/world-map
spawn, the reflected CharacterCustomization struct path, sprint/crouch/aim
visual quality and context transitions on the shipping game.

`SetCharacterCustomization`, `SetMovementState`, `SetDesiredGait` and
`SetStance` are called only after checking each UFunction, parameter count,
type, size, offset and `ParmsSize`. Any mismatch logs a bounded fail-open
reason. `CurrentCharacterWorld` is changed only through a validated FName
property and is restored by scope guard even if spawning throws.
