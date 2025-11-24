#include <Wire.h>
#include <Adafruit_PN532.h>
#include <LiquidCrystal.h>

// =========== LCD Pins ===========
const int LCD_RS = 6;
const int LCD_E  = 7;
const int LCD_D4 = 8;
const int LCD_D5 = 9;
const int LCD_D6 = 10;
const int LCD_D7 = 11;

// ============= LCD Objekt =======
LiquidCrystal lcd(LCD_RS, LCD_E, LCD_D4, LCD_D5, LCD_D6, LCD_D7);

// =========== NFC Settings =======
#define PN532_IRQ   -1
#define PN532_RESET -1
Adafruit_PN532 nfc(PN532_IRQ, PN532_RESET, &Wire);

#define LED_PIN 25

unsigned long lastSendTime = 0;
String lastSpoolID = "";
unsigned long lastReset = 0;
const unsigned long RESET_INTERVAL = 300000;  // 5 Min

const unsigned long TAG_DISPLAY_TIMEOUT = 30000; // 30 Sek. (anpassbar!)

// Laufschrift: wie oft pro Sekunde scrollen? (ms)
const unsigned long SCROLL_INTERVAL = 350;

unsigned long lastTagDisplay = 0;
bool tagDataValid = false;

// die Zeileninhalte und Scrollpositionen zwischenspeichern:
String lcdLine[4] = {"", "", "", ""};
int scrollPos[4] = {0,0,0,0};
unsigned long scrollTime[4] = {0,0,0,0};

static bool tagPresent = false;

void setup() {
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  Serial.begin(115200);

  Wire.setSDA(4);
  Wire.setSCL(5);
  Wire.begin();
  Wire.setClock(100000);

  lcd.begin(20, 4);
  lcd.clear();
  lcd.setCursor(0,0); lcd.print("NFC Bridge v1");
  lcd.setCursor(0,1); lcd.print("OpenNeptune ready");

  initPN532();

  Serial.println("NFC Bridge v1– OpenNeptune ready");
  blinkLED(3, 200);
}

void initPN532() {
  nfc.begin();
  uint32_t ver = nfc.getFirmwareVersion();
  if (ver == 0) {
    Serial.println("PN532 nicht gefunden – retry");
    lcd.setCursor(0,3); lcd.print("PN532 NICHT gefunden");
    delay(500);
    initPN532();
    return;
  }

  Serial.print("PN532 FW: ");
  Serial.println((ver >> 8) & 0xFF);

  lcd.setCursor(0,3);
  lcd.print("PN532 ok, Ver: ");
  lcd.setCursor(16,3);
  lcd.print((ver >> 8) & 0xFF);

  if (!nfc.SAMConfig()) {
    Serial.println("SAMConfig failed – retry");
    lcd.setCursor(0,3); lcd.print("SAMConfig Fehler");
    delay(100);
    nfc.SAMConfig();
  } else {
    Serial.println("SAMConfig OK");
    lcd.setCursor(0,3); lcd.print("SAMConfig OK        ");
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

// --- LCD Laufschrift/Scroller ---
void drawScrolledLCD() {
  for (int i=0; i<4; i++) {
    lcd.setCursor(0, i);
    int len = lcdLine[i].length();
    if (len <= 20) {
      lcd.print(lcdLine[i]);
      for (int s = len; s < 20; s++) lcd.print(" ");
    } else {
      // Laufschrift: Scrollen nur, wenn Timeout noch läuft!
      if (millis() - scrollTime[i] > SCROLL_INTERVAL) {
        scrollTime[i] = millis();
        scrollPos[i]++;
        if (scrollPos[i] > len - 20) scrollPos[i] = 0;
      }
      lcd.print(lcdLine[i].substring(scrollPos[i], scrollPos[i]+20));
    }
  }
}

void loop() {
  // --- WATCHDOG MIT FULL RECOVERY ---
  if (millis() - lastReset > RESET_INTERVAL) {
    Serial.println("Watchdog Reset – FULL RECOVERY");
    lcd.setCursor(0,3); lcd.print("Watchdog: RESET    ");
    fullRecovery();
    lastReset = millis();
  }

  uint8_t uid[7];
  uint8_t uidLen;

  if (safeReadTarget(uid, &uidLen)) {
    if (!tagPresent) {
      tagPresent = true;
    }

    digitalWrite(LED_PIN, HIGH);

    uint8_t page4[4];
    if (safeReadPage(4, page4)) {
      if (page4[0] == 0x03) {
        uint8_t len = page4[1];

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

            String id  = extract(text, "SpoolID:");
            String typ = extract(text, "Type:");
            String col = extract(text, "Color:");
            String ven = extract(text, "Vendor:");

            // SpoolID Serial an Klipper/Bridge wie gehabt!
            if (id != "" && millis() - lastSendTime > 3000) {
              Serial.print("SET_SPOOL_ID ID=");
              Serial.println(id);
              lastSpoolID = id;
              lastSendTime = millis();
              blinkLED(2, 150);
            }

            // Zeilen fürs Display füllen:
            lcdLine[0] = "SpoolID:" + id;
            lcdLine[1] = "Type:"    + typ;
            lcdLine[2] = "Color:"   + col;
            lcdLine[3] = "Vendor:"  + ven;

            // Scroll-Parameter zurücksetzen
            for (int i=0; i<4; i++) { scrollPos[i]=0; scrollTime[i]=millis(); }

            lastTagDisplay = millis();
            tagDataValid = true;
          }
        }
      }
    }
    digitalWrite(LED_PIN, LOW);
    delay(1000);
  } else {
    // Tagdaten anzeigen solange Timeout läuft:
    if (tagDataValid && millis() - lastTagDisplay < TAG_DISPLAY_TIMEOUT) {
      drawScrolledLCD();
    } else {
      // Nach Ablauf des Timeouts wieder Wartestatus anzeigen
      if (tagDataValid) {
        lcd.clear();
        lcdLine[0] = "NFC-Reader wartet.";
        lcdLine[1] = "Bitte Tag anlegen.";
        lcdLine[2] = "";
        lcdLine[3] = "";
        // Scroll-Parameter zurücksetzen
        for (int i=0; i<4; i++) { scrollPos[i]=0; scrollTime[i]=millis(); }
        tagDataValid = false;
      }
      tagPresent = false;
      lastSpoolID = "";

      // --- Timeout Recovery: NUR im Wartemodus!! ---
      static unsigned long lastLog = 0;
      if (millis() - lastLog > 10000) {
        Serial.println("readTarget Timeout → FULL RECOVERY");
        lcdLine[3] = "Timeout Recovery   ";
        lastLog = millis();
      }
      fullRecovery();
    }
    drawScrolledLCD();
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
