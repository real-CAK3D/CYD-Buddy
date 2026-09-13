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
stream on
stream off
threshold 900
help
```

The bridge prints `BUDDY event ...` lines that can be relayed into the CYD firmware later.

## PC Relay With Ollama

Run this on the Windows host while both boards are plugged in:

```powershell
python tools\cyd_sense_ollama_relay.py --xiao-port COM7 --cyd-port COM8 --model gemma4:latest
```

The relay:

- initializes the XIAO Sense camera/mic bridge
- periodically asks the XIAO for a camera capture
- forwards `BUDDY event ...` lines to the CYD as `event ...`
- asks Ollama for short Gemma responses; set `OLLAMA_URL` or pass `--ollama-url`
- sends Gemma text to the CYD as `say ...`

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

- XIAO ESP32S3 Sense: eyes and ears, camera captures, mic levels, fast sensor events.
- CYD Buddy: face, moods, touch personality, phrase display, persistent tiny memory counters.
- NukeBox Ollama/Gemma: richer speech, chat personality, reasoning, weather/online/tool-backed answers through the PC relay.

Good future onboard model targets:

- XIAO: wake-word, clap/loud/quiet classifier, face/person/motion detection, simple visual mood cues.
- CYD: rule-based mood memory, phrase selection, touch habits, low-cost personality state.
- Ollama/OpenAI: full conversation, tool use, web/weather/system context, longer memory summaries.

The XIAO and CYD are good for tiny classifiers and reflex behavior. They are not practical targets for a full Gemma-style LLM; that stays on NukeBox/Ollama or OpenAI.

## Portable Offline Mode

When both boards are powered from a power bank, they can work without NukeBox or Wi-Fi:

- XIAO advertises over BLE as `CYD-Sense`.
- CYD scans for that BLE service and subscribes to sensor events.
- XIAO initializes its camera/mic automatically on boot.
- XIAO sends tiny/reflex events such as:
  - `event sound:loud level=...`
  - `event sound:quiet`
  - `event face`
  - `event vision:motion`
  - `event vision:dark`
  - `event vision:busy`
- CYD maps those events into moods and phrases locally.
- CYD can also send settings back to XIAO over BLE:
  - `capture`
  - `threshold <level>`
  - `stream on`
  - `stream off`
  - `remember <name>`
  - `forget person`

This is the first tiny-AI layer. It is not a full LLM on-device; it is an offline reflex/classifier layer that makes the buddy portable. Rich chat, weather, online info, and longer reasoning still use Ollama/OpenAI when a relay is available.

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
remember me as <name>
```

The password is saved locally in ESP32 preferences and is not printed by `wifi status`.

When CYD is on a phone hotspot, home Wi-Fi, or any network that can reach Tailscale/Ollama, the PC relay can use `wifi status`, `ollama host`, and normal `say`/`event` commands to give the buddy accurate time, weather, and richer AI responses. Offline, CYD keeps using the XIAO BLE sensor events and local phrase/mood logic.

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
