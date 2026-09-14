# XIAO S3 Sense Voice ESP-IDF Target

Portable voice firmware target for CYD Buddy.

This is the real battery-only direction: the XIAO ESP32S3 Sense uses its onboard mic for local wake-word and command recognition, then emits compact events that the CYD can react to.

## Target Behavior

- Read the XIAO Sense PDM microphone on GPIO 41/42.
- Feed 16 kHz, 16-bit mono audio into Espressif ESP-SR AFE.
- Use WakeNet for wake-word detection. The current built-in wake phrase is `Jarvis`; a true `Buddy` wake word will require a custom WakeNet model later. Other built-in ESP-SR wake models can be selected at build time in `sdkconfig.defaults`.
- Use MultiNet for offline command recognition with a larger CYD Buddy intent vocabulary.
  The current flashed build registers 83 active offline commands out of the
  100-entry intent table; ESP-SR rejects some short or question-like phrases,
  so the firmware keeps the accepted mobile command set and logs rejected
  phrases at boot.
- Print recognized commands as serial events:

```text
event voice:wake
event voice:cmd take_picture
event voice:cmd tell_joke
event voice:cmd use_ollama
event voice:cmd remember_me
```

The CYD or relay can consume those lines exactly like the current `BUDDY event ...` sensor lines.

## Requirements

This project requires ESP-IDF and ESP-SR/ESP-Skainet models. PlatformIO Arduino is not enough for WakeNet/MultiNet.

Recommended setup:

```powershell
git clone --recursive https://github.com/espressif/esp-skainet.git C:\esp\esp-skainet
```

Install ESP-IDF v5.0.x, then from an ESP-IDF PowerShell:

```powershell
cd "C:\Users\CAK3D\OneDrive\Documents\ChatGPT\CYD ESP32"
powershell -ExecutionPolicy Bypass -File tools\setup_xiao_voice_idf.ps1 -Port COM7
powershell -ExecutionPolicy Bypass -File tools\setup_xiao_voice_idf.ps1 -Port COM7 -Flash
```

The helper script stages this project into `C:\esp\cyd-buddy-idf` before building.
ESP-SR 2.0.5 has linker flags that break when the project path contains spaces,
so do not build this target directly from the OneDrive `CYD ESP32` folder.

## Status

This is a starter target. It is intentionally separate from `XIAO-S3-Sense-Bridge` so the working Arduino camera/mic bridge stays intact while ESP-SR voice work is brought up.

Expected bring-up order:

1. Build ESP-IDF project with ESP-SR dependencies and model partition.
2. Confirm PDM mic feed on XIAO pins.
3. Confirm WakeNet trigger.
4. Configure MultiNet command list.
5. Map command IDs to CYD events.
6. Add board-to-board transport after serial recognition is stable.

## Offline Versus Online

This target is for offline wake word and command phrases. It is not full dictation. Open-ended questions still need online Ollama/OpenAI/weather tooling.

The mobile fallback is:

- XIAO S3 Sense listens for the wake word and offline commands.
- XIAO emits compact `event voice:*` lines.
- CYD maps those events to moods and local phrasebank replies.
- When WiFi/Tailscale/Ollama is reachable, the same command layer can hand off to the bigger brain.

Wake-word naming is separate from the CYD display name. The CYD can call itself `Buddy`, and the CYD firmware already has saved-name and train-name controls. The XIAO cannot wake to an arbitrary spoken name until a matching WakeNet model or a different wake engine exists. Built-in ESP-SR wake models can still be swapped at build time.
