param(
    [string]$GamePath = "",
    [string]$PlayerName = "",
    [string]$ServerHost = "",
    [int]$ServerPort = 7777,
    [switch]$SkipDoctor
)

$ErrorActionPreference = "Stop"
$compatibleRevision = "UE4SS v3.0.1-1106-g3a2d2bc1 (commit 3a2d2bc127c857acdfc34ce1518712406d7b0a0d)"

function Test-GamePath([string]$Path) {
    if ([string]::IsNullOrWhiteSpace($Path)) { return $false }
    return Test-Path -LiteralPath (Join-Path $Path "Sandfall\Binaries\Win64") -PathType Container
}

function Find-GamePath {
    $candidates = @()
    if ($env:ProgramFiles) { $candidates += (Join-Path $env:ProgramFiles "Steam\steamapps\common\Clair Obscur Expedition 33") }
    $programFilesX86 = [Environment]::GetFolderPath('ProgramFilesX86')
    if ($programFilesX86) { $candidates += (Join-Path $programFilesX86 "Steam\steamapps\common\Clair Obscur Expedition 33") }
    foreach ($candidate in $candidates) {
        if (Test-GamePath $candidate) { return $candidate }
    }
    return ""
}

try {
    $sourceMod = Join-Path $PSScriptRoot "Mod\ExpeditionOnline"
    if (-not (Test-Path -LiteralPath (Join-Path $sourceMod "dlls\main.dll"))) {
        throw "The Client package is incomplete: Mod\ExpeditionOnline\dlls\main.dll is missing."
    }
    if (-not (Test-GamePath $GamePath)) { $GamePath = Find-GamePath }
    while (-not (Test-GamePath $GamePath)) {
        $GamePath = Read-Host "Clair Obscur installation folder (the folder containing Sandfall)"
        $GamePath = $GamePath.Trim('"')
        if (-not (Test-GamePath $GamePath)) { Write-Host "That folder does not contain Sandfall\Binaries\Win64." -ForegroundColor Yellow }
    }

    $win64 = Join-Path $GamePath "Sandfall\Binaries\Win64"
    $ue4ss = Join-Path $win64 "ue4ss"
    $ue4ssDetected = (Test-Path -LiteralPath $ue4ss -PathType Container) -and (
        (Test-Path -LiteralPath (Join-Path $ue4ss "UE4SS.dll")) -or
        (Test-Path -LiteralPath (Join-Path $win64 "dwmapi.dll")) -or
        (Test-Path -LiteralPath (Join-Path $win64 "xinput1_3.dll"))
    )
    if (-not $ue4ssDetected) {
        Write-Host "UE4SS is required and was not found." -ForegroundColor Red
        Write-Host "Install the compatible official build: $compatibleRevision"
        Write-Host "Official releases: https://github.com/UE4SS-RE/RE-UE4SS/releases"
        Write-Host "Then run this installer again. No executable was downloaded automatically."
        exit 2
    }

    $destinationMod = Join-Path $ue4ss "Mods\ExpeditionOnline"
    New-Item -ItemType Directory -Force -Path $destinationMod | Out-Null
    $existingConfig = Join-Path $destinationMod "config\config.ini"
    $savedConfig = $null
    if (Test-Path -LiteralPath $existingConfig) { $savedConfig = Get-Content -Raw -LiteralPath $existingConfig }
    $destinationDlls = Join-Path $destinationMod "dlls"
    $destinationConfig = Join-Path $destinationMod "config"
    New-Item -ItemType Directory -Force -Path $destinationDlls, $destinationConfig | Out-Null
    Copy-Item -Path (Join-Path $sourceMod "dlls\*") -Destination $destinationDlls -Force
    if (-not (Test-Path -LiteralPath $existingConfig)) {
        Copy-Item -LiteralPath (Join-Path $sourceMod "config\config.ini") -Destination $existingConfig
    }
    if ($null -ne $savedConfig) { Set-Content -LiteralPath $existingConfig -Value $savedConfig -NoNewline -Encoding UTF8 }
    Copy-Item -LiteralPath (Join-Path $sourceMod "VERSION.txt") -Destination $destinationMod -Force -ErrorAction SilentlyContinue
    if (-not (Test-Path -LiteralPath (Join-Path $destinationMod "enabled.txt"))) {
        New-Item -ItemType File -Path (Join-Path $destinationMod "enabled.txt") | Out-Null
    }

    $configureArgs = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', (Join-Path $PSScriptRoot 'Configure-ExpeditionOnline.ps1'), '-GamePath', $GamePath, '-ServerPort', $ServerPort)
    if (-not [string]::IsNullOrWhiteSpace($PlayerName)) { $configureArgs += @('-PlayerName', $PlayerName) }
    if (-not [string]::IsNullOrWhiteSpace($ServerHost)) { $configureArgs += @('-ServerHost', $ServerHost) }
    if ($SkipDoctor) { $configureArgs += '-SkipDoctor' }
    & powershell.exe @configureArgs
    if ($LASTEXITCODE -ne 0) { throw "Configuration did not complete." }

    Write-Host "ExpeditionOnline installed successfully." -ForegroundColor Green
    Write-Host "No other mods or their configuration files were changed."
    exit 0
}
catch {
    Write-Host "Installation failed: $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}
