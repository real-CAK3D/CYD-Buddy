# Portable Voice Architecture

Goal: run CYD Buddy from only the CYD display, XIAO ESP32S3 Sense, and a battery pack.

## What The XIAO Can Do Offline

The XIAO ESP32S3 Sense has the right class of chip for offline embedded speech, but not for full open-ended dictation. The portable voice path should use Espressif ESP-SR:

- WakeNet: local wake word detection.
- MultiNet: local command phrase recognition.
- AFE: microphone front-end processing for 16 kHz, 16-bit, mono audio.

This supports a real wake word plus a fixed command vocabulary. Espressif documents MultiNet as supporting user-defined English/Chinese commands, up to 200 command phrases, with low latency on ESP32-S3.

## What It Cannot Do Alone

The XIAO cannot realistically run full Whisper/Gemma-style speech-to-text and open-ended language understanding while also acting as camera/mic sensor on battery. The CYD cannot do that either.

For open questions such as:

- "What is the weather?"
- "Search this."
- "Remember that I like ..."
- "Tell me what you see and think about it."

the portable boards need a network brain when available:

- CYD or XIAO joins a hotspot/Wi-Fi.
- Relay/server receives audio, text, or command intent.
- Ollama/OpenAI/weather/tool layer answers.
- CYD displays the response and later speaks it through a speaker.

## Practical Offline Command Vocabulary

Start with commands that make the buddy feel alive without requiring cloud STT:

- "wake up"
- "go to sleep"
- "look around"
- "take a picture"
- "what do you see"
- "how are you"
- "tell me a joke"
- "say something rude"
- "be nice"
- "be sarcastic"
- "be quiet"
- "talk more"
- "change mood"
- "happy mode"
- "sad mode"
- "angry mode"
- "suspicious mode"
- "love mode"
- "remember me"
- "forget me"
- "connect wifi"
- "status"

Each command maps to a compact serial/event line for the CYD:

```text
event voice:cmd take_picture
event voice:cmd tell_joke
event voice:cmd remember_me
```

The CYD then uses phrase banks, moods, camera events, and saved memory to respond locally.

## Firmware Direction

The current Arduino XIAO bridge is good for camera snapshots, mic levels, and relay experiments. The next real portable voice milestone is a separate ESP-IDF/ESP-SR XIAO firmware target:

1. Keep the current Arduino firmware as the stable sensor bridge.
2. Add a new ESP-IDF XIAO voice firmware target using ESP-SR WakeNet + MultiNet.
3. Configure wake word first, likely defaulting to a supported built-in wake word while custom wake-name support is evaluated.
4. Add a 20-100 phrase command set.
5. Emit recognized commands to CYD over the chosen board-to-board link.
6. Keep full dictation/Ollama as the online mode.

## Speaker / Text-To-Speech Direction

Offline English TTS on the ESP32-S3 is not the main path. For a physical speaker, use one of these:

- Online/relay TTS when Wi-Fi/Ollama/OpenAI is available.
- Pre-rendered phrase audio files on SD card for offline personality.
- A small dedicated TTS/audio module if fully offline generated speech becomes important.

For the near-term buddy personality, SD-card phrase audio plus CYD text is the best portable fit.
