#pragma once
// microSD helper layer for board_sonde.ino. Lives in a header because the Arduino
// sketch preprocessor inserts generated prototypes at the top of the .ino, before any
// type declared in it, so a struct in a function signature does not compile there.
#include <Arduino.h>
#include <cctype>

struct SdBus { int cs, di, d0, clk; };

static uint8_t sdXfer(SdBus& b, uint8_t out) {
  uint8_t v = 0;
  for (int i = 7; i >= 0; i--) {
    digitalWrite(b.clk, LOW);  delayMicroseconds(1);
    digitalWrite(b.di, (out >> i) & 1); delayMicroseconds(1);
    digitalWrite(b.clk, HIGH); delayMicroseconds(1);
    v = (uint8_t)((v << 1) | (uint8_t)digitalRead(b.d0));
  }
  digitalWrite(b.clk, LOW); delayMicroseconds(1);
  return v;
}
static void sdCmd(SdBus& b, uint8_t cmd, uint32_t arg, uint8_t crc) {
  sdXfer(b, (uint8_t)(0x40 | cmd));
  sdXfer(b, (uint8_t)(arg >> 24)); sdXfer(b, (uint8_t)(arg >> 16));
  sdXfer(b, (uint8_t)(arg >> 8));  sdXfer(b, (uint8_t)arg);
  sdXfer(b, crc);
}
static uint8_t sdR1(SdBus& b) {
  for (int i = 0; i < 16; i++) { uint8_t r = sdXfer(b, 0xFF); if (r != 0xFF) return r; }
  return 0xFF;
}
static void sdPinMode(SdBus& b) {
  digitalWrite(b.cs, HIGH); delayMicroseconds(50);
  pinMode(b.cs, OUTPUT); pinMode(b.di, OUTPUT); pinMode(b.clk, OUTPUT); pinMode(b.d0, INPUT_PULLUP);
}
static uint8_t sdCmd0Reply(SdBus& b) {                 // cheap discriminator
  sdPinMode(b);
  for (int i = 0; i < 10; i++) sdXfer(b, 0xFF);         // >= 74 clocks, CS high
  digitalWrite(b.cs, LOW); delayMicroseconds(50);
  sdCmd(b, 0, 0, 0x95);
  uint8_t r = sdR1(b);
  digitalWrite(b.cs, HIGH); delayMicroseconds(50);
  return r;
}

static bool sdFullRead(SdBus& b) {
  sdPinMode(b);
  for (int i = 0; i < 10; i++) sdXfer(b, 0xFF);
  digitalWrite(b.cs, LOW); delayMicroseconds(50);
  sdCmd(b, 0, 0, 0x95);        if (sdR1(b) != 0x01) { digitalWrite(b.cs, HIGH); return false; }
  sdCmd(b, 8, 0x000001AA, 0x87);
  uint8_t r8 = sdR1(b);
  uint8_t check[4] = {0,0,0,0};
  if (r8 == 0x01 || r8 == 0x05) for (int i = 0; i < 4; i++) check[i] = sdXfer(b, 0xFF);
  SAY("    CMD8 -> 0x%02x  echo 0x%02x%02x%02x%02x (%s)\n", r8, check[0], check[1], check[2], check[3],
      r8 == 0x01 ? "SD v2 / voltage ok" : r8 == 0x05 ? "rejected: SD v1 or MMC" : "no reply");
  uint8_t r = 0x01;
  for (int i = 0; i < 200 && r == 0x01; i++) {
    sdCmd(b, 55, 0, 0x01); sdR1(b);
    sdCmd(b, 41, 0x40000000, 0x01);                       // HCS: ask for SDHC/SDXC
    r = sdR1(b);
  }
  if (r != 0x00) { SAY("    ACMD41 never left idle (0x%02x)\n", r); digitalWrite(b.cs, HIGH); return false; }
  sdCmd(b, 58, 0, 0x01);                                  // CMD58: OCR
  uint8_t r58 = sdR1(b);
  uint8_t ocr[4] = {0,0,0,0};
  for (int i = 0; i < 4; i++) ocr[i] = sdXfer(b, 0xFF);
  bool ccs = (ocr[0] & 0x40) != 0;
  SAY("    ACMD41 ready, CMD58 -> 0x%02x OCR=%02x%02x%02x%02x -> %s\n", r58, ocr[0], ocr[1], ocr[2], ocr[3],
      ccs ? "SDHC/SDXC (block addressing)" : "SDSC (byte addressing)");

  sdCmd(b, 10, 0, 0x01);                                   // CID: card identity
  if (sdR1(b) == 0x00) {
    for (int i = 0; i < 8 && sdXfer(b, 0xFF) != 0xFE; i++) {}
    uint8_t cid[16]; for (int i = 0; i < 16; i++) cid[i] = sdXfer(b, 0xFF);
    sdXfer(b, 0xFF); sdXfer(b, 0xFF);
    SAY("    CID: mid=0x%02x oid=\"%c%c\" prv=\"%c%c%c%c%c\"\n", cid[0],
        isprint(cid[1]) ? cid[1] : '.', isprint(cid[2]) ? cid[2] : '.',
        isprint(cid[4]) ? cid[4] : '.', isprint(cid[5]) ? cid[5] : '.',
        isprint(cid[6]) ? cid[6] : '.', isprint(cid[7]) ? cid[7] : '.',
        isprint(cid[8]) ? cid[8] : '.');
  }

  sdCmd(b, 16, 512, 0x01); sdR1(b);                       // fixed block length (SDSC)
  sdCmd(b, 17, 0, 0x01);                                   // CMD17: read block 0
  if (sdR1(b) != 0x00) { SAY("    CMD17 rejected\n"); digitalWrite(b.cs, HIGH); return false; }
  static uint8_t blk[512];
  int guard = 0;
  while (guard++ < 100 && sdXfer(b, 0xFF) != 0xFE) {}
  if (guard > 100) { SAY("    no data token\n"); digitalWrite(b.cs, HIGH); return false; }
  for (int i = 0; i < 512; i++) blk[i] = sdXfer(b, 0xFF);
  sdXfer(b, 0xFF); sdXfer(b, 0xFF);
  digitalWrite(b.cs, HIGH);

  SAY("    block 0 head:");
  for (int i = 0; i < 32; i++) SAY(" %02x", blk[i]);
  SAY("\n");
  bool sig = blk[510] == 0x55 && blk[511] == 0xAA;
  SAY("    boot sector signature 0x%02x%02x -> %s\n", blk[510], blk[511], sig ? "OK" : "MISSING");
  for (int e = 0; e < 4; e++) {
    uint8_t* p = blk + 446 + e * 16;
    if (p[4] == 0) continue;
    uint32_t lba = p[8] | (p[9] << 8) | (p[10] << 16) | ((uint32_t)p[11] << 24);
    uint32_t sec = p[12] | (p[13] << 8) | (p[14] << 16) | ((uint32_t)p[15] << 24);
    SAY("    MBR partition %d: type 0x%02x start LBA %u size %u sectors (%s)\n", e + 1, p[4], lba,
        sec, p[4] == 0x0B ? "FAT32" : p[4] == 0x0C ? "FAT32 LBA" : p[4] == 0x06 ? "FAT16" :
              p[4] == 0x07 ? "HPFS/NTFS/exFAT" : p[4] == 0x83 ? "Linux" : "other");
  }
  return sig;
}


// ---------------------------------------------------------------- hierarchical search
// A bare µSD slot usually has no board-side pull-ups at all, and the card's own 50k
// pull-ups on CMD/DAT1..3 do not identify DAT0/CLK/CS, so restricting the search to
// "pads that look pulled-up" simply cannot find the slot. Instead: enumerate
// (CLK, CS, DI) and read the CMD0 reply on *every* remaining pad simultaneously, one
// shared bit-clock. 30^3 tuples with a parallel read stays in seconds.
//
// All three drivers are open-drain (a "1" is only the weak internal pull-up), so the
// sweep can never fight a driver that is really on the board.
// Returns false when a "1" never arrived: the net is being held low by something, and
// every tuple built on that pad would be a silent false negative. With only the ~45k
// internal pull-up available, a capacitive net can take tens of microseconds to rise, so
// the wait is part of the protocol, not a nicety.
static inline bool sdOe(int p, int v) {
  if (!v) { pinMode(p, OUTPUT); digitalWrite(p, LOW); return true; }
  pinMode(p, INPUT_PULLUP);
  for (int i = 0; i < 40; i++) {
    delayMicroseconds(2);
    if (digitalRead(p)) return true;
  }
  return false;
}

static uint8_t g_reply[64];           // HIGH-bit count per candidate DO pad

// The card's R1 for CMD0 is 0x01: exactly one HIGH bit in the idle-low stream. Comparing
// byte-aligned windows would miss it if the response started one clock late, so count the
// HIGH samples over the whole response window instead - alignment free. A floating pad
// held by the internal pull-down gives 0, a pad with a strong pull-up gives all ones.
static bool sdSearchTuple(int cs, int di, int clk, const int* doPins, int n) {
  for (int k = 0; k < n; k++) { pinMode(doPins[k], INPUT_PULLDOWN); g_reply[k] = 0; }
  if (!sdOe(clk, 1)) return false;                                    // clock is dead
  if (!sdOe(di, 1)) return false;                                     // CMD held low
  if (!sdOe(cs, 1)) return false;                                     // CS held low
  for (int i = 0; i < 8; i++) { sdOe(clk, 0); if (!sdOe(clk, 1)) return false; }
  static const uint8_t msg[6] = {0x40, 0x00, 0x00, 0x00, 0x00, 0x95};  // CMD0 + CRC
  sdOe(cs, 0);
  for (int b = 0; b < 6; b++)
    for (int i = 7; i >= 0; i--) {
      if (!sdOe(di, (msg[b] >> i) & 1)) return false;
      sdOe(clk, 0);
      if (!sdOe(clk, 1)) return false;
    }
  sdOe(di, 1);
  for (int i = 0; i < 24; i++) {                                      // 3 response bytes
    sdOe(clk, 0);
    for (int k = 0; k < n; k++)
      if (digitalRead(doPins[k])) g_reply[k]++;
    if (!sdOe(clk, 1)) return false;
  }
  sdOe(cs, 1);
  return true;
}

// A card needs >=74 clocks with DI high once after power-up, not once per tuple.
static bool sdPowerClocks(int clk) {
  if (!sdOe(clk, 1)) return false;
  for (int i = 0; i < 96; i++) { sdOe(clk, 0); if (!sdOe(clk, 1)) return false; }
  return true;
}
