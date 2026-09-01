# Prueba rápida de ExpeditionOnline

Esta build es un MVP de exploración. No sincroniza combate, historia, quests, inventario ni saves. Cada partida sigue siendo local.

## Prueba A — servidor + dos probes

1. Abre una terminal dentro de `Server` y ejecuta `ExpeditionOnlineServer.exe`.
2. Debe aparecer `READY TCP 0.0.0.0:7777 protocol=2`.
3. Abre dos terminales dentro de `Probe`.
4. En la primera ejecuta `ExpeditionOnlineProbe.exe --name ProbeA --zone SharedZone --x 0 --y 0 --z 0 --yaw 0 --radius 300 --duration 30`.
5. En la segunda ejecuta `ExpeditionOnlineProbe.exe --name ProbeB --zone SharedZone --x 1000 --y 1000 --z 0 --yaw 90 --radius 300 --duration 30`.
6. Cada probe debe mostrar un `Welcome` con un ID distinto y eventos `PlayerJoined`, `ZoneState`, `AppearanceState` y `TransformSnapshot` del otro.
7. Cierra ProbeA. ProbeB debe recibir `PlayerLeft` con el ID de ProbeA.

## Prueba B — Clair Obscur + probe

1. Instala una build de UE4SS compatible con la revisión indicada en `VERSION.txt`.
2. Copia la carpeta `UE4SS/Mods/ExpeditionOnline` del ZIP dentro de:

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

3. Si tu instalación no reconoce `enabled.txt`, añade `ExpeditionOnline : 1` a `ue4ss/Mods/mods.txt`. No dupliques la entrada si ya existe.
4. Confirma en `config/config.ini`:

   ```ini
   ServerHost=127.0.0.1
   ServerPort=7777
   ```

5. Ejecuta `Server/ExpeditionOnlineServer.exe`.
6. Inicia Clair Obscur, carga una partida y permanece en exploración.
7. Comprueba que `ue4ss/UE4SS.log` no muestre un error al cargar `ExpeditionOnline/dlls/main.dll`.
8. Abre `ue4ss/Mods/ExpeditionOnline/ExpeditionOnline.log` y copia:

   ```text
   LOCAL_ZONE <zona exacta completa>
   LOCAL_TRANSFORM x=<X> y=<Y> z=<Z> yaw=<Yaw>
   ```

9. Ejecuta el probe sustituyendo los valores entre ángulos (las comillas de la zona son obligatorias):

   ```powershell
   .\ExpeditionOnlineProbe.exe --name Probe --zone "<LOCAL_ZONE>" --x <X> --y <Y> --z <Z> --yaw <Yaw> --radius 300 --duration 60
   ```

   El probe envía snapshots cada 250 ms y recorre un círculo de radio 300 alrededor de esa posición. Usa `--radius 0` para dejarlo quieto. Ejecuta `ExpeditionOnlineProbe.exe --help` para ver todos los valores predeterminados.

10. El resultado esperado es:

   ```text
   REMOTE_PLAYER_JOINED player=... name=Probe
   REMOTE_SPAWNED player=... actor=BP_Pawn_AICompanion_...
   ```

   El servidor también debe mostrar `CONNECT`, `HELLO`, `ZONE`, `PLAYER_JOINED` y tráfico de apariencia.

El actor remoto es una instancia nueva e independiente. El mod detiene su movimiento y BrainComponent, desactiva tick y colisión, aplica transforms de red y destruye solo ese actor al desconectar. No modifica companions reales de la partida.

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

- Arranca primero el servidor y confirma `READY ...:7777`.
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

### `LOCAL_APPEARANCE` muestra `character=Unknown`

- La identidad se infiere del `SkeletalMesh` activo de `Pawn.Mesh`, no de la clase genérica `BP_jRPG_Character_World_C`.
- El log debe incluir `outfit=SkeletalMesh /Game/...`. Si `outfit` queda vacío, conserva el log completo para identificar el nombre real del componente en esa versión del juego.
- Las rutas de Maelle, Lune, Sciel, Verso, Gustave y Monoco se reconocen; solo las clases remotas de Maelle, Sciel y Verso están verificadas por ahora.

### Protocolo incompatible

- Servidor, probe y mod deben mostrar `protocol=2`. Sustituye juntos los tres binarios del mismo ZIP.

### Windows Firewall

- Permite `ExpeditionOnlineServer.exe` en redes privadas o crea una regla TCP entrante para el puerto 7777.
- No expongas este MVP sin autenticación directamente a Internet; usa LAN o una VPN de confianza.

## Fuera de alcance y futuro combate

No hay coop battle ni PvP. Durante batalla, `PlayerController.Pawn` puede ser `null`; los combatientes son actores separados como `BP_Maelle_Battle_C`, `BP_Verso_Battle2_C` y `BP_Sciel_Battle_C`. Las armas usan el patrón `BP_WeaponSkin_<Character>_<Weapon>_C`. Estos datos quedan documentados para una fase futura y no se sincronizan ahora.
