/*
 * TEST STRISCIA LED - VERSIONE 254 mm (8 strisce da 33 LED 144 LED/m, a serpentina) - ESP32
 * Ogni casella usa il LED piu' vicino al suo centro (gli altri restano spenti).
 * Libreria: Adafruit NeoPixel
 *
 * Ordine dei LED sulla striscia: riga 1 (a1 -> h1), riga 2 (h2 -> a2),
 * riga 3 (a3 -> h3), ... cioe' ogni riga e' girata di 180 gradi.
 *
 * Cosa fa, in loop:
 *   1) tutti i LED rosso, poi verde, poi blu  (controlla l'ordine dei colori)
 *   2) accende UNA casella alla volta: a1, b1 ... h1, a2 ... h8
 *      (controlla che l'ordine sia giusto e non a zig-zag)
 *   3) disegna la scacchiera (caselle chiare accese, scure spente)
 */
#include <Adafruit_NeoPixel.h>

// ===================== CONFIGURAZIONE =====================
#define PIN_LED      5      // <<< MODIFICA: pin dati (D5, verso il 74HCT125)
#define LUMINOSITA   30     // <<< MODIFICA: 0-255, tienila bassa
#define SERPENTINA   true   // <<< MODIFICA: false se ogni riga parte dallo stesso lato
#define INVERTI_COL  true   // <<< MODIFICA: true se le colonne a-h sono specchiate
#define INVERTI_RIGA true   // <<< MODIFICA: true se le traverse 1-8 sono capovolte
#define LED_PER_RIGA 33     // <<< MODIFICA: LED per traversa (33 con 144 LED/m; 8 con 30 LED/m)
#define LED_PASSO_MM 6.944f // <<< MODIFICA: distanza tra due LED (144 LED/m = 6.944; 30 LED/m = 33.33)
#define CASELLA_MM   31.0f  // <<< MODIFICA: lato casella (31 versione 254; 33 versione 268)
#define LED_PRIMO_MM 9.4f   // <<< MODIFICA: dal bordo della casella al centro del 1 LED (striscia centrata = 9.4)
// ==========================================================

#define N_LED (8 * LED_PER_RIGA)
// <<< MODIFICA: se i colori del punto 1 escono in ordine sbagliato, cambia NEO_GRB in NEO_RGB
Adafruit_NeoPixel strip(N_LED, PIN_LED, NEO_GRB + NEO_KHZ800);

// f = colonna 0-7 (a-h), r = traversa 0-7 (1-8)  ->  numero del LED sulla striscia
int ledIndex(int f, int r) {
  int col = INVERTI_COL  ? 7 - f : f;
  int row = INVERTI_RIGA ? 7 - r : r;
  if (SERPENTINA && (row & 1)) col = 7 - col;   // righe dispari girate
  int k = (int)lroundf((CASELLA_MM / 2 + col * CASELLA_MM - LED_PRIMO_MM) / LED_PASSO_MM);   // LED piu' vicino al centro
  if (k < 0) k = 0;
  if (k > LED_PER_RIGA - 1) k = LED_PER_RIGA - 1;
  return row * LED_PER_RIGA + k;
}

void riempi(uint32_t colore) {
  for (int i = 0; i < N_LED; i++) strip.setPixelColor(i, colore);
  strip.show();
}

void setup() {
  strip.begin();
  strip.setBrightness(LUMINOSITA);
  strip.show();                                  // parte spenta
}

void loop() {
  // 1) test colori (si accendono TUTTI i LED della striscia)
  riempi(strip.Color(255, 0, 0));   delay(700);  // rosso
  riempi(strip.Color(0, 255, 0));   delay(700);  // verde
  riempi(strip.Color(0, 0, 255));   delay(700);  // blu

  // 2) una casella alla volta: a1, b1, ... h1, a2, ... h8
  for (int r = 0; r < 8; r++) {
    for (int f = 0; f < 8; f++) {
      strip.clear();
      strip.setPixelColor(ledIndex(f, r), strip.Color(255, 255, 255));
      strip.show();
      delay(120);
    }
  }

  // 3) scacchiera: a1 e' scura, quindi si accendono le caselle con (f + r) dispari
  strip.clear();
  for (int r = 0; r < 8; r++)
    for (int f = 0; f < 8; f++)
      if ((f + r) & 1) strip.setPixelColor(ledIndex(f, r), strip.Color(255, 255, 255));
  strip.show();
  delay(2500);
}
