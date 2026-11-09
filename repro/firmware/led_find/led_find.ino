// led_find — which pad drives which indicator on the CAM+SD board.
// Plain drive first, then WS2812-style data, so a serial LED and an addressable one are
// told apart by the same run. Console = UART0 (GPIO43/44), the CH9102 socket.
#include <Arduino.h>

static const int kPads[] = { 2, 48 };          // the two pads the silkscreen names
static const char* kPhases[] = {
  "all pads released      ",
  "GPIO2  driven HIGH     ",
  "GPIO2  driven LOW      ",
  "GPIO48 driven HIGH     ",
  "GPIO48 driven LOW      ",
  "rgbLedWrite(RGB_BUILTIN, RED  )",
  "rgbLedWrite(RGB_BUILTIN, GREEN)",
  "rgbLedWrite(RGB_BUILTIN, BLUE )",
  "rgbLedWrite(48,          WHITE)",
  "rgb strip off          ",
};

static void phase(int i) {
  Serial0.printf("[%6.1f s] phase %d: %s\n", millis() / 1000.0, i, kPhases[i]);
  Serial0.flush();
  delay(2500);                                  // long enough to watch, short enough to loop
}

static void drive(int p, int v) {
  if (p < 0) { for (int q : kPads) pinMode(q, INPUT); return; }
  if (v < 0) { pinMode(p, INPUT); return; }
  pinMode(p, OUTPUT);
  digitalWrite(p, v);
}

static void stop(int p) { drive(p, -1); }

void setup() {
  Serial0.begin(115200, SERIAL_8N1, 44, 43);
  delay(400);
  Serial0.printf("\n\n=== led_find ===  pad set: 2 48   (watch the board; phases are 2.5 s)\n"
                 "Note also which LEDs are lit in phase 0 - that is the board's idle state.\n\n", 0);
}

void loop() {
  drive(-1, 0);                                              phase(0);
  drive(2, HIGH);                                            phase(1);
  drive(2, LOW);                                             phase(2);
  stop(2);
  drive(48, HIGH);                                           phase(3);
  drive(48, LOW);                                            phase(4);
  pinMode(48, INPUT);
  rgbLedWrite(RGB_BUILTIN, 64, 0, 0);                        phase(5);
  rgbLedWrite(RGB_BUILTIN, 0, 64, 0);                        phase(6);
  rgbLedWrite(RGB_BUILTIN, 0, 0, 64);                        phase(7);
  rgbLedWrite(48, 64, 64, 64);                               phase(8);
  rgbLedWrite(RGB_BUILTIN, 0, 0, 0);                         phase(9);
  Serial0.println("--- cycle end: report the phase number(s) in which you saw a light, "
                  "and which socket/label it was under ---\n");
}
