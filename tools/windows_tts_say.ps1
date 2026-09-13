param(
  [Parameter(Mandatory = $true)]
  [string]$Text,
  [int]$Rate = 0,
  [int]$Volume = 100
)

$ErrorActionPreference = 'Stop'
$voice = New-Object -ComObject SAPI.SpVoice
$voice.Rate = [Math]::Max(-10, [Math]::Min(10, $Rate))
$voice.Volume = [Math]::Max(0, [Math]::Min(100, $Volume))
[void]$voice.Speak($Text)
