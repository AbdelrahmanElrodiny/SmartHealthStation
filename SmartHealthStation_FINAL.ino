// =============================================================================
//  Smart Health Station — FINAL
//  D2=TRIG  D3=ECHO  D4=IR  D5=LED_RED  D6=LED_GREEN  D7=BUZZER
//  D8=SPRAY  D9=BTN_START  D10=BTN_RESET  D12=BTN_LIGHT  D13=LED_LIGHT
//  I2C: A4=SDA  A5=SCL
// =============================================================================
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_BMP280.h>
#include <Adafruit_MLX90614.h>

Adafruit_SSD1306  display(128, 64, &Wire, -1);
Adafruit_BMP280   bmp;
Adafruit_MLX90614 mlx;
bool oledOK=false, bmpOK=false, mlxOK=false;

// ─── Pins ─────────────────────────────────────────────────────────────────────
#define PIN_TRIG   2
#define PIN_ECHO   3
#define PIN_IR     4
#define PIN_RED    5
#define PIN_GREEN  6
#define PIN_BUZ    7
#define PIN_SPRAY  8
#define PIN_START  9
#define PIN_RESET  10
#define PIN_LIGHT  12   // زرار الإضاءة
#define PIN_LLED   13   // LED الإضاءة

// ─── Config ───────────────────────────────────────────────────────────────────
#define DIST_CM    5.0
#define FEVER      38.5
#define SPRAY_MS   5000UL
#define ENV_MS     2000UL
#define MLX_N      4
#define MLX_STEP   500UL
#define DEB        50UL

// ─── States ───────────────────────────────────────────────────────────────────
enum St { IDLE, SCAN, MEAS, RES, SPRAYING };
St st = IDLE;

unsigned long tEnv=0, tMlx=0;
unsigned long sprayGood=0, sprayEdge=0;
bool lastHand=false;
float bodyT=0, airT=0, pres=0;
byte  mlxN=0;
float mlxS=0;

// ─── Button debounce ──────────────────────────────────────────────────────────
bool sPrev=HIGH, rPrev=HIGH, lPrev=HIGH;
unsigned long sDeb=0, rDeb=0, lDeb=0;
bool lightOn = false;   // ★ الإضاءة مش بتتأثر بالـ reset

// ─── Distance (3 samples) ─────────────────────────────────────────────────────
float dArr[3];
void sortD(float a[], int n) {
  for (int i=0; i<n-1; i++)
    for (int j=i+1; j<n; j++)
      if (a[j]<a[i]) { float t=a[i]; a[i]=a[j]; a[j]=t; }
}
float getDist() {
  int c=0;
  while (c<3) {
    digitalWrite(PIN_TRIG, LOW);  delayMicroseconds(5);
    digitalWrite(PIN_TRIG, HIGH); delayMicroseconds(10);
    digitalWrite(PIN_TRIG, LOW);
    long d = pulseIn(PIN_ECHO, HIGH, 30000UL);
    float cm = d * 0.0343 / 2.0;
    if (d>0 && cm<400) dArr[c++] = cm;
    delay(30);
  }
  sortD(dArr, 3);
  return dArr[1];
}

// ─── Buzzer ───────────────────────────────────────────────────────────────────
void bip(byte n, int on, int off) {
  for (byte i=0; i<n; i++) {
    digitalWrite(PIN_BUZ, HIGH); delay(on);
    digitalWrite(PIN_BUZ, LOW);  delay(off);
  }
}

// ─── OLED helpers ─────────────────────────────────────────────────────────────
void cls() { if (oledOK) display.clearDisplay(); }
void upd() { if (oledOK) display.display(); }

void topBar(const __FlashStringHelper* s) {
  if (!oledOK) return;
  display.fillRect(0, 0, 128, 13, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK); display.setTextSize(1);
  int16_t x1,y1; uint16_t w,h;
  display.getTextBounds(s, 0, 2, &x1,&y1,&w,&h);
  display.setCursor((128-w)/2, 2); display.print(s);
  display.setTextColor(SSD1306_WHITE);
}

void mid(const __FlashStringHelper* s, int y, byte sz=1) {
  if (!oledOK) return;
  display.setTextSize(sz); display.setTextColor(SSD1306_WHITE);
  int16_t x1,y1; uint16_t w,h;
  display.getTextBounds(s, 0, y, &x1,&y1,&w,&h);
  display.setCursor((128-w)/2, y); display.print(s);
}

void midC(char* s, int y, byte sz=1) {
  if (!oledOK) return;
  display.setTextSize(sz); display.setTextColor(SSD1306_WHITE);
  int16_t x1,y1; uint16_t w,h;
  display.getTextBounds(s, 0, y, &x1,&y1,&w,&h);
  display.setCursor((128-w)/2, y); display.print(s);
}

// ─── Screens ──────────────────────────────────────────────────────────────────
void scrIdle() {
  if (bmpOK) { airT=bmp.readTemperature(); pres=bmp.readPressure()/100.0; }
  cls();
  char b[12];
  dtostrf(airT, 4, 1, b); strcat(b, " C");   midC(b, 6, 3);
  dtostrf(pres, 5, 0, b); strcat(b, " hPa"); midC(b, 42, 2);
  upd();
}

void scrScan(float d) {
  cls(); topBar(F("SCANNING"));
  char b[10]; dtostrf(d, 4, 1, b); strcat(b, " cm"); midC(b, 18, 2);
  mid(d > DIST_CM ? F("Come closer!") : F("In range!"), 46);
  upd();
}

void scrMeas(byte p) {
  cls(); topBar(F("MEASURING"));
  mid(F("Hold still..."), 16);
  if (oledOK) {
    display.drawRect(4, 32, 120, 12, SSD1306_WHITE);
    display.fillRect(4, 32, map(p, 0, MLX_N, 0, 120), 12, SSD1306_WHITE);
    display.setTextSize(1); display.setTextColor(SSD1306_WHITE);
    char c[4]; itoa(p, c, 10); strcat(c, "/4");
    int16_t x1,y1; uint16_t w,h;
    display.getTextBounds(c, 0, 50, &x1,&y1,&w,&h);
    display.setCursor((128-w)/2, 50); display.print(c);
  }
  upd();
}

void scrRes(float t) {
  cls(); topBar(F("RESULT"));
  char b[8]; dtostrf(t, 4, 1, b); strcat(b, " C"); midC(b, 14, 2);
  if (t > FEVER) { mid(F("!! FEVER !!"), 38); mid(F("Seek medical help"), 52); }
  else           { mid(F("Normal"), 38);       mid(F("Show hand to spray"), 52); }
  upd();
}

void scrSpray(int s) {
  cls(); topBar(F("SANITIZING"));
  char b[4]; itoa(s, b, 10); midC(b, 16, 3);
  mid(F("Keep hand still"), 52);
  upd();
}

void scrWait() {
  cls(); topBar(F("SANITIZING"));
  mid(F("Put hand back"), 22);
  mid(F("to continue..."), 36);
  upd();
}

void scrDone() {
  cls();
  mid(F("DONE!"), 14, 2);
  mid(F("Stay safe!"), 44);
  upd();
}

// ─── Reset ────────────────────────────────────────────────────────────────────
void doReset() {
  digitalWrite(PIN_RED,   LOW);
  digitalWrite(PIN_GREEN, LOW);
  digitalWrite(PIN_BUZ,   LOW);
  digitalWrite(PIN_SPRAY, LOW);
  // ★ PIN_LLED مش بيتأثر بالـ reset
  st = IDLE; tEnv = 0;
  cls(); mid(F("System Reset"), 26); upd();
  delay(800);
}

// =============================================================================
void setup() {
  Serial.begin(9600);
  Wire.begin();
  delay(500);

  pinMode(PIN_TRIG,  OUTPUT);
  pinMode(PIN_ECHO,  INPUT);
  pinMode(PIN_IR,    INPUT_PULLUP);
  pinMode(PIN_RED,   OUTPUT);
  pinMode(PIN_GREEN, OUTPUT);
  pinMode(PIN_BUZ,   OUTPUT);
  pinMode(PIN_SPRAY, OUTPUT);
  pinMode(PIN_START, INPUT_PULLUP);
  pinMode(PIN_RESET, INPUT_PULLUP);
  pinMode(PIN_LIGHT, INPUT_PULLUP);
  pinMode(PIN_LLED,  OUTPUT);

  digitalWrite(PIN_SPRAY, LOW);
  digitalWrite(PIN_RED,   LOW);
  digitalWrite(PIN_GREEN, LOW);
  digitalWrite(PIN_BUZ,   LOW);
  digitalWrite(PIN_LLED,  LOW);

  // ★ الترتيب الصح — BMP → MLX → OLED أخيراً
  bmpOK = bmp.begin(0x76) || bmp.begin(0x77);
  mlxOK = mlx.begin();
  delay(200);
  oledOK = display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  if (!oledOK) oledOK = display.begin(SSD1306_SWITCHCAPVCC, 0x3D);

  Serial.print(F("BMP:"));  Serial.print(bmpOK  ? F("OK") : F("X"));
  Serial.print(F(" MLX:")); Serial.print(mlxOK  ? F("OK") : F("X"));
  Serial.print(F(" OLED:")); Serial.println(oledOK ? F("OK") : F("X"));

  if (oledOK) {
    cls();
    display.fillRect(0, 0, 128, 13, SSD1306_WHITE);
    display.setTextColor(SSD1306_BLACK); display.setTextSize(1);
    display.setCursor(22, 2); display.print(F("HEALTH STATION"));
    display.setTextColor(SSD1306_WHITE); display.setTextSize(1);
    display.setCursor(10, 22);
    display.print(F("BMP:")); display.print(bmpOK ? F("OK") : F("--"));
    display.print(F("  MLX:")); display.print(mlxOK ? F("OK") : F("--"));
    display.setCursor(28, 36); display.print(F("Display: OK"));
    upd();
  }

  // نغمة بداية
  byte mel[] = {80,60,40,20,220};
  byte gap[] = {70,50,35,15,0};
  for (byte i=0; i<5; i++) {
    digitalWrite(PIN_BUZ, HIGH); delay(mel[i]);
    digitalWrite(PIN_BUZ, LOW);  delay(gap[i]);
  }

  delay(1000);
  st = IDLE;
  tEnv = millis();
}

// =============================================================================
void loop() {

  // ── قراءة الأزرار ─────────────────────────────────────────────────────────
  bool sN = digitalRead(PIN_START);
  bool rN = digitalRead(PIN_RESET);
  bool lN = digitalRead(PIN_LIGHT);

  bool sP = (sPrev==HIGH && sN==LOW && millis()-sDeb > DEB);
  bool rP = (rPrev==HIGH && rN==LOW && millis()-rDeb > DEB);
  bool lP = (lPrev==HIGH && lN==LOW && millis()-lDeb > DEB);
  if (sP || rP || lP) { digitalWrite(PIN_BUZ,HIGH); delay(50); digitalWrite(PIN_BUZ,LOW); }

  if (sPrev==HIGH && sN==LOW) sDeb = millis();
  if (rPrev==HIGH && rN==LOW) rDeb = millis();
  if (lPrev==HIGH && lN==LOW) lDeb = millis();
  sPrev=sN; rPrev=rN; lPrev=lN;

  // ★ زرار الإضاءة — تشغيل/إيقاف LED بس، مستقل عن كل حاجة
  if (lP) {
    delay(20);                             // تأكيد الضغطة
    if (digitalRead(PIN_LIGHT) == LOW) {   // لو لسه مضغوط = ضغطة حقيقية
      lightOn = !lightOn;
      digitalWrite(PIN_LLED, lightOn ? HIGH : LOW);
    }
  }

  if (rP) { doReset(); return; }

  // ── State Machine ─────────────────────────────────────────────────────────
  switch (st) {

    case IDLE:
      if (millis()-tEnv >= ENV_MS) { tEnv=millis(); scrIdle(); }
      if (sP) st = SCAN;
      break;

    case SCAN: {
      float d = getDist();
      scrScan(d);
      if (d <= DIST_CM) {
        st=MEAS; mlxN=0; mlxS=0; tMlx=millis();
        digitalWrite(PIN_RED, LOW);
        digitalWrite(PIN_GREEN, HIGH);
        bip(1, 200, 0);
        scrMeas(0);
      }
      if (sP) { st=IDLE; tEnv=0; }
      break;
    }

    case MEAS:
      digitalWrite(PIN_GREEN, HIGH);
      if (mlxOK && millis()-tMlx >= MLX_STEP) {
        tMlx = millis();
        float r = mlx.readObjectTempC();
        if (r>30.0 && r<45.0) { mlxS+=r; mlxN++; scrMeas(mlxN); }
      }
      if (mlxN >= MLX_N) {
        bodyT = (mlxS / mlxN) + 3.5;
        Serial.print(F("T:")); Serial.println(bodyT);
        // ★ LED حسب النتيجة
        if (bodyT < 36.5 || bodyT > 38.5) {
          digitalWrite(PIN_RED, HIGH); digitalWrite(PIN_GREEN, LOW);  // حرارة غير طبيعية
          digitalWrite(PIN_BUZ, HIGH); delay(2000); digitalWrite(PIN_BUZ, LOW);
        } else {
          digitalWrite(PIN_RED, LOW); digitalWrite(PIN_GREEN, HIGH);  // طبيعي
          bip(2, 200, 150);
        }
        scrRes(bodyT);
        st = RES;
      }
      break;

    case RES:
      if (digitalRead(PIN_IR) == LOW) {
        st=SPRAYING; sprayGood=0; sprayEdge=millis();
        bip(1, 100, 0);
      }
      break;

    case SPRAYING: {
      bool hand = (digitalRead(PIN_IR) == LOW);
      digitalWrite(PIN_SPRAY, hand ? HIGH : LOW);

      // ★ بيحسب الوقت الفعلي بس لما الايد موجودة — بدون overflow
      if (hand && !lastHand) sprayEdge = millis();   // بدأت الايد
      if (hand)              sprayGood += millis() - sprayEdge;
      if (hand)              sprayEdge  = millis();  // reset كل iteration

      lastHand = hand;

      if (sprayGood >= SPRAY_MS) {
        digitalWrite(PIN_SPRAY, LOW); digitalWrite(PIN_GREEN, LOW); digitalWrite(PIN_RED, LOW);
        sprayGood=0; lastHand=false;
        scrDone(); bip(2, 400, 200);
        delay(2000);
        st=IDLE; tEnv=0;
      } else if (!hand) {
        scrWait();
      } else {
        scrSpray((SPRAY_MS - sprayGood) / 1000 + 1);
      }
      break;
    }
  }
}
