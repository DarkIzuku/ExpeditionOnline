# Build reproducible en Windows

El workflow `Build Windows x64` compila servidor, Probe, SelfTest y Doctor en `Release x64`, ejecuta unitarios e integración (relay, zonas, late join, reconnect, timeout, cliente malformado y demos) y compila `main.dll` como mod C++ dentro del árbol oficial de UE4SS en modo `Game__Shipping__Win64`.

Al quedar verde publica tres artifacts: desarrollo completo, Host y Client.

## Revisión fijada

La revisión exacta está en `UE4SS_BUILD_REVISION.txt`. El workflow no usa `latest` ni `main`.

El repositorio oficial vigente es `UE4SS-RE/RE-UE4SS`. La URL histórica `UE4SS-RE/UE4SS` devuelve 404. La plantilla de referencia es `UE4SS-RE/UE4SSCPPTemplate`.

## Requisito de acceso de UE4SS

UE4SS incluye `deps/first/Unreal` desde el repositorio oficial pero restringido `Re-UE4SS/UEPseudo`. El propio workflow oficial de UE4SS hace checkout con un secreto llamado `UEPSEUDO_PAT`.

Para que GitHub Actions pueda compilar `main.dll`:

1. Vincula tu cuenta de GitHub con Epic Games y acepta la invitación a la organización requerida por UE4SS.
2. Crea un token de lectura que pueda acceder a `Re-UE4SS/UEPseudo`.
3. En el repositorio ExpeditionOnline abre `Settings > Secrets and variables > Actions`.
4. Crea el repository secret `UEPSEUDO_PAT` con ese token.
5. Ejecuta `Actions > Build Windows x64 > Run workflow`.

El workflow falla explícitamente antes del checkout si el secreto no está configurado. Nunca se guarda el token en el repositorio ni en el artifact.

## Build local

Servidor, probe y tests:

```powershell
.\scripts\build-server.ps1 -Configuration Release
.\scripts\test-relay.ps1 -Configuration Release
.\scripts\test-self.ps1 -Configuration Release
```

Cliente UE4SS, usando un checkout oficial completo en la revisión fijada:

```powershell
.\scripts\build-client.ps1 -UE4SSRoot C:\src\RE-UE4SS
```

El checkout de UE4SS debe incluir todos sus submódulos. No se admiten mocks ni forks de SDK en el artifact final.
