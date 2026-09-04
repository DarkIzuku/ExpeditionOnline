# ExpeditionOnline

`0.6.0-rc1` is the Exploration vNext candidate for *Clair Obscur:
Expedition 33*. It combines ExpeditionOnline's timestamped TCP relay,
interpolation, explicit JumpEvent and remote network authority with the
independent world-character, world-map and vanilla customization routes found
through directed analysis of OnlineExpedition 0.6.0.

Combat, story, quests, inventory and save synchronization remain deliberately
out of scope. Each player's progression stays local.

## Exploration vNext

- Binary TCP `EXON` protocol 5. Protocol 4 is intentionally rejected.
- Server-authoritative player IDs, exact-zone isolation, heartbeat, reconnect
  and late-join replay.
- Timestamped `TransformSnapshot` interpolation remains the only positional
  authority. `remote_network_authority=true` disables only remote
  CharacterMovement tick; Actor, mesh and AnimBP keep ticking.
- Normal remotes use an independent
  `BP_jRPG_Character_World_C`; world-map remotes use
  `BP_WorldMapCharacter_C`. AI companion actors are diagnostic fallback only.
- Appearance transports vanilla `character_id`, `customization_skin` and
  `customization_face`, then uses the validated
  `LoadCharacterBaseDataFromID` → `CharacterCustomizationItemData` →
  `LoadCharacterCustomizationFromItemData` → `SetCharacterCustomization`
  route. Struct size, types and parameter offsets come from reflection at
  runtime; mismatches fail open.
- Locomotion is discrete, on-change state: movement mode, locomotion state,
  gait, stance, sprint, crouch, aim and aim pitch. Validated
  `SetMovementState`, `SetDesiredGait` and `SetStance` calls feed vanilla
  presentation without controlling position.
- Explicit `JumpEvent` is preserved. AIR without JumpEvent represents a
  natural fall. No `AddMovementInput`, `Jump`, launch impulse or root-motion
  position authority is used.
- Exploration↔world-map transitions replace the visual actor and clear
  incompatible interpolation. Same-context discontinuities above
  `teleport_threshold_units` snap once with temporary zero Velocity.
- Remote collision is disabled. Generated companion managers are disabled and
  `UnspawnAICompanions` is requested when available.

## Safety defaults

```ini
remote_actor_mode=world_character
fallback_ai_companion=true
remote_network_authority=true
remote_use_movement_input=false
vanilla_customization=true
world_map_remote=true
unsafe_direct_appearance=false
unsafe_direct_hair=false
sync_locomotion_state=true
sync_gait=true
sync_crouch=true
sync_aim=true
teleport_threshold_units=5000
```

Direct writes to `CharacterMesh0` and `Haircut_SkeletalMesh` are not part of
the normal route. The ProcessEvent hook rejects untracked objects before name
allocation and only records the explicit skin/jump events needed by the mod.

## Components

- `ExpeditionOnlineServer.exe`: ephemeral same-zone relay.
- `ExpeditionOnlineProbe.exe`: normal/world-map, appearance, teleport,
  movement, jump and `--full-exploration-demo` simulator.
- `ExpeditionOnlineSelfTest.exe`: protocol rejection, relay, context,
  locomotion, natural-fall, late-join and reconnect checks.
- `ExpeditionOnlineDoctor.exe`: installation and compatibility diagnostic.
- `main.dll`: native UE4SS client built against the pinned official revision
  in [UE4SS_BUILD_REVISION.txt](UE4SS_BUILD_REVISION.txt).

## Build and test

Standalone server, Probe, tools and tests:

```powershell
.\scripts\build-server.ps1 -Configuration Release
.\scripts\test-relay.ps1 -Configuration Release
.\scripts\test-self.ps1 -Configuration Release
.\scripts\test-probe-demos.ps1 -Configuration Release
```

The native client requires a complete checkout of the pinned official UE4SS
source, including its restricted `Re-UE4SS/UEPseudo` submodule:

```powershell
.\scripts\build-client.ps1 -UE4SSRoot C:\src\RE-UE4SS
```

See [building instructions](docs/BUILDING.md), the [protocol](docs/PROTOCOL.md),
the [quick runtime test](docs/QUICK_TEST.md), and the directed
[OnlineExpedition reverse-engineering report](documents/onlineexpedition-re.md).

GitHub Actions builds and verifies real Windows x64 PE outputs, the UE4SS
exports, Doctor integration, package contents and three downloadable ZIPs:
combined, Host and Client.

## Network boundary

The relay does not authenticate or encrypt traffic. Use it only on a trusted
LAN or VPN; do not expose port 7777 directly to the public Internet.
