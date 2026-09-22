# CYD-Buddy

Firmware for a CYD / ESP32-2432S028R desk-buddy face. This project is now CYD-only: animated eyes, moods, touch reactions, corner menus, Wi-Fi setup, serial commands, and an SD-card phrase bank all run on the CYD itself.

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

## CYD-Only Scope

The XIAO ESP32S3 Sense path has been removed. CYD Buddy no longer scans for BLE sensors, sends XIAO commands, or expects a camera/microphone companion board.

What remains:

- Touch personality: boop, pet, tickle, eye-poke, idle, bored, sleepy, and wake reactions.
- Mood system: auto/manual modes, selectable personalities, mood-driven eyes, time-of-day colors, custom eye/pupil colors, plus stoner/drunk/hippy and bored/restless/anxious life moods.
- Life stats: health, hunger, play need, restlessness, anxiety, strength, armor, touch counters, directional swipes, missed feedings, and new Wi-Fi/Bluetooth discovery memory.
- Lifecycle stats: boot count, power-loss death count, total alive time, current session time, and estimated dead time between boots.
- Menus: system menu, face menu, on-screen Wi-Fi setup, AI/personality settings, phrase bank, mood/eye/color controls.
- Time/date/weather: Wi-Fi can sync time and fetch current weather from Open-Meteo after latitude/longitude are saved.
- SD phrase bank: built-in phrases plus optional `/cydbuddy/phrases.csv`.
- Wi-Fi setup: save SSID/password locally, connect, sync time, save an Ollama host URL, and dock to Spac3-Gh0st telemetry.
- Serial bridge surface: `say`, `event`, `stats`, and settings commands still work for a PC, phone, or future network bridge.

What is not onboard:

- No microphone on the CYD.
- No camera on the CYD.
- No local LLM or open-ended speech-to-text on the ESP32.
- Voice/chat requires an external host or future separate hardware, but the CYD face no longer depends on that.

## Serial Commands

```text
rotate [0-3]
mood happy
mood stoner
mood drunk
mood hippy
mood bored
mood restless
mood anxious
event face
stats cpu=90 temp=80
tap
boop
pet
tickle
poke left
wake
feed
play
boost
calm
care reset
health
bt seen <bluetooth-device-name>
preferences
memory bank
memory add <thing to remember>
memory think
preference seed
prefer season summer
prefer month October
prefer time night
prefer activity playing
dislike season winter
time 06:30
date 2026-09-14
timezone -5
dst on
dst off
clock 12
clock 24
time edit
schedule edit
schedule
schedule early 04:00
schedule morning 07:00
schedule day 11:00
schedule latepm 16:00
schedule night 20:00
schedule latenight 23:00
lifecycle
time sync
memory
life
stats system
touch cal
touch reset
blink
wink
auto
manual
speak
menu system
menu item <0-4>
menu face
menu close
diag
sd status
phrase add <mood> <phrase>
phrase seed
phrase fast
phrase sd on
phrase sd off
phrase sd status
eye color default
eye color amber
pupil color lime
pupil color default
say <text to show on the speech line>
scroll speed <fast|normal|slow|ms>
name <buddy-name>
personality
personality next
personality <sassy|sweet|rude|nerdy|chill|chaotic>
wifi ssid <hotspot-or-router-name>
wifi pass <password>
wifi edit
wifi setup
wifi connect
wifi scan
wifi status
weather loc <latitude> <longitude>
weather update
weather status
ollama host <url>
spac3 host <url>
spac3 update
spac3 heartbeat
spac3 status
spac3 on
spac3 off
remember me as <name>
```

Color names include `black`, `navy`, `blue`, `sky`, `cyan`, `teal`, `green`, `lime`, `amber`, `yellow`, `red`, `pink`, `purple`, `white`, and `gray`.

## Menus

- Upper-left hotspot opens the system menu.
- Upper-right hotspot opens the face menu.
- Menu rows are selectable.
- Rows can open submenus.
- Back returns to the previous menu or closes the current top-level menu.
- System menus include AI/phrases, Wi-Fi, time/touch settings, and stats.
- The Wi-Fi menu opens a scanned network list first. Tap a network, enter the password with the touch keyboard, then tap `SAVE` to store and connect.
- Face menus include eyes, moods, auto/manual mode, and default/fixed eye color choices.
- Extra moods include `stoner` with light-pink half-open eyes, `drunk` with intentionally offset blinking and drifting pupils, `hippy` with psychedelic animated colors, plus `bored`, `restless`, and `anxious`.
- The time/touch menu opens a date/time/timezone editor, toggles 12/24-hour time, toggles daylight savings, and starts touch calibration.
- The time/touch menu also opens a daily schedule editor for early AM, morning, daytime, late afternoon, night, and late-night start times.
- The stats menu opens care stats, system stats, network stats, and dead-timer/lifecycle stats. Tap left/right/bottom controls to page or close.
- In manual mode, a normal face tap cycles moods.
- In auto mode, a normal face tap wakes or nudges the buddy to react instead of forcing the next mood.
- Drawing/scribbling on the face tickles the buddy.
- A gentle short stroke pets the buddy.
- Strong directional swipes are tracked as left/right/up/down gestures and affect mood/life stats.
- Poking an eye makes that eye squint and increments a tiny persistent memory counter.
- Touch reactions rotate through built-in phrase pools for boops, pets, tickles, eye pokes, wakeups, boredom, and sleepy states.
- Life stats slowly drift over time. Interaction lowers play/restless/anxiety needs, finding new Wi-Fi names increases strength, and remembered Bluetooth names increase armor. The drift is intentionally slow enough for a desk buddy; he should not max out hunger/restlessness just from sitting powered on for a short session.
- `calm` lowers the current care pressure without wiping memory. `care reset` is the stronger recovery command for an obviously stuck/overstressed buddy.
- Feeding and playing can raise health by up to 10.0 total points per 24-hour care day. Feeding has a 30-minute cooldown and playing has a 15-minute cooldown.
- Each new care day costs 5.0 health. If the previous day had no feeding, the buddy records a missed feeding, loses another 0.5 health, gets hungrier, and gets a little more anxious.
- The dead timer estimates powered-off time after the clock is set or Wi-Fi time syncs. Dead time costs 0.1 health per missing minute, raises hunger/play/anxiety, and is saved with lifetime/death counters.
- `boost` is a once-per-calendar-week recovery that returns health and daily needs to 100/clear. It needs valid time from manual date/time entry or Wi-Fi sync so the CYD knows which week it is.
- Seasons are calculated from month and saved weather latitude. Northern hemisphere uses spring/summer/fall/winter normally; southern hemisphere flips the seasons.
- Buddy now learns preferences for season, month, time of day, activities, and weather. Good interactions, feeding, playing, Wi-Fi discoveries, Bluetooth discoveries, and weather updates all nudge persistent preference scores.
- Preferences can change over time. When a new favorite beats an old favorite, the buddy writes a small memory like “I used to favor summer, but now winter is winning.”
- The memory bank keeps five rotating onboard memory notes in ESP32 preferences. `remember <note>` and `memory add <note>` both store a memory.
- Auto mode occasionally speaks from the preference/memory system instead of only the current mood, so his personality drifts with repeated events.
- `preference seed` appends 200 preference/memory phrases per personality to the SD phrase bank as `all` mood rows.
- Default eye colors follow the time of day: early AM yellow/white, morning light blue/yellow, daytime mood-driven colors, evening/night dark blue/black.
- A fixed `eye color <name>` or `pupil color <name>` overrides the default time-of-day color until set back to `default`.
- Text scroll speed can be changed in the AI menu or over serial with `scroll speed fast`, `scroll speed normal`, `scroll speed slow`, or a millisecond value from `50` to `600`.

## SD Phrase Bank

`phrase seed` appends generated phrases for every mood, including stoner/drunk/hippy, to `/cydbuddy/phrases.csv` on the CYD SD card. It does not reformat the SD card or touch other directories. Boot only creates the CSV header if the file is missing; bulk phrase generation is intentionally manual so startup stays responsive.

Add a phrase manually over serial:

```text
phrase add happy I am suspiciously cheerful today.
```

Expand the generated bank:

```text
phrase expand
```

`phrase expand` appends 100 additional generated phrases per mood for every personality. `phrase seed` appends the full generated set: 150 phrases per mood for every personality.

Generated phrase rows are tagged by personality in the CSV `source` column, such as `seed-sassy` or `seed-nerdy`. Custom `user` phrases are shared across personalities.

Fast phrase mode is the default to reduce animation lag. It uses the same mood/personality generator directly from firmware instead of scanning the large SD CSV every time the buddy speaks. Use `phrase sd on` only when you specifically want the buddy to sample custom SD phrases; use `phrase fast` or `phrase sd off` to return to smoother generated speech.

## Personalities

The current personality changes how generated mood phrases sound and which generated SD-card rows are preferred.

- `sassy`: the default smart-mouth buddy style.
- `sweet`: warmer and more supportive.
- `rude`: sharper, crankier, and more insulting.
- `nerdy`: diagnostic, analytical, and system-log flavored.
- `chill`: calmer and more relaxed.
- `chaotic`: louder, weirder, and more dramatic.

Change it from the AI/Ollama menu or over serial:

```text
personality chaotic
personality next
```

## Online AI Path

CYD can save Wi-Fi, weather location, and Ollama host settings, but the ESP32 cannot run a useful local LLM or full speech-to-text model. For real conversation, use an external host that sends short serial/network commands into the CYD:

```text
say <assistant reply>
event voice:cmd tell_joke
event message
stats cpu=80 temp=70
```

The clean split is:

- CYD Buddy: face, moods, touch personality, phrase display, Wi-Fi settings, tiny persistent memory counters.
- External host when available: speech-to-text, LLM/Ollama/OpenAI, weather, online info, and longer memory.

Offline, the CYD remains a self-contained animated buddy with touch-driven personality and SD-backed phrases.

## Spac3-Gh0st Dock

CYD can dock to Hack-Safe v2 / Spac3-Gh0st over its `Wu-Tang LAN` hotspot. The firmware defaults to:

```text
http://10.42.7.1:8766
```

When Wi-Fi is connected and Spac3 telemetry is enabled, CYD polls `/api/cyd/telemetry`, posts a heartbeat to `/api/cyd/heartbeat`, and maps Spac3-Gh0st mood/thought/alert/system fields into the CYD face and bottom speech strip. CYD also accepts the telemetry `time` value as a backup clock source when NTP is unavailable on the hotspot.

Passive late-night telemetry, such as low room light, normal watch, GPS no-fix notes, and quiet weather updates, is stored for status but does not keep Buddy awake. Urgent alerts, hot CPU, camera/audio/motion/device/security events, or direct touch/serial wake events can still wake him.

Useful serial commands:

```text
spac3 status
spac3 update
spac3 heartbeat
spac3 host http://10.42.7.1:8766
spac3 off
spac3 on
```

Wi-Fi passwords are saved only in ESP32 preferences on the CYD, not in this repo.

## Weather And Time

Wi-Fi credentials are saved locally in ESP32 preferences. The password is masked on the CYD screen and is not printed by `wifi status`. If an SSID is saved, the CYD automatically tries to connect on boot.

From the CYD screen:

1. Tap the upper-left corner to open the system menu.
2. Tap `WiFi setup`.
3. Tap a network from the scanned list.
4. Enter the password with the on-screen keyboard.
5. Tap `SAVE` to store the credentials and connect.

Use `MORE` to page through scanned networks, `RESCAN` to refresh the list, or the manual/hidden row to type a hidden SSID.

The serial command `wifi edit` opens the same on-screen editor. The old `wifi setup` command still starts the `CYD-Buddy-Setup` hotspot as a fallback rescue path. Use `wifi scan` to print nearby 2.4 GHz networks and `wifi status` to print the saved SSID, connection state, IP, RSSI, and password length without printing the password.

Set location once:

```text
weather loc 44.1000 -70.2148
```

Then connect and update:

```text
wifi connect
time sync
weather update
```

Weather uses the Open-Meteo forecast API, so no API key is required. Current weather is shown in the small top info line with the synced date/time.

Set clock and calibration without serial:

1. Tap the upper-left corner.
2. Tap `Time/Touch`.
3. Tap `Set date/time` for the on-screen +/- editor, `Daily schedule` for wake/sleep phase times, or `Touch cal` for the two-point touch calibration.

Serial backups:

```text
date 2026-09-14
time 14:30
timezone -5
dst on
clock 12
schedule night 21:30
lifecycle
touch cal
touch reset
```

Dead-time tracking is best-effort until the CYD has a trusted clock. After Wi-Fi time sync or manual date/time entry, it persists the current Unix timestamp while powered on. On the next boot, it counts that power cycle as a death and can calculate the missing off-time once real time is known again.
