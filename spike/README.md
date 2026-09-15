# Throwaway spikes

These exist to answer one question each, on real hardware, before the firmware
is designed around a guess. They are **not** the firmware and are not built or
tested by CI.

`heap/` — does TLS + a streaming JSON parse + the 35 KB lane sprite fit in
ESP32 heap at the same time? Answer: yes, with roughly 2x headroom. It also
turned up the chunked-transfer-encoding bug that would otherwise have shipped.

Findings are written up in `docs/hardware-notes.md`. The code is kept only
because `stage3.cpp` contains a working de-chunking Stream the firmware will
need; delete the rest once that lands.
