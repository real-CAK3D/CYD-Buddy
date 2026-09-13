param(
  [string]$Project = "XIAO-S3-Sense-Voice-ESP-IDF",
  [string]$Port = "COM7"
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$projectPath = Join-Path $root $Project

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

Push-Location $projectPath
try {
  idf.py set-target esp32s3
  idf.py build
  Write-Host "Build complete. To flash:"
  Write-Host "  idf.py -p $Port flash monitor"
} finally {
  Pop-Location
}
