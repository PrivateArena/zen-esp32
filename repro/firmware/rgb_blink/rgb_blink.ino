// Blink.ino, RGB edition — zen-esp32 / ESP32-S3 Dev Module variant
// On this variant LED_BUILTIN/RGB_BUILTIN is the WS2812 status LED (PIN_RGB_LED = GPIO48).
// digitalWrite(LED_BUILTIN, HIGH) lights it white; rgbLedWrite() picks the colour.

static const uint32_t STEP_MS = 150;   // short step so the colour cycle is unmistakable

struct Color { uint8_t r, g, b; const char *name; };
static const Color SEQ[] = {
  {  0,  0,  0, "OFF   " },
  { 64,  0,  0, "RED   " },
  {  0, 64,  0, "GREEN " },
  {  0,  0, 64, "BLUE  " },
  { 64, 64, 64, "WHITE " },
};
static const size_t SEQ_LEN = sizeof(SEQ) / sizeof(SEQ[0]);

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.printf("\nBlink-RGB: LED_BUILTIN=%u (WS2812 data on GPIO48) RGB_BRIGHTNESS=%u step=%lums cycle=%lums\n",
                (unsigned)LED_BUILTIN, (unsigned)RGB_BRIGHTNESS, (unsigned long)STEP_MS,
                (unsigned long)(STEP_MS * SEQ_LEN));
}

void loop() {
  static size_t i = 0;
  static uint32_t n = 0;
  const Color &c = SEQ[i];
  rgbLedWrite(RGB_BUILTIN, c.r, c.g, c.b);
  Serial.printf("n=%-6lu %s rgb=(%u,%u,%u)\n", (unsigned long)n++, c.name, c.r, c.g, c.b);
  i = (i + 1) % SEQ_LEN;
  delay(STEP_MS);
}
