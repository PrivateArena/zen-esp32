// board_sonde — identify an unknown ESP32-S3 dev board purely from software.
//
// Stages (each one is non-destructive; nothing is driven on flash/PSRAM pads):
//   S0 identity      chip, rev, crystal, flash size, PSRAM class + runtime size
//   S1 passive survey every pad read floating / with internal PU / with internal PD,
//                    which exposes board-level pull-ups (I2C bus, SD slot, straps)
//   S2 SCCB sweep    bit-bang I2C over every ordered pair of drive-safe GPIOs,
//                    full 0x08..0x77 address sweep per pair
//   S3 sensor ID     if something ACKs, read the OV2640 / OV5640 chip-id registers
//   S4 SD probe      SD in 1-bit SPI mode over the pull-up marked pin group,
//                    looking for the CMD0 idle reply 0x01
//
// Console is UART0 (GPIO43/44) = the board's USB-UART bridge, addressed as Serial0
// so it does not depend on the CDCOnBoot FQBN option.
//
// PROBE_I2C: the SCCB passes are only meaningful with a camera module fitted (pass C
// rotates XCLK over every pad and costs ~40 s). Set to 1 for the camera sweep.
#define PROBE_I2C 0
// PROBE_SD=1 runs the exhaustive slot sweep (~6 min). PROBE_SD=0 skips it and runs the
// live monitor instead, which is the tool you want while someone is swapping cards.
#define PROBE_SD 0
#define MONITOR_SECS 0   // 0 = watch until the board is reset

// PROBE_OPI_PADS: also survey and drive GPIO33-37. That is ONLY safe in a build made
// with PSRAM=disabled, because re-parenting an OPI PSRAM pad to the GPIO matrix breaks
// PSRAM access the same way it breaks flash on 26-32. stage1() refuses to do it while
// psramFound() is true. Use this to answer "does this board route the octal-PSRAM pads
// out to a connector?" - a question no amount of pin-reading can answer otherwise.
#define PROBE_OPI_PADS 1

// Restore the factory image afterwards with:
//   esptool --port <p> write-flash 0x0 repro/backup-new-s3-app.bin

#include <Arduino.h>
#include <vector>
#include <cctype>
#include <cstring>
#include "esp_chip_info.h"
#include "esp_mac.h"
#include "driver/gpio.h"
#include "soc/gpio_reg.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_task_wdt.h"

static bool dbg = true;
#define SAY(...)  do { if (dbg) Serial0.printf(__VA_ARGS__); } while (0)
#define PUTC(c)   do { if (dbg) Serial0.write(c); } while (0)

#include "sd_probe.h"   // needs SAY() for its progress output

// ------------------------------------------------------------------ pin sets
static const int kFlashPins[]  = {26, 27, 28, 29, 30, 31, 32};   // MSPI, never drive
static const int kPsramPins[]  = {33, 34, 35, 36, 37};           // OPI PSRAM on N*R8
static const int kConsole[]    = {43, 44};                       // UART0 to the bridge
static const int kAllPads[]    = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,
                                  26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,
                                  45,46,47,48};                 // 22..25 do not exist
static const int kInputOnly[]  = {46};

static bool inList(int p, const int* l, size_t n) {
  for (size_t i = 0; i < n; i++) if (l[i] == p) return true;
  return false;
}

// Drive-safe set: real pads, not flash / PSRAM / console / input-only, and (pass 1)
// not the native-USB pads 19/20 which we only try if pass 1 comes up empty.
static std::vector<int> driveSet(bool extended) {
  std::vector<int> v;
  const bool skip_psram = !(PROBE_OPI_PADS && !psramFound());
  for (int p : kAllPads) {
    if (inList(p, kFlashPins, 7) || (skip_psram && inList(p, kPsramPins, 5)) ||
        inList(p, kConsole, 2)) continue;
    if (inList(p, kInputOnly, 1)) continue;
    if (p == 46) continue;                       // input-only + strap
    if (!extended && (p == 19 || p == 20)) continue;
    v.push_back(p);
  }
  return v;
}

// ------------------------------------------------------------------ stage S0
static void stage0() {
  SAY("S0 identity\n");
  esp_chip_info_t ci;
  esp_chip_info(&ci);
  SAY("  chip        model=%d cores=%d revision=%d features=0x%x\n", (int)ci.model,
      ci.cores, ci.revision, (unsigned)ci.features);
  SAY("  flash       %u MB  freq %u kHz  mode %d\n", ESP.getFlashChipSize() / 1048576,
      ESP.getFlashChipSpeed(), (int)ESP.getFlashChipMode());
  SAY("  psram       present=%d size=%u KB free=%u KB\n", (int)psramFound(),
      ESP.getPsramSize() / 1024, ESP.getFreePsram() / 1024);
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  SAY("  mac         %02x:%02x:%02x:%02x:%02x:%02x\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

// ------------------------------------------------------------------ stage S1
struct PadInfo { int pad, float_lv, pu_lv, pd_lv; const char* klass; };
static std::vector<PadInfo> g_pullups;

// Read a pad's DC environment: floating, with the ~45k internal pull-up, with the ~45k
// internal pull-down. The triple separates "nothing attached" (0/1/0) from a weak
// external pull-up such as an SD card's 50k on CMD/DAT1-3 (1/1/0) from a strong board
// resistor or a driver (1/1/1). Never call this on an MSPI pad with the bus live.
static int readMajority(int p, int mode, int n) {
  pinMode(p, mode);
  delayMicroseconds(50);
  int hi = 0;
  for (int i = 0; i < n; i++) { hi += digitalRead(p); delayMicroseconds(20); }
  return hi * 2 > n;
}

static void padTriple(int p, int* f, int* u, int* d) {
  *f = readMajority(p, INPUT, 9);
  *u = readMajority(p, INPUT_PULLUP, 9);
  *d = readMajority(p, INPUT_PULLDOWN, 9);
  pinMode(p, INPUT);
}

static void stage1() {
  SAY("S1 passive pad survey (F=floating, U=internal pull-up, D=internal pull-down)\n");
  // GPIO26-37 are deliberately NOT touched, not even for a passive read: pinMode()
  // re-parents the pad to the GPIO matrix, which detaches the MSPI function from the
  // flash pads and hangs instruction fetch on the next cache miss (TG1WDT_SYS_RESET).
  SAY("  (GPIO26-37 excluded: pinMode() detaches MSPI -> XIP hang, see comment)\n");
  SAY("  pad   F U D   class                note\n");
  for (int p : kAllPads) {
    if (inList(p, kFlashPins, 7)) {
      SAY("  GPIO%-3d - - -   %-18s %s\n", p, "mspi-flash", "untouched: pins the XIP bus"); continue;
    }
    if (inList(p, kPsramPins, 5)) {
      if (PROBE_OPI_PADS && !psramFound()) {           // build has PSRAM off -> pads are free
        int f, u, d; padTriple(p, &f, &u, &d);
        SAY("  GPIO%-3d %d %d %d   %-18s %s\n", p, f, u, d, "opi-pad(probed)", "PSRAM disabled in this build");
        if (f == 1) g_pullups.push_back({p, f, u, d, "opi-pad(probed)"});
        continue;
      }
      SAY("  GPIO%-3d - - -   %-18s %s\n", p, "opi-psram", "untouched: reserved on N*R8"); continue;
    }
    if (inList(p, kConsole, 2)) { SAY("  GPIO%-3d - - -   %-18s %s\n", p, "uart0", "console"); continue; }
    int f, u, d;
    padTriple(p, &f, &u, &d);

    const char* klass = "floating";
    const char* note  = "";
    if (d == 1 && u == 1)         { klass = "strong-high"; note = ">45k driver or strap resistor"; }
    else if (f == 1 && d == 0)    { klass = "pull-up";     note = "external pull-up, I2C/SD suspect"; }
    else if (u == 0 && f == 0)    { klass = "low";         note = ""; }
    SAY("  GPIO%-3d %d %d %d   %-18s %s\n", p, f, u, d, klass, note);
    if (!strcmp(klass, "pull-up") || !strcmp(klass, "strong-high")) g_pullups.push_back({p, f, u, d, klass});
  }
  SAY("  -> %u pad(s) show an external pull-up / driver: ", (unsigned)g_pullups.size());
  for (auto& i : g_pullups) SAY("%d ", i.pad);
  SAY("\n");
}

// ------------------------------------------------------------------ bit-bang I2C
static int g_sda = -1, g_scl = -1;
#define HALF_US 5
static inline void sdaW(int v) { if (v) pinMode(g_sda, INPUT_PULLUP); else { pinMode(g_sda, OUTPUT); digitalWrite(g_sda, LOW); } delayMicroseconds(HALF_US); }
static inline void sclW(int v) { if (v) pinMode(g_scl, INPUT_PULLUP); else { pinMode(g_scl, OUTPUT); digitalWrite(g_scl, LOW); } delayMicroseconds(HALF_US); }
static inline int  sdaR()      { return digitalRead(g_sda); }

static void i2cStart() { sdaW(1); sclW(1); sdaW(0); sclW(0); }
static void i2cStop()  { sdaW(0); sclW(1); sdaW(1); }

static bool i2cWrite(uint8_t b) {                 // returns true on ACK
  for (int i = 7; i >= 0; i--) { sdaW((b >> i) & 1); sclW(1); sclW(0); }
  sdaW(1); sclW(1); int a = sdaR(); sclW(0);
  return a == 0;
}
static uint8_t i2cRead(bool ack) {
  uint8_t v = 0;
  sdaW(1);
  for (int i = 0; i < 8; i++) { v <<= 1; sclW(1); if (sdaR()) v |= 1; sclW(0); }
  sdaW(ack ? 0 : 1); sclW(1); sclW(0); sdaW(1);
  return v;
}
static bool i2cProbe(uint8_t addr7) {
  i2cStart();
  bool ack = i2cWrite((uint8_t)(addr7 << 1));
  i2cStop();
  return ack;
}

// ---------------- combined I2C+XCLK sweep: XCLK is needed by some sensors (OV5640)
// LEDC on the S3 cannot reach a 20 MHz pixel clock (80 MHz APB / min divider) and
// leaks its channel on every failed attach, so XCLK is generated by a task pinned to
// the other core that toggles the pad straight through the GPIO OUT_W1TS/TC registers.
// The toggle rate is measured, not assumed.
static volatile uint32_t* s_wr[2][2] = {{nullptr, nullptr}, {nullptr, nullptr}}; // [hi/lo][set/clear]
static volatile uint32_t  s_xclk_mask[2] = {0, 0};
static volatile uint32_t  s_run = 0;
static volatile uint32_t  s_loops = 0;
static uint8_t            s_xclk_pad = 0;
static uint32_t           s_xclk_hz = 0;

static void xclkTask(void* arg) {
  const bool hi = (bool)(uintptr_t)arg;
  volatile uint32_t* set = s_wr[hi][0];
  volatile uint32_t* clr = s_wr[hi][1];
  const uint32_t mask = s_xclk_mask[hi];
  while (s_run) { *set = mask; *clr = mask; s_loops++; }
  *clr = mask;
  vTaskDelete(NULL);
}

static bool xclkStart(int pin, uint32_t want_hz) {
  (void)want_hz;
  xclkStop();
  if (pin < 0) return false;
  gpio_num_t g = (gpio_num_t)pin;
  gpio_reset_pin(g);
  gpio_set_direction(g, GPIO_MODE_OUTPUT);
  const bool hi = (pin > 31);
  s_xclk_mask[hi] = 1u << (pin - (hi ? 32 : 0));
  s_wr[hi][0] = (volatile uint32_t*)(hi ? GPIO_OUT1_W1TS_REG : GPIO_OUT_W1TS_REG);
  s_wr[hi][1] = (volatile uint32_t*)(hi ? GPIO_OUT1_W1TC_REG : GPIO_OUT_W1TC_REG);
  s_xclk_pad = (uint8_t)pin;
  s_loops = 0;
  s_run = 1;
  xTaskCreatePinnedToCore(xclkTask, "xclk", 2048, (void*)(uintptr_t)hi, 20, nullptr, 0);  // core 0; Arduino main owns core 1
  delay(10);
  s_xclk_hz = s_loops * 2u * 100u;               // loops in 10 ms -> toggles/s = Hz
  return s_xclk_hz > 500000;                      // >= 0.5 MHz, well under OV2640's 24 MHz max
}
static void xclkStop() {
  if (!s_xclk_pad && !s_run) return;
  s_run = 0;
  delay(2);
  if (s_xclk_pad) { gpio_reset_pin((gpio_num_t)s_xclk_pad); s_xclk_pad = 0; }
  s_xclk_hz = 0;
}

struct Hit { int sda, scl; uint8_t addr; uint8_t xclk; };
static std::vector<Hit> g_hits;

static void probeAndLog(uint8_t raw, const char* tag, int xclk) {
  if (!i2cProbe(raw)) return;
  SAY("  %s ACK sda=%d scl=%d addr7=0x%02x (8bit 0x%02x) xclk=%d@%uHz\n",
      tag, g_sda, g_scl, raw, (unsigned)(raw << 1), xclk, (unsigned)s_xclk_hz);
  g_hits.push_back({g_sda, g_scl, raw, (uint8_t)xclk});
}

static const uint8_t kCamAddrs[] = {0x1E, 0x1F, 0x21, 0x24, 0x2C, 0x30, 0x36, 0x3C, 0x42, 0x50, 0x6C, 0x7A};

static void sweepPairs(const std::vector<int>& set, int xclk, const char* tag, bool full_scan = true) {
  unsigned probed = 0;
  for (size_t a = 0; a < set.size(); a++) {
    for (size_t b = 0; b < set.size(); b++) {
      if (a == b) continue;
      g_sda = set[a]; g_scl = set[b];
      pinMode(g_sda, INPUT_PULLUP); pinMode(g_scl, INPUT_PULLUP);
      delayMicroseconds(200);
      if (!sdaR()) continue;                       // bus held low / not a bus: skip silently
      if (full_scan) {
        for (uint8_t raw = 0x08; raw <= 0x77; raw++) probeAndLog(raw, tag, xclk);
      } else {
        for (uint8_t raw : kCamAddrs) probeAndLog(raw, tag, xclk);
      }
      probed++;
      if ((probed % 100) == 0) { SAY("  %s %u pairs, %u hit(s)\n", tag, probed, (unsigned)g_hits.size()); }
    }
  }
}

static void stage2() {
  SAY("S2 SCCB/I2C sweep (cheapest pass first)\n");

  // pass A: only pads that showed a pull-up, no clock
  std::vector<int> pull;
  for (auto& i : g_pullups) if (i.pad <= 21 || (i.pad >= 38 && i.pad <= 48)) pull.push_back(i.pad);
  SAY("  pass A: pull-up pads {");
  for (int p : pull) SAY("%d ", p);
  SAY("}\n");
  if (pull.size() >= 2) sweepPairs(pull, 0, "A");
#if PROBE_I2C

  // pass C: pull-up pairs, rotating a real XCLK over every drive-safe pad. Some
  // sensors (OV5640, and anything with an enable/power-down net) answer SCCB only
  // once the pixel clock is running.
  if (g_hits.empty() && pull.size() >= 2) {
  SAY("  pass C: %u pull-up pairs x XCLK rotation, measured rate\n",
      (unsigned)(pull.size() * (pull.size() - 1)));
    std::vector<int> xset = driveSet(true);
    for (int x : xset) {
      if (!xclkStart(x, 20000000)) { SAY("  C xclk=%d too slow (%u Hz)\n", x, (unsigned)s_xclk_hz); continue; }
      SAY("  C xclk=%d ~%u MHz\n", x, (unsigned)(s_xclk_hz / 1000000));
      sweepPairs(pull, x, "C");
      xclkStop();
      if (!g_hits.empty()) break;
    }
  }

  // pass D: include the native-USB pads 19/20, no clock
  if (g_hits.empty()) { SAY("  pass D: extended drive set (incl. GPIO19/20 = USB D-/D+)\n"); sweepPairs(driveSet(true), 0, "D"); }

  // pass B removed: the extended drive set of pass D is a superset of it.

  // pass E: every pair x every XCLK, camera-typical addresses only
  if (g_hits.empty()) {
    SAY("  pass E: full cross product, camera-typical addresses\n");
    std::vector<int> all = driveSet(true);
    for (int x : all) {
      if (!xclkStart(x, 20000000)) continue;
      delay(2);
      sweepPairs(all, x, "E", false);
      xclkStop();
      if (!g_hits.empty()) break;
    }
  }
#endif

  if (g_hits.empty()) SAY("  no I2C device found on any candidate pair\n");
  g_sda = g_scl = -1;
}

// ------------------------------------------------------------------ stage S3
static void stage3() {
  if (g_hits.empty()) { SAY("S3 skipped (no I2C device)\n"); return; }
  SAY("S3 sensor identification\n");
  for (auto& h : g_hits) {
    g_sda = h.sda; g_scl = h.scl;
    xclkStart(h.xclk, 20000000);
    SAY("  pair sda=%d scl=%d addr7=0x%02x:\n", h.sda, h.scl, h.addr);
    // OV2640/OV3660: 8-bit register address, chip id reg 0x0A -> 0x90 (OV2640) / 0x6F? 
    bool ok = false;
    i2cStart();
    ok = i2cWrite((uint8_t)(h.addr << 1)) && i2cWrite(0x0A);
    if (ok) {
      i2cStart();
      ok = i2cWrite((uint8_t)((h.addr << 1) | 1));
    }
    if (ok) {
      uint8_t id = i2cRead(false); i2cStop();
      SAY("    8-bit reg0x0A = 0x%02x  (%s)\n", id, id == 0x90 ? "OV2640" : id == 0x6F ? "OV965x/OV9281 class" : "?");
    } else i2cStop();
    // OV5640: 16-bit register address, 0x300A/0x300B -> 0x56/0x40
    i2cStart();
    ok = i2cWrite((uint8_t)(h.addr << 1)) && i2cWrite(0x30) && i2cWrite(0x0A);
    if (ok) {
      i2cStart();
      ok = i2cWrite((uint8_t)((h.addr << 1) | 1));
    }
    if (ok) {
      uint8_t a = i2cRead(true), b = i2cRead(true), c = i2cRead(false); i2cStop();
      SAY("    16-bit 0x300A..C = 0x%02x 0x%02x 0x%02x  (%s)\n", a, b, c,
          (a == 0x56 && b == 0x40) ? "OV5640" : "?");
    } else i2cStop();
    xclkStop();
  }
}

// ------------------------------------------------------------------ stage S4
// A microSD card supplies 50k pull-ups on CMD and DAT1..DAT3 when powered, but NOT on
// CLK — so CS/DI/DO are taken from the pads stage S1 marked as pulled-up while the
// clock is rotated over the whole drive set. Finding the net map is only half the job:
// the winning tuple then has to read a real block, so the map is proven by data, not
// by a single ACK.
static void stage4() {
  SAY("S4 microSD slot: hierarchical SPI sweep (CMD0 reply read on every pad at once)\n");
  std::vector<int> all = driveSet(true);
  SAY("  %u pads in play (flash 26-32, OPI PSRAM 33-37, console 43/44 and input-only 46 excluded)\n",
      (unsigned)all.size());
  unsigned tuples = 0, found = 0, g_susp = 0, dead = 0;
  int doPins[64];
  for (int clk : all) {
    if (!sdPowerClocks(clk)) { SAY("  clk=%-3d cannot be driven high, whole row skipped\n", clk); continue; }
    for (int cs : all) {
      if (cs == clk) continue;
      for (int di : all) {
        if (di == clk || di == cs) continue;
        int n = 0;
        for (int d0 : all) if (d0 != clk && d0 != cs && d0 != di) doPins[n++] = d0;
        if (!sdSearchTuple(cs, di, clk, doPins, n)) { dead++; continue; }
        for (int k = 0; k < n; k++) {
          if (g_reply[k] == 0 || g_reply[k] > 8) continue;   // lone HIGH bit = an R1 reply
          int d0 = doPins[k];
          SAY("  CMD0 reply on cs=%d di=%d d0=%d clk=%d (%u high bits)\n", cs, di, d0, clk, g_reply[k]);
          found++; tuples++; g_susp++;
          SdBus b{cs, di, d0, clk};
          if (sdFullRead(b)) SAY("  >>> slot net map PROVEN: CS=%d MOSI=%d MISO=%d CLK=%d\n", cs, di, d0, clk);
          else SAY("  >>> reply seen but block read failed on that tuple\n");
          if (found) return;
        }
        tuples++;
      }
    }
    SAY("  clk=%-3d %5u tuples, %u suspicious pad(s)\n", clk, tuples, g_susp);
  }
  SAY("  %u tuples tried, %u replies, %u tuples rejected (a net could not be driven high)\n",
      tuples, found, dead);

}

// ------------------------------------------------------------------ stage S5
// Live change monitor: the cheapest way to find out which nets a plugin changes, because
// it needs neither a schematic nor a second board to diff against. Run it, then insert or
// remove the card (or press the camera FPC in) while it watches.
static void stage5(int secs) {
  delay(200);
  SAY("S5 monitor: %s - now INSERT or REMOVE the card\n", secs ? "watching for a fixed time" : "watching until reset");
  SAY("  coverage: GPIO26-32 (flash), GPIO33-37 unless PSRAM is off, and GPIO43/44 (the\n"
      "  console: 43 is our TX, 44 is driven HIGH by the bridge) are NOT observable here\n");
  int npad = 0;
  int pads[64], cur[64][3], prev[64][3], pend[64] = {0};
  for (int p : kAllPads) {
    if (inList(p, kFlashPins, 7) || inList(p, kConsole, 2) || p == 46) continue;
    if (inList(p, kPsramPins, 5) && !(PROBE_OPI_PADS && !psramFound())) continue;
    pads[npad] = p; padTriple(p, &cur[npad][0], &cur[npad][1], &cur[npad][2]);
    memcpy(prev[npad], cur[npad], sizeof(cur[npad]));
    npad++;
  }
  SAY("  baseline:");
  for (int i = 0; i < npad; i++) if (cur[i][0]) SAY(" %d=%d%d%d", pads[i], cur[i][0], cur[i][1], cur[i][2]);
  SAY("\n");
  uint32_t t0 = millis(), last_beat = 0;
  while (secs == 0 || (millis() - t0) / 1000u < (unsigned)secs) {
    if (millis() - last_beat > 10000) {
      last_beat = millis();
      SAY("  t=%u s floating-high: ", (millis() - t0) / 1000);
      for (int i = 0; i < npad; i++) if (cur[i][0]) SAY("%d ", pads[i]);
      SAY("\n");
    }
    for (int i = 0; i < npad; i++) {
      padTriple(pads[i], &cur[i][0], &cur[i][1], &cur[i][2]);
      bool differ = cur[i][0] != prev[i][0] || cur[i][1] != prev[i][1] || cur[i][2] != prev[i][2];
      pend[i] = differ ? pend[i] + 1 : 0;
      // A single differing sample is what a hand walking past a floating pad looks like:
      // the pad's own charge depends on what the previous cycle did to it. Only report a
      // change that survives three full cycles (~2 s) in a row.
      if (differ && pend[i] >= 3) {
        pend[i] = 0;
        SAY("  +%u ms GPIO%-3d %d%d%d -> %d%d%d\n", millis() - t0, pads[i],
            prev[i][0], prev[i][1], prev[i][2], cur[i][0], cur[i][1], cur[i][2]);
        memcpy(prev[i], cur[i], sizeof(cur[i]));
        // A connector that is really being mated changes its nets and STAYS changed. A
        // floating pad that a hand walked past flickers. Re-read after 400 ms and say
        // which one it was - otherwise every handling artefact looks like a discovery.
        delay(400);
        int nf, nu, nd; padTriple(pads[i], &nf, &nu, &nd);
        if (nf == cur[i][0] && nu == cur[i][1] && nd == cur[i][2])
          SAY("        holds after 400 ms -> %s\n", cur[i][0] ? "STABLE pull-up: a net the card drives/pulls"
                                                              : "STABLE low");
        else
          SAY("        transient -> charge/handling on a floating pad, not a net\n");
      }
    }
    delay(200);
  }
  SAY("  monitor end\n");
}

// ------------------------------------------------------------------ wiring
// Leave every probed pad high-impedance. A probe still holding IO0 when esptool tries
// its auto-reset produces a "failed to connect" that looks like a bad cable.
static void releaseAllPads() {
  for (int p : driveSet(true)) pinMode(p, INPUT);
}

void setup() {
  Serial0.begin(115200, SERIAL_8N1, 44, 43);
  delay(500);
  // The XCLK generator is a spin loop; it starves IDLE0, which the task watchdog
  // (CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0) treats as a hang and aborts. This is a
  // measurement fixture, so the watchdog is turned off for the run.
  SAY("\n\n=== board_sonde ===\n");
  if (esp_task_wdt_deinit() == ESP_OK) SAY("  task watchdog disabled for this fixture\n");
#if PROBE_OPI_PADS
  SAY("  PROBE_OPI_PADS=%d psramFound=%d -> OPI pads %s\n", PROBE_OPI_PADS, (int)psramFound(),
      (!psramFound()) ? "will be probed" : "SKIPPED (PSRAM is live: driving them corrupts PSRAM)");
#endif
  stage0(); stage1(); stage2(); stage3();
#if PROBE_SD
  stage4();
#endif
  stage5(MONITOR_SECS);
  releaseAllPads();
  SAY("=== done (press RST or re-power to re-run) ===\n");
}

void loop() { delay(1000); }
