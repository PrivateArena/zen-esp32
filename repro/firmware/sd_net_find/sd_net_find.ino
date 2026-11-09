// sd_net_find — targeted microSD net discovery, CAM+SD board (yuanxinwei-esp32s3-cam-sd).
//
// Why this exists: the exhaustive board_sonde S4 sweep reported "0 replies" and concluded the
// slot was dead. That sweep drives every line OPEN-DRAIN (a "1" is the ~45 kOhm internal
// pull-up). The header silkscreen marks GPIO38/39/40 with the board's SD-card symbol and the
// pad survey reads them as strong-high, i.e. a board pull-up in the ~10 kOhm class. Against a
// 10 k pull-up an open-drain "0" sits at ~2.7 V - the command was never actually asserted low,
// so the sweep was blind on exactly the pads that matter. This probe drives push-pull SPI, which
// is what a real SD transaction uses.
//
// Net map from silkscreen (back legend "~ : SD Card"): 38, 39, 40 are SD nets. The fourth one
// is unknown, so: permute (CS, DI, DO, CLK) over {38, 39, 40, X} for every candidate X, with
// push-pull CMD0, and prove any winner by reading block 0.
//
// Build: PSRAM=disabled is REQUIRED if X may be 33-37; those pads belong to octal PSRAM on this
// N16R8 module and are only legal to touch with PSRAM off. The probe refuses otherwise.
#include <Arduino.h>
#define SAY(...)  do { Serial0.printf(__VA_ARGS__); } while (0)
#include "sd_probe.h"

static const int kMarked[3] = { 38, 39, 40 };         // "~" on the silk
// candidate for the 4th net: user pads, minus flash (26-32), console (43/44), input-only 46,
// boot straps (0/45), and native USB (19/20, which the debug console may be sitting on).
static const int kCand[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 21,
                             33, 34, 35, 36, 37, 41, 42, 47, 48 };

static void release(int p) { pinMode(p, INPUT); }

// How hard is something pulling this pad up? A board pull-up beats the ~45 k internal
// pull-down; the card's own 50 k does not. Reported so the result is falsifiable.
static void pullClass(int p) {
  pinMode(p, INPUT);            int f = digitalRead(p);
  pinMode(p, INPUT_PULLUP);     int u = digitalRead(p);
  pinMode(p, INPUT_PULLDOWN);   int d = digitalRead(p);
  release(p);
  SAY("  GPIO%-3d F=%d U=%d D=%d -> %s\n", p, f, u, d,
      (f && u && d) ? "strong-high: board pull-up or driver (<~45k)"
                    : (f && u && !d) ? "external pull-up ~50k: a card net"
                    : (f ? "floating-high" : "low/floating"));
}

static int tryTuple(SdBus b) {
  uint8_t r = sdCmd0Reply(b);
  if (r == 0xFF) return 0;
  SAY("  CMD0 -> 0x%02x  CS=%d MOSI=%d MISO=%d CLK=%d\n", r, b.cs, b.di, b.d0, b.clk);
  if (r != 0x01 && r != 0x00) return 0;
  if (sdFullRead(b)) SAY("  >>> NET MAP PROVEN: CS=%d MOSI=%d MISO=%d CLK=%d\n", b.cs, b.di, b.d0, b.clk);
  else SAY("  >>> reply decoded but block 0 not read: a reply is not proof\n");
  return 1;
}

void setup() {
  Serial0.begin(115200, SERIAL_8N1, 44, 43);
  delay(500);
  SAY("\n\n=== sd_net_find ===\n");
  if (psramFound()) {
    SAY("  ABORT: this build has PSRAM live, so GPIO33-37 are not probeable here.\n"
        "  Rebuild with :PSRAM=disabled (see AGENTS.md) or the 4th net cannot be tested.\n");
    return;
  }
  SAY("  silk-marked SD pads:");
  for (int p : kMarked) { SAY(" %d", p); }
  SAY("\n");
  for (int p : kMarked) pullClass(p);

  int n = 0;
  for (int x : kCand) {
    int set[4] = { kMarked[0], kMarked[1], kMarked[2], x };
    // 24 role assignments of the 4 pads
    for (int a = 0; a < 4; a++) for (int b = 0; b < 4; b++) if (b != a)
    for (int c = 0; c < 4; c++) if (c != a && c != b)
    for (int d = 0; d < 4; d++) if (d != a && d != b && d != c) {
      SdBus bus { set[a], set[b], set[c], set[d] };      // cs, di, d0, clk
      if (tryTuple(bus)) { n++; }
    }
  }
  SAY("  done: %u candidate tuple(s) answered\n", n);
  for (int p : kMarked) release(p);
  for (int p : kCand) release(p);
  SAY("  pads released\n");
}

void loop() { delay(1000); }
