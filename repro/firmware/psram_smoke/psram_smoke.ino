// zen-esp32 smoke test: ESP32-S3 N16R8 (16MB flash / 8MB OPI PSRAM)
// Console = UART0 (GPIO43/44) behind CH9102, USB CDC On Boot disabled.
#include "esp_heap_caps.h"

static const uint8_t PIN_STATUS_LED = 48;

static void reportHeap(const char *tag) {
  Serial.printf("[%s] internal free=%u min=%u\n", tag,
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL));
  size_t ps_total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
  size_t ps_free  = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  Serial.printf("[%s] PSRAM total=%u free=%u -> %s\n", tag, (unsigned)ps_total, (unsigned)ps_free,
                ps_total ? "OPI PSRAM OK" : "PSRAM MISSING");
  if (ps_total) {
    void *p = heap_caps_malloc(1024 * 1024, MALLOC_CAP_SPIRAM);
    if (p) { memset(p, 0x5A, 1024 * 1024); Serial.printf("[%s] PSRAM 1MB alloc+write ok\n", tag); heap_caps_free(p); }
    else   { Serial.printf("[%s] PSRAM 1MB alloc FAILED\n", tag); }
  }
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.printf("\nzen-esp32 smoke: flash=%uMB psramFound=%d psramSize=%u\n",
                (unsigned)(ESP.getFlashChipSize() / 1048576), (int)psramFound(),
                (unsigned)ESP.getPsramSize());
  Serial.printf("build: CDC_ON_BOOT=%d psram_mode=%s freq=%dMHz\n",
                (int)ARDUINO_USB_CDC_ON_BOOT,
#if CONFIG_SPIRAM_MODE_OCT
                "OPI",
#else
                "QSPI/none",
#endif
                (int)(ESP.getCpuFreqMHz()));
  reportHeap("boot");
  pinMode(PIN_STATUS_LED, OUTPUT);
}

void loop() {
  static uint32_t n = 0;
  digitalWrite(PIN_STATUS_LED, (n++ & 1) ? HIGH : LOW);
  Serial.printf("tick=%u free_heap=%u\n", (unsigned)n, ESP.getFreeHeap());
  if (n % 5 == 0) reportHeap("t5");   // repeating evidence for late-attaching monitors
  delay(1000);
}
