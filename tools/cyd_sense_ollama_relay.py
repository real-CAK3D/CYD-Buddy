#!/usr/bin/env python3
"""
Relay XIAO ESP32S3 Sense events into the CYD Buddy face and optional Ollama speech.

Data flow:
  XIAO COM7 -> BUDDY event sound:loud / face -> CYD COM8 "event ..."
  event summary -> Ollama/Gemma -> CYD COM8 "say ..."
"""

from __future__ import annotations

import argparse
import json
import os
import queue
import re
import signal
import sys
import threading
import time
from dataclasses import dataclass
from typing import Optional

import requests
import serial
from serial import SerialException


DEFAULT_OLLAMA = os.environ.get("OLLAMA_URL", "http://127.0.0.1:11434")


@dataclass
class BridgeState:
    last_event: str = "idle"
    last_level: Optional[int] = None
    last_frame: Optional[str] = None
    last_ai_at: float = 0.0
    sent_count: int = 0
    ai_count: int = 0


class SerialEndpoint:
    def __init__(self, name: str, port: str, baud: int):
        self.name = name
        self.port = port
        self.baud = baud
        self.ser: Optional[serial.Serial] = None

    def open(self) -> bool:
        if self.ser and self.ser.is_open:
            return True
        try:
            self.ser = serial.Serial(self.port, self.baud, timeout=0.15, write_timeout=0.5)
            print(f"[{self.name}] open {self.port}")
            return True
        except SerialException as exc:
            print(f"[{self.name}] unavailable {self.port}: {exc}")
            self.ser = None
            return False

    def close(self) -> None:
        if self.ser:
            try:
                self.ser.close()
            except SerialException:
                pass
        self.ser = None

    def write_line(self, line: str) -> bool:
        if not self.open() or not self.ser:
            return False
        try:
            self.ser.write((line.rstrip() + "\n").encode("utf-8", errors="replace"))
            return True
        except SerialException as exc:
            print(f"[{self.name}] write failed: {exc}")
            self.close()
            return False

    def read_line(self) -> Optional[str]:
        if not self.open() or not self.ser:
            time.sleep(0.5)
            return None
        try:
            raw = self.ser.readline()
        except SerialException as exc:
            print(f"[{self.name}] read failed: {exc}")
            self.close()
            return None
        if not raw:
            return None
        return raw.decode("utf-8", errors="replace").strip()

    def drain(self, label: Optional[str] = None, seconds: float = 0.35) -> None:
        if not self.ser or not self.ser.is_open:
            return
        end = time.time() + seconds
        old_timeout = self.ser.timeout
        self.ser.timeout = 0.05
        try:
            while time.time() < end:
                raw = self.ser.readline()
                if not raw:
                    continue
                line = raw.decode("utf-8", errors="replace").strip()
                if line:
                    print(f"[{label or self.name}] {line}")
        except SerialException as exc:
            print(f"[{self.name}] drain failed: {exc}")
            self.close()
        finally:
            if self.ser and self.ser.is_open:
                self.ser.timeout = old_timeout


def parse_xiao_line(line: str, state: BridgeState) -> Optional[str]:
    if not line:
        return None

    if line.startswith("BUDDY event "):
        return line[len("BUDDY ") :].strip()

    if line.startswith("{") and line.endswith("}"):
        try:
            payload = json.loads(line)
        except json.JSONDecodeError:
            return None

        kind = payload.get("kind")
        if kind == "camera_frame":
            state.last_frame = f"{payload.get('width', 0)}x{payload.get('height', 0)} {payload.get('bytes', 0)} bytes"
            return None
        if kind == "mic":
            level = int(payload.get("level", -1))
            state.last_level = level
            return None
        return None

    return None


def event_context(event_line: str, state: BridgeState) -> str:
    if "face" in event_line:
        return "The buddy's XIAO camera just captured a face or visual movement."
    if "sound:loud" in event_line:
        level = state.last_level if state.last_level is not None else "unknown"
        return f"The buddy heard a loud sound. Mic level is {level}."
    if "sound:quiet" in event_line:
        return "The room got quiet."
    return f"The buddy received this sensor event: {event_line}."


def ask_ollama(base_url: str, model: str, event_line: str, state: BridgeState, timeout: float) -> Optional[str]:
    prompt = (
        "You are the tiny CYD Buddy face. Respond to the sensor event in one short line, "
        "max 90 characters. Be witty, lifelike, a little sarcastic, but not cruel. "
        "No markdown, no quotes.\n\n"
        f"Event: {event_line}\n"
        f"Context: {event_context(event_line, state)}\n"
        f"Recent frame: {state.last_frame or 'none'}"
    )
    try:
        response = requests.post(
            f"{base_url.rstrip('/')}/api/generate",
            json={
                "model": model,
                "prompt": prompt,
                "stream": False,
                "options": {"temperature": 0.85, "num_predict": 40},
            },
            timeout=timeout,
        )
        response.raise_for_status()
        text = response.json().get("response", "").strip()
    except Exception as exc:
        print(f"[ollama] failed: {exc}")
        return None

    text = re.sub(r"[\r\n]+", " ", text).strip().strip('"')
    return text[:110] if text else None


def xiao_reader(xiao: SerialEndpoint, events: queue.Queue[str], state: BridgeState, stop: threading.Event) -> None:
    while not stop.is_set():
        line = xiao.read_line()
        if line is None:
            continue
        print(f"[xiao] {line}")
        event = parse_xiao_line(line, state)
        if event:
            events.put(event)


def main() -> int:
    parser = argparse.ArgumentParser(description="Bridge XIAO Sense events to CYD Buddy and Ollama.")
    parser.add_argument("--xiao-port", default="COM7")
    parser.add_argument("--cyd-port", default="COM8")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--ollama-url", default=DEFAULT_OLLAMA)
    parser.add_argument("--model", default="gemma4:latest")
    parser.add_argument("--no-ollama", action="store_true")
    parser.add_argument("--no-cyd", action="store_true")
    parser.add_argument("--capture-interval", type=float, default=8.0)
    parser.add_argument("--ai-cooldown", type=float, default=8.0)
    parser.add_argument("--duration", type=float, default=0.0, help="Optional run length in seconds.")
    args = parser.parse_args()

    state = BridgeState()
    events: queue.Queue[str] = queue.Queue()
    stop = threading.Event()

    def stop_now(_sig=None, _frame=None) -> None:
        stop.set()

    signal.signal(signal.SIGINT, stop_now)
    signal.signal(signal.SIGTERM, stop_now)

    xiao = SerialEndpoint("xiao", args.xiao_port, args.baud)
    cyd = SerialEndpoint("cyd", args.cyd_port, args.baud)

    if not xiao.open():
        return 2
    if not args.no_cyd:
        cyd.open()

    reader = threading.Thread(target=xiao_reader, args=(xiao, events, state, stop), daemon=True)
    reader.start()

    time.sleep(0.8)
    xiao.write_line("stream on")
    xiao.write_line("init")

    print("[relay] running; Ctrl+C to stop")
    started = time.time()
    next_capture = time.time() + 1.5

    while not stop.is_set():
        now = time.time()
        if args.duration and now - started >= args.duration:
            break

        if args.capture_interval > 0 and now >= next_capture:
            xiao.write_line("capture")
            next_capture = now + args.capture_interval

        try:
            event_line = events.get(timeout=0.2)
        except queue.Empty:
            continue

        state.last_event = event_line
        print(f"[relay] {event_line}")

        if not args.no_cyd:
            if cyd.write_line(event_line):
                state.sent_count += 1
                cyd.drain("cyd")

        if not args.no_ollama and now - state.last_ai_at >= args.ai_cooldown:
            speech = ask_ollama(args.ollama_url, args.model, event_line, state, timeout=20.0)
            state.last_ai_at = time.time()
            if speech:
                state.ai_count += 1
                print(f"[ollama] {speech}")
                if not args.no_cyd:
                    cyd.write_line(f"say {speech}")
                    cyd.drain("cyd")

    stop.set()
    xiao.close()
    cyd.close()
    print(f"[relay] stopped sent={state.sent_count} ai={state.ai_count}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
