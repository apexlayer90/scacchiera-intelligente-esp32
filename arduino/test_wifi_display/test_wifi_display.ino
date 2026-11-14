/*
 * TEST WI-FI + DISPLAY - ESP32
 * Si collega al Wi-Fi e mostra in alto a destra un simbolo di segnale:
 *   tacche verdi = connesso (piu' tacche = segnale piu' forte)
 *   tacche grigie con X rossa = non connesso (riprova da solo ogni 10 s)
 * In basso: rete, indirizzo IP e potenza del segnale.
 * Librerie: Adafruit ILI9341 (+ Adafruit GFX). WiFi e' gia' inclusa nell'ESP32.
 */
#include <WiFi.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>

// ===================== CONFIGURAZIONE =====================
const char* WIFI_NOME     = "NomeDellaTuaRete";   // <<< MODIFICA: nome della rete (SSID)
const char* WIFI_PASSWORD = "LaTuaPassword";      // <<< MODIFICA: password
#define ROTAZIONE 1                               // <<< MODIFICA: 1 o 3, come nel test display
#define TFT_CS  27
#define TFT_DC  26
#define TFT_RST 25
// ==========================================================

Adafruit_ILI9341 tft(TFT_CS, TFT_DC, TFT_RST);

const int LATO = 26, X0 = (320 - 8 * LATO) / 2, Y0 = 20;   // scacchiera come nel test display
int ultimoStato = -1;            // per ridisegnare solo quando cambia qualcosa
unsigned long ultimoTentativo = 0;

void disegnaScacchiera() {
  tft.fillScreen(ILI9341_BLACK);
  for (int r = 0; r < 8; r++)
    for (int f = 0; f < 8; f++)
      tft.fillRect(X0 + f * LATO, Y0 + (7 - r) * LATO, LATO, LATO, ((f + r) & 1) ? 0xEF5B : 0x6A20);
}

// Simbolo Wi-Fi: 4 tacche crescenti in alto a destra
void disegnaWifi(int tacche, bool connesso) {
  const int x = 290, y = 2;
  tft.fillRect(x - 2, y, 32, 16, ILI9341_BLACK);            // cancella il vecchio simbolo
  for (int i = 0; i < 4; i++) {
    int h = 4 + i * 4;
    uint16_t col = (connesso && i < tacche) ? ILI9341_GREEN : 0x4208;   // verde o grigio
    tft.fillRect(x + i * 7, y + 16 - h, 5, h, col);
  }
  if (!connesso) {                                          // X rossa
    tft.drawLine(x, y, x + 12, y + 12, ILI9341_RED);
    tft.drawLine(x + 12, y, x, y + 12, ILI9341_RED);
  }
}

// RSSI (dBm) -> numero di tacche
int tacche(int rssi) {
  if (rssi > -55) return 4;
  if (rssi > -65) return 3;
  if (rssi > -75) return 2;
  return 1;
}

void scriviInfo(const String& riga) {
  tft.fillRect(0, 230, 320, 10, ILI9341_BLACK);
  tft.setTextSize(1);
  tft.setTextColor(ILI9341_WHITE);
  tft.setCursor(4, 231);
  tft.print(riga);
}

void setup() {
  Serial.begin(115200);
  SPI.begin();
  tft.begin();
  tft.setRotation(ROTAZIONE);
  disegnaScacchiera();
  disegnaWifi(0, false);
  scriviInfo("Connessione a " + String(WIFI_NOME) + "...");

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_NOME, WIFI_PASSWORD);
  ultimoTentativo = millis();
}

void loop() {
  bool ok = WiFi.status() == WL_CONNECTED;
  int t = ok ? tacche(WiFi.RSSI()) : 0;
  int stato = ok ? t : 0;

  if (stato != ultimoStato) {                    // ridisegna solo se cambia
    disegnaWifi(t, ok);
    if (ok) {
      String info = String(WIFI_NOME) + "  IP " + WiFi.localIP().toString() + "  " + WiFi.RSSI() + " dBm";
      scriviInfo(info);
      Serial.println("Connesso: " + info);
    } else {
      scriviInfo("Wi-Fi non connesso, riprovo...");
      Serial.println("Wi-Fi non connesso");
    }
    ultimoStato = stato;
  }

  // se non connesso, riprova ogni 10 secondi
  if (!ok && millis() - ultimoTentativo > 10000) {
    WiFi.disconnect();
    WiFi.begin(WIFI_NOME, WIFI_PASSWORD);
    ultimoTentativo = millis();
  }
  delay(500);
}
