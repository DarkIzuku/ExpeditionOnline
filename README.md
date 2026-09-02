# ExpeditionOnline

Exploration-only online co-op prototype for *Clair Obscur: Expedition 33*. The current `0.4.0-rc2` instrumentation release candidate focuses on easy installation, smooth same-zone movement, independent remote cosmetics, post-skin-change outfit capture, jump-signal diagnostics, character changes, reconnect and diagnostics. Combat and shared progression are intentionally out of scope.

Normal users should start with [client quick start](docs/QUICK_START_CLIENT.md) or [host quick start](docs/QUICK_START_HOST.md). Technical scope, verified companions and remaining limitations are in [Exploration RC](docs/EXPLORATION_RC.md).

MVP experimental de cooperación online en exploración para **Clair Obscur: Expedition 33**, construido como mod C++ nativo de UE4SS y relay C++ independiente.

La historia, las quests, el inventario y los archivos de save permanecen totalmente locales. No hay combate cooperativo ni PvP.

## Estado

- Protocolo binario TCP `EXON` v3, versionado y con límites de tamaño.
- `ExpeditionOnlineServer.exe`: relay efímero solo entre jugadores de la misma zona.
- `ExpeditionOnlineProbe.exe`: cliente de consola con posición base, yaw y movimiento circular configurables.
- `main.dll`: cliente UE4SS nativo; se compila contra una revisión oficial fijada.
- Unit tests e integración automática con servidor + Probe A + Probe B.
- `PlayerJoined`, `PlayerLeft`, `ZoneState`, `AppearanceState` (Character/Outfit/Hair), `TransformSnapshot` y `MovementState`.
- TCP-only en este MVP; `TransformSnapshot` queda separado para una futura ruta UDP.

## Funcionamiento

El servidor asigna un ID tras `Hello`, conserva solo estado de sesión y reenvía mensajes únicamente cuando el string de zona coincide. Nunca lee ni escribe progreso del juego.

En exploración, el cliente usa la arquitectura validada:

- Controller: `BP_jRPG_Controller_World_C`.
- Pawn local: `BP_jRPG_Character_World_C`.
- Visual remoto: un actor nuevo `BP_Pawn_AICompanion_*_C`.
- Apariencia: primero se usa el `SkeletalMeshComponent` exacto `Body`. Tras un cambio de skin, si desaparece, se valida `Pawn.Mesh` como el componente dinámico del outfit y después se hace un fallback con `K2_GetComponentsByClass` limitado al Pawn. Solo se aceptan meshes reconocidos bajo `/Game/Characters/Heros/<Character>/Customization/Skin/`; `SKM_Quinn`, `CharacterMesh0`, Face, Hair y Placeholder se rechazan. No se realizan scans globales de UObjects. El pelo se lee de `Haircut_SkeletalMesh`.
- Character se infiere desde la ruta del body mesh. Solo Maelle, Sciel y Verso tienen clases companion verificadas; cualquier otro usa el fallback configurado y deja un warning claro.
- El log `LOCAL_TRANSFORM` aparece aproximadamente cada dos segundos para copiar una posición de prueba sin inundar el archivo.
- La locomoción remota calcula `Velocity = (posición actual - anterior) / deltaTime` usando snapshots reales y la escribe en `CharacterMovement.Velocity`. La presentación interpola un buffer ordenado de 24 snapshots con retardo configurable y rotación por el arco más corto. `MovementState` replica solo MovementMode/CustomMovementMode cuando cambian para permitir probar Jump/Fall/Land; no se envían nombres, montages ni frames de animación.

Cada remoto es una instancia independiente: se detiene su movimiento/BrainComponent, se desactivan el tick del AIController y la colisión, se aplican transforms de red y se destruye únicamente ese actor al salir de zona o desconectarse. El actor, su SkeletalMesh, AnimBP y CharacterMovement siguen actualizándose. No se reutiliza ni altera un companion real.

Cuando `PlayerController.Pawn` es `null`, el cliente considera que la exploración no está disponible y elimina sus remotos. Esto evita interferir con combate.

La captura de apariencia usa backoff de 500 ms, 1 s y 2 s durante carga y conserva la última apariencia válida. Tanto local como remoto pueden seguir `Mesh`, `BP_CharacterSkinComponent`, `ChildActor_Skin` y actores attached/child alcanzables desde el Pawn; nunca recorren el conjunto global de UObjects.

## Build y CI

La revisión exacta está en [UE4SS_BUILD_REVISION.txt](UE4SS_BUILD_REVISION.txt). El workflow [build-windows.yml](.github/workflows/build-windows.yml) produce el artifact `ExpeditionOnline-Windows-x64` con un ZIP listo para probar.

UE4SS depende del submódulo restringido `Re-UE4SS/UEPseudo`. Antes de compilar en GitHub es necesario configurar el secreto de lectura `UEPSEUDO_PAT`, igual que en el workflow oficial de UE4SS. Los pasos exactos están en [docs/BUILDING.md](docs/BUILDING.md).

Build local de servidor, probe y tests:

```powershell
.\scripts\build-server.ps1 -Configuration Release
.\scripts\test-relay.ps1 -Configuration Release
```

Build del cliente con un checkout oficial completo de UE4SS:

```powershell
.\scripts\build-client.ps1 -UE4SSRoot C:\src\RE-UE4SS
```

## Configuración

El artifact usa por defecto:

```ini
ServerHost=127.0.0.1
ServerPort=7777
interpolation_delay_ms=100
```

Para dos equipos, cambia `ServerHost` por la IP LAN del PC que ejecuta el servidor. No hardcodees ni publiques una IP pública.

## Primera prueba

Sigue [docs/QUICK_TEST.md](docs/QUICK_TEST.md). La especificación completa está en [docs/PROTOCOL.md](docs/PROTOCOL.md).

## Límites deliberados

- Sin batalla, enemigos, armas, acciones, animaciones, daño o PvP.
- Sin story flags, quests, inventario, equipo funcional o saves compartidos.
- Sin autenticación ni cifrado: usar solo en LAN/VPN de confianza.
- Sin extrapolación prolongada: tras 750 ms sin snapshots la Velocity pasa a cero y se conserva la última posición.
- La locomoción depende de que el AnimBP existente del companion consuma `GetVelocity`; los logs `REMOTE_MOTION_SETUP` y `REMOTE_MOTION` exponen los valores observados para la prueba runtime.

Para una fase futura quedan documentados los actores de batalla `BP_Maelle_Battle_C`, `BP_Verso_Battle2_C`, `BP_Sciel_Battle_C` y el patrón de armas `BP_WeaponSkin_<Character>_<Weapon>_C`.
