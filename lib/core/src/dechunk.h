#pragma once
#include <stddef.h>
#include <stdint.h>

// Unwraps HTTP chunked transfer-encoding, one chunk header at a time, so
// memory stays flat however large the body is.
//
// AT's GTFS API is chunked. Handing the raw body to a JSON parser is a silent
// failure: it reads the chunk-size line ("2561\r\n") as a valid JSON number,
// stops, and reports success with an empty document - zero departures, no
// error, forever (docs/hardware-notes.md).
//
// read()/readBytes() are the pair ArduinoJson accepts as a custom input, so a
// Dechunker can be passed straight to deserializeJson.
class Dechunker {
 public:
  // Returns the next byte from the transport, or -1 on end-of-stream, error
  // or timeout.
  using ReadFn = int (*)(void* ctx);

  Dechunker(ReadFn fn, void* ctx) : fn_(fn), ctx_(ctx) {}

  int read();
  size_t readBytes(char* buffer, size_t length);

  // True when the framing ended badly: a truncated chunk or an unparsable
  // size line, as opposed to a clean terminating 0-chunk.
  bool failed() const { return failed_; }

 private:
  bool ensure();  // make left_ > 0, reading a chunk header if needed

  ReadFn fn_;
  void* ctx_;
  size_t left_ = 0;
  bool done_ = false;
  bool failed_ = false;
};
