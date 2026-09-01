# Prueba rápida de ExpeditionOnline

Esta build es un MVP de exploración. No sincroniza combate, historia, quests, inventario ni saves. Cada partida sigue siendo local.

## Prueba A — servidor + dos probes

1. Abre una terminal dentro de `Server` y ejecuta `ExpeditionOnlineServer.exe`.
2. Debe aparecer `SERVER_READY address=0.0.0.0:7777` junto con versión, protocolo y commit.
3. Abre dos terminales dentro de `Probe`.
4. En la primera ejecuta `ExpeditionOnlineProbe.exe --name ProbeA --zone SharedZone --x 0 --y 0 --z 0 --yaw 0 --radius 300 --angular-speed 1 --duration 30`.
5. En la segunda ejecuta `ExpeditionOnlineProbe.exe --name ProbeB --zone SharedZone --x 1000 --y 1000 --z 0 --yaw 90 --radius 300 --angular-speed 1 --duration 30`.
6. Cada probe debe mostrar un `Welcome` con un ID distinto y eventos `PlayerJoined`, `ZoneState`, `AppearanceState` y `TransformSnapshot` del otro.
7. Cierra ProbeA. ProbeB debe recibir `PlayerLeft` con el ID de ProbeA.

## Prueba B — Clair Obscur + probe

1. Instala una build de UE4SS compatible con la revisión indicada en `VERSION.txt`.
2. Para el paquete de usuario, ejecuta `Client/Install-ExpeditionOnline.bat`. La ruta instalada será:

   ```text
   Clair Obscur Expedition 33/
   └── Sandfall/
       └── Binaries/
           └── Win64/
               └── ue4ss/
                   └── Mods/
                       └── ExpeditionOnline/
                           ├── dlls/
                           │   └── main.dll
                           ├── config/
                           │   └── config.ini
                           └── enabled.txt
   ```

3. Confirma en `config/config.ini`:

   ```ini
   ServerHost=127.0.0.1
   ServerPort=7777
   interpolation_delay_ms=300
   ```

4. Ejecuta `Host/Start-Server.bat`.
5. Inicia Clair Obscur, carga una partida y permanece en exploración.
6. Comprueba que `ue4ss/UE4SS.log` no muestre un error al cargar `ExpeditionOnline/dlls/main.dll`.
7. Abre `ue4ss/Mods/ExpeditionOnline/ExpeditionOnline.log` y copia:

   ```text
   LOCAL_ZONE <zona exacta completa>
   LOCAL_TRANSFORM x=<X> y=<Y> z=<Z> yaw=<Yaw>
   ```

8. Ejecuta el probe sustituyendo los valores entre ángulos (las comillas de la zona son obligatorias):

   ```powershell
   .\ExpeditionOnlineProbe.exe --name Probe --zone "<LOCAL_ZONE>" --x <X> --y <Y> --z <Z> --yaw <Yaw> --radius 300 --angular-speed 1 --snapshot-hz 4 --duration 60
   ```

   El probe envía aquí 4 snapshots por segundo y recorre un círculo de radio 300 a 1 radián/segundo: aproximadamente 300 unidades/segundo, suficiente para provocar locomoción. Para esta prueba deliberadamente lenta usa `interpolation_delay_ms=300`; con clientes reales a `snapshot_hz=15`, empieza con 100-150 ms. Usa `--radius 0` o `--angular-speed 0` para dejarlo quieto. Ejecuta `ExpeditionOnlineProbe.exe --help` para ver todos los valores predeterminados.

9. El resultado esperado es:

   ```text
   REMOTE_PLAYER_JOINED player=... name=Probe
   REMOTE_SPAWNED player=... actor=BP_Pawn_AICompanion_...
   REMOTE_MOTION_SETUP player=... movement_component=... movement_class=... anim_instance_class=...
   REMOTE_MOTION player=... speed=... velocity=... observed=... movement_mode=... is_falling=...
   REMOTE_INTERPOLATION player=... buffer=... delay_ms=300 alpha=... render_xyz=... target_xyz=...
   ```

   El servidor también debe mostrar `PLAYER_CONNECTED`, `PLAYER_READY`, `PLAYER_ZONE_CHANGE` y tráfico de apariencia.

El actor remoto es una instancia nueva e independiente. El mod detiene su AI y BrainComponent, desactiva el tick del AIController y la colisión, pero mantiene SkeletalMesh, AnimBP y CharacterMovement activos. Aplica transforms de red y destruye solo ese actor al desconectar. No modifica companions reales de la partida.

Para una secuencia automática de locomoción usa `--movement-demo --snapshot-hz 15`. Para la trayectoria vertical experimental usa `--jump-demo --snapshot-hz 15`; no se envían animaciones explícitas.

Para probar apariencia remota real, copia literalmente `outfit=` y `hair=` de una línea `LOCAL_APPEARANCE` y pásalos entre comillas:

```powershell
ExpeditionOnlineProbe.exe --host 127.0.0.1 --port 7777 --name "Maelle Appearance Probe" --zone "<LOCAL_ZONE exacta>" --duration 60 --x <X> --y <Y> --z <Z> --yaw <YAW> --snapshot-hz 15 --character Maelle --outfit "SkeletalMesh /Game/Characters/Heros/Maelle/Customization/Skin/SK_Maelle_Bikini_skin.SK_Maelle_Bikini_skin" --hair "<full UObject hair copiado de LOCAL_APPEARANCE>"
```

El Probe conserva esos tres valores literalmente en `AppearanceState`; no intenta interpretarlos ni sustituirlos.

## Troubleshooting

### `main.dll` no carga

- Comprueba que sea el archivo x64 del ZIP y que esté exactamente en `ExpeditionOnline/dlls/main.dll`.
- Usa la revisión de UE4SS indicada en `VERSION.txt`; los mods C++ dependen de su ABI.
- Instala el Microsoft Visual C++ Redistributable x64 actual si Windows informa que falta una DLL de runtime.
- Pasa `UE4SS.log` completo, no solo una captura.

### El mod no aparece en UE4SS

- Confirma la carpeta `ue4ss/Mods/ExpeditionOnline` y `enabled.txt`.
- Si tu loader usa la lista heredada, añade `ExpeditionOnline : 1` a `ue4ss/Mods/mods.txt`.
- Revisa que Windows no haya creado una carpeta doble `ExpeditionOnline/ExpeditionOnline` al extraer.

### `connection refused`

- Arranca primero el servidor y confirma `SERVER_READY ...:7777`.
- Revisa `ServerHost` y `ServerPort` en `config/config.ini`.
- Para otro PC, usa la IP LAN del servidor; no uses `127.0.0.1` fuera del mismo equipo.

### El servidor no recibe al juego

- Busca `START`, `CONNECTING` y `CONNECTED` en `ExpeditionOnline.log`.
- Si no existen, el problema es carga de UE4SS/DLL. Si existe `CONNECTING` repetido, es host, puerto o firewall.

### Cliente y probe están en zonas distintas

- El relay solo comparte jugadores cuyo `ZoneState` coincide byte por byte.
- Copia el valor completo de `LOCAL_ZONE` del log del juego al argumento `--zone` del probe.

### `RemotePlayer` no aparece

- Confirma `REMOTE_PLAYER_JOINED`, `ZoneState`, `AppearanceState` y `TransformSnapshot` en los logs.
- Busca `REMOTE_SPAWN_WAIT` o `REMOTE_SPAWN_FAILED`; indican que la clase companion no está cargada o no coincide con el mapping.
- Prueba en exploración. Durante combate `PlayerController.Pawn` puede ser `null` y esta versión elimina los remotos deliberadamente.

### La apariencia tarda en estar lista

- La identidad se infiere únicamente del `SkeletalMesh` de un componente exacto `Body` enumerado desde el Pawn con `K2_GetComponentsByClass`, no de la clase genérica `BP_jRPG_Character_World_C`, `Pawn.Mesh` ni el pelo.
- Busca `APPEARANCE_BODY_COMPONENT component=...Body` y `APPEARANCE_BODY_MESH mesh=SkeletalMesh /Game/Characters/Heros/...`.
- Mientras Unreal reconstruye el skin puede aparecer `LOCAL_APPEARANCE_PENDING reason=body_not_ready`; el cliente conserva la última apariencia válida y no envía `Unknown`.
- `APPEARANCE_SCAN duration_us=... candidates=... source=...` debe permanecer muy por debajo de 5000 microsegundos. Un scan superior a 5 ms se registra como warning.
- Si el Body directo desaparece, busca `LOCAL_SKIN_PROPERTY`, `LOCAL_COMPONENT_DIAGNOSTIC`, `LOCAL_VISUAL_ROUTE` y `APPEARANCE_VISUAL_ROUTE`. La búsqueda sigue solamente relaciones alcanzables desde el Pawn y nunca hace un scan UObject global.
- Para el remoto busca `REMOTE_VISUAL_ACTOR`, `REMOTE_SKELETAL_COMPONENT`, `REMOTE_APPEARANCE_RETRY`, `REMOTE_OUTFIT_APPLIED` y `REMOTE_HAIR_APPLIED`. El inventario se emite una vez por spawn; los reintentos terminan en fail-open.
- Las rutas de Maelle, Lune, Sciel, Verso, Gustave y Monoco se reconocen; solo las clases remotas de Maelle, Sciel y Verso están verificadas por ahora.

### El remoto se mueve a tirones

- Busca `REMOTE_INTERPOLATION`. `buffer` debe crecer, `delay_ms` debe coincidir con la configuración y `render_xyz` normalmente debe quedar entre snapshots, no saltar siempre a `target_xyz`.
- Para Probe a 4 Hz usa 300 ms. Para clientes reales a 15 Hz usa 100-150 ms.
- Si no llegan snapshots durante más de 750 ms, el actor conserva la última posición y su Velocity pasa a cero; no extrapola indefinidamente.

### El remoto se mueve pero permanece en idle

- `REMOTE_MOTION_SETUP` debe mostrar un `CharacterMovementComponent` y la clase del `AnimInstance`.
- Mientras el Probe recorre el círculo, `REMOTE_MOTION speed` debe estar cerca de `radius × angular-speed`; con los valores recomendados será aproximadamente 300.
- `velocity` es el valor calculado y escrito; `observed` es lo que devuelve `GetVelocity` del actor. Si `velocity` es distinto de cero pero `observed` queda en cero o el AnimBP sigue en idle, conserva esas líneas junto con `movement_mode` para la siguiente iteración.

### El salto mueve el actor pero no reproduce Jump/Fall

- Es el resultado runtime conocido: `Velocity.Z` no cambia por sí sola `IsFalling` en este companion/AnimBP.
- Salta normalmente con el jugador local y conserva todas las líneas `LOCAL_MOVEMENT_STATE`. Deben mostrar el valor real de `movement_mode`, `is_falling`, `phase`, velocidad y speed al despegar, pasar por el apex, caer y aterrizar.
- Esta build no escribe `MovementMode` en el remoto ni envía `MovementState`; esos cambios esperan la evidencia del salto local para no asumir valores numéricos.

### Protocolo incompatible

- Servidor, probe y mod deben mostrar `protocol=2`. Sustituye juntos los tres binarios del mismo ZIP.

### Windows Firewall

- Permite `ExpeditionOnlineServer.exe` en redes privadas o crea una regla TCP entrante para el puerto 7777.
- No expongas este MVP sin autenticación directamente a Internet; usa LAN o una VPN de confianza.

## Fuera de alcance y futuro combate

No hay coop battle ni PvP. Durante batalla, `PlayerController.Pawn` puede ser `null`; los combatientes son actores separados como `BP_Maelle_Battle_C`, `BP_Verso_Battle2_C` y `BP_Sciel_Battle_C`. Las armas usan el patrón `BP_WeaponSkin_<Character>_<Weapon>_C`. Estos datos quedan documentados para una fase futura y no se sincronizan ahora.
