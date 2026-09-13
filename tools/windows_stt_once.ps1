param(
  [double]$ListenSeconds = 5.0,
  [double]$MinConfidence = 0.25
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Speech

$recognizer = $null
try {
  $culture = [System.Globalization.CultureInfo]::GetCultureInfo('en-US')
  try {
    $recognizer = New-Object System.Speech.Recognition.SpeechRecognitionEngine($culture)
  } catch {
    $recognizer = New-Object System.Speech.Recognition.SpeechRecognitionEngine
  }

  $recognizer.SetInputToDefaultAudioDevice()
  $recognizer.LoadGrammar((New-Object System.Speech.Recognition.DictationGrammar))
  $recognizer.InitialSilenceTimeout = [TimeSpan]::FromSeconds([Math]::Max(1.0, $ListenSeconds))
  $recognizer.BabbleTimeout = [TimeSpan]::FromSeconds([Math]::Max(1.0, $ListenSeconds))
  $recognizer.EndSilenceTimeout = [TimeSpan]::FromMilliseconds(700)
  $recognizer.EndSilenceTimeoutAmbiguous = [TimeSpan]::FromMilliseconds(1000)

  $result = $recognizer.Recognize([TimeSpan]::FromSeconds([Math]::Max(1.0, $ListenSeconds + 1.0)))
  if ($null -eq $result -or $result.Confidence -lt $MinConfidence) {
    exit 0
  }

  [pscustomobject]@{
    text = $result.Text
    confidence = [Math]::Round($result.Confidence, 3)
  } | ConvertTo-Json -Compress
} finally {
  if ($null -ne $recognizer) {
    $recognizer.Dispose()
  }
}
