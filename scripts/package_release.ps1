param(
    [string]$BuildDir = "build-release",
    [string]$Generator = "MinGW Makefiles",
    [string]$Configuration = "Release",
    [string]$OpenCVDir = "D:/Project/opencv-mingw/x64/mingw/lib",
    [switch]$Force
)

$ErrorActionPreference = "Stop"

$Root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")).Path
$BuildPath = Join-Path $Root $BuildDir
$ReleaseRoot = Join-Path $Root "dist\release"

$VersionFile = Join-Path $Root "VERSION"
if (-not (Test-Path -LiteralPath $VersionFile)) {
    throw "Unable to read ImZiv version: VERSION file not found"
}

$Version = (Get-Content -LiteralPath $VersionFile -Raw).Trim()
if ([string]::IsNullOrWhiteSpace($Version)) {
    throw "Unable to read ImZiv version: VERSION file is empty"
}

$Arch = if ([Environment]::Is64BitProcess) { "x86_64" } else { "x86" }
$PackageName = "ImZiv-$Version-Windows-$Arch"
$PackageDir = Join-Path $ReleaseRoot $PackageName
$ZipPath = Join-Path $ReleaseRoot "$PackageName.zip"

New-Item -ItemType Directory -Force -Path $ReleaseRoot | Out-Null
$ReleaseRootPath = (Resolve-Path -LiteralPath $ReleaseRoot).Path

foreach ($Path in @($PackageDir, $ZipPath)) {
    if (-not (Test-Path -LiteralPath $Path)) {
        continue
    }

    if (-not $Force) {
        throw "Release output already exists: $Path. Re-run with -Force to replace it."
    }

    $ResolvedPath = (Resolve-Path -LiteralPath $Path).Path
    if (-not $ResolvedPath.StartsWith($ReleaseRootPath, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to remove path outside release root: $ResolvedPath"
    }

    Remove-Item -LiteralPath $ResolvedPath -Recurse -Force
}

cmake -S $Root -B $BuildPath -G $Generator `
    "-DCMAKE_BUILD_TYPE=$Configuration" `
    "-DCMAKE_INSTALL_PREFIX=$PackageDir" `
    "-DOpenCV_DIR=$OpenCVDir"

cmake --build $BuildPath --config $Configuration
cmake --install $BuildPath --config $Configuration

Compress-Archive -Path $PackageDir -DestinationPath $ZipPath

Write-Host "Release package: $ZipPath"
