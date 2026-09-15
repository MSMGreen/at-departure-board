// THROWAWAY SPIKE - stage 1 of 2.
//
// Question: does the memory budget in the design survive the real chip?
// Paper estimate was ~85 KB of ~300 KB: 40 KB TLS + 35 KB lane sprite + 8 KB parser.
//
// This stage needs no WiFi and no display wiring. It proves the toolchain
// works, reports what heap actually exists, and finds the largest contiguous
// block - which is the number that matters, because a 35 KB sprite needs 35 KB
// in ONE piece, not 35 KB scattered.

#include <Arduino.h>

static void banner(const char *s) {
  Serial.println();
  Serial.print("=== ");
  Serial.print(s);
  Serial.println(" ===");
}

static void heap(const char *when) {
  Serial.printf("%-22s free %6u   largest block %6u   min-ever %6u\n", when,
                (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap(),
                (unsigned)ESP.getMinFreeHeap());
}

void setup() {
  Serial.begin(115200);
  delay(1500);

  banner("chip");
  Serial.printf("model      %s rev %d\n", ESP.getChipModel(),
                (int)ESP.getChipRevision());
  Serial.printf("cores      %d @ %d MHz\n", (int)ESP.getChipCores(),
                (int)getCpuFrequencyMhz());
  Serial.printf("flash      %u bytes\n", (unsigned)ESP.getFlashChipSize());
  Serial.printf("sketch     %u used / %u free\n", (unsigned)ESP.getSketchSize(),
                (unsigned)ESP.getFreeSketchSpace());
  Serial.printf("psram      %s\n", psramFound() ? "YES" : "none");
  if (psramFound()) {
    Serial.printf("psram free %u\n", (unsigned)ESP.getFreePsram());
  }

  banner("baseline heap");
  heap("at boot");

  // The lane sprite: 320 x 56 x 2 bytes. Must be one contiguous block.
  banner("lane sprite (320x56x16bpp)");
  const size_t SPRITE = 320 * 56 * 2;
  Serial.printf("want %u bytes contiguous\n", (unsigned)SPRITE);
  uint8_t *sprite = (uint8_t *)malloc(SPRITE);
  Serial.printf("result: %s\n", sprite ? "ALLOCATED" : "FAILED");
  if (sprite) {
    memset(sprite, 0xA5, SPRITE);  // touch it; lazy allocators lie otherwise
    heap("with sprite held");
  }

  // How much more could a TLS session plus a parser take on top?
  banner("headroom on top of the sprite");
  size_t largest = ESP.getMaxAllocHeap();
  Serial.printf("largest single block still available: %u\n", (unsigned)largest);
  Serial.printf("design needs ~40960 for TLS + ~8192 for the parser = 49152\n");
  Serial.printf("verdict: %s\n",
                largest >= 49152 ? "FITS with room" : "TIGHT - investigate");

  // Walk up in 8 KB steps to find the real ceiling while the sprite is held.
  banner("ceiling while sprite is held");
  size_t step = 8192, total = 0;
  void *blocks[48];
  int n = 0;
  while (n < 48) {
    void *p = malloc(step);
    if (!p) break;
    memset(p, 0x5A, step);
    blocks[n++] = p;
    total += step;
  }
  Serial.printf("allocated %u more bytes in %d x %u chunks before failing\n",
                (unsigned)total, n, (unsigned)step);
  heap("at exhaustion");
  for (int i = 0; i < n; i++) free(blocks[i]);
  if (sprite) free(sprite);

  banner("after releasing everything");
  heap("final");

  Serial.println();
  Serial.println("SPIKE-STAGE-1-COMPLETE");
}

void loop() { delay(1000); }
