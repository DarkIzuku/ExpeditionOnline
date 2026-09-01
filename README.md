# ExpeditionOnline

MVP experimental de cooperación online en exploración para **Clair Obscur: Expedition 33**, construido como mod C++ nativo de UE4SS y relay C++ independiente.

La historia, las quests, el inventario y los archivos de save permanecen totalmente locales. No hay combate cooperativo ni PvP.

## Estado

- Protocolo binario TCP `EXON` v2, versionado y con límites de tamaño.
- `ExpeditionOnlineServer.exe`: relay efímero solo entre jugadores de la misma zona.
- `ExpeditionOnlineProbe.exe`: cliente de consola con posición base, yaw y movimiento circular configurables.
- `main.dll`: cliente UE4SS nativo; se compila contra una revisión oficial fijada.
- Unit tests e integración automática con servidor + Probe A + Probe B.
- `PlayerJoined`, `PlayerLeft`, `ZoneState`, `AppearanceState` (Character/Outfit/Hair) y `TransformSnapshot`.
- TCP-only en este MVP; `TransformSnapshot` queda separado para una futura ruta UDP.

## Funcionamiento

El servidor asigna un ID tras `Hello`, conserva solo estado de sesión y reenvía mensajes únicamente cuando el string de zona coincide. Nunca lee ni escribe progreso del juego.

En exploración, el cliente usa la arquitectura validada:

- Controller: `BP_jRPG_Controller_World_C`.
- Pawn local: `BP_jRPG_Character_World_C`.
- Visual remoto: un actor nuevo `BP_Pawn_AICompanion_*_C`.
- Apariencia: el cuerpo se resuelve primero desde `Pawn.Mesh` (componente `Body`) y el pelo desde el componente owned `Haircut_SkeletalMesh`; ambos leen su `SkeletalMesh` activo.
- Character se infiere desde la ruta del body mesh. Solo Maelle, Sciel y Verso tienen clases companion verificadas; cualquier otro usa el fallback configurado y deja un warning claro.
- El log `LOCAL_TRANSFORM` aparece aproximadamente cada dos segundos para copiar una posición de prueba sin inundar el archivo.

Cada remoto es una instancia independiente: se detiene su movimiento/BrainComponent, se desactivan tick y colisión, se aplican transforms de red y se destruye únicamente ese actor al salir de zona o desconectarse. No se reutiliza ni altera un companion real.

Cuando `PlayerController.Pawn` es `null`, el cliente considera que la exploración no está disponible y elimina sus remotos. Esto evita interferir con combate.

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
```

Para dos equipos, cambia `ServerHost` por la IP LAN del PC que ejecuta el servidor. No hardcodees ni publiques una IP pública.

## Primera prueba

Sigue [docs/QUICK_TEST.md](docs/QUICK_TEST.md). La especificación completa está en [docs/PROTOCOL.md](docs/PROTOCOL.md).

## Límites deliberados

- Sin batalla, enemigos, armas, acciones, animaciones, daño o PvP.
- Sin story flags, quests, inventario, equipo funcional o saves compartidos.
- Sin autenticación ni cifrado: usar solo en LAN/VPN de confianza.
- Sin interpolación de snapshots todavía.

Para una fase futura quedan documentados los actores de batalla `BP_Maelle_Battle_C`, `BP_Verso_Battle2_C`, `BP_Sciel_Battle_C` y el patrón de armas `BP_WeaponSkin_<Character>_<Weapon>_C`.
