param(
    [string]$BuildDir = "build",
    [string]$Config = "RelWithDebInfo",
    [string]$OutputRoot = "dist"
)

$ErrorActionPreference = "Stop"

function Invoke-Step {
    param(
        [string]$Name,
        [scriptblock]$Action
    )

    Write-Host "[STEP] $Name"
    & $Action
}

function Require-File {
    param([string]$Path)
    if (-not (Test-Path $Path)) {
        throw "Required file not found: $Path"
    }
}

$timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
$packageRoot = Join-Path $OutputRoot "novaria-$timestamp"
$binDir = Join-Path $packageRoot "bin"
$configDir = Join-Path $packageRoot "config"
$modsDir = Join-Path $packageRoot "mods"
$symbolsDir = Join-Path $packageRoot "symbols"

Invoke-Step "Configure" {
    cmake -S . -B $BuildDir -G "Visual Studio 17 2022" -A x64
}

Invoke-Step "Build" {
    cmake --build $BuildDir --config $Config
}

Invoke-Step "Prepare package directories" {
    if (Test-Path $packageRoot) {
        Remove-Item -Recurse -Force $packageRoot
    }

    New-Item -ItemType Directory -Force $packageRoot | Out-Null
    New-Item -ItemType Directory -Force $binDir | Out-Null
    New-Item -ItemType Directory -Force $configDir | Out-Null
    New-Item -ItemType Directory -Force $modsDir | Out-Null
    New-Item -ItemType Directory -Force $symbolsDir | Out-Null
}

$runtimeBin = Join-Path $BuildDir $Config
$executables = @(
    "novaria.exe",
    "novaria_server.exe",
    "novaria_net_smoke.exe",
    "novaria_net_soak.exe"
)

Invoke-Step "Copy runtime binaries" {
    foreach ($exeName in $executables) {
        $sourcePath = Join-Path $runtimeBin $exeName
        Require-File $sourcePath
        Copy-Item $sourcePath -Destination (Join-Path $binDir $exeName) -Force
    }

    $sdlDll = Join-Path $runtimeBin "SDL3.dll"
    if (Test-Path $sdlDll) {
        Copy-Item $sdlDll -Destination (Join-Path $binDir "SDL3.dll") -Force
    }
}

Invoke-Step "Copy config and mods" {
    Copy-Item "config/game.cfg" -Destination (Join-Path $configDir "game.cfg") -Force
    Copy-Item "mods/core" -Destination (Join-Path $modsDir "core") -Recurse -Force
}

Invoke-Step "Collect symbols" {
    foreach ($exeName in $executables) {
        $pdbName = [System.IO.Path]::ChangeExtension($exeName, ".pdb")
        $pdbSource = Join-Path $runtimeBin $pdbName
        if (Test-Path $pdbSource) {
            Copy-Item $pdbSource -Destination (Join-Path $symbolsDir $pdbName) -Force
        }
    }
}

Invoke-Step "Generate checksums" {
    $checksumFile = Join-Path $packageRoot "checksums.sha256"
    if (Test-Path $checksumFile) {
        Remove-Item $checksumFile -Force
    }

    Get-ChildItem $packageRoot -Recurse -File |
        Where-Object { $_.Name -ne "checksums.sha256" } |
        Sort-Object FullName |
        ForEach-Object {
            $hash = (Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLower()
            $relativePath = $_.FullName.Substring($packageRoot.Length + 1).Replace('\', '/')
            "$hash  $relativePath" | Out-File -FilePath $checksumFile -Encoding utf8 -Append
        }
}

Invoke-Step "Write release manifest" {
    $manifestPath = Join-Path $packageRoot "release-manifest.txt"
    @(
        "package_root=$packageRoot"
        "build_dir=$BuildDir"
        "build_config=$Config"
        "generated_at=$(Get-Date -Format "yyyy-MM-dd HH:mm:ss")"
        "binaries=$($executables -join ',')"
    ) | Out-File -FilePath $manifestPath -Encoding utf8
}

Write-Host "[DONE] Release package prepared: $packageRoot"
