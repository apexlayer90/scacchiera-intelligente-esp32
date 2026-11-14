/*
 * CALIBRAZIONE TOUCH - ESP32 + ILI9341 + XPT2046
 * Tocca con l'unghia il centro di ogni croce rossa (4 croci, una alla volta).
 * Alla fine il display e il Monitor seriale (115200) mostrano le righe da copiare
 * negli altri sketch al posto delle vecchie TS_MINX ... TOUCH_INVERTI_Y.
 * Poi puoi toccare lo schermo per verificare: il puntino verde deve stare sotto l'unghia.
 */
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <XPT2046_Touchscreen.h>

#define TFT_CS 27
#define TFT_DC 26
#define TFT_RST 25
#define TOUCH_CS 33
#define ROTAZIONE 1          // <<< MODIFICA: lo stesso valore degli altri sketch

Adafruit_ILI9341 tft(TFT_CS, TFT_DC, TFT_RST);
XPT2046_Touchscreen ts(TOUCH_CS);

const int M = 20;                                      // croci a 20 px dai bordi
const int PX[4] = {M, 319 - M, 319 - M, M};
const int PY[4] = {M, M, 239 - M, 239 - M};
long minX, maxX, minY, maxY;

void croce(int x, int y, uint16_t c) {
  tft.drawLine(x - 10, y, x + 10, y, c);
  tft.drawLine(x, y - 10, x, y + 10, c);
}

TS_Point leggiTocco() {                                // media di 16 letture, aspetta il rilascio
  while (!ts.touched()) delay(10);
  delay(80);
  long sx = 0, sy = 0; int n = 0;
  while (ts.touched() && n < 16) { TS_Point p = ts.getPoint(); sx += p.x; sy += p.y; n++; delay(10); }
  while (ts.touched()) delay(10);
  delay(300);
  return TS_Point(sx / max(n, 1), sy / max(n, 1), 0);
}

void setup() {
  Serial.begin(115200);
  SPI.begin();
  tft.begin(); tft.setRotation(ROTAZIONE);
  ts.begin();  ts.setRotation(ROTAZIONE);

  long rx[4], ry[4];
  for (int i = 0; i < 4; i++) {
    tft.fillScreen(ILI9341_BLACK);
    tft.setTextColor(ILI9341_WHITE); tft.setTextSize(2);
    tft.setCursor(60, 110); tft.printf("Tocca la croce %d/4", i + 1);
    croce(PX[i], PY[i], ILI9341_RED);
    TS_Point p = leggiTocco();
    rx[i] = p.x; ry[i] = p.y;
    Serial.printf("croce %d: grezzo x=%ld y=%ld\n", i + 1, rx[i], ry[i]);
  }
  // valori grezzi a sinistra/destra e in alto/in basso (media delle 2 croci)
  float xl = (rx[0] + rx[3]) / 2.0, xr = (rx[1] + rx[2]) / 2.0;
  float yt = (ry[0] + ry[1]) / 2.0, yb = (ry[2] + ry[3]) / 2.0;
  // estende ai bordi dello schermo (le croci sono a 20 px dal bordo)
  float kx = (xr - xl) / (319 - 2 * M), ky = (yb - yt) / (239 - 2 * M);
  minX = xl - kx * M; maxX = xr + kx * M;
  minY = yt - ky * M; maxY = yb + ky * M;
  // se un asse e' invertito MIN > MAX: map() lo gestisce da solo, niente INVERTI

  char r[6][48];
  sprintf(r[0], "#define TS_MINX %ld", minX);
  sprintf(r[1], "#define TS_MAXX %ld", maxX);
  sprintf(r[2], "#define TS_MINY %ld", minY);
  sprintf(r[3], "#define TS_MAXY %ld", maxY);
  sprintf(r[4], "#define TOUCH_INVERTI_X false");
  sprintf(r[5], "#define TOUCH_INVERTI_Y false");
  tft.fillScreen(ILI9341_BLACK);
  tft.setTextSize(1); tft.setTextColor(ILI9341_YELLOW);
  tft.setCursor(10, 10); tft.print("Copia queste righe negli sketch:");
  Serial.println("\n=== COPIA QUESTE RIGHE ===");
  for (int i = 0; i < 6; i++) {
    tft.setCursor(10, 30 + i * 14); tft.setTextColor(ILI9341_WHITE); tft.print(r[i]);
    Serial.println(r[i]);
  }
  tft.setCursor(10, 130); tft.setTextColor(ILI9341_GREEN); tft.print("Ora tocca lo schermo per verificare");
}

void loop() {
  if (ts.touched()) {
    TS_Point p = ts.getPoint();
    int x = constrain(map(p.x, minX, maxX, 0, 320), 0, 319);
    int y = constrain(map(p.y, minY, maxY, 0, 240), 0, 239);
    tft.fillCircle(x, y, 3, ILI9341_GREEN);
    delay(20);
  }
}
