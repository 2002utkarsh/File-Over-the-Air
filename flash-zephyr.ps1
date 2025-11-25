param (
    [Parameter(Mandatory = $true)]
    [string]$Port
)

# ----------------------------
#  Print helpers
# ----------------------------
function Write-Info($msg)   { Write-Host "[INFO]  $msg" -ForegroundColor Cyan }
function Write-Success($msg){ Write-Host "[OK]    $msg" -ForegroundColor Green }
function Write-ErrorMsg($msg){ Write-Host "[ERROR] $msg" -ForegroundColor Red }

# ----------------------------
#  Locate bossac.exe
# ----------------------------
$bossacPath = $null

if ($env:BOSSAC -and (Test-Path $env:BOSSAC)) {
    $bossacPath = $env:BOSSAC
    Write-Info "Using BOSSAC from environment variable: $bossacPath"
}
elseif (Get-Command "bossac.exe" -ErrorAction SilentlyContinue) {
    $bossacPath = "bossac.exe"
    Write-Info "Using bossac.exe from PATH"
}
else {
    $defaultPath = "C:\Users\Utkarsh Gupta\AppData\Local\Arduino15\packages\arduino\tools\bossac\1.9.1-arduino2\bossac.exe"
    if (Test-Path $defaultPath) {
        $bossacPath = $defaultPath
        Write-Info "Using bossac.exe from default Arduino path"
    }
}

if (-not $bossacPath) {
    Write-ErrorMsg "Could not find bossac.exe! Please add it to PATH or set BOSSAC environment variable."
    exit 1
}

# ----------------------------
#  Find the latest zephyr.bin
# ----------------------------
$projectPath = "C:\UG\Uni\Capstone_Embedded\zephyrproject\build\zephyr"
$binFile = Get-ChildItem -Path $projectPath -Filter "zephyr.bin" -Recurse -ErrorAction SilentlyContinue |
           Sort-Object LastWriteTime -Descending |
           Select-Object -First 1

if (-not $binFile) {
    # Fallback to blinky sample
    $blinkyPath = "C:\UG\Uni\Capstone_Embedded\zephyrproject\zephyr\samples\basic\blinky\build\zephyr\zephyr.bin"
    if (Test-Path $blinkyPath) {
        $binFile = Get-Item $blinkyPath
        Write-Info "Using fallback build from: $($binFile.FullName)"
    }
}

if (-not $binFile) {
    Write-ErrorMsg "No zephyr.bin found in build directory or blinky sample."
    exit 1
}

Write-Info "Found latest build: $($binFile.FullName)"

# ----------------------------
#  Flash using bossac
# ----------------------------
Write-Info "Flashing to $Port..."

$cmd = @(
    "-i",
    "-d",
    "--port=$Port",
    "-e",
    "-w",
    "-v",
    "-b", "`"$($binFile.FullName)`"",
    "-R"
)

& $bossacPath @cmd
$exitCode = $LASTEXITCODE

if ($exitCode -eq 0) {
    Write-Success "Flashing completed successfully!"
} else {
    Write-ErrorMsg "Flashing failed (exit code: $exitCode)"
}
