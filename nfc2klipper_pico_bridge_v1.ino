#include <Wire.h>
#include <Adafruit_PN532.h>

#define PN532_IRQ   -1
#define PN532_RESET -1
Adafruit_PN532 nfc(PN532_IRQ, PN532_RESET, &Wire);

#define LED_PIN 25

unsigned long lastSendTime = 0;
String lastSpoolID = "";
unsigned long lastReset = 0;
const unsigned long RESET_INTERVAL = 300000;  // 5 Min

// --- TAG-ENTFERNUNG ERKENNEN ---
static bool tagPresent = false;

void setup() {
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  Serial.begin(115200);
  while (!Serial) delay(10);

  Wire.setSDA(4);
  Wire.setSCL(5);
  Wire.begin();
  Wire.setClock(100000);

  initPN532();

  Serial.println("NFC Bridge v1– OpenNeptune ready");
  blinkLED(3, 200);
}

void initPN532() {
  nfc.begin();
  uint32_t ver = nfc.getFirmwareVersion();
  if (ver == 0) {
    Serial.println("PN532 nicht gefunden – retry");
    delay(500);
    initPN532();
    return;
  }
  Serial.print("PN532 FW: ");
  Serial.println((ver >> 8) & 0xFF);

  if (!nfc.SAMConfig()) {
    Serial.println("SAMConfig failed – retry");
    delay(100);
    nfc.SAMConfig();
  } else {
    Serial.println("SAMConfig OK");
  }
  lastReset = millis();
}

bool safeReadTarget(uint8_t* uid, uint8_t* uidLen) {
  uint32_t start = millis();
  while (millis() - start < 300) {
    if (nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, uidLen, 100)) {
      return true;
    }
    delay(10);
  }
  return false;
}

void loop() {
  // --- WATCHDOG MIT FULL RECOVERY ---
  if (millis() - lastReset > RESET_INTERVAL) {
    Serial.println("Watchdog Reset – FULL RECOVERY");
    fullRecovery();
    lastReset = millis();
  }

  uint8_t uid[7];
  uint8_t uidLen;

  if (safeReadTarget(uid, &uidLen)) {
    if (!tagPresent) {
      tagPresent = true;
      Serial.println("NEUER TAG ERKANNT");
    }

    digitalWrite(LED_PIN, HIGH);
    Serial.print("Tag UID: ");
    for (int i = 0; i < uidLen; i++) Serial.printf("%02X", uid[i]);
    Serial.println();

    uint8_t page4[4];
    if (safeReadPage(4, page4)) {
      if (page4[0] == 0x03) {
        uint8_t len = page4[1];
        Serial.print("NDEF Len: ");
        Serial.println(len);

        if (len > 0 && len <= 140) {
          uint8_t ndef[140];
          int read = 0;
          memcpy(ndef, page4, 4);
          read = 4;

          for (int p = 5; read < len + 2 && p < 40; p++) {
            uint8_t page[4];
            if (safeReadPage(p, page)) {
              int copy = min(4, len + 2 - read);
              memcpy(ndef + read, page, copy);
              read += copy;
            } else {
              break;
            }
          }

          if (read >= len + 2) {
            String text = parseNdef(ndef + 2, len);
            Serial.print("Text: ");
            Serial.println(text);

            String id = extract(text, "SpoolID:");
            if (id != "" && millis() - lastSendTime > 3000) {
              Serial.print("SET_SPOOL_ID ID=");
              Serial.println(id);
              lastSpoolID = id;
              lastSendTime = millis();
              blinkLED(2, 150);
            }
          }
        }
      }
    } else {
      Serial.println("I2C Reset – PN532 neu");
      fullRecovery();
    }

    digitalWrite(LED_PIN, LOW);
    delay(1000);
  } else {
    // --- TAG ENTFERNT? ---
    if (tagPresent) {
      tagPresent = false;
      lastSpoolID = "";
      Serial.println("TAG ENTFERNT – bereit für neuen Tag");
    }

    // --- KEIN TAG → FULL RECOVERY ---
    static unsigned long lastLog = 0;
    if (millis() - lastLog > 10000) {
      Serial.println("readTarget Timeout → FULL RECOVERY");
      lastLog = millis();
    }
    fullRecovery();
  }
}

void fullRecovery() {
  Wire.end();
  delay(50);
  Wire.begin();
  Wire.setClock(100000);
  nfc.begin();
  if (!nfc.SAMConfig()) {
    delay(100);
    nfc.SAMConfig();
  }
}

bool safeReadPage(uint8_t page, uint8_t* data) {
  uint32_t start = millis();
  while (millis() - start < 200) {
    if (nfc.ntag2xx_ReadPage(page, data)) {
      return true;
    }
    delay(10);
  }
  return false;
}

// --- NDEF Parser ---
String parseNdef(uint8_t* d, int len) {
  int p = 0;
  while (p < len) {
    if (p + 3 >= len) break;
    uint8_t hdr = d[p];
    uint8_t tnf = hdr & 0x07;
    uint8_t typeLen = d[p + 1];
    uint8_t payLen = d[p + 2];
    uint8_t type = d[p + 3];
    if (tnf == 1 && typeLen == 1 && type == 'T') {
      int ps = p + 4;
      if (ps + payLen > len) break;
      uint8_t status = d[ps];
      uint8_t langLen = status & 0x3F;
      int ts = ps + 1 + langLen;
      int tl = payLen - 1 - langLen;
      if (ts + tl > len) break;
      char buf[tl + 1];
      memcpy(buf, &d[ts], tl);
      buf[tl] = 0;
      return String(buf);
    }
    p += 4 + payLen;
  }
  return "";
}

String extract(String s, String k) {
  int i = s.indexOf(k);
  if (i == -1) return "";
  int a = i + k.length();
  int b = s.indexOf('\n', a);
  if (b == -1) b = s.length();
  String r = s.substring(a, b);
  r.trim();
  return r;
}

void blinkLED(int n, int d) {
  for (int i = 0; i < n; i++) {
    digitalWrite(LED_PIN, HIGH);
    delay(d);
    digitalWrite(LED_PIN, LOW);
    delay(d);
  }
}
