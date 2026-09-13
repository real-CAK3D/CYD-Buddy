param(
  [string]$Project = "XIAO-S3-Sense-Voice-ESP-IDF",
  [string]$Port = "COM7",
  [string]$StageRoot = "C:\esp\cyd-buddy-idf",
  [switch]$Flash,
  [switch]$Monitor
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$projectPath = Join-Path $root $Project
$stagePath = Join-Path $StageRoot $Project

if (-not (Test-Path $projectPath)) {
  throw "Project not found: $projectPath"
}

$idf = Get-Command idf.py -ErrorAction SilentlyContinue
if (-not $idf) {
  Write-Host "ESP-IDF is not active in this shell."
  Write-Host "Install/open the ESP-IDF PowerShell recommended by ESP-Skainet, then rerun:"
  Write-Host "  powershell -ExecutionPolicy Bypass -File tools\setup_xiao_voice_idf.ps1 -Port $Port"
  exit 2
}

if ($stagePath -match '\s') {
  throw "ESP-SR 2.0.5 link flags do not tolerate spaces. Use a StageRoot with no spaces: $stagePath"
}

if ((Test-Path $stagePath) -and (-not ((Resolve-Path $stagePath).Path.StartsWith((Resolve-Path $StageRoot).Path)))) {
  throw "Refusing to clean unexpected staging path: $stagePath"
}

New-Item -ItemType Directory -Force -Path $stagePath | Out-Null

$sourceItems = @(
  "CMakeLists.txt",
  "commands_en.txt",
  "dependencies.lock",
  "main",
  "partitions.csv",
  "README.md",
  "sdkconfig.defaults"
)

foreach ($item in $sourceItems) {
  $src = Join-Path $projectPath $item
  $dst = Join-Path $stagePath $item
  if (-not (Test-Path $src)) {
    continue
  }
  if (Test-Path $dst) {
    Remove-Item -LiteralPath $dst -Recurse -Force
  }
  Copy-Item -LiteralPath $src -Destination $dst -Recurse -Force
}

Push-Location $stagePath
try {
  Remove-Item -LiteralPath (Join-Path $stagePath "sdkconfig") -Force -ErrorAction SilentlyContinue
  Remove-Item -LiteralPath (Join-Path $stagePath "sdkconfig.old") -Force -ErrorAction SilentlyContinue
  idf.py set-target esp32s3
  idf.py build
  if ($Flash) {
    idf.py -p $Port flash
  }
  if ($Monitor) {
    idf.py -p $Port monitor
  }
  Write-Host "Build complete in $stagePath."
  Write-Host "Firmware: $(Join-Path $stagePath 'build\xiao_s3_sense_voice.bin')"
  if (-not $Flash) {
    Write-Host "To flash:"
    Write-Host "  powershell -ExecutionPolicy Bypass -File tools\setup_xiao_voice_idf.ps1 -Port $Port -Flash"
  }
} finally {
  Pop-Location
}
