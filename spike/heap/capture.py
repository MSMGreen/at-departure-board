#!/usr/bin/env python3
"""Reset the board and capture serial until a marker or timeout. Throwaway."""
import sys
import time

import serial

PORT = sys.argv[1] if len(sys.argv) > 1 else "COM8"
MARKER = sys.argv[2] if len(sys.argv) > 2 else "SPIKE-STAGE-1-COMPLETE"
TIMEOUT = float(sys.argv[3]) if len(sys.argv) > 3 else 90.0

ser = serial.Serial(PORT, 115200, timeout=1)

# Pulse EN low->high to reset, so we catch output from the very first line.
ser.dtr = False
ser.rts = True
time.sleep(0.15)
ser.rts = False
ser.reset_input_buffer()

print(f"--- capturing {PORT} for up to {TIMEOUT:.0f}s ---", flush=True)
start = time.time()
saw = False
while time.time() - start < TIMEOUT:
    try:
        line = ser.readline().decode("utf-8", errors="replace").rstrip()
    except Exception as e:  # noqa: BLE001
        print(f"[serial error] {e}", flush=True)
        break
    if line:
        print(line, flush=True)
        if MARKER in line:
            saw = True
            break
ser.close()
print(f"--- {'marker seen' if saw else 'TIMED OUT without marker'} ---", flush=True)
sys.exit(0 if saw else 1)
