# CYD-Buddy

Firmware for a CYD / ESP32-2432S028R desk-buddy face. The current build runs animated eyes, moods, corner menus, a transparent speech line, serial commands, and a dedicated SD-card phrase-bank folder.

## Project

- Firmware: `CYD-Buddy-Eyes`
- Board: ESP32 Dev Module / CYD with ILI9341 TFT
- Display pins: configured in `CYD-Buddy-Eyes/User_Setup.h`
- SD phrase folder on the CYD card: `/cydbuddy/`
- Phrase file on the CYD card: `/cydbuddy/phrases.csv`

The firmware only creates or writes inside `/cydbuddy/` on the SD card. It does not reformat the card or touch unrelated files.

## Build And Flash

```powershell
cd "CYD-Buddy-Eyes"
$env:PLATFORMIO_CORE_DIR='.pio-core'
python -m platformio run
python -m platformio run -t upload --upload-port COM8
```

Use the live port shown by Windows if it is not `COM8`.

## Serial Commands

```text
rotate [0-3]
mood happy
event face
stats cpu=90 temp=80
tap
boop
pet
tickle
poke left
wake
time 06:30
memory
blink
wink
auto
manual
speak
menu system
menu face
menu close
sd status
phrase add <mood> <phrase>
eye color default
eye color amber
pupil color lime
pupil color default
say <text to show on the speech line>
scroll speed <fast|normal|slow|ms>
name <buddy-name>
train name
```

Color names include `black`, `navy`, `blue`, `sky`, `cyan`, `teal`, `green`, `lime`, `amber`, `yellow`, `red`, `pink`, `purple`, `white`, and `gray`.

## Menus

- Upper-left hotspot opens the system menu.
- Upper-right hotspot opens the face menu.
- Menu rows are selectable.
- Rows can open submenus.
- Back returns to the previous menu or closes the current top-level menu.
- Face menus include auto/manual mode and default/fixed eye color choices.
- In manual mode, a normal face tap cycles moods.
- In auto mode, a normal face tap wakes or nudges the buddy to react instead of forcing the next mood.
- Drawing/scribbling on the face tickles the buddy.
- A gentle short stroke pets the buddy.
- A normal face tap in auto mode boops or nudges the buddy.
- Poking an eye makes that eye squint and increments a tiny persistent memory counter.
- Touch reactions now rotate through larger built-in phrase pools for boops, pets, tickles, eye pokes, wakeups, boredom, and sleepy states.
- Default eye colors follow the time of day: early AM yellow/white, morning light blue/yellow, daytime mood-driven colors, evening/night dark blue/black.
- A fixed `eye color <name>` or `pupil color <name>` overrides the default time-of-day color until set back to `default`.
- System menus include the SD phrase bank and placeholders for XIAO, AI model, and Wi-Fi bridge setup.
- The AI menu includes the buddy name, wake-name training, text scroll speed, and online status.
- Text scroll speed can also be changed over serial with `scroll speed fast`, `scroll speed normal`, `scroll speed slow`, or a millisecond value from `50` to `600`.

## Next Steps

- Expand the phrase bank to 50+ phrases per mood.
- Add an on-screen phrase editor/keyboard.
- Add the XIAO ESP32S3 Sense as a camera/mic sensor node.
- Bridge sensor events and local Gemma output into the CYD over serial, Wi-Fi, BLE, or a local host bridge.

## XIAO S3 Sense Bridge

- Firmware: `XIAO-S3-Sense-Bridge`
- Board: Seeed Studio XIAO ESP32S3 Sense
- Current role: USB serial sensor node for camera/mic events.

```powershell
cd "XIAO-S3-Sense-Bridge"
$env:PLATFORMIO_CORE_DIR='..\CYD-Buddy-Eyes\.pio-core'
python -m platformio run
python -m platformio run -t upload --upload-port COM7
```

If the ESP32-S3 compiler fails on Windows with `CreateProcess: No such file or directory`, build from a short temp path:

```powershell
$proj="$env:TEMP\xiao"
$core="$env:TEMP\piocorex"
Remove-Item -LiteralPath $proj -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Path $proj | Out-Null
Copy-Item -Path ".\XIAO-S3-Sense-Bridge\*" -Destination $proj -Recurse -Force
cd $proj
$env:PLATFORMIO_CORE_DIR=$core
python -m platformio run -j 1
python -m platformio run -t upload --upload-port COM7 -j 1
```

Serial commands:

```text
status
init
capture
snapshot 3000
voice on
voice off
wake name Buddy
wake train Buddy
stream on
stream off
threshold 900
help
```

The bridge prints `BUDDY event ...` lines that can be relayed into the CYD firmware. The current Arduino wake-name feature records a lightweight mic-level signature of the spoken name; it is a temporary wake trigger, not real speech-to-text.

For the portable voice target, the XIAO mic must run local speech recognition. The planned path is a separate ESP-IDF/ESP-SR firmware using WakeNet for wake-word detection and MultiNet for offline command phrases. See [Portable Voice Architecture](docs/portable_voice_architecture.md).

## XIAO Portable Voice Target

- Firmware: `XIAO-S3-Sense-Voice-ESP-IDF`
- Framework: ESP-IDF with ESP-SR
- Current role: starter target for battery-only wake word and offline command phrases.

This target is intentionally separate from the Arduino bridge so the current camera/mic sensor firmware stays usable while portable voice is brought up.

The offline command vocabulary starts in:

```text
XIAO-S3-Sense-Voice-ESP-IDF/commands_en.txt
```

The first event format is serial text:

```text
event voice:wake
event voice:cmd take_picture id=1 prob=0.95
```

Build helper:

```powershell
powershell -ExecutionPolicy Bypass -File tools\setup_xiao_voice_idf.ps1 -Port COM7
```

This requires an ESP-IDF shell with ESP-SR/ESP-Skainet dependencies available. The helper currently reports that ESP-IDF is not active in this Windows shell.

## Dev Relay With Ollama

Run this on the Windows host while both boards are plugged in. This is a development bridge, not the final portable voice path:

```powershell
python tools\cyd_sense_ollama_relay.py --xiao-port COM7 --cyd-port COM8 --model gemma4:latest
```

The relay:

- initializes the XIAO Sense camera/mic bridge
- sets the XIAO to 3-second snapshot mode with live mic events
- periodically asks the XIAO for a camera capture
- forwards `XIAO_CMD ...` lines from CYD to the XIAO so CYD menus can change S3 settings while direct BLE is paused
- forwards `BUDDY event ...` lines to the CYD as `event ...`
- asks Ollama for short Gemma responses; set `OLLAMA_URL` or pass `--ollama-url`
- sends Gemma text to the CYD as `say ...`
- can use Windows speech recognition for temporary spoken-prompt testing
- can use Windows/SAPI text-to-speech for temporary reply testing

Dev speech-to-text currently uses the Windows default microphone through the relay. That is useful for testing Ollama conversations, but it is not the portable target. Wake-triggered listening is enabled by default:

```powershell
python tools\cyd_sense_ollama_relay.py --xiao-port COM7 --cyd-port COM8 --model gemma4:latest --stt windows
```

For testing without the wake trigger, keep the microphone listening in short repeated windows:

```powershell
python tools\cyd_sense_ollama_relay.py --xiao-port COM7 --cyd-port COM8 --model gemma4:latest --stt windows --stt-always
```

Text-to-speech is optional and off by default. To speak replies through Windows audio:

```powershell
python tools\cyd_sense_ollama_relay.py --xiao-port COM7 --cyd-port COM8 --model gemma4:latest --stt windows --tts windows
```

When a physical speaker is added to the buddy hardware, online TTS can be routed to that output path. For fully offline portable speech, prefer pre-rendered phrase audio on SD card or a dedicated audio/TTS module.

For testing without the CYD:

```powershell
python tools\cyd_sense_ollama_relay.py --no-cyd --duration 30
```

For NukeBox Ollama, pass the NukeBox Ollama URL at runtime:

```powershell
$env:OLLAMA_URL='http://<nukebox-tailscale-ip>:11434'
python tools\cyd_sense_ollama_relay.py --xiao-port COM7 --cyd-port COM8 --model gemma4:latest
```

## AI Split

The current working split is:

- XIAO ESP32S3 Sense: eyes and ears, camera captures, mic levels, fast sensor events; future ESP-SR wake word and command recognition.
- CYD Buddy: face, moods, touch personality, phrase display, persistent tiny memory counters.
- NukeBox Ollama/Gemma: richer speech, chat personality, reasoning, weather/online/tool-backed answers through the PC relay.

Good future onboard model targets:

- XIAO: ESP-SR WakeNet wake word, ESP-SR MultiNet command phrases, clap/loud/quiet classifier, face/person/motion detection, simple visual mood cues.
- CYD: rule-based mood memory, phrase selection, touch habits, low-cost personality state.
- Ollama/OpenAI: full conversation, tool use, web/weather/system context, longer memory summaries.
- Optional ESP32 devboard: useful later as an audio/speaker board, simple UART/BLE/Wi-Fi bridge, or debug middleman; not the main STT board.

The XIAO and CYD are good for wake words, fixed speech commands, tiny classifiers, and reflex behavior. They are not practical targets for full open-ended dictation or a Gemma-style LLM; that stays on NukeBox/Ollama or OpenAI when Wi-Fi is available.

## Portable Offline Mode

When both boards are powered from a power bank, they can work without NukeBox or Wi-Fi:

- XIAO advertises over BLE as `CYD-Sense`.
- Direct CYD-to-XIAO BLE is currently paused on the CYD because ESP32 BLE client attach was unstable.
- XIAO initializes its camera/mic automatically on boot, but now defaults to cool snapshot mode instead of continuous event streaming.
- XIAO sends tiny/reflex events such as:
  - `event sound:loud level=...`
  - `event sound:quiet`
  - `event face`
  - `event vision:motion`
  - `event vision:dark`
  - `event vision:busy`
- CYD maps those events into moods and phrases locally when events arrive by serial/relay.

The XIAO Sense can get hot if the camera is treated like a live video stream. Use snapshot mode with live mic events by default:

```text
cool
voice on
snapshot 3000
capture
status
```

Use `active` only for short tests; it samples faster and runs hotter.

This is the first tiny-AI layer. It is not a full LLM on-device; it is an offline reflex/classifier layer that makes the buddy portable. Rich chat, weather, online info, and longer reasoning still use Ollama/OpenAI when a relay is available.

Portable voice target:

- XIAO mic runs ESP-SR WakeNet for the wake word.
- XIAO mic runs ESP-SR MultiNet for a fixed command phrase list.
- XIAO sends recognized command events to CYD.
- CYD answers from local phrase banks, moods, memory, and camera/mic events.
- When Wi-Fi/Ollama is reachable, the same command can escalate to richer AI.

Full arbitrary speech-to-text is not expected to run locally on the CYD/XIAO pair. Offline voice should be command recognition; online voice can be full dictation.

## CYD Wi-Fi and Online Bridge

CYD firmware now uses the larger `huge_app.csv` partition so BLE, Wi-Fi, SD, touch, and the face UI can fit together on the 4 MB CYD.

Serial commands for hotspot/Tailscale/Ollama setup:

```text
wifi ssid <hotspot-or-router-name>
wifi pass <password>
wifi connect
wifi status
time sync
ollama host <url>
xiao <command>
xiao connect
remember me as <name>
phrase seed
name Buddy
train name
scroll speed fast
```

The password is saved locally in ESP32 preferences and is not printed by `wifi status`.

When CYD is on a phone hotspot, home Wi-Fi, or any network that can reach Tailscale/Ollama, the PC relay can use `wifi status`, `ollama host`, and normal `say`/`event` commands to give the buddy accurate time, weather, and richer AI responses. While direct BLE is paused, use the relay to pass XIAO snapshot events into CYD.

`phrase seed` appends 50 generated phrases per mood to `/cydbuddy/phrases.csv` on the CYD SD card. It does not reformat the SD card or touch other directories.

## Remembered Person

`remember me as <name>` tells CYD to send `remember <name>` to the XIAO Sense. XIAO captures a lightweight visual signature and stores it in its own preferences. When later camera samples look similar, XIAO emits:

```text
event person:<name>
```

CYD responds by greeting that name. This is a lightweight portable recognition scaffold, not full face-recognition embeddings yet. A later ESP-DL/TFLite or network AI layer can replace the visual signature with real face/audio identity recognition.

## MimiClaw Reference

Local reference file found:

```text
C:\Users\CAK3D\Downloads\MimiClaw__ESP32-S3_.bin
```

`esptool image-info` identifies it as an ESP32-S3 image for 16 MB flash, built with ESP-IDF v5.5.2, compile time `Mar 17 2026 04:24:31`, size `16,711,680` bytes. Treat it as a reference artifact only for now; it is too large/mismatched for the 8 MB XIAO Sense flash target.
