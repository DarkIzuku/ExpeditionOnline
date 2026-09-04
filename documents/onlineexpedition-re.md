# OnlineExpedition 0.6.0 — reverse engineering dirigido

Fecha del análisis: 2026-09-04  
Binario analizado: `OnlineExpedition - Client 820 0.6.0 2026-08-08T23-39Z V7lPUnpgk.zip`  
Base de comparación: ExpeditionOnline `0.5.0-rc1`, protocolo 4, commit `16e6d5beecdca9ff9987c1333aa291ccb6d4c69e`

## Resumen ejecutivo

El hallazgo de mayor valor es una ruta de apariencia vanilla mucho más segura que escribir `CharacterMesh0` o el mesh de pelo directamente. OnlineExpedition no contiene referencias a `SetSkeletalMesh`, `SetLeaderPoseComponent`, `SetMasterPoseComponent`, `BP_CharacterSkinComponent` ni `CSAP_SwapAssign`. En su lugar:

1. obtiene o inicializa los datos vanilla del personaje mediante `GetCharacterByID` / `InitCharacterData`;
2. carga la base con `LoadCharacterBaseDataFromID`;
3. escribe dos `FName` —`customizationSkin` y `customizationFace`— en `CharacterCustomizationItemData`;
4. llama `LoadCharacterCustomizationFromItemData`;
5. copia el `CharacterCustomization` resultante (0x50 bytes en esta build);
6. lo aplica al actor remoto con `SetCharacterCustomization`.

El remoto normal tampoco es un `BP_Pawn_AICompanion_*`: es una instancia independiente de `BP_jRPG_Character_World_C`. Para el mapa mundial usa `BP_WorldMapCharacter_C`. El código toca `BP_AICompanion_CompanionManager` para impedir que ese actor genere companions propios y para limpiar duplicados, no para usar el companion como avatar remoto.

La arquitectura de movimiento del binario de referencia no debe reemplazar lo que ya funciona en ExpeditionOnline. Combina posición/rotación de red con `Velocity`, `SetDesiredGait`, `SetMovementState`, `AddMovementInput` y `MovementMode`; nuestro `remote_network_authority=true`, la interpolación con timestamp y `JumpEvent` explícito son más deterministas. Sí son reutilizables el spawn de un actor world independiente, la ruta de apariencia vanilla y el filtro temprano de eventos.

## Alcance y método

Se hizo análisis estático dirigido del ZIP y del PE: inventario, hashes, cabeceras PE, imports/exports, RTTI, strings ASCII/UTF-16, rutas de assets, desensamblado x64 y xrefs a strings/imports de UE4SS. No se ejecutó el DLL, no se conectó al relay y no se intentó decompilar todo el programa.

Los RVAs de este documento son relativos a `ImageBase=0x180000000`. Las firmas de bytes identifican exactamente esta build; para otra build son más fiables las anclas de strings y la secuencia de llamadas descritas.

## Inventario del ZIP

| Entrada | Tamaño | SHA-256 | Tipo y observaciones |
|---|---:|---|---|
| ZIP original | 284,411 B | `DE6038E900E31D4D34F423621E3C0184FD70ECD58091DF9BB46984D9FAED6499` | Contenedor ZIP, sin símbolos ni fuentes adicionales |
| `OnlineExpedition/dlls/main.dll` | 806,912 B | `8754997532AC6B59F53B075B91325123EDC1C68F0D810E0E2CBA574C1D96BE2D` | PE32+ x64, C++ nativo, mod UE4SS; no es .NET |
| `OnlineExpedition/relay.txt` | 55 B | `A0FA20DE127E0A5DF5E28D766C41362CD8915495976BD30F19B48094B4F847B1` | Configuración de dos líneas: URL WebSocket `ws://zephyr.proxy.rlwy.net:53446/` y secreto compartido en texto claro (omitido aquí deliberadamente) |
| `OnlineExpedition/version.txt` | 5 B | `29EF1BE9A3A40608EE71E155EB871CA34630E23BEEE9AF29441ABA9D0EA76D54` | Texto completo: `0.6.0` |

No hay Lua, INI/JSON, EXE, PDB, documentación ni otros binarios dentro del ZIP. El secreto de `relay.txt` se leyó para caracterizar el protocolo, pero no se reproduce en este informe porque el repositorio puede compartirse.

## Clasificación y arquitectura del PE

- PE32+ AMD64 (`Machine=0x8664`), C++ nativo MSVC, linker 14.44, seis secciones.
- Timestamp PE: `0x6A77A9D6` (`2026-08-08 18:12:38`, valor no autenticado).
- `ImageBase=0x180000000`, entry point RVA `0x97850`, imagen `0xCA000` bytes.
- ASLR/high-entropy VA y NX activos; sin tabla COFF y sin directorio CLR.
- Compilación LTCG (`/GL`), por lo que la mayoría de los nombres C++ fueron eliminados o fusionados.
- CodeView conservado: `C:\Users\Felipe\repos\online-expedition\Output\src\OnlineExpedition.pdb`, GUID `{B7EEC496-6FCD-4F61-9F78-3DDA793639C7}`, age 358. El PDB no viene en el ZIP.
- RTTI recuperable: `OnlineExpeditionMod`, `ITransport`, `WebSocketTransport`, y lambdas asociadas a `AvatarStart`, `MapMarkersPoll`, `ConnectTransport` y `on_unreal_init`.
- Metadatos embebidos: nombre `OnlineExpedition`, versión `0.6.0`, descripción `Online co-op mod for Expedition 33`, autor `Felipe Guajardo`.

Secciones relevantes:

| Sección | RVA | Tamaño virtual | Uso inferido |
|---|---:|---:|---|
| `.text` | `0x1000` | `0x9D56F` | Código x64 |
| `.rdata` | `0x9F000` | `0x1CBD6` | imports, RTTI, strings, vtables |
| `.data` | `0xBC000` | `0x2EC0` | estado global y cachés |
| `.pdata` | `0xBF000` | `0x85B0` | unwind/function ranges x64 |

### Exports y dependencias

Exports:

- `start_mod`, RVA `0x3EE0`.
- `uninstall_mod`, RVA `0x3F80`.

Dependencias directas: `UE4SS.dll`, `KERNEL32.dll`, `WSOCK32.dll`, `WS2_32.dll`, runtime MSVC/CRT. `steam_api64.dll` es delay-loaded; usa `SteamAPI_GetHSteamUser`, `SteamInternal_ContextInit` y `SteamInternal_FindOrCreateUserInterface`, con interfaces `SteamUser023` y `SteamFriends018`.

Imports UE4SS de mayor interés:

- objetos/reflection: `FindFirstOf`, `FindObject`, `StaticConstructObject`, `GetFunctionByNameInChain`, `GetValuePtrByPropertyNameInChain`, `GetName`, `GetFullName`, `GetOutermost`, constructores de `FName`;
- ejecución/spawn: `UObject::ProcessEvent`, `UWorld::SpawnActor`, `AActor::GetWorld`, `AActor::SetActorEnableCollision`;
- ciclo de vida: `RegisterEngineTickPostCallback`, `UnregisterCallback`, `RegisterHook`.

No importa `StaticLoadObject` ni APIs de Asset Registry. Tampoco importa ni contiene como string `SetSkeletalMesh`, `SetLeaderPoseComponent` o `SetMasterPoseComponent`.

## Arquitectura funcional inferida

El mod usa un callback post-tick del engine para captura, networking y actualización de avatares. `RegisterHook` aparece únicamente con dos rutas concretas de UMG para activación/desactivación del minimapa:

```text
/Game/UI/Widgets/HUD_Exploration/MiniMap/WBP_MiniMap_Main.WBP_MiniMap_Main_C:BP_OnActivated
/Game/UI/Widgets/HUD_Exploration/MiniMap/WBP_MiniMap_Main.WBP_MiniMap_Main_C:BP_OnDeactivated
```

No registra un observador global de `ProcessEvent`. Sus llamadas a `ProcessEvent` son invocaciones dirigidas de UFunctions previamente buscadas. Esta diferencia explica por qué su arquitectura evita el coste de convertir a string miles de eventos irrelevantes.

Flujo principal inferido:

```text
EngineTickPost
  -> localizar GI/controller/pawn y nivel actual
  -> capturar estado local
  -> conectar/reconectar WebSocket y enviar join/state/move
  -> procesar connected/reject/state/move/players/leave/battle
  -> por cada remoto del mismo level:
       cargar datos/assets vanilla si faltan
       spawn de ghost world o world-map
       desactivar colisión y companions generados
       aplicar CharacterCustomization vanilla
       interpolar/aplicar transform
       alimentar locomoción, salto y traversal visual
  -> destruir/ocultar remotos al cambiar de level, batalla o desconexión
```

## Respuestas a las preguntas dirigidas

### 1. Actor usado para los remotos

**Alta confianza:** el remoto de exploración normal es `BP_jRPG_Character_World_C`, ruta exacta:

```text
/Game/jRPGTemplate/Blueprints/Basics/BP_jRPG_Character_World.BP_jRPG_Character_World_C
```

En world map usa:

```text
/Game/Gameplay/WorldMap/BP_WorldMapCharacter.BP_WorldMapCharacter_C
```

No hay referencias a `BP_Pawn_AICompanion_Maelle_C`, `BP_Pawn_AICompanion_Sciel_C`, `BP_Pawn_AICompanion_Verso_C` ni a otras clases `BP_Pawn_AICompanion_*`. Sí hay referencias a `BP_AICompanion_CompanionManager`, `SpawnCompanionsEnabled`, `ForceRefreshCharacterAndCompanions` y `UnspawnAICompanions`: se usan para impedir/limpiar companions generados alrededor del nuevo actor.

### 2–4. Apariencia independiente y aplicación segura

**Alta confianza:** cada estado remoto transporta `characterId`, `customizationSkin` y `customizationFace`. Antes del spawn se asigna temporalmente el `FName` del personaje a `BP_jRPG_GI_Custom_C.CurrentCharacterWorld`; después se crea un actor world independiente y se aplica la personalización a ese actor.

Pseudocódigo resumido de la ruta de carga, RVA `0x37100`:

```cpp
bool build_customization(character_id, skin_id, face_id, out_customization) {
    gi = FindFirstOf("BP_jRPG_GI_Custom_C");
    character_data = get_or_construct_character_data(gi, character_id);
    ProcessEvent(character_data, "LoadCharacterBaseDataFromID", FName(character_id));

    item_data = property(character_data, "CharacterCustomizationItemData");
    item_data.skin = FName(skin_id);       // primer FName observado
    item_data.face = FName(face_id);       // segundo FName, offset +8

    ProcessEvent(character_data, "LoadCharacterCustomizationFromItemData", nullptr);
    memcpy(out_customization,
           property(character_data, "CharacterCustomization"),
           0x50);
    return true;
}
```

Pseudocódigo de aplicación, RVA `0x36E20`:

```cpp
bool apply_customization(actor, character_id, customization) {
    fn = actor->GetFunctionByNameInChain("SetCharacterCustomization");
    params.customization = customization; // 0x50 bytes en esta build
    params.character_id = FName(character_id);
    actor->ProcessEvent(fn, &params);
}
```

`customizationFace` es el nombre real del campo de red. Puede incluir la selección que visualmente cambia cara/pelo, pero el binario no demuestra que sea un mesh de pelo aislado. Por tanto, no debe mapearse ciegamente desde `Haircut_SkeletalMesh`; primero hay que capturar el `FName` vanilla correspondiente.

No hay strings/xrefs directos a:

```text
BP_CharacterSkinComponent
CSAP_SwapAssign
SpawnedCharacterSkin
AttachedBodyOnCharacter
OnBodySkinAssigned
OnFaceSkinAssigned
OnSkinAssignCompleted
CharacterMesh0
Haircut_SkeletalMesh
ChildActor_Skin
SetSkeletalMesh
SetLeaderPoseComponent
SetMasterPoseComponent
```

Conclusión: OnlineExpedition no controla directamente `BP_CharacterSkinComponent`/`CSAP_SwapAssign`; entra por una API superior del personaje. Es razonable inferir que la implementación vanilla termina activando esos componentes/eventos internamente, pero eso no está probado por xrefs del DLL.

### 5. Movimiento y jitter

**Alta confianza para las APIs; media para el algoritmo matemático completo:** el remoto conserva un transform objetivo de red y aplica `K2_SetActorLocation` y `K2_SetActorRotation` cada actualización. Además escribe/alimenta:

```text
CharacterMovement
Velocity
MovementMode
CurrentMovementMode
CurrentMovementSettings
SetDesiredGait
SetMovementState
AddMovementInput
SetStance
```

El helper `K2_SetActorLocation` de RVA `0x378C0` construye parámetros con sweep desactivado y teleport habilitado. `K2_SetActorRotation`, RVA `0x37980`, aplica la rotación con teleport physics habilitado. El core mantiene historial/targets y realiza suavizado entre actualizaciones; no aparecen sequence IDs o timestamps en el JSON que permitan una interpolación temporal equivalente a la de ExpeditionOnline.

Medidas que reducen interferencias:

- actor visual separado del pawn/AI companion local;
- colisión desactivada mediante `SetActorEnableCollision(false)`;
- `SpawnCompanionsEnabled=false` y limpieza con `UnspawnAICompanions`;
- transform de red reaplicado, acompañado de velocity/gait/state para animación.

No se encontró evidencia de que desactive el tick de `CharacterMovement`; de hecho usa `AddMovementInput`. Copiar ese comportamiento podría reintroducir jitter. Se debe conservar la autoridad remota e interpolación actuales.

### 6. Jump, fall y land

**Alta confianza:** no transmite un `JumpEvent` explícito. La captura local incluye `locomotionState`; el remoto compara la transición anterior con `"air"`, comprueba desplazamiento/velocidad vertical positiva y llama `OnJumped` solo al entrar en aire como salto ascendente.

El helper de RVA `0x338E0` resuelve `Body` o `Mesh`, luego `AnimScriptInstance`, y verifica que exista `OnJumped`. El helper de RVA `0x387F0` invoca esa función con parámetros nulos. El call site de transición está alrededor de RVA `0x35CBB`.

No hay referencia a `OnLanded`. La caída/aterrizaje se representa cambiando `MovementMode` y `SetMovementState`; `"air"` usa el modo observado 3. ExpeditionOnline ya tiene un mecanismo superior para salto: `JumpEvent` monotónico, no cacheado para late join y separado de la física. Debe preservarse.

### 7. Zonas, world map y teleports

**Alta confianza:** la función RVA `0x1EE50` busca `BP_jRPG_Controller_World_C` y, como alternativa, `BP_PlayerController_WorldMap_C`. Obtiene el paquete exterior con `GetOutermost()` y su nombre completo con `GetFullName()`. Ese nombre de paquete/nivel es el campo JSON `level` y la clave de presencia.

Al cambiar `level`, el core registra un reset del contexto local, limpia actores/datos asociados y vuelve a resolver la clase de avatar adecuada. El world map tiene una ruta de spawn distinta. No hay xrefs a Asset Registry, APIs explícitas de streaming levels ni una UFunction `Teleport`; los teleports parecen absorbidos por el estado de posición y la ruta `K2_SetActorLocation`, no por un mensaje específico. Esto último es de confianza media.

### 8. Protocolo y estado de red

**Alta confianza:** JSON sobre WebSocket (biblioteca ixwebsocket, WinSock), relay `ws://`, sin cifrado. Steam aporta `steamId` y `personaName`. El cliente declara `protocolVersion="2.0.0"` y `modVersion="0.6.0"`.

Tipos/valores observados:

- salida: `join`, `state`, `move`;
- entrada/control: `state`, `move`, `players`, `reject`, `leave`, `battle`, además del handshake de versión;
- late join: array `players` con estados completos;
- rechazo: `code`, `message`, `retryable`;
- handshake de servidor: `serverVersion`, `serverProtocolVersion`.

Estado completo serializado/deserializado:

```text
steamId, personaName, characterId,
locomotionState, customizationSkin, customizationFace,
sprinting, crouching, aiming, combatAction, aimPitch,
traversalState, mantleStart, montagePos,
level, x, y, z, yaw
```

`move` es un delta reducido que incluye identidad, orientación y estado locomotor/traversal; la posición forma parte inequívoca del estado completo y del objeto interno. No se observaron números de secuencia, timestamps, ack, predicción autoritativa, UDP ni Unreal replication. La desconexión usa `leave`; `battle` oculta o excluye ghosts. La reconexión y los rechazos retryable están implementados.

### 9. UFunctions, propiedades y rutas reutilizables

Las piezas más útiles para ExpeditionOnline son:

```text
BP_jRPG_GI_Custom_C
CurrentCharacterWorld
GetCharacterByID
AC_jRPG_CharactersManager
InitCharacterData
RemoveCharacterFromCollection
LoadCharacterBaseDataFromID
CharacterCustomizationItemData
LoadCharacterCustomizationFromItemData
CharacterCustomization
SetCharacterCustomization

/Game/jRPGTemplate/Blueprints/Basics/BP_jRPG_Character_World.BP_jRPG_Character_World_C
/Game/Gameplay/WorldMap/BP_WorldMapCharacter.BP_WorldMapCharacter_C
/Script/Engine.Default__GameplayStatics
BeginDeferredActorSpawnFromClass
FinishSpawningActor

BP_AICompanion_CompanionManager
SpawnCompanionsEnabled
ForceRefreshCharacterAndCompanions
UnspawnAICompanions
SetActorHiddenInGame
SetActorTickEnabled
SetActorEnableCollision

Body
Mesh
AnimScriptInstance
OnJumped
CharacterMovement
Velocity
MovementMode
SetMovementState
SetDesiredGait
AddMovementInput
SetStance
```

Otras capacidades sincronizadas, útiles para una fase posterior: free aim (`BP_FreeAimControlComponent`, `IsInFreeAimMode`, `CachedAimingRotation`), ataques, mantle, grapple, climb/ladder/rope y posición de montage. El binario incluye rutas de montages vanilla para esas acciones, pero quedan fuera del blocker actual.

### 10. Qué incorporar sin romper lo existente

1. **Incorporar primero la ruta `CharacterCustomization` como experimento protegido**, sin activar ningún `SetSkeletalMesh` directo y sin tocar el pipeline actual de networking/movimiento.
2. **Filtrar el post-hook global de `ProcessEvent` antes de crear nombres/strings.** OnlineExpedition demuestra que no necesita observar globalmente todos los eventos; ExpeditionOnline puede conservar el hook, pero salir inmediatamente si `object` no pertenece a sus mapas acotados.
3. **Prototipar un actor `BP_jRPG_Character_World_C` independiente** en una rama posterior. Es la explicación más sólida de la independencia visual, pero tiene más riesgo que aplicar solo la personalización vanilla.
4. Mantener `JumpEvent`, snapshots con timestamp, interpolation y `remote_network_authority=true`.
5. Mantener el protocolo v4 y traducir solamente los IDs de personalización; no adoptar el relay, secreto, JSON sin secuencia ni dependencia de Steam.

## RVAs, xrefs y anclas de alta confianza

| RVA / rango | Función inferida | Evidencia y ancla para reencontrarla |
|---:|---|---|
| `0x1C2C0–0x1C83F` | serializar `join` | xrefs contiguos `steamId`, `personaName`, `level`, `secret`, `protocolVersion`, `modVersion`; bytes iniciales `40 55 53 56 57 41 54 41 55 41 56 41 57` |
| `0x1C840–0x1D1CF` | serializar `move` | xrefs `yaw`, locomoción, sprint/crouch/aim/combat/traversal y `type=move` |
| `0x1D1D0–0x1D474` | serializar `state` | llamada directa a `0x2F1F0`; xref `state` |
| `0x1D520–0x1EB3F` | dispatcher de mensajes | xrefs `2.0.0`, `0.6.0`, `reject`, `state`, `move`, `leave`, `battle`; llamadas a parsers `0x1F690`, `0x20020`, `0x20720`, `0x209C0`, `0x20F10`, `0x213B0` |
| `0x1EE50–0x1F101` | obtener `level` | xrefs de controladores world/world-map; importa `GetOutermost` y `GetFullName`; bytes `48 89 5C 24 10 48 89 6C 24 18 56 57 41 56` |
| `0x22D10–0x24153` | capturar estado local | xrefs de clases pawn, transform, gait/stance, free aim, mantle, grapple y llamada a encoders de state/move |
| `0x2A0B0–0x2ABF5` | deserializar PlayerState | todos los 18 campos JSON aparecen en orden, incluidos `x/y/z/yaw` |
| `0x2F1F0–0x2FD95` | serializar PlayerState | todos los 18 campos JSON aparecen en orden |
| `0x33790–0x337CF` | desactivar companions del ghost | obtiene `BP_AICompanion_CompanionManager`, luego `SpawnCompanionsEnabled`, y escribe byte cero |
| `0x33D70–0x34007` | obtener/inicializar character data | `BP_jRPG_GI_Custom_C`, `GetCharacterByID`, `AC_jRPG_CharactersManager`, `InitCharacterData` |
| `0x34520–0x36E13` | tick/update de avatar remoto | bloque grande con photo mode, asset load, spawn, transform, locomoción, jump, traversal y cleanup |
| `0x36E20–0x36F9B` | aplicar customization | xref único `SetCharacterCustomization`; copia 0x50 bytes y añade `FName` del personaje antes de `ProcessEvent`; bytes `48 85 C9 0F 84 ?? ?? ?? ?? 48 89 5C 24 20` |
| `0x37100–0x374E1` | construir customization vanilla | secuencia única `LoadCharacterBaseDataFromID` → `CharacterCustomizationItemData` → `LoadCharacterCustomizationFromItemData` → `CharacterCustomization`; bytes `40 53 55 56 57 41 54 41 56 41 57 48 81 EC B0 00 00 00` |
| `0x378C0–0x3797E` | `K2_SetActorLocation` | xref literal y `ProcessEvent`, sweep false/teleport true |
| `0x37980–0x37A04` | `K2_SetActorRotation` | xref literal y `ProcessEvent`, teleport physics true |
| `0x37A10–0x37E03` | aplicar locomoción/movement mode | `CharacterMovement`, `Velocity`, `MovementMode`, `SetMovementState`, VFX y settings |
| `0x37E40–0x3812A` | spawn de world-map/Esquie | ruta `BP_WorldMapCharacter`, companion manager y `ForceRefreshCharacterAndCompanions` |
| `0x38130–0x385A8` | spawn de world ghost | ruta `BP_jRPG_Character_World`, `CurrentCharacterWorld`, deferred spawn/fallback `UWorld::SpawnActor`, disable companions/collision |
| `0x387F0–0x38833` | disparar salto visual | helper `Body/Mesh -> AnimScriptInstance`, xref `OnJumped`, tail-call a `ProcessEvent`; bytes `40 53 48 83 EC 20 E8 ?? ?? ?? ?? 48 8B D8` |

Las firmas con `??` contienen desplazamientos relativos y deben enmascararse. Para otra build, buscar las secuencias de strings/UFunctions es más robusto que buscar solo el prólogo.

## OnlineExpedition vs ExpeditionOnline

| Área | OnlineExpedition 0.6.0 | ExpeditionOnline 0.5.0-rc1 | Decisión recomendada |
|---|---|---|---|
| Actor remoto | `BP_jRPG_Character_World_C`; world map separado | `BP_Pawn_AICompanion_*` | Prototipar world character después de la ruta de apariencia, sin reemplazo inmediato |
| Independencia visual | actor world propio + `CharacterCustomization` | companion hereda visual local | Adoptar la API vanilla, no mesh writes |
| Outfit/cara-pelo | IDs `FName` `customizationSkin`/`customizationFace` | paths de SkeletalMesh outfit/hair | Capturar IDs vanilla y extender protocolo solo cuando estén verificados |
| Carga | character-data manager y loaders vanilla | Asset Registry para meshes directos | Conservar Asset Registry diagnóstico; aplicar con loader vanilla |
| Body/hair directo | no observado | probado y desactivado tras crash | Mantener `unsafe_direct_* = false` |
| Transform | `K2_SetActorLocation/Rotation`, smoothing propio | snapshots con timestamp e interpolación | Mantener implementación actual |
| Anti-jitter | ghost independiente, colisión/companions off; tick de movement no demostrado off | `remote_network_authority=true` desactiva solo movement tick | Mantener autoridad actual |
| Locomoción | velocity + gait/state/input + montages | velocity + movement mode/AnimBP | No copiar `AddMovementInput` salvo prueba aislada |
| Salto | infiere entrada a `air` + velocidad Z; `OnJumped` | `JumpEvent` explícito monotónico + movement mode | Mantener `JumpEvent` |
| Landing | salida de `air`/movement state; sin `OnLanded` | mode 3 → 1 | Mantener enfoque actual |
| Zona | nombre del outermost package en `level` | `ZoneState` exacto | Comparar ambos nombres en runtime; no cambiar aún |
| Transporte | WebSocket JSON público + Steam | TCP binario propio, protocolo 4 | Mantener v4; no copiar relay/secreto |
| Late join | array `players` de estados completos | caché de zone/appearance/transform/movement | Mantener diseño actual |
| ProcessEvent | llamadas dirigidas; no observer global | post-hook global con filtrado tardío | Añadir fast-path por objeto antes de nombrar la función |

## Hallazgos por nivel de confianza

### Alta

- PE C++ nativo x64 UE4SS, no .NET; inventario completo de tres archivos.
- WebSocket JSON + identidad Steam, protocolo `2.0.0` y campos de estado listados.
- Remoto normal `BP_jRPG_Character_World_C`; world map `BP_WorldMapCharacter_C`.
- Ruta de apariencia `LoadCharacterBaseDataFromID` → item data → `LoadCharacterCustomizationFromItemData` → `SetCharacterCustomization`.
- Ausencia de escritura directa de meshes y ausencia de uso directo de `BP_CharacterSkinComponent`/`CSAP_SwapAssign`.
- Salto visual mediante `AnimScriptInstance.OnJumped`; sin `OnLanded`.
- Zona basada en outermost package/full name.
- No hay observador global de `ProcessEvent` en el binario de referencia.

### Media

- `customizationFace` probablemente encapsula la selección que incluye cara/pelo, pero aún falta correlacionarla con `Haircut_SkeletalMesh` en runtime.
- La posición remota se suaviza con estado objetivo interno; el desensamblado confirma múltiples aplicaciones de transform, pero no se reconstruyó todo el cálculo.
- Los teleports se resuelven como discontinuidad de transform/teleport flag, no como mensaje dedicado.
- El uso temporal de `CurrentCharacterWorld` antes del spawn es necesario para la clase vanilla, pero habría que serializar/restaurar cuidadosamente ese estado global en nuestra implementación.

### Baja/no demostrado

- Que `SetCharacterCustomization` sea seguro sobre un actor AI companion ya existente. La evidencia fuerte es sobre un `BP_jRPG_Character_World_C` creado por el mod.
- El offset/ABI de `FCharacterCustomization` en otra versión del juego. `0x50` solo está demostrado en este binario/build.
- Que `customizationFace` acepte directamente un path de SkeletalMesh o el valor actual de `Haircut_SkeletalMesh`.
- Que el algoritmo de OnlineExpedition elimine mejor el jitter que `remote_network_authority=true`.

## Cambios concretos recomendados, priorizados

1. **Corregir ya el coste del hook global:** buscar `object` en `local_skin_objects_` y `local_jump_objects_` antes de `object_leaf_name(function_object)`. Es un cambio aislado, comprobable estáticamente y no altera JumpEvent, networking, interpolation ni autoridad remota.
2. **Añadir captura diagnóstica acotada de IDs vanilla:** en los objetos ya alcanzados, registrar cambios de `CharacterCustomizationItemData`/`CharacterCustomization` y correlacionar `customizationSkin`/`customizationFace` con outfit/hair visible. No aumentar el alcance del hook.
3. **Implementar un prototipo opt-in de `SetCharacterCustomization`:** resolver UFunctions por nombre, validar `ParmsSize` y offsets reflejados cuando sea posible, no hardcodear 0x50 sin guardas y restaurar cualquier cambio temporal a `CurrentCharacterWorld`.
4. **Probar el actor world independiente:** spawn deferred de `BP_jRPG_Character_World_C`, `SpawnCompanionsEnabled=false`, finish spawn, `UnspawnAICompanions`, colisión off y tick de movement bajo la autoridad actual. Hacer A/B contra AI companion sin cambiar el default.
5. **Después, world map y traversal:** separar `BP_WorldMapCharacter_C`; comparar `GetOutermost()->GetFullName()` con `LOCAL_ZONE`; mantener teleports y traversal fuera hasta que apariencia/spawn estén estables.

## Criterio de seguridad para la siguiente fase

La evidencia sí justifica un cambio inmediato de bajo riesgo: el fast-path del hook de `ProcessEvent`. No justifica todavía activar apariencia vanilla en producción, porque falta validar el ABI/reflection de `CharacterCustomization`, el valor real de `customizationFace` y el comportamiento de la UFunction sobre nuestra clase remota actual. Por ello, cualquier implementación de apariencia debe empezar opt-in, con logs acotados y prueba A/A–B/B en juego.

## Limitaciones

- Análisis estático de una sola build; sin PDB, source ni ejecución dinámica.
- Los nombres de funciones C++ internos son inferidos por xrefs y RTTI; los nombres de UFunctions/propiedades son literales del binario.
- No se accedió al servidor relay; su comportamiento autoritativo solo puede inferirse desde el cliente.
- No se reprodujo el secreto incluido en el ZIP.
- No se atribuye licencia ni se copia código fuente: este documento describe comportamiento observable, APIs, rutas y offsets para una reimplementación compatible.
