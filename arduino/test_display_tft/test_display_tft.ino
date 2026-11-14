/*
 * TEST DISPLAY TFT 3,2" ILI9341 + TOUCH XPT2046 - ESP32
 *
 * Librerie (Strumenti -> Gestisci librerie):
 *   - "Adafruit ILI9341"        (installa anche le dipendenze: Adafruit GFX, Adafruit BusIO)
 *   - "XPT2046_Touchscreen"     di Paul Stoffregen
 *
 * Collegamenti (come nella tabella):
 *   VCC -> 5V   GND -> GND   CS -> D27   RESET -> D25   DC -> D26
 *   SDI(MOSI) -> D23   SCK -> D18   SDO(MISO) -> D19   LED -> 3V3
 *   T_CLK -> D18   T_DIN -> D23   T_DO -> D19   T_CS -> D33   T_IRQ -> D32
 *
 * Cosa fa:
 *   1) schermo rosso, verde, blu, bianco  (controlla che il display funzioni)
 *   2) disegna una scacchiera 8x8 con le coordinate
 *   3) toccando lo schermo disegna un punto e scrive le coordinate grezze
 *      del touch sul Monitor seriale (115200) -> servono per calibrare
 */
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <XPT2046_Touchscreen.h>

// ===================== CONFIGURAZIONE =====================
#define TFT_CS    27
#define TFT_DC    26
#define TFT_RST   25
#define TOUCH_CS  33
#define TOUCH_IRQ 32
#define ROTAZIONE 1      // <<< MODIFICA: 1 o 3 = orizzontale (prova 3 se l'immagine e' capovolta)

// Calibrazione touch: valori grezzi agli angoli dello schermo
#define TS_MINX 300      // <<< MODIFICA dopo la prova (valore x grezzo a sinistra)
#define TS_MAXX 3800     // <<< MODIFICA (x grezzo a destra)
#define TS_MINY 300      // <<< MODIFICA (y grezzo in alto)
#define TS_MAXY 3800     // <<< MODIFICA (y grezzo in basso)
// ==========================================================

Adafruit_ILI9341 tft(TFT_CS, TFT_DC, TFT_RST);   // SPI hardware: D18 / D19 / D23
XPT2046_Touchscreen ts(TOUCH_CS, TOUCH_IRQ);

void disegnaScacchiera() {
  const int lato = 26;                               // 8 x 26 = 208 px
  const int x0 = (320 - 8 * lato) / 2, y0 = 20;
  tft.fillScreen(ILI9341_BLACK);
  for (int r = 0; r < 8; r++) {
    for (int f = 0; f < 8; f++) {
      bool chiara = (f + r) & 1;                     // a1 scura
      uint16_t col = chiara ? 0xEF5B : 0x6A20;       // crema / marrone
      tft.fillRect(x0 + f * lato, y0 + (7 - r) * lato, lato, lato, col);
    }
    tft.setTextColor(ILI9341_WHITE);
    tft.setTextSize(1);
    tft.setCursor(x0 - 10, y0 + (7 - r) * lato + 9);  tft.print(r + 1);         // 1..8
    tft.setCursor(x0 + r * lato + 10, y0 + 8 * lato + 4); tft.print(char('a' + r)); // a..h
  }
  tft.setCursor(4, 4);
  tft.print("Tocca lo schermo");
}

void setup() {
  Serial.begin(115200);
  SPI.begin();                       // bus condiviso display + touch
  tft.begin();
  tft.setRotation(ROTAZIONE);
  ts.begin();
  ts.setRotation(ROTAZIONE);

  Serial.println("Test display avviato");
  uint16_t colori[] = {ILI9341_RED, ILI9341_GREEN, ILI9341_BLUE, ILI9341_WHITE};
  for (uint16_t c : colori) { tft.fillScreen(c); delay(500); }

  tft.fillScreen(ILI9341_BLACK);
  tft.setTextColor(ILI9341_YELLOW);
  tft.setTextSize(3);
  tft.setCursor(40, 100);
  tft.print("Scacchiera OK");
  delay(1200);

  disegnaScacchiera();
}

void loop() {
  if (ts.touched()) {
    TS_Point p = ts.getPoint();      // valori grezzi 0..4095
    int x = map(p.x, TS_MINX, TS_MAXX, 0, 320);
    int y = map(p.y, TS_MINY, TS_MAXY, 0, 240);
    x = constrain(x, 0, 319);
    y = constrain(y, 0, 239);
    tft.fillCircle(x, y, 3, ILI9341_RED);
    Serial.printf("grezzo x=%4d y=%4d pressione=%4d  ->  schermo x=%3d y=%3d\n", p.x, p.y, p.z, x, y);
    delay(30);
  }
}
