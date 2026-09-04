# Prueba rápida de Exploration vNext

Esta build es `0.6.0-rc1`, protocolo 5. Servidor, Probe y `main.dll` deben salir
del mismo workflow. No sincroniza combate, historia, quests, inventario ni
saves.

## Comprobación sin abrir el juego

En `Host`, inicia `ExpeditionOnlineServer.exe`. Desde `Probe`, una demo completa
de exploración se ejecuta con una sola línea:

```powershell
.\ExpeditionOnlineProbe.exe --host 127.0.0.1 --port 7777 --name Probe --zone ProbeZone --context exploration --char Maelle --customization-skin Maelle_ActeIII --customization-face Maelle_Face_02 --snapshot-hz 15 --full-exploration-demo --crouch-demo
```

La salida debe recorrer IDLE, WALK, RUN, SPRINT, STOP, un único JumpEvent,
AIR y vuelta a STOP. `PlayerLocomotionState` se envía solo cuando cambia.

## Preparación dentro del juego

1. Instala el Client ZIP y confirma `main.dll`, `config/config.ini` y
   `enabled.txt` bajo `ue4ss/Mods/ExpeditionOnline`.
2. Deja los defaults seguros: world-character, network authority y vanilla
   customization activos; movement input y escrituras directas desactivados.
3. Arranca el servidor, abre el juego y revisa
   `ue4ss/Mods/ExpeditionOnline/ExpeditionOnline.log`.
4. Deben aparecer `WELCOME`, `LOCAL_CONTEXT`, `LOCAL_ZONE`,
   `LOCAL_CHARACTER_ID`, `LOCAL_CUSTOMIZATION` y `LOCAL_TRANSFORM`.
5. No debe repetirse `GAME_BRIDGE_EXCEPTION ... bad allocation`. Si ocurre,
   conserva la línea completa, incluido `stage=`.

## Prueba normal de exploración

1. Carga un nivel normal y espera `LOCAL_CONTEXT ... new=exploration`.
2. Copia exactamente `LOCAL_ZONE` y XYZ/yaw de `LOCAL_TRANSFORM`.
3. Ejecuta el Probe anterior sustituyendo zona y coordenadas; añade
   `--x`, `--y`, `--z` y `--yaw`.
4. Confirma `REMOTE_SPAWNED ... backend=world_character` y que el actor sea
   `BP_jRPG_Character_World_C`, no un companion salvo fallback explícito.
5. Comprueba `REMOTE_NETWORK_AUTHORITY ... movement_tick_enabled=false`, con
   Actor y mesh tick activos.
6. Busca `REMOTE_CUSTOMIZATION_APPLIED ... reflection_validated=true`. Si la
   ABI o una propiedad no coincide, el resultado correcto es un
   `REMOTE_APPEARANCE_FAIL_OPEN` con razón; no debe haber crash ni escritura
   directa de meshes.
7. Observa Idle/Walk/Run/Sprint/Stop y un salto. Deben aparecer
   `REMOTE_LOCOMOTION`, un solo `REMOTE_JUMP_EVENT`, MovementMode AIR y
   Velocity calculada desde snapshots.
8. Para caída natural usa un Probe/cliente que envíe AIR sin JumpEvent: el
   remoto debe caer visualmente sin reproducir impulso de salto.
9. Cambia de personaje y outfit. El PlayerId debe permanecer, el actor visual
   debe recrearse, y deben reaplicarse customization, transform, movimiento,
   gait y stance.

## Prueba de mapa mundial

1. Entra al mapa mundial y espera
   `LOCAL_CONTEXT old=exploration new=world_map interpolation_reset=true`.
2. Copia el nuevo `LOCAL_ZONE` y transform.
3. Ejecuta el mismo Probe con `--context world-map`.
4. Confirma `REMOTE_SPAWNED ... backend=world_map` y actor
   `BP_WorldMapCharacter_C`.
5. Verifica interpolación, transform y network authority. Las UFunctions de
   locomoción se usan solo si ese actor expone firmas reflejadas compatibles;
   de otro modo debe aparecer fail-open.
6. Vuelve a exploración. El actor world-map anterior debe destruirse, el
   buffer incompatible debe vaciarse y el nuevo world-character debe aparecer
   tras recibir snapshots del contexto nuevo.

## Teleport/fast travel

Ejecuta Probe con `--teleport-demo`. El cliente debe registrar
`REMOTE_TELEPORT`, limpiar el buffer, colocar el actor en el snapshot nuevo y
usar Velocity cero para ese salto. Los snapshots siguientes vuelven a la
interpolación normal; CharacterMovement no intenta recorrer la distancia.

## Diagnóstico rápido

- `REMOTE_SPAWN_WAIT`: clase o `CurrentCharacterWorld` aún no validado.
- `REMOTE_BACKEND_FALLBACK`: se activó el companion legacy porque faltó la
  clase world; no es el backend esperado para la validación principal.
- `REMOTE_APPEARANCE_FAIL_OPEN`: la reflexión evitó una llamada con ABI no
  demostrada. Conserva la razón exacta.
- `REMOTE_LOCOMOTION_FAIL_OPEN`: ninguna de `SetMovementState`,
  `SetDesiredGait` o `SetStance` confirmó una firma de un byte.
- `REMOTE_TRANSFORM_DRIFT`: Unreal intentó mover el actor entre aplicaciones;
  confirma que `remote_network_authority=true`.
- `PROTOCOL_MISMATCH`: sustituye juntos Server, Probe y Client; protocolo 4 y
  protocolo 5 no son compatibles.
