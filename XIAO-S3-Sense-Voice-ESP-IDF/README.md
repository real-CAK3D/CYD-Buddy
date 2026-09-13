# XIAO S3 Sense Voice ESP-IDF Target

Portable voice firmware target for CYD Buddy.

This is the real battery-only direction: the XIAO ESP32S3 Sense uses its onboard mic for local wake-word and command recognition, then emits compact events that the CYD can react to.

## Target Behavior

- Read the XIAO Sense PDM microphone on GPIO 41/42.
- Feed 16 kHz, 16-bit mono audio into Espressif ESP-SR AFE.
- Use WakeNet for wake-word detection.
- Use MultiNet for offline command recognition.
- Print recognized commands as serial events:

```text
event voice:wake
event voice:cmd take_picture
event voice:cmd tell_joke
```

The CYD or relay can consume those lines exactly like the current `BUDDY event ...` sensor lines.

## Requirements

This project requires ESP-IDF and ESP-SR/ESP-Skainet models. PlatformIO Arduino is not enough for WakeNet/MultiNet.

Recommended setup:

```powershell
git clone --recursive https://github.com/espressif/esp-skainet.git C:\esp\esp-skainet
```

Install the ESP-IDF version recommended by the ESP-Skainet repo, then from an ESP-IDF PowerShell:

```powershell
cd "C:\Users\CAK3D\OneDrive\Documents\ChatGPT\CYD ESP32\XIAO-S3-Sense-Voice-ESP-IDF"
idf.py set-target esp32s3
idf.py build
idf.py -p COM7 flash monitor
```

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
