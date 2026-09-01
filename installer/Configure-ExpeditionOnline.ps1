param(
    [string]$GamePath = "",
    [string]$PlayerName = "",
    [string]$ServerHost = "",
    [int]$ServerPort = 7777,
    [switch]$SkipDoctor
)

$ErrorActionPreference = "Stop"

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

function Set-IniValue([string]$Path, [string]$Key, [string]$Value) {
    $lines = [System.Collections.Generic.List[string]](Get-Content -LiteralPath $Path)
    $pattern = '^\s*' + [regex]::Escape($Key) + '\s*='
    $found = $false
    for ($index = 0; $index -lt $lines.Count; $index++) {
        if ($lines[$index] -match $pattern) {
            $lines[$index] = "$Key=$Value"
            $found = $true
            break
        }
    }
    if (-not $found) { $lines.Add("$Key=$Value") }
    Set-Content -LiteralPath $Path -Value $lines -Encoding UTF8
}

try {
    if (-not (Test-GamePath $GamePath)) { $GamePath = Find-GamePath }
    while (-not (Test-GamePath $GamePath)) {
        $GamePath = Read-Host "Clair Obscur installation folder (the folder containing Sandfall)"
        $GamePath = $GamePath.Trim('"')
        if (-not (Test-GamePath $GamePath)) { Write-Host "That folder does not contain Sandfall\Binaries\Win64." -ForegroundColor Yellow }
    }

    $win64 = Join-Path $GamePath "Sandfall\Binaries\Win64"
    $mod = Join-Path $win64 "ue4ss\Mods\ExpeditionOnline"
    $config = Join-Path $mod "config\config.ini"
    if (-not (Test-Path -LiteralPath $config)) { throw "ExpeditionOnline is not installed at $mod" }

    if ([string]::IsNullOrWhiteSpace($PlayerName)) { $PlayerName = Read-Host "Player name" }
    if ([string]::IsNullOrWhiteSpace($PlayerName)) { $PlayerName = "Expeditioner" }
    if ([string]::IsNullOrWhiteSpace($ServerHost)) { $ServerHost = Read-Host "Server host or IP" }
    if ([string]::IsNullOrWhiteSpace($ServerHost)) { $ServerHost = "127.0.0.1" }
    if (-not $PSBoundParameters.ContainsKey('ServerPort')) {
        $portInput = Read-Host "Server port [$ServerPort]"
        if (-not [string]::IsNullOrWhiteSpace($portInput)) { $ServerPort = [int]$portInput }
    }
    if ($ServerPort -lt 1 -or $ServerPort -gt 65535) { throw "Server port must be in 1..65535" }

    Set-IniValue $config "PlayerName" $PlayerName
    Set-IniValue $config "ServerHost" $ServerHost
    Set-IniValue $config "ServerPort" $ServerPort
    Write-Host "ExpeditionOnline configured successfully." -ForegroundColor Green

    if (-not $SkipDoctor) {
        $answer = Read-Host "Run Doctor now? [Y/N]"
        if ($answer -match '^(?i)y') {
            & (Join-Path $PSScriptRoot "ExpeditionOnlineDoctor.exe") --game-path $GamePath
        }
    }
    exit 0
}
catch {
    Write-Host "Configuration failed: $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}
