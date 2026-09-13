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
- System menus include the SD phrase bank and placeholders for XIAO, AI model, and Wi-Fi bridge setup.

## Next Steps

- Expand the phrase bank to 50+ phrases per mood.
- Add an on-screen phrase editor/keyboard.
- Add the XIAO ESP32S3 Sense as a camera/mic sensor node.
- Bridge sensor events and local Gemma output into the CYD over serial, Wi-Fi, BLE, or a local host bridge.
