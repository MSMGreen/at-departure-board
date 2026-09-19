#include "dechunk.h"

#include <stdlib.h>
#include <string.h>

int Dechunker::read() {
  if (!ensure()) return -1;
  const int c = fn_(ctx_);
  if (c < 0) {
    done_ = true;
    failed_ = true;  // the chunk promised more than the transport delivered
    return -1;
  }
  if (--left_ == 0) {
    fn_(ctx_);  // \r
    fn_(ctx_);  // \n
  }
  return c;
}

size_t Dechunker::readBytes(char* buffer, size_t length) {
  size_t n = 0;
  while (n < length) {
    const int c = read();
    if (c < 0) break;
    buffer[n++] = static_cast<char>(c);
  }
  return n;
}

bool Dechunker::ensure() {
  if (done_) return false;
  if (left_ > 0) return true;

  char line[24];
  size_t n = 0;
  // Read size lines until one is not blank. A loop, not recursion: a peer
  // sending blank lines must not be able to exhaust the fetch task's stack.
  do {
    n = 0;
    for (;;) {
      const int c = fn_(ctx_);
      if (c < 0) {
        done_ = true;
        failed_ = true;
        return false;
      }
      if (c == '\n') break;
      if (c != '\r' && n < sizeof line - 1) line[n++] = static_cast<char>(c);
    }
  } while (n == 0);  // a stray blank line between chunks
  line[n] = '\0';

  char* end = nullptr;
  const long size = strtol(line, &end, 16);  // chunk sizes are hex; ";ext" stops it
  if (end == line || size < 0) {
    done_ = true;
    failed_ = true;
    return false;
  }
  if (size == 0) {
    done_ = true;  // clean end
    return false;
  }
  left_ = static_cast<size_t>(size);
  return true;
}
