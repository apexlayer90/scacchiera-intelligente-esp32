/*
 * SCACCHIERA INTELLIGENTE - sketch completo (ESP32)
 * ------------------------------------------------------------------
 *  - Display TFT 3,2" ILI9341 + touch XPT2046 (calibrato)
 *  - 64 reed su 4 moduli MCP23017 (0x20..0x23)
 *  - 64 LED WS2812B a serpentina su D5 (tramite 74HCT125)
 *  - Wi-Fi con simbolo sul display
 *  - Avversari: Lichess (Stockfish livello 1-8), ESP32 offline (livello 1-4), 2 giocatori
 *  - Pulsante AIUTO: suggerimento della mossa (LED + display)
 *  - Inizio partita: traverse 1-2 VERDI (Bianco), 3-6 BIANCHE, 7-8 ROSSE (Nero);
 *    la casella lampeggia finche' manca il suo pezzo, arancione lampeggiante = pezzo di troppo
 *  - Fine partita: tutta VERDE se vince il Bianco, ROSSA se vince il Nero, BIANCA se patta
 *  - Patte automatiche (offline e 2 giocatori): stallo, tripla ripetizione, 50 mosse, materiale insufficiente
 *    per 10 s, poi di nuovo traverse 1-2 verdi, 3-6 bianche, 7-8 rosse
 *  - Mossa non valida: casella sbagliata ROSSA lampeggiante veloce, la casella da cui
 *    riprendere il pezzo lampeggia BLU; pezzo tolto per sbaglio: ARANCIONE lampeggiante
 *  - Conferma mossa: lampeggio VERDE sulla casella d'arrivo (ROSSO se Lichess la rifiuta)
 *  - ANALISI a fine partita: rivede le tue mosse, trova errori e imprecisioni e spiega
 *    perche' (pezzo lasciato in presa, cattura mancata, matto) con la mossa migliore
 *  - All'accensione: schermata CONTROLLO (moduli MCP, reed, Wi-Fi, LED); dal menu con "CONTROLLO"
 *  - Monitor seriale: ogni reed che cambia ("Reed e2: pezzo tolto") per controllare i cablaggi
 *  - Motore ESP32: libro delle aperture, ricerca con limite di tempo e scelta casuale tra le
 *    mosse equivalenti (partite sempre diverse)
 *  - PROBLEMI: matto in 1, 2 e 3 mosse (25 problemi, in ordine casuale); disponi i pezzi come sul
 *    display, poi risolvi; AIUTO mostra la mossa giusta, AVANTI passa al problema successivo
 *  - OPZIONI: scelta della rete Wi-Fi dall'elenco delle reti vicine e token Lichess, con la
 *    tastiera sul display oppure dal telefono (pagina web); salvati nella memoria dell'ESP32.
 *    All'accensione, se la rete salvata non si trova, compare l'elenco delle reti
 *  - ANNULLA: riprende l'ultima mossa (2 giocatori) o la tua mossa + la risposta (contro ESP32);
 *    i LED mostrano dove rimettere i pezzi. Contro Lichess la mossa parte dopo RITARDO_LICHESS_MS
 *    con il pezzo fermo: se lo rialzi prima, non viene inviata
 *  - PROBLEMA LICHESS: scarica un problema da lichess.org (serve il Wi-Fi, non serve il token)
 *  - MODALITA' SIMULAZIONE: se i 4 moduli MCP non rispondono, le mosse si fanno
 *    toccando le caselle sul display (utile per provare tutto prima di montare i reed)
 *
 * Librerie (Strumenti -> Gestisci librerie):
 *   Adafruit ILI9341, Adafruit GFX, XPT2046_Touchscreen (Paul Stoffregen),
 *   Adafruit MCP23017 Arduino Library, Adafruit NeoPixel, ArduinoJson (v7)
 * Scheda: "ESP32 Dev Module".  Monitor seriale: 115200.
 * Il giocatore ha sempre i BIANCHI e siede dal lato della traversa 1.
 */
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <XPT2046_Touchscreen.h>
#include <Adafruit_MCP23X17.h>
#include <Adafruit_NeoPixel.h>
#include <Preferences.h>              // memoria dell'ESP32 (rete e token)
#include <WebServer.h>                // pagina per configurare col telefono
#include <DNSServer.h>

// ========================= CONFIGURAZIONE =========================
// Rete e token si possono anche lasciare vuoti e scegliere dalla scacchiera (menu OPZIONI):
// questi valori si usano solo finche' non ne salvi altri dalla scacchiera.
const char* WIFI_NOME     = "";   // <<< MODIFICA: nome Wi-Fi iniziale
const char* WIFI_PASSWORD = "";      // <<< MODIFICA: password Wi-Fi iniziale
const char* LICHESS_TOKEN = "";            // <<< MODIFICA: token Lichess iniziale (non condividerlo)
const char* AP_NOME = "Scacchiera-Setup";  // <<< MODIFICA: rete creata per configurare col telefono
const char* AP_PASS = "scacchiera";        // <<< MODIFICA: sua password (almeno 8 caratteri)

#define ROTAZIONE   1          // orientamento display (provato: 1)
#define TS_MINX     3892       // calibrazione touch (dai tuoi valori)
#define TS_MAXX     364
#define TS_MINY     3871
#define TS_MAXY     249

#define LED_PIN     5          // dati striscia (verso 74HCT125)
#define LUMINOSITA  40         // <<< MODIFICA: 0-255, tienila bassa (consumo e calore)
#define SERPENTINA  true       // <<< MODIFICA: come nel test LED
#define LED_INV_COL true       // <<< MODIFICA: true se le colonne a-h dei LED sono specchiate
#define LED_INV_RIGA true      // <<< MODIFICA: true se le traverse 1-8 dei LED sono capovolte
#define ANALISI_PROF 3         // <<< MODIFICA: profondita' dell'analisi (3 = ~1-2 min a partita, 4 = piu' precisa ma lenta)
#define FINE_LED_MS  10000      // <<< MODIFICA: per quanto la scacchiera resta tutta verde/rossa/bianca a fine partita (ms)
// Reed cablati come nella tabella stampata: modulo 1 = traverse 1-2 ... modulo 4 = traverse 7-8,
// PA = traversa dispari (1,3,5,7), PB = traversa pari (2,4,6,8), pin 0..7 = colonne a..h
#define REED_SCAMBIA_AB false  // <<< MODIFICA: true se PA e PB sono scambiati (traverse lette a coppie invertite)
#define REED_INV_RIGA   false  // <<< MODIFICA: true se le traverse sono capovolte (8 letta come 1)
#define REED_INV_COL    false  // <<< MODIFICA: true se le colonne sono specchiate (h letta come a)
#define WIFI_POTENZA WIFI_POWER_8_5dBm  // <<< MODIFICA: potenza Wi-Fi ridotta contro il brownout (max WIFI_POWER_19_5dBm)
#define LICHESS_PROB_TEMA "endgame"     // <<< MODIFICA: tema problemi Lichess: "" = tutti, "endgame" = finali (pochi pezzi), "mateIn2", "short"...
#define LICHESS_PROB_DIFF "normal"      // <<< MODIFICA: difficolta': easiest, easier, normal, harder, hardest
#define RITARDO_LICHESS_MS 1500         // <<< MODIFICA: contro Lichess la mossa parte se il pezzo resta fermo per questo tempo (ms)
#define RITARDO_MOSSA_MS   0            // <<< MODIFICA: stessa cosa nelle altre modalita' (0 = subito, come prima)
// =================================================================

#define TFT_CS 27
#define TFT_DC 26
#define TFT_RST 25
#define TOUCH_CS 33

Adafruit_ILI9341 tft(TFT_CS, TFT_DC, TFT_RST);
XPT2046_Touchscreen ts(TOUCH_CS);
Adafruit_MCP23X17 mcp[4];
Adafruit_NeoPixel strip(64, LED_PIN, NEO_GRB + NEO_KHZ800);

// rete e token in uso (salvati nella memoria dell'ESP32, modificabili dal menu OPZIONI)
Preferences prefs;
String cfgSsid = "", cfgPass = "", cfgToken = "";

// ======================= REGOLE DEGLI SCACCHI =====================
// Casella: sq = traversa*8 + colonna  (a1 = 0, h1 = 7, a8 = 56)
// Pezzi: 'PNBRQK' bianchi, 'pnbrqk' neri, '.' vuota
struct Pos  { char b[64]; bool white; uint8_t castle; int8_t ep; };   // castle: 1=K 2=Q 4=k 8=q
struct Move { int8_t from, to; char promo; };                          // promo: 'q','r','b','n' o 0
#define MAXM 256                 // mosse massime in una posizione (record teorico: 218)
struct Btn { int x, y, w, h; const char* t; };
enum Modo  { M_LICHESS, M_OFFLINE, M_2P, M_PROBLEMI };
enum Stato { S_STATO, S_MENU, S_LIVELLO, S_SETUP, S_GIOCA, S_ATTESA, S_SYNC, S_PROMO, S_FINE, S_ANALISI,
             S_IMPOST, S_RETI, S_TASTIERA, S_PORTALE };
enum Destino { T_PASS, T_TOKEN };                   // dove va il testo scritto con la tastiera

// rete e token: lettura e scrittura nella memoria dell'ESP32
void caricaConfig() {
  prefs.begin("scacchiera", true);
  cfgSsid  = prefs.getString("ssid",  WIFI_NOME);
  cfgPass  = prefs.getString("pass",  WIFI_PASSWORD);
  cfgToken = prefs.getString("token", LICHESS_TOKEN);
  prefs.end();
}
void salvaConfig() {
  prefs.begin("scacchiera", false);
  prefs.putString("ssid", cfgSsid); prefs.putString("pass", cfgPass); prefs.putString("token", cfgToken);
  prefs.end();
}
void connettiWifi() { if (cfgSsid.length()) WiFi.begin(cfgSsid.c_str(), cfgPass.c_str()); }

static inline bool isW(char c) { return c >= 'A' && c <= 'Z'; }
static inline bool isB(char c) { return c >= 'a' && c <= 'z'; }
static inline bool own(char c, bool w) { return w ? isW(c) : isB(c); }
static inline bool enemy(char c, bool w) { return w ? isB(c) : isW(c); }
static inline char up(char c) { return isB(c) ? c - 32 : c; }
static inline bool onB(int r, int f) { return r >= 0 && r < 8 && f >= 0 && f < 8; }

const int8_t KN[8][2] = {{1,2},{2,1},{2,-1},{1,-2},{-1,-2},{-2,-1},{-2,1},{-1,2}};
const int8_t KG[8][2] = {{1,0},{1,1},{0,1},{-1,1},{-1,0},{-1,-1},{0,-1},{1,-1}};
const int8_t RD[4][2] = {{1,0},{-1,0},{0,1},{0,-1}};
const int8_t BD[4][2] = {{1,1},{1,-1},{-1,1},{-1,-1}};

void startPos(Pos& p) {
  const char* s = "RNBQKBNRPPPPPPPP................................pppppppprnbqkbnr";
  for (int i = 0; i < 64; i++) p.b[i] = s[i];
  p.white = true; p.castle = 15; p.ep = -1;
}

// ============================ PROBLEMI ============================
// liv: 1 = matto in 1, 2 = matto in 2, 3 = matto in 3.  sol: mosse UCI separate da spazi:
// tua mossa, risposta dell'avversario, tua mossa ...  L'ultima mossa e' accettata anche se e' un
// altro scacco matto. Tutti verificati con un risolutore (soluzione unica, nessun matto piu' corto).
struct Problema { uint8_t liv; const char* fen; const char* sol; };
const Problema PROBLEMI[] = {          // <<< MODIFICA: puoi aggiungerne altri (stesso formato)
  {1, "6k1/5ppp/8/8/8/8/5PPP/3R2K1 w - - 0 1", "d1d8"},
  {1, "7k/8/6K1/8/8/8/8/Q7 w - - 0 1", "a1a8"},
  {1, "6rk/6pp/7N/8/8/8/8/6K1 w - - 0 1", "h6f7"},
  {1, "6K1/8/6k1/8/8/8/8/r7 b - - 0 1", "a1a8"},
  {1, "k7/8/1K6/8/8/8/8/7Q w - - 0 1", "h1h8"},
  {1, "k7/7R/8/8/8/8/8/K5R1 w - - 0 1", "g1g8"},
  {1, "k7/2P5/1K6/8/8/8/8/8 w - - 0 1", "c7c8q"},
  {1, "r5k1/5ppp/8/8/8/8/5PPP/6K1 b - - 0 1", "a8a1"},
  {2, "r5k1/5ppp/8/8/8/8/2R2PPP/2R3K1 w - - 0 1", "c2c8 a8c8 c1c8"},
  {2, "k7/p4n2/8/N7/7Q/8/3P1PPK/8 w - - 0 1", "h4e4 a8b8 e4b7"},
  {2, "1k6/pp6/2pr4/8/6B1/8/1P1PP2Q/K7 w - - 0 1", "h2d6 b8a8 d6d8"},
  {2, "6k1/6p1/8/8/8/R4Q2/6PP/4K3 w - - 0 1", "a3a8 g8h7 f3h5"},
  {2, "6bk/6pp/8/7N/8/Q7/2PPP1P1/1K6 w - - 0 1", "a3f8 g7g6 f8f6"},
  {2, "k7/pp2R3/8/5b2/8/R4N2/4K3/8 w - - 0 1", "e7e8 f5c8 e8c8"},
  {2, "kr6/p7/4R1r1/1B6/8/8/8/5K2 w - - 0 1", "b5c6 b8b7 e6e8"},
  {2, "1k6/1pp4R/6n1/2Q4n/5N2/8/P7/4K3 w - - 0 1", "c5c7 b8a7 c7b7"},
  {2, "k3r3/p2Q4/1p6/8/8/8/2B5/2K4R w - - 0 1", "c2e4 e8e4 d7c8"},
  {2, "k4n2/p2r4/2R5/8/8/R7/P1P4Q/7K w - - 0 1", "c6c8 a8b7 h2b8"},
  {2, "1k6/1p5Q/p7/5N2/8/8/1P3PP1/5K2 w - - 0 1", "f5d6 a6a5 h7b7"},
  {3, "2k5/1p1p2R1/2p5/8/8/3B4/P2KP2P/6B1 w - - 0 1", "g1b6 d7d6 d3f5 c8b8 g7g8"},
  {3, "1k6/1p2B3/7Q/8/8/8/P2P1PK1/8 w - - 0 1", "h6b6 b8a8 e7c5 a8b8 b6d8"},
  {3, "6kB/1n3ppp/2R2n2/8/8/8/KP5P/8 w - - 0 1", "c6c8 b7d8 c8d8 f6e8 d8e8"},
  {3, "1k1n4/1pR4Q/2p5/8/8/8/1P1KP3/3R4 w - - 0 1", "d1a1 d8f7 c7f7 c6c5 f7f8"},
  {3, "3k4/2p1pR2/6n1/8/B7/8/2KP2P1/3R4 w - - 0 1", "a4c6 e7e6 d1a1 e6e5 a1a8"},
  {3, "6k1/5pp1/2b1R3/3Q4/8/8/PKP2P2/8 w - - 0 1", "e6c6 g7g6 c6g6 g8h8 d5h5"},
  // --- da partite reali Stockfish contro Stockfish (verificati due volte) ---
  {1, "8/4QR2/p5pk/1p5p/1P6/P3pKbP/4N3/2r5 w - - 6 59", "f7h7"},
  {1, "4N3/1p3p1k/4bQ2/p7/8/6PP/7K/8 w - - 5 68", "f6g7"},
  {1, "6rk/p4p2/5Pp1/1P2P1Qp/P2p2nP/1P3qr1/8/3RK3 b - - 5 47", "f3f2"},
  {1, "7k/3Q4/6P1/8/7P/b3Pp2/5P2/3K4 w - - 4 70", "d7h7"},
  {1, "1R6/8/8/6p1/kpK3P1/P4p2/5P2/8 w - - 0 59", "b8a8"},
  {1, "8/p1p3kp/6q1/2P5/3PP3/8/PP3r2/RNB1K3 b - - 0 39", "g6g1"},
  {1, "8/4R3/2p2kp1/p5p1/Pp4P1/1P6/7K/4R3 w - - 0 47", "e1e6"},
  {1, "4r1k1/p1r2pbp/6p1/2Bq4/2bP2P1/P3n3/3RP1BP/4Q2K b - - 5 29", "d5g2"},
  {1, "1Q6/6pk/7p/1q5P/1P6/6P1/5r2/6K1 b - - 1 60", "b5f1"},
  {1, "r5k1/1p3ppp/p1N4q/8/2P1Q1b1/1BP1b1P1/PP4P1/4NK2 b - - 4 25", "h6h1"},
  {1, "4r3/1bn3pp/1k6/2p1bQ2/8/5P1N/1q1B1KPP/R2r4 b - - 1 27", "b2d2"},
  {1, "8/7Q/7q/3P2p1/2B4k/p4Pp1/6P1/2R2K2 w - - 2 52", "h7h6"},
  {1, "4R3/pp3p1k/nq3Bpp/1b5n/PPP5/2Qr1NPP/6BK/8 w - - 1 28", "e8h8"},
  {1, "1kb3r1/p1q2p2/2Q2p2/6np/1P5r/P4B2/2P3PB/4RR1K b - - 2 28", "c7h2"},
  {1, "1r5k/6R1/1q2p2p/7P/3pQ3/6P1/1PPB4/5RK1 w - - 1 38", "e4h7"},
  {1, "4r3/p1p5/3p4/1Q1R4/8/2k3P1/P7/6K1 w - - 0 41", "b5b3"},
  {1, "5rk1/1p4p1/8/p2P4/PP6/6bp/8/1qQ3K1 b - - 2 43", "b1c1"},
  {1, "8/2k3b1/p7/8/1p5P/1P1p2P1/2r1Q2K/5q2 b - - 6 57", "c2e2"},
  {1, "r4r2/pb1p1k2/1pn1p3/2q2pQ1/8/2P1P3/PP1nBPPP/R3K1NR w KQ - 2 15", "e2h5"},
  {1, "3k4/8/3KP3/1R4p1/6P1/5P1p/7P/8 w - - 1 63", "b5b8"},
  {1, "8/1p2k3/8/2p5/P1b5/4P1R1/P1q5/4K3 b - - 5 50", "c2e2"},
  {1, "1q1N1k2/6p1/6Pp/8/2Q5/r7/4P1K1/8 w - - 14 55", "c4f7"},
  {1, "7R/5kp1/2Q5/2p1pp2/2P4B/4qP2/6PK/8 w - - 4 42", "c6e8"},
  {1, "8/8/3pB1p1/2pPb1Bk/2P1P2P/5K2/8/8 w - - 5 79", "e6g4"},
  {1, "r7/P7/4p1p1/k7/5P1p/7P/3R1P2/1R4K1 w - - 0 50", "d2a2"},
  {1, "2k3r1/5p2/p3pn2/1qP1Q3/3P4/P7/3B1P2/4K3 b - - 12 47", "g8g1"},
  {1, "6R1/5p2/4pk2/6p1/3n2Pp/1r2b3/8/4K3 b - - 3 59", "b3b1"},
  {1, "r1R5/4R3/4p1p1/5k2/5P2/4PK1P/8/6r1 w - - 17 45", "e7f7"},
  {1, "5r2/6k1/3P2pp/p7/B7/6Pb/2NR3P/6K1 b - - 9 42", "f8f1"},
  {1, "1r6/2pkb3/p1p5/6pp/2K4P/2N1pp2/3r1P2/5B1R b - - 2 33", "b8b4"},
  {1, "7k/6p1/p2q3p/1p1p1bN1/6QP/7K/R5P1/5r2 b - - 13 52", "f1h1"},
  {1, "5r1k/p3P1pp/8/3B4/7P/1P3p2/7K/2B2q2 b - - 0 46", "f1g2"},
  {1, "3r3k/R7/1P3Np1/7p/4p3/2N2bR1/1P3P1P/4K3 w - - 6 39", "a7h7"},
  {1, "8/p1p2p2/5k2/8/3p4/2r5/Pr3P1P/4K2R b - - 3 39", "c3c1"},
  {1, "1Q3b1k/7p/3P4/5P1N/3p3P/p7/5PK1/1b6 w - - 2 42", "b8f8"},
  {1, "6k1/1p6/8/p3P1pp/P7/1Pq2P2/2r5/1K2R3 b - - 6 54", "c3b2"},
  {2, "2k5/1p6/2p4p/p7/P1PB2q1/3B1Nb1/1R2r1P1/6K1 b - - 1 37", "g3h2 f3h2 g4g2"},
  {2, "1N3r2/3P2pk/4n3/6pp/3n4/1R5P/P1r5/1R2K3 b - - 10 42", "c2e2 e1d1 f8f1"},
  {2, "r3kb1R/2R5/p4p2/4p3/p3P3/1B2P1Pp/1PP4P/1K6 w - - 0 35", "c7f7 a4a3 h8f8"},
  {2, "8/7k/4P1p1/3PB1P1/8/P7/K7/1R5q w - - 0 54", "b1h1 h7g8 h1h8"},
  {2, "2b3rk/5q1p/8/p3p3/R1PN4/1P1NP1P1/5r2/Q5K1 b - - 1 37", "g8g3 g1h1 f7h5"},
  {2, "7k/6p1/p1b1p3/7B/1P5P/3P2PK/5q2/1N6 b - - 1 38", "f2f1 h3g4 f1f5"},
  {2, "3R4/3R4/5k2/4pN1p/4Pb2/5P1p/6r1/7K w - - 3 60", "d8f8 f6g6 d7g7"},
  {2, "2Q5/3P2r1/1kP5/2r5/P7/4p2P/1P4PK/8 w - - 7 54", "d7d8q g7c7 d8c7"},
  {2, "1r6/p6k/4pBR1/2P2p1p/1b6/3Q3P/qP3PP1/2R3K1 w - - 1 33", "d3d7 h7g6 d7g7"},
  {2, "4rk2/2R4R/8/p3B1rp/Pp1P4/1P2P1p1/1P4P1/6K1 w - - 2 42", "h7h8 g5g8 e5g7"},
  {2, "2R5/2R5/p3p1k1/5r2/2qPp2P/3b2P1/3Q2B1/6K1 w - - 1 40", "c8g8 g6f6 d2h6"},
  {2, "r7/qp3Qpk/2p1n1r1/8/P1P4p/2NB4/P2P1RPP/5RK1 w - - 3 27", "f7g6 h7h8 g6h7"},
  {2, "2n1k3/3n4/3b1pP1/p7/B1Q1P1p1/P1P5/6K1/8 w - - 0 40", "c4f7 e8d8 f7d7"},
  {2, "8/5R2/1p4kp/p5p1/P1P3P1/1P3R1q/5P1K/8 w - - 15 54", "h2h3 b6b5 f3f6"},
  {2, "5rk1/1q3p1R/2nPpPp1/2p1P3/8/2b1N1P1/r1N4P/5QK1 w - - 1 32", "h7g7 g8h8 f1h3"},
  {2, "1r2k2r/2bq1ppp/Q7/pp2p3/1P2K1nP/P1P5/3P2P1/R1B4R b k - 4 22", "f7f5 e4f3 d7d3"},
  {2, "r3k2r/pp3p2/4p2p/2Pp1Pq1/b1nP2p1/P1P5/3QB2P/2K3RR b kq - 9 24", "g5d2 c1b1 d2b2"},
  {2, "3k4/3rR2R/p1B3pp/2r1p2n/4P3/7P/1P3PP1/6K1 w - - 7 43", "e7d7 d8e8 h7h8"},
  {2, "2b1kb1r/1p6/1B3p2/1N2p1p1/1nB1P2p/7P/PP1R1PP1/K6R w - - 0 32", "b5c7 e8e7 b6c5"},
  {2, "7r/pR2R2p/5p2/5Qqk/7p/8/5PP1/2B3K1 w - - 5 30", "e7h7 h8h7 b7h7"},
  {2, "5k2/6p1/1p1p1bQp/2pP4/4p2P/6P1/4KP2/1q6 b - - 11 44", "b1d3 e2e1 f6c3"},
  {2, "8/pp6/8/5Q2/PP4r1/6kP/8/5K2 w - - 0 47", "f5g4 g3h2 g4g2"},
  {2, "5r1k/pB5p/3P2p1/4b3/P1P4q/6QP/6P1/3R3K b - - 0 36", "h4g3 h3h4 g3h2"},
  {2, "8/4R3/2N5/5p1k/2n2p2/r5p1/6P1/6K1 b - - 1 60", "a3a1 e7e1 a1e1"},
  {2, "6r1/1Q3p2/4k3/1P3R2/5PP1/5r2/4K3/8 w - - 0 50", "b7f7 e6d6 f5d5"},
  {2, "6k1/5pp1/p4r2/3p4/7p/P6P/1P4P1/2r2Q1K b - - 0 34", "f6f1 h1h2 f1h1"},
  {2, "r2q1r1k/pp2R2p/n1pn2pN/3p1p2/3P1BQP/8/PPP2PBP/5RK1 w - - 1 19", "f4e5 f8f6 e5f6"},
  {2, "5r1k/2R5/7p/3Pb3/R2B4/2p5/4KP2/8 w - - 1 43", "d4e5 f8f6 a4a8"},
  {2, "2bq1rk1/r2nb1pp/2p1pp2/p7/3PQ3/P2B1N2/2P2PPP/1RB2RK1 w - - 0 16", "e4h7 g8f7 d3g6"},
  {2, "3n1r1k/3P4/8/p3P3/P7/6RP/7K/2R1nr2 b - - 2 61", "f8f2 g3g2 f2g2"},
  {2, "k7/rp5p/1P2q3/4B3/p7/P1N1P3/7P/2KR4 w - - 0 38", "d1d8 e6c8 d8c8"},
  {2, "2r4k/p4pp1/7p/Q2pp3/1P6/1Bq3P1/8/1K6 b - - 1 38", "c3b3 b1a1 c8c1"},
  {2, "8/2p2ppk/1r5p/1Q2p3/4P3/P4P2/KP1q2PP/8 b - - 8 33", "b6b5 a2b1 d2b2"},
  {2, "2r3k1/7p/1P4p1/3p1q2/5N2/1P2p1P1/P3P2R/2rQK3 b - - 3 35", "c1d1 e1d1 f5b1"},
  {2, "7r/p3qk1p/3p1npb/2pN4/2P5/1P6/PBK1BP1P/R6Q b - - 0 22", "e7e2 c2b1 e2d3"},
  {3, "3qk2r/4b3/rpp1Q2B/4P2P/2pP4/p1P5/PP2KPR1/7R w - - 0 29", "h1g1 b6b5 g2g8 h8g8 g1g8"},
  {3, "8/1p4pk/p2R2r1/2p2B1p/8/P7/6PP/3R2K1 w - - 0 32", "f5g6 h7h6 d1d5 c5c4 d5h5"},
  {3, "4r1k1/p3r1pp/7q/p1p2pn1/5N2/1P1R2P1/2P2P2/R5KQ b - - 3 43", "e7e1 a1e1 e8e1 g1g2 h6h1"},
  {3, "5k2/1p2r3/5bP1/7p/r4P1P/5K2/6P1/7R b - - 2 37", "a4a3 f3f2 f6d4 f2f1 a3a1"},
  {3, "2r5/5pk1/bp2p3/4P2q/1pPQ1PpN/6n1/P2N1R1K/R7 b - - 0 33", "h5h4 h2g2 h4h3 g2g1 h3h1"},
  {3, "5rk1/6p1/8/1p2R3/1p1P2p1/7P/1PP2q2/4R2K b - - 1 36", "g4g3 e5e2 g3g2 h1h2 g2g1q"},
  {3, "3k3r/6bp/Q3p3/1N3p1q/8/1P2P1P1/P2BpK2/R6b w - - 2 30", "a6d6 d8e8 d6e6 e8f8 d2b4"},
  {3, "r5k1/6Bp/Pp2p3/6p1/P5n1/7R/4q3/4R2K b - - 0 42", "e2e1 h1g2 e1f2 g2h1 f2f1"},
  {3, "8/4k1pB/4P1qp/p6P/1b2Q3/5R2/6P1/2rR3K w - - 2 54", "e4b7 c1c7 d1d7 c7d7 b7d7"},
  {3, "k6r/1pp3p1/2Nq1p2/3Q4/2n3r1/2P1P3/6RP/4R1K1 w - - 3 39", "e1a1 d6a3 a1a3 c4a3 d5a5"},
  {3, "8/4Q2p/6pk/3p4/8/1p2KP2/b6r/3R4 w - - 10 51", "d1d5 h2e2 e3e2 b3b2 e7f8"},
  {3, "B7/R5pk/4r3/7p/PP3K1P/3r4/4bPP1/5n2 b - - 3 57", "d3d4 a8e4 d4e4 f4g5 e6e5"},
  {3, "2Q4R/8/8/4pp2/7p/5k2/1K6/r2q4 b - - 6 61", "a1b1 b2c3 d1b3 c3d2 b1d1"},
  {3, "2k1r3/1p4p1/1b4p1/PPn3P1/2q2p1P/3Q4/5pB1/R4K2 b - - 2 33", "c4d3 f1f2 c5e4 f2e1 b6f2"},
  {3, "2r3k1/1pq2p2/6p1/1P1b3p/PP1p1P1P/3Rb1P1/8/2Q2K2 b - - 0 41", "c7c1 f1e2 c8c2 d3d2 c2d2"},
  {3, "5k2/8/4Bq2/1P3Q2/P4P2/7P/6PK/4r3 w - - 7 64", "f5f6 f8e8 f6f7 e8d8 f7d7"},
  {3, "8/1p6/5R2/1Pb2p2/2k1p2p/4P2P/6KP/r7 b - - 6 59", "c5e3 f6c6 b7c6 b5b6 a1g1"},
  {3, "5k2/6p1/7p/p2Q1P2/q6P/1pP5/1P3P2/4RK2 w - - 3 42", "d5d8 f8f7 e1e7 f7f6 d8f8"},
  {3, "2b5/1p6/1R2p2p/p3P3/P7/5pk1/1P1r2B1/6K1 b - - 5 45", "d2d1 g2f1 f3f2 g1h1 d1f1"},
  {3, "4k3/2R2N2/5Kp1/4P3/5P2/6P1/8/7r w - - 3 58", "e5e6 h1c1 c7c1 g6g5 c1c8"},
  {3, "rnq5/bppk1pp1/3pr3/3P2P1/p3Q3/2P4B/PP2P3/R3K2R w KQ - 5 24", "h3e6 f7e6 e4e6 d7d8 h1h8"},
  {3, "4r2k/p6p/5p1B/2Pp3b/7Q/P4P2/7K/5q2 b - - 7 38", "e8e2 h4f2 f1f2 h2h3 f2h2"},
  {3, "2b1k3/5R1R/p6p/P2pp3/8/4P3/1n4PP/6K1 w - - 0 45", "f7a7 c8d7 a7a8 d7c8 a8c8"},
  {3, "8/8/5kp1/5R2/P7/1P2PKP1/2r4r/8 b - - 0 47", "g6f5 e3e4 c2c3 f3f4 h2f2"},
  {3, "b3Q3/6rk/p7/2pn3p/2Bb3P/1P6/P4PP1/3R2K1 w - - 1 34", "c4d3 g7g6 e8g6 h7h8 g6h7"},
  {3, "4k3/1pp5/5Pnr/p6p/4n3/1BP3Pp/PP6/2KRR3 w - - 6 28", "e1e4 g6e5 e4e5 e8f8 d1d8"},
  {3, "2kr4/ppp2p2/5B1p/8/3p4/PP1PPB1q/2P2P2/RN3bK1 b - - 3 22", "d8g8 f6g5 g8g5 f3g2 h3g2"},
  {3, "2Qqk2r/3r1pp1/p1B1b2p/2p5/2p2NP1/1P5P/P4P2/4RRK1 w k - 1 24", "c6d7 e8e7 f4d5 e7f8 c8d8"},
  {3, "8/1p5k/3b3P/1p3p1R/b3p1p1/5rP1/2r1N3/R3KB2 b - - 5 35", "d6b4 e2c3 b4c3 e1d1 f3f1"},
  {3, "1k1r1R2/1p2R1p1/pN4Pb/7P/1P6/8/8/1K6 w - - 7 39", "f8d8 b8a7 e7e6 a6a5 d8a8"},
  {3, "1k6/1pp3r1/p7/4P3/1PP1Q3/5B2/P2q3B/7K b - - 0 44", "d2c1 f3d1 c1d1 h2g1 d1g1"},
  {3, "2kr4/pp3p1p/8/4Q3/4RNP1/q4P2/b1B4P/K6R b - - 0 28", "a2b3 a1b1 a3a2 b1c1 a2c2"},
  {3, "4nk2/p4r2/6bp/Q2P2q1/8/2p1N3/8/2K1R3 b - - 1 48", "g5e3 e1e3 f7f1 e3e1 f1e1"},
  {3, "5Q2/kp6/p7/3pP3/3q2Qp/4R3/PPr5/4K3 b - - 2 47", "d4e3 e1f1 e3c1 g4d1 c1d1"}
};
const int N_PROBLEMI = sizeof(PROBLEMI) / sizeof(PROBLEMI[0]);
int  probIdx = 0, probLiv = 1, probPly = 0;      // problema in corso, livello, mossa della soluzione
bool probVisto[N_PROBLEMI];                      // problemi gia' usciti (si azzera quando sono finiti)
bool probBianco = true;                          // colore che muove nel problema (lo giochi tu)
bool probOnline = false;                         // true = problema scaricato da Lichess
String probSolOnline = ""; int probRating = 0;   // soluzione e punteggio del problema Lichess

// legge una posizione FEN (solo pezzi e colore che muove)
bool parseFen(const char* fen, Pos& p) {
  for (int i = 0; i < 64; i++) p.b[i] = '.';
  int r = 7, f = 0; const char* s = fen;
  for (; *s && *s != ' '; s++) {
    if (*s == '/') { r--; f = 0; }
    else if (*s >= '1' && *s <= '8') f += *s - '0';
    else { if (r < 0 || f > 7) return false; p.b[r * 8 + f] = *s; f++; }
  }
  if (*s != ' ') return false;
  s++; p.white = (*s == 'w');
  p.castle = 0; p.ep = -1;
  while (*s && *s != ' ') s++;                     // arrocchi (KQkq o -)
  if (*s == ' ') s++;
  for (; *s && *s != ' '; s++) p.castle |= *s == 'K' ? 1 : *s == 'Q' ? 2 : *s == 'k' ? 4 : *s == 'q' ? 8 : 0;
  if (*s == ' ') s++;                              // casella en passant (es. e3 o -)
  if (s[0] >= 'a' && s[0] <= 'h' && s[1] >= '1' && s[1] <= '8') p.ep = (s[1] - '1') * 8 + (s[0] - 'a');
  return r == 0;
}

// la casella sq e' attaccata dal colore byW?
bool attacked(const Pos& p, int sq, bool byW) {
  int r = sq / 8, f = sq % 8;
  int pr = byW ? r - 1 : r + 1;                         // pedoni
  for (int df = -1; df <= 1; df += 2)
    if (onB(pr, f + df) && p.b[pr * 8 + f + df] == (byW ? 'P' : 'p')) return true;
  for (auto& d : KN) {                                  // cavalli
    int rr = r + d[0], ff = f + d[1];
    if (onB(rr, ff) && p.b[rr * 8 + ff] == (byW ? 'N' : 'n')) return true;
  }
  for (auto& d : KG) {                                  // re
    int rr = r + d[0], ff = f + d[1];
    if (onB(rr, ff) && p.b[rr * 8 + ff] == (byW ? 'K' : 'k')) return true;
  }
  for (int k = 0; k < 8; k++) {                         // torri/donna e alfieri/donna
    int dr = k < 4 ? RD[k][0] : BD[k - 4][0], df = k < 4 ? RD[k][1] : BD[k - 4][1];
    int rr = r + dr, ff = f + df;
    while (onB(rr, ff)) {
      char c = p.b[rr * 8 + ff];
      if (c != '.') {
        if (own(c, byW)) {
          char u = up(c);
          if (u == 'Q' || (k < 4 && u == 'R') || (k >= 4 && u == 'B')) return true;
        }
        break;
      }
      rr += dr; ff += df;
    }
  }
  return false;
}

bool inCheck(const Pos& p, bool w) {
  char k = w ? 'K' : 'k';
  for (int i = 0; i < 64; i++) if (p.b[i] == k) return attacked(p, i, !w);
  return false;
}

int genPseudo(const Pos& p, Move* ml) {
  int n = 0; bool w = p.white;
  auto add = [&](int a, int b, char pr) { if (n < MAXM) { ml[n].from = a; ml[n].to = b; ml[n].promo = pr; n++; } };   // mai oltre MAXM
  for (int sq = 0; sq < 64; sq++) {
    char c = p.b[sq];
    if (!own(c, w)) continue;
    int r = sq / 8, f = sq % 8; char u = up(c);
    if (u == 'P') {
      int dir = w ? 1 : -1, r1 = r + dir;
      if (!onB(r1, f)) continue;
      bool last = (r1 == 7 || r1 == 0);
      auto addP = [&](int t) { if (last) { add(sq, t, 'q'); add(sq, t, 'r'); add(sq, t, 'b'); add(sq, t, 'n'); } else add(sq, t, 0); };
      if (p.b[r1 * 8 + f] == '.') {
        addP(r1 * 8 + f);
        int r2 = r + 2 * dir;
        if (((w && r == 1) || (!w && r == 6)) && p.b[r2 * 8 + f] == '.') add(sq, r2 * 8 + f, 0);
      }
      for (int df = -1; df <= 1; df += 2) {
        if (!onB(r1, f + df)) continue;
        int t = r1 * 8 + f + df;
        if (enemy(p.b[t], w)) addP(t);
        else if (t == p.ep) add(sq, t, 0);
      }
    } else if (u == 'N' || u == 'K') {
      const int8_t (*D)[2] = (u == 'N') ? KN : KG;
      for (int k = 0; k < 8; k++) {
        int rr = r + D[k][0], ff = f + D[k][1];
        if (onB(rr, ff) && !own(p.b[rr * 8 + ff], w)) add(sq, rr * 8 + ff, 0);
      }
    } else {
      for (int k = 0; k < 8; k++) {
        if (u == 'R' && k >= 4) break;
        if (u == 'B' && k < 4) continue;
        int dr = k < 4 ? RD[k][0] : BD[k - 4][0], df = k < 4 ? RD[k][1] : BD[k - 4][1];
        int rr = r + dr, ff = f + df;
        while (onB(rr, ff)) {
          char t = p.b[rr * 8 + ff];
          if (own(t, w)) break;
          add(sq, rr * 8 + ff, 0);
          if (t != '.') break;
          rr += dr; ff += df;
        }
      }
    }
  }
  // arrocco
  if (w && p.b[4] == 'K' && !attacked(p, 4, false)) {
    if ((p.castle & 1) && p.b[7] == 'R' && p.b[5] == '.' && p.b[6] == '.' && !attacked(p, 5, false) && !attacked(p, 6, false)) add(4, 6, 0);
    if ((p.castle & 2) && p.b[0] == 'R' && p.b[1] == '.' && p.b[2] == '.' && p.b[3] == '.' && !attacked(p, 3, false) && !attacked(p, 2, false)) add(4, 2, 0);
  }
  if (!w && p.b[60] == 'k' && !attacked(p, 60, true)) {
    if ((p.castle & 4) && p.b[63] == 'r' && p.b[61] == '.' && p.b[62] == '.' && !attacked(p, 61, true) && !attacked(p, 62, true)) add(60, 62, 0);
    if ((p.castle & 8) && p.b[56] == 'r' && p.b[57] == '.' && p.b[58] == '.' && p.b[59] == '.' && !attacked(p, 59, true) && !attacked(p, 58, true)) add(60, 58, 0);
  }
  return n;
}

void makeMove(Pos& p, const Move& m) {
  char c = p.b[m.from]; char u = up(c); bool w = p.white;
  if (u == 'P' && m.to == p.ep && p.b[m.to] == '.') p.b[m.to + (w ? -8 : 8)] = '.';      // en passant
  if (u == 'K' && abs(m.to - m.from) == 2) {                                              // arrocco: sposta la torre
    if (m.to > m.from) { p.b[m.from + 1] = p.b[m.from + 3]; p.b[m.from + 3] = '.'; }
    else               { p.b[m.from - 1] = p.b[m.from - 4]; p.b[m.from - 4] = '.'; }
  }
  p.b[m.to] = m.promo ? (w ? m.promo - 32 : m.promo) : c;
  p.b[m.from] = '.';
  p.ep = (u == 'P' && abs(m.to - m.from) == 16) ? (m.from + m.to) / 2 : -1;
  for (int s : {(int)m.from, (int)m.to}) {
    if (s == 4) p.castle &= ~3;
    if (s == 60) p.castle &= ~12;
    if (s == 7) p.castle &= ~1;
    if (s == 0) p.castle &= ~2;
    if (s == 63) p.castle &= ~4;
    if (s == 56) p.castle &= ~8;
  }
  p.white = !w;
}

int genLegal(const Pos& p, Move* ml) {
  Move tmp[MAXM];
  int n = genPseudo(p, tmp), k = 0;
  for (int i = 0; i < n; i++) {
    Pos q = p; makeMove(q, tmp[i]);
    if (!inCheck(q, p.white)) ml[k++] = tmp[i];
  }
  return k;
}

String sqName(int s) { String r; r += char('a' + s % 8); r += char('1' + s / 8); return r; }
String uci(const Move& m) { String s = sqName(m.from) + sqName(m.to); if (m.promo) s += m.promo; return s; }
bool parseUci(const Pos& p, const String& u, Move& out) {
  if (u.length() < 4) return false;
  int from = (u[0] - 'a') + (u[1] - '1') * 8, to = (u[2] - 'a') + (u[3] - '1') * 8;
  char pr = u.length() > 4 ? u[4] : 0;
  Move ml[MAXM]; int n = genLegal(p, ml);
  for (int i = 0; i < n; i++)
    if (ml[i].from == from && ml[i].to == to && ml[i].promo == pr) { out = ml[i]; return true; }
  return false;
}

// ======================= MOTORE OFFLINE (ESP32) =====================
int pieceVal(char u) {
  switch (u) { case 'P': return 100; case 'N': return 320; case 'B': return 330;
               case 'R': return 500; case 'Q': return 900; default: return 0; }
}
int evalWhite(const Pos& p) {       // punteggio dal punto di vista del Bianco
  int s = 0;
  for (int sq = 0; sq < 64; sq++) {
    char c = p.b[sq]; if (c == '.') continue;
    char u = up(c); int r = sq / 8, f = sq % 8;
    int centro = 7 - (abs(2 * f - 7) + abs(2 * r - 7)) / 2;   // 0 bordo .. 6 centro
    int v = pieceVal(u);
    if (u == 'N' || u == 'B') v += centro * 5;
    else if (u == 'P') v += centro * 2 + (isW(c) ? r - 1 : 6 - r) * 6;
    else if (u == 'Q') v += centro;
    else if (u == 'K') v -= centro * 3;                        // re al riparo
    s += isW(c) ? v : -v;
  }
  // sviluppo: cavalli e alfieri ancora sulla casa di partenza
  const int8_t PART[4] = {1, 2, 5, 6};
  for (int i = 0; i < 4; i++) {
    char w = p.b[PART[i]], b = p.b[56 + PART[i]];
    if (w == 'N' || w == 'B') s -= 12;
    if (b == 'n' || b == 'b') s += 12;
  }
  int nbW = 0, nbB = 0, colW[8] = {0}, colB[8] = {0};
  for (int sq = 0; sq < 64; sq++) {
    char c = p.b[sq];
    if (c == 'B') nbW++; else if (c == 'b') nbB++;
    else if (c == 'P') colW[sq % 8]++; else if (c == 'p') colB[sq % 8]++;
  }
  if (nbW >= 2) s += 30;                                         // coppia degli alfieri
  if (nbB >= 2) s -= 30;
  for (int f = 0; f < 8; f++) { if (colW[f] > 1) s -= 15 * (colW[f] - 1); if (colB[f] > 1) s += 15 * (colB[f] - 1); }
  if (p.b[6] == 'K' || p.b[2] == 'K') s += 20;                   // re arroccato
  if (p.b[62] == 'k' || p.b[58] == 'k') s -= 20;
  return s;
}
bool engineMove(const Pos& p, int liv, Move& out, bool varieta = true);   // definito piu' avanti

// ---------- motore per l'ANALISI: come sopra + ricerca delle catture (piu' affidabile) ----------
int qsearch(const Pos& p, int alpha, int beta, int qd) {        // continua finche' ci sono catture
  int stand = p.white ? evalWhite(p) : -evalWhite(p);
  if (qd == 0 || stand >= beta) return stand;
  if (stand > alpha) alpha = stand;
  Move ml[MAXM]; int n = genPseudo(p, ml);
  for (int i = 0; i < n; i++) {
    if (p.b[ml[i].to] == '.') continue;                          // solo catture
    Pos q = p; makeMove(q, ml[i]);
    if (inCheck(q, p.white)) continue;                           // mossa illegale
    int v = -qsearch(q, -beta, -alpha, qd - 1);
    if (v >= beta) return v;
    if (v > alpha) alpha = v;
  }
  return alpha;
}
int cerca(const Pos& p, int depth, int alpha, int beta, int ply, Move* best) {
  if (depth == 0) return qsearch(p, alpha, beta, 4);
  Move ml[MAXM]; int n = genLegal(p, ml);
  if (n == 0) return inCheck(p, p.white) ? -30000 + ply : 0;
  int k = 0;
  for (int i = 0; i < n; i++) if (p.b[ml[i].to] != '.') { Move t = ml[k]; ml[k] = ml[i]; ml[i] = t; k++; }
  if (best) *best = ml[0];
  for (int i = 0; i < n; i++) {
    Pos q = p; makeMove(q, ml[i]);
    int v = -cerca(q, depth - 1, -beta, -alpha, ply + 1, nullptr);
    if (v > alpha) { alpha = v; if (best) *best = ml[i]; }
    if (alpha >= beta) break;
  }
  return alpha;
}

// ============================ STATO ==============================
Modo  modo = M_OFFLINE;
Stato stato = S_MENU;
int   livello = 3;
Pos   pos;
Pos   setupPos;                       // posizione da disporre (iniziale o del problema)
bool  sim = false;                   // true = niente reed, mosse col touch
uint64_t occ = 0, rawPrev = 0, seenEmpty = 0;
int   stableCnt = 0;
bool  occChanged = false;
int   plyCount = 0;
Move  lastMove = {-1, -1, 0};
Move  syncMove; bool syncCapture = false;
Pos   posPrima;                       // posizione prima della mossa dell'avversario
Move  promoCand[4]; int nPromo = 0;
int   selSq = -1;                    // casella selezionata col touch (simulazione)
Move  hint = {-1, -1, 0}; unsigned long hintUntil = 0;
String messaggio = "";
Move  storia[600]; int nStoria = 0;       // tutte le mosse della partita (per l'analisi)
struct Errore { int16_t ply; Move giocata, migliore; int16_t perdita; char perche[72]; };
Errore errori[40]; int nErrori = 0, aIdx = -1;   // aIdx = -1: pagina riassunto
String ultimaPartitaLichess = "";
unsigned long avvioLedFino = 0;            // traverse colorate ancora accese a inizio partita
int   flashSq = -1; uint32_t flashCol = 0; unsigned long flashFino = 0;   // lampeggio di conferma
String risultato = "";

static inline uint64_t BB(int s) { return 1ULL << s; }
uint64_t dotMask = 0;
#define CONFERMA_MS 1800       // <<< MODIFICA: durata del lampeggio di conferma (ms)
void lampeggia(int sq, uint32_t col, int ms = CONFERMA_MS) { flashSq = sq; flashCol = col; flashFino = millis() + ms; }
uint64_t occOf(const Pos& p) { uint64_t o = 0; for (int i = 0; i < 64; i++) if (p.b[i] != '.') o |= BB(i); return o; }

// ======================== MOTORE ESP32 (offline) ========================
// Migliorato: ricerca a profondita' crescente con limite di tempo, ricerca delle catture
// alla fine (non lascia pezzi in presa), libro delle aperture e scelta casuale tra le
// mosse quasi equivalenti (cosi' non gioca sempre le stesse partite).
const int  MOT_PROF[5]    = {0, 1, 2, 3, 5};             // profondita' massima per livello 1..4
const unsigned long MOT_TEMPO[5] = {0, 1500, 3000, 5000, 8000};   // <<< MODIFICA: tempo massimo per mossa (ms)
const int  MOT_MARGINE[5] = {0, 120, 50, 20, 8};         // <<< MODIFICA: varieta' (centipedoni): piu' alto = piu' varia/debole
unsigned long motFine = 0; bool motStop = false; long motNodi = 0;

int cercaM(const Pos& p, int depth, int alpha, int beta, int ply) {
  if ((++motNodi & 255) == 0 && millis() > motFine) motStop = true;
  if (motStop) return 0;
  if (depth == 0) return qsearch(p, alpha, beta, 4);
  Move ml[MAXM]; int n = genLegal(p, ml);
  if (n == 0) return inCheck(p, p.white) ? -30000 + ply : 0;
  int k = 0;                                                    // catture per prime
  for (int i = 0; i < n; i++) if (p.b[ml[i].to] != '.') { Move t = ml[k]; ml[k] = ml[i]; ml[i] = t; k++; }
  for (int i = 0; i < n; i++) {
    Pos q = p; makeMove(q, ml[i]);
    int v = -cercaM(q, depth - 1, -beta, -alpha, ply + 1);
    if (motStop) return 0;
    if (v > alpha) alpha = v;
    if (alpha >= beta) break;
  }
  return alpha;
}

// libro delle aperture: dopo queste mosse (dall'inizio) l'ESP32 sceglie a caso una delle risposte
struct Libro { const char* mosse; const char* risposte; };
const Libro LIBRO[] = {
  {"e2e4", "e7e5 c7c5 e7e6 c7c6 d7d5 g8f6 d7d6"},
  {"d2d4", "d7d5 g8f6 e7e6 d7d6 g7g6"},
  {"c2c4", "e7e5 g8f6 c7c5 e7e6"},
  {"g1f3", "d7d5 g8f6 c7c5 g7g6"},
  {"e2e4 e7e5 g1f3", "b8c6 g8f6 d7d6"},
  {"e2e4 e7e5 g1f3 b8c6 f1b5", "a7a6 g8f6 f8c5"},
  {"e2e4 e7e5 g1f3 b8c6 f1c4", "f8c5 g8f6 f8e7"},
  {"e2e4 e7e5 g1f3 b8c6 d2d4", "e5d4"},
  {"e2e4 e7e5 b1c3", "g8f6 b8c6"},
  {"e2e4 c7c5 g1f3", "d7d6 b8c6 e7e6"},
  {"e2e4 c7c5 b1c3", "b8c6 e7e6 d7d6"},
  {"e2e4 e7e6 d2d4", "d7d5"},
  {"e2e4 c7c6 d2d4", "d7d5"},
  {"e2e4 d7d5 e4d5", "d8d5 g8f6"},
  {"d2d4 d7d5 c2c4", "e7e6 c7c6 d5c4"},
  {"d2d4 d7d5 g1f3", "g8f6 e7e6 c7c5"},
  {"d2d4 g8f6 c2c4", "e7e6 g7g6 c7c5"},
  {"d2d4 g8f6 g1f3", "d7d5 e7e6 g7g6"},
  {"c2c4 e7e5 b1c3", "g8f6 b8c6"},
  {"b1c3", "d7d5 g8f6 e7e5 c7c5"},
  {"g2g3", "d7d5 g8f6 e7e5 g7g6"},
  {"f2f4", "d7d5 g8f6 c7c5"},
  {"b2b3", "e7e5 d7d5 g8f6"},
  {"e2e3", "e7e5 d7d5 g8f6 c7c5"},
  {"d2d3", "e7e5 d7d5 g8f6"},
};
bool libroAperture(const Pos& p, Move& out) {
  if (modo != M_OFFLINE || nStoria == 0 || nStoria > 6) return false;
  String chiave = "";
  for (int i = 0; i < nStoria; i++) { if (i) chiave += " "; chiave += uci(storia[i]); }
  for (const Libro& l : LIBRO) {
    if (chiave != l.mosse) continue;
    String r = l.risposte, opz[8]; int n = 0, i = 0;
    while (i < (int)r.length() && n < 8) {
      int sp = r.indexOf(' ', i); if (sp < 0) sp = r.length();
      opz[n++] = r.substring(i, sp); i = sp + 1;
    }
    for (int t = 0; t < 4 && n > 0; t++) if (parseUci(p, opz[random(0, n)], out)) return true;
  }
  return false;
}

// varieta = true: partite contro l'ESP32 (sceglie a caso tra le mosse quasi uguali)
// ===================== PATTE AUTOMATICHE =====================
// Solo partite offline e a 2 giocatori: su Lichess le patte le decide il server.
#define PATTA_RIPETIZIONE true   // <<< MODIFICA: false = niente patta per tripla ripetizione
#define PATTA_50_MOSSE    true   // <<< MODIFICA: false = niente patta per regola delle 50 mosse
#define PATTA_MATERIALE   true   // <<< MODIFICA: false = niente patta per materiale insufficiente

// casella en passant che conta davvero: solo se un pedone puo' catturare (regola FIDE)
int8_t epVero(const Pos& p) {
  if (p.ep < 0) return -1;
  int ps = p.white ? p.ep - 8 : p.ep + 8;          // pedone che ha appena fatto il doppio passo
  char mio = p.white ? 'P' : 'p'; int f = ps % 8;
  if ((f > 0 && p.b[ps - 1] == mio) || (f < 7 && p.b[ps + 1] == mio)) return p.ep;
  return -1;
}

bool stessaPosizione(const Pos& a, const Pos& b) {
  return a.white == b.white && a.castle == b.castle && epVero(a) == epVero(b) && memcmp(a.b, b.b, 64) == 0;
}

// quante volte 'q' e' gia' comparsa nella partita (posizione iniziale compresa)
int volteVista(const Pos& q) {
  if (nStoria != plyCount) return 0;               // storia incompleta (partita oltre 600 semimosse): non si conta
  Pos p; startPos(p); int n = stessaPosizione(p, q);
  for (int i = 0; i < nStoria; i++) { makeMove(p, storia[i]); n += stessaPosizione(p, q); }
  return n;
}

// semimosse dall'ultima cattura o mossa di pedone
int semimosse50() {
  Pos p; startPos(p); int hm = 0;
  for (int i = 0; i < nStoria; i++) {
    const Move& m = storia[i];
    hm = (up(p.b[m.from]) == 'P' || p.b[m.to] != '.') ? 0 : hm + 1;
    makeMove(p, m);
  }
  return hm;
}

// nessuno puo' piu' dare matto: R-R, R+A-R, R+C-R, alfieri tutti sullo stesso colore
bool materialeInsufficiente(const Pos& p) {
  int minori = 0, cavalli = 0, alfChiari = 0, alfScuri = 0;
  for (int i = 0; i < 64; i++) {
    char u = up(p.b[i]);
    if (u == 'P' || u == 'Q' || u == 'R') return false;
    if (u == 'N') { cavalli++; minori++; }
    if (u == 'B') { minori++; if (((i / 8) + (i % 8)) & 1) alfChiari++; else alfScuri++; }
  }
  if (minori <= 1) return true;
  return cavalli == 0 && (alfChiari == 0 || alfScuri == 0);
}

// motivo della patta, oppure nullptr se la partita continua
const char* pattaAutomatica() {
  if (modo != M_OFFLINE && modo != M_2P) return nullptr;
  if (PATTA_RIPETIZIONE && nStoria == plyCount && volteVista(pos) >= 3) return "tripla ripetizione";
  if (PATTA_50_MOSSE && nStoria == plyCount && semimosse50() >= 100) return "regola delle 50 mosse";
  if (PATTA_MATERIALE && materialeInsufficiente(pos)) return "materiale insufficiente";
  return nullptr;
}

// varieta = false: suggerimento AIUTO (sempre la mossa migliore)
bool engineMove(const Pos& p, int liv, Move& out, bool varieta) {
  Move ml[MAXM]; int n = genLegal(p, ml);
  if (n == 0) return false;
  if (varieta && libroAperture(p, out)) return true;
  liv = constrain(liv, 1, 4);
  int margine = varieta ? MOT_MARGINE[liv] : 0;
  int sc[MAXM], fin[MAXM], ord[MAXM];
  for (int i = 0; i < n; i++) { ord[i] = i; fin[i] = 0; }
  bool fatto = false;
  bool rip[MAXM];                                               // mosse che portano alla 3a ripetizione = patta
  bool usaStoria = PATTA_RIPETIZIONE && (modo == M_OFFLINE || modo == M_2P) && stessaPosizione(p, pos);
  for (int i = 0; i < n; i++) { rip[i] = false; if (usaStoria) { Pos q = p; makeMove(q, ml[i]); rip[i] = volteVista(q) >= 2; } }
  motStop = false; motNodi = 0;
  unsigned long limite = millis() + MOT_TEMPO[liv];
  for (int d = 1; d <= MOT_PROF[liv]; d++) {
    if (d > 1 && millis() > limite) break;
    motFine = d == 1 ? millis() + 60000UL : limite;            // la profondita' 1 si completa sempre
    int best = -32000;
    for (int j = 0; j < n; j++) {
      int i = ord[j];
      Pos q = p; makeMove(q, ml[i]);
      int a = max(-32000, best - margine - 1);                   // finestra: le mosse peggiori escono sotto 'a'
      sc[i] = rip[i] ? 0 : -cercaM(q, d - 1, -32000, -a, 1);   // ripetizione: vale patta (0)
      if (motStop) break;
      if (sc[i] > best) best = sc[i];
    }
    if (motStop) break;                                        // tiene il risultato della profondita' precedente
    for (int i = 0; i < n; i++) fin[i] = sc[i];
    fatto = true;
    for (int a = 1; a < n; a++) { int t = ord[a], b = a; while (b > 0 && sc[ord[b - 1]] < sc[t]) { ord[b] = ord[b - 1]; b--; } ord[b] = t; }
    if (best > 29000) break;                                   // matto trovato
  }
  if (!fatto) { out = ml[0]; return true; }
  int best = -32000;
  for (int i = 0; i < n; i++) if (fin[i] > best) best = fin[i];
  if (best > 29000 || best < -29000) margine = 0;              // matti: niente scherzi
  int cand[MAXM], nc = 0;
  for (int i = 0; i < n; i++) if (fin[i] >= best - margine) cand[nc++] = i;
  out = ml[cand[nc > 1 ? random(0, nc) : 0]];
  Serial.printf("ESP32: %s (%d mosse candidate, %ld nodi)\n", uci(out).c_str(), nc, motNodi);
  return true;
}

// =========================== DISPLAY =============================
const int LATO = 25, X0 = 14, Y0 = 16, PX = 228;     // scacchiera a sinistra, pannello a destra
#define C_CHIARA 0xEF5B
#define C_SCURA  0x6A20
Btn bNuova = {PX, 172, 88, 30, "NUOVA"};
Btn bAiuto = {PX, 207, 88, 30, "AIUTO"};
Btn bAnalisi = {PX, 207, 88, 30, "ANALISI"};
Btn bAvanti = {PX, 207, 88, 30, "AVANTI"};      // problema successivo (al posto di ANALISI)
Btn bAnnulla = {PX, 137, 88, 30, "ANNULLA"};    // riprende l'ultima mossa
bool puoAnnullare() {                           // 2 giocatori: 1 mossa; contro ESP32: la tua + la sua risposta
  if (stato != S_GIOCA && stato != S_SYNC) return false;
  return (modo == M_2P && nStoria >= 1) || (modo == M_OFFLINE && nStoria >= 2);
}
Btn bPrec = {PX, 207, 42, 30, "<"};
Btn bSucc = {PX + 46, 207, 42, 30, ">"};

void drawBtn(const Btn& b, uint16_t col) {
  tft.fillRoundRect(b.x, b.y, b.w, b.h, 6, col);
  tft.drawRoundRect(b.x, b.y, b.w, b.h, 6, ILI9341_WHITE);
  tft.setTextSize(2); tft.setTextColor(ILI9341_WHITE);
  int tw = strlen(b.t) * 12;
  tft.setCursor(b.x + (b.w - tw) / 2, b.y + (b.h - 16) / 2);
  tft.print(b.t);
}
bool inBtn(const Btn& b, int x, int y) { return x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h; }

int wifiLast = -2;
void drawWifi() {
  int& last = wifiLast;
  bool ok = WiFi.status() == WL_CONNECTED;
  int rssi = ok ? WiFi.RSSI() : -100;
  int t = !ok ? 0 : rssi > -55 ? 4 : rssi > -65 ? 3 : rssi > -75 ? 2 : 1;
  if (t == last) return;
  if (ok != (last > 0)) {                               // stato cambiato: scrivilo anche nel Monitor seriale
    if (ok) Serial.printf("Wi-Fi connesso a %s, IP %s, segnale %d dBm\n", cfgSsid.c_str(), WiFi.localIP().toString().c_str(), rssi);
    else Serial.println("Wi-Fi NON connesso");
  }
  last = t;
  const int x = 290, y = 2;
  tft.fillRect(x - 2, y, 32, 16, ILI9341_BLACK);
  for (int i = 0; i < 4; i++) {
    int h = 4 + i * 4;
    tft.fillRect(x + i * 7, y + 16 - h, 5, h, (ok && i < t) ? ILI9341_GREEN : 0x4208);
  }
  if (!ok) { tft.drawLine(x, y, x + 12, y + 12, ILI9341_RED); tft.drawLine(x + 12, y, x, y + 12, ILI9341_RED); }
}

const char* nomeIt(char u) {          // lettere italiane: Re, Donna, Torre, Alfiere, Cavallo, Pedone
  switch (u) { case 'K': return "R"; case 'Q': return "D"; case 'R': return "T";
               case 'B': return "A"; case 'N': return "C"; default: return "P"; }
}

void drawSquare(int sq) {
  int f = sq % 8, r = sq / 8;
  int x = X0 + f * LATO, y = Y0 + (7 - r) * LATO;
  tft.fillRect(x, y, LATO, LATO, ((f + r) & 1) ? C_CHIARA : C_SCURA);
  bool hintOn = millis() < hintUntil;
  uint16_t frame = 0; bool hasFrame = false;
  if (sq == lastMove.from || sq == lastMove.to) { frame = ILI9341_YELLOW; hasFrame = true; }
  if (hintOn && (sq == hint.from || sq == hint.to)) { frame = sq == hint.from ? ILI9341_CYAN : ILI9341_MAGENTA; hasFrame = true; }
  if (sq == selSq) { frame = ILI9341_BLUE; hasFrame = true; }
  if (hasFrame) { tft.drawRect(x, y, LATO, LATO, frame); tft.drawRect(x + 1, y + 1, LATO - 2, LATO - 2, frame); }
  char c = pos.b[sq];
  if (c != '.') {
    int cx = x + LATO / 2, cy = y + LATO / 2;
    bool w = isW(c);
    tft.fillCircle(cx, cy, 9, w ? ILI9341_WHITE : ILI9341_BLACK);
    tft.drawCircle(cx, cy, 9, w ? ILI9341_BLACK : ILI9341_WHITE);
    tft.setTextSize(1); tft.setTextColor(w ? ILI9341_BLACK : ILI9341_WHITE);
    tft.setCursor(cx - 2, cy - 3); tft.print(nomeIt(up(c)));
  } else if (dotMask & BB(sq)) {                        // puntino: mossa possibile
    tft.fillCircle(x + LATO / 2, y + LATO / 2, 3, ILI9341_GREEN);
  }
}
void drawBoard() {
  dotMask = 0;
  if (selSq >= 0 && sim) { Move ml[MAXM]; int n = genLegal(pos, ml); for (int i = 0; i < n; i++) if (ml[i].from == selSq) dotMask |= BB(ml[i].to); }
  for (int i = 0; i < 64; i++) drawSquare(i);
}

void drawFrameAndCoords() {
  tft.fillScreen(ILI9341_BLACK);
  tft.drawRect(X0 - 1, Y0 - 1, 8 * LATO + 2, 8 * LATO + 2, 0x8410);
  tft.setTextSize(1); tft.setTextColor(ILI9341_WHITE);
  for (int i = 0; i < 8; i++) {
    int cx = X0 + i * LATO + LATO / 2 - 3, cy = Y0 + (7 - i) * LATO + LATO / 2 - 4;
    tft.setCursor(cx, Y0 + 8 * LATO + 4); tft.print(char('a' + i));
    tft.setCursor(cx, Y0 - 11);           tft.print(char('a' + i));
    tft.setCursor(X0 - 11, cy);           tft.print(i + 1);
    tft.setCursor(X0 + 8 * LATO + 5, cy); tft.print(i + 1);
  }
}

void printWrap(const String& s, int x, int y, int maxCh, uint16_t col) {
  tft.setTextColor(col); tft.setTextSize(1);
  int i = 0, line = 0;
  while (i < (int)s.length() && line < 4) {
    int len = min(maxCh, (int)s.length() - i);
    if (i + len < (int)s.length()) { int sp = s.lastIndexOf(' ', i + len); if (sp > i) len = sp - i; }
    tft.setCursor(x, y + line * 10); tft.print(s.substring(i, i + len));
    i += len; while (i < (int)s.length() && s[i] == ' ') i++; line++;
  }
}

void drawPanel() {
  tft.fillRect(PX, 20, 92, 150, ILI9341_BLACK);
  tft.setTextSize(1); tft.setTextColor(0xAD55);
  tft.setCursor(PX, 22);
  if (modo == M_LICHESS) tft.printf("Lichess liv.%d", livello);
  else if (modo == M_OFFLINE) tft.printf("ESP32 liv.%d", livello);
  else if (modo == M_PROBLEMI && probOnline) tft.printf("Lichess %d", probRating);
  else if (modo == M_PROBLEMI) tft.printf("Problema %d", probIdx + 1);
  else tft.print("2 giocatori");
  if (sim) { tft.setCursor(PX, 34); tft.setTextColor(ILI9341_ORANGE); tft.print("SIMULAZIONE"); }
  tft.setTextSize(2);
  tft.setCursor(PX, 50);
  tft.setTextColor(pos.white ? ILI9341_WHITE : 0x8410);
  if (stato == S_FINE) { tft.setTextColor(ILI9341_YELLOW); tft.print("FINE"); }
  else tft.print(pos.white ? "Bianco" : "Nero");
  tft.setTextSize(1);
  if (lastMove.from >= 0) { tft.setTextColor(ILI9341_YELLOW); tft.setCursor(PX, 72); tft.print("Ultima: " + uci(lastMove)); }
  printWrap(stato == S_FINE ? risultato : messaggio, PX, 88, 15, ILI9341_WHITE);
  if (puoAnnullare()) drawBtn(bAnnulla, 0x8200);           // arancione scuro
  if (stato == S_FINE) drawBtn(modo == M_PROBLEMI ? bAvanti : bAnalisi, 0x0320);   // al posto di AIUTO
  if (stato == S_PROMO) {
    const char* nomi[4] = {"Donna", "Torre", "Alfiere", "Cavallo"};
    for (int i = 0; i < 4; i++) { Btn b = {PX, 40 + i * 32, 88, 28, nomi[i]}; drawBtn(b, 0x2124); }
  }
}
void setMsg(const String& s) { messaggio = s; Serial.println(s); drawPanel(); }

void drawGameScreen() {
  drawFrameAndCoords(); drawBoard(); drawPanel();
  drawBtn(bNuova, 0x3186); drawBtn(bAiuto, 0x03EF);
  wifiLast = -2;                                        // ridisegna il simbolo Wi-Fi
}

// --- menu ---
Btn bLichess = {20, 48, 280, 34, "Contro Lichess"};
Btn bOffline = {20, 86, 280, 34, "Contro ESP32"};
Btn b2p      = {20, 124, 280, 34, "2 giocatori"};
Btn bProblemi = {20, 162, 280, 34, "Problemi"};
Btn bProbLichess = {20, 202, 280, 34, "Problema Lichess"};
Btn bStato = {20, 204, 136, 32, "CONTROLLO"};
Btn bOpzioni = {164, 204, 136, 32, "OPZIONI"};
void drawMenu() {
  tft.fillScreen(ILI9341_BLACK);
  tft.setTextSize(2); tft.setTextColor(ILI9341_YELLOW); tft.setCursor(40, 12); tft.print("Nuova partita");
  drawBtn(bLichess, 0x0320); drawBtn(bOffline, 0x0019); drawBtn(b2p, 0x4208); drawBtn(bProblemi, 0x7800);
  drawBtn(bStato, sim ? 0xA000 : 0x2124);               // rosso scuro se manca qualche modulo
  drawBtn(bOpzioni, 0x4208);                            // rete Wi-Fi e token Lichess
  if (sim) { tft.setTextSize(1); tft.setTextColor(ILI9341_ORANGE); tft.setCursor(40, 36); tft.print("SIMULAZIONE: moduli reed mancanti"); }
  wifiLast = -2;                                        // simbolo Wi-Fi anche nel menu
}
int nLivelli() { return modo == M_LICHESS ? 8 : modo == M_PROBLEMI ? 3 : 4; }
Btn livBtn(int i) {
  static char t[8][2];
  t[i][0] = '1' + i; t[i][1] = 0;
  return Btn{20 + (i % 4) * 72, 70 + (i / 4) * 70, 60, 55, t[i]};
}
void drawLivelli() {
  tft.fillScreen(ILI9341_BLACK);
  tft.setTextSize(2); tft.setTextColor(ILI9341_YELLOW); tft.setCursor(40, 20);
  tft.print(modo == M_LICHESS ? "Livello Stockfish" : modo == M_PROBLEMI ? "Problemi" : "Livello ESP32");
  for (int i = 0; i < nLivelli(); i++) drawBtn(livBtn(i), 0x0019);
  if (modo == M_PROBLEMI) {
    tft.setTextSize(2); tft.setTextColor(ILI9341_WHITE);
    tft.setCursor(20, 136); tft.print("1 = matto in 1");
    tft.setCursor(20, 157); tft.print("2 = matto in 2");
    tft.setCursor(20, 178); tft.print("3 = matto in 3");
    drawBtn(bProbLichess, 0x0320);                       // problema scaricato da lichess.org
  }
  wifiLast = -2;
}

// ============================ ANALISI ============================
const char* nomeLungo(char u) {
  switch (up(u)) { case 'K': return "il Re"; case 'Q': return "la Donna"; case 'R': return "la Torre";
                   case 'B': return "l'Alfiere"; case 'N': return "il Cavallo"; default: return "il Pedone"; }
}
String mossaLeggibile(const Pos& p, const Move& m) {   // es. "C g1-f3"
  String s = nomeIt(up(p.b[m.from])); s += " " + sqName(m.from) + (p.b[m.to] != '.' ? "x" : "-") + sqName(m.to);
  if (m.promo) s += "=" + String(nomeIt(up(m.promo)));
  return s;
}
bool mossaDelGiocatore(int ply) { return modo == M_2P || (ply % 2 == 0); }   // il giocatore ha i Bianchi

void eseguiAnalisi() {
  stato = S_ANALISI; nErrori = 0; aIdx = -1;
  int tot = 0; for (int i = 0; i < nStoria; i++) if (mossaDelGiocatore(i)) tot++;
  Pos p; startPos(p); int fatte = 0;
  Serial.println("=== ANALISI DELLA PARTITA ===");
  for (int i = 0; i < nStoria; i++) {
    const Move& m = storia[i];
    if (mossaDelGiocatore(i)) {
      tft.fillRect(PX, 20, 92, 150, ILI9341_BLACK);           // barra di avanzamento
      tft.setTextSize(1); tft.setTextColor(ILI9341_WHITE); tft.setCursor(PX, 40); tft.print("Analisi...");
      tft.setCursor(PX, 55); tft.printf("mossa %d di %d", fatte + 1, tot);
      tft.drawRect(PX, 70, 88, 10, ILI9341_WHITE); tft.fillRect(PX + 1, 71, 86 * fatte / max(tot, 1), 8, ILI9341_GREEN);
      Move best, risposta;
      int sBest = cerca(p, ANALISI_PROF, -32000, 32000, 0, &best);
      Pos q = p; makeMove(q, m);
      int sGioc = -cerca(q, ANALISI_PROF - 1, -32000, 32000, 1, &risposta);
      int perdita = sBest - sGioc;
      if (perdita >= 60 && !(best.from == m.from && best.to == m.to) && nErrori < 40) {
        Errore& e = errori[nErrori++];
        e.ply = i; e.giocata = m; e.migliore = best; e.perdita = perdita;
        if (sBest > 29000) snprintf(e.perche, sizeof(e.perche), "C'era scacco matto con %s", mossaLeggibile(p, best).c_str());
        else if (sGioc < -29000) snprintf(e.perche, sizeof(e.perche), "Dopo questa mossa l'avversario ti da' scacco matto");
        else if (q.b[risposta.to] != '.' && pieceVal(up(q.b[risposta.to])) >= 300 && perdita >= 120)
          snprintf(e.perche, sizeof(e.perche), "Lasci in presa %s in %s", nomeLungo(q.b[risposta.to]), sqName(risposta.to).c_str());
        else if (p.b[best.to] != '.' && perdita >= 120)
          snprintf(e.perche, sizeof(e.perche), "Potevi prendere %s in %s", nomeLungo(p.b[best.to]), sqName(best.to).c_str());
        else if (q.b[risposta.to] != '.' && perdita >= 80)
          snprintf(e.perche, sizeof(e.perche), "Perdi %s in %s", nomeLungo(q.b[risposta.to]), sqName(risposta.to).c_str());
        else snprintf(e.perche, sizeof(e.perche), "Perdi circa %d.%d pedoni di vantaggio", perdita / 100, (perdita % 100) / 10);
        Serial.printf("Mossa %d%s %s: %s. Meglio %s\n", i / 2 + 1, i % 2 ? "..." : ".", mossaLeggibile(p, m).c_str(), e.perche, mossaLeggibile(p, best).c_str());
      }
      fatte++;
    }
    makeMove(p, m);
  }
  Serial.printf("Analisi finita: %d errori/imprecisioni\n", nErrori);
  if (ultimaPartitaLichess.length()) Serial.println("Analisi completa con Stockfish: https://lichess.org/" + ultimaPartitaLichess);
}

const char* gravita(int perdita) { return perdita >= 300 ? "GRAVE ERRORE" : perdita >= 120 ? "ERRORE" : "IMPRECISIONE"; }
uint16_t coloreGravita(int perdita) { return perdita >= 300 ? ILI9341_RED : perdita >= 120 ? ILI9341_ORANGE : ILI9341_YELLOW; }

void mostraAnalisi() {                                     // disegna la pagina aIdx
  hintUntil = 0; lastMove = {-1, -1, 0};
  if (aIdx >= 0) {                                        // scacchiera prima dell'errore
    startPos(pos); for (int i = 0; i < errori[aIdx].ply; i++) makeMove(pos, storia[i]);
    lastMove = errori[aIdx].giocata;                      // giallo = mossa giocata
    hint = errori[aIdx].migliore; hintUntil = millis() + 3600000UL;   // azzurro/viola = migliore
  }
  drawFrameAndCoords(); drawBoard();
  tft.fillRect(PX, 20, 92, 186, ILI9341_BLACK);
  tft.setTextSize(1);
  if (aIdx < 0) {
    int g = 0, e = 0, im = 0;
    for (int i = 0; i < nErrori; i++) { if (errori[i].perdita >= 300) g++; else if (errori[i].perdita >= 120) e++; else im++; }
    tft.setTextColor(ILI9341_YELLOW); tft.setTextSize(2); tft.setCursor(PX, 22); tft.print("ANALISI"); tft.setTextSize(1);
    tft.setTextColor(ILI9341_RED);    tft.setCursor(PX, 48); tft.printf("Gravi:     %d", g);
    tft.setTextColor(ILI9341_ORANGE); tft.setCursor(PX, 60); tft.printf("Errori:    %d", e);
    tft.setTextColor(ILI9341_YELLOW); tft.setCursor(PX, 72); tft.printf("Imprecis.: %d", im);
    printWrap(nErrori ? "Premi > per vedere gli errori" : "Nessun errore trovato. Bravo!", PX, 92, 15, ILI9341_WHITE);
    if (ultimaPartitaLichess.length()) printWrap("Analisi completa: lichess.org/" + ultimaPartitaLichess, PX, 130, 15, 0xAD55);
  } else {
    Errore& er = errori[aIdx];
    tft.setTextColor(0xAD55); tft.setCursor(PX, 22);
    tft.printf("%d/%d  mossa %d", aIdx + 1, nErrori, er.ply / 2 + 1);
    tft.setTextColor(coloreGravita(er.perdita)); tft.setCursor(PX, 36); tft.print(gravita(er.perdita));
    tft.setTextColor(ILI9341_YELLOW); tft.setCursor(PX, 52); tft.print("Hai: " + mossaLeggibile(pos, er.giocata));
    tft.setTextColor(ILI9341_CYAN);   tft.setCursor(PX, 64); tft.print("Meglio: " + mossaLeggibile(pos, er.migliore));
    printWrap(er.perche, PX, 82, 15, ILI9341_WHITE);
    tft.setTextColor(0x8410); tft.setCursor(PX, 128); tft.print("giallo = tua");
    tft.setCursor(PX, 140); tft.print("azzurro = meglio");
  }
  if (nErrori) { drawBtn(bPrec, 0x4208); drawBtn(bSucc, 0x4208); }
  drawBtn(bNuova, 0x3186);
}

// ============================ SENSORI ============================
bool mcpOk[4] = {false, false, false, false};
bool initMcp() {
  int ok = 0;
  for (int c = 0; c < 4; c++) {
    mcpOk[c] = mcp[c].begin_I2C(0x20 + c);
    if (mcpOk[c]) {
      ok++;
      for (int i = 0; i < 16; i++) mcp[c].pinMode(i, INPUT_PULLUP);
    } else Serial.printf("MCP 0x%02X non trovato\n", 0x20 + c);
  }
  return ok == 4;
}
uint64_t readRaw() {
  uint64_t o = 0;
  for (int c = 0; c < 4; c++) {
    if (!mcpOk[c]) continue;                          // modulo mancante: le sue caselle restano vuote
    uint16_t v = mcp[c].readGPIOAB();                 // bit a 0 = reed chiuso = pezzo presente
    for (int b = 0; b < 16; b++) if (!(v & (1 << b))) {
      int r = c * 2 + (REED_SCAMBIA_AB ? 1 - b / 8 : b / 8), f = b % 8;   // b 0-7 = PA, 8-15 = PB
      if (REED_INV_COL) f = 7 - f;
      if (REED_INV_RIGA) r = 7 - r;
      o |= BB(r * 8 + f);
    }
  }
  return o;
}
void readSensors() {
  static unsigned long t0 = 0;
  if (sim && stato != S_STATO) { uint64_t e = occOf(pos); if (e != occ) { occ = e; occChanged = true; } return; }   // nel CONTROLLO legge i moduli presenti
  if (millis() - t0 < 25) return;
  t0 = millis();
  uint64_t raw = readRaw();
  if (raw == rawPrev) { if (stableCnt < 10) stableCnt++; } else { stableCnt = 0; rawPrev = raw; }
  if (stableCnt >= 3 && raw != occ) {
    uint64_t d = raw ^ occ;                             // stampa i reed cambiati (utile per i cablaggi)
    for (int i = 0; i < 64; i++) if (d & BB(i)) Serial.printf("Reed %s: pezzo %s\n", sqName(i).c_str(), (raw & BB(i)) ? "messo" : "tolto");
    occ = raw; occChanged = true;
  }
}

// ========================= SCHERMATA CONTROLLO =========================
unsigned long statoDa = 0; bool statoAvvio = false;
void drawStatoFisso() {
  tft.fillScreen(ILI9341_BLACK);
  tft.setTextSize(2); tft.setTextColor(ILI9341_YELLOW); tft.setCursor(10, 6); tft.print("Controllo scacchiera");
  tft.setTextSize(1);
  for (int c = 0; c < 4; c++) {
    int y = 34 + c * 18;
    tft.setTextColor(ILI9341_WHITE); tft.setCursor(10, y);
    tft.printf("Modulo %d  0x%02X  traverse %d-%d", c + 1, 0x20 + c, c * 2 + 1, c * 2 + 2);
    tft.setCursor(230, y); tft.setTextColor(mcpOk[c] ? ILI9341_GREEN : ILI9341_RED);
    tft.print(mcpOk[c] ? "OK" : "NON TROVATO");
  }
  tft.setTextColor(0xAD55); tft.setCursor(10, 184);
  tft.print("LED: verdi = traverse lette, rosse = modulo mancante,");
  tft.setCursor(10, 196); tft.print("bianche = pezzo/magnete sentito dal reed");
  tft.setTextSize(2); tft.setCursor(10, 216);
  if (!sim) { tft.setTextColor(ILI9341_GREEN);  tft.print("Tutto OK  - tocca"); }
  else      { tft.setTextColor(ILI9341_ORANGE); tft.print("SIMULAZIONE - tocca"); }
  wifiLast = -2;
}
void drawStatoVariabile() {                               // reed e Wi-Fi, aggiornati ogni mezzo secondo
  tft.fillRect(0, 110, 320, 70, ILI9341_BLACK);
  tft.setTextSize(1);
  int n = 0; for (int i = 0; i < 64; i++) if (occ & BB(i)) n++;
  tft.setCursor(10, 112); tft.setTextColor(ILI9341_WHITE);
  tft.printf("Reed chiusi (pezzi/magneti sentiti): %d", n);
  if (n && n <= 6) {                                     // elenca le caselle, utile per provare i reed
    tft.setCursor(10, 126); tft.setTextColor(ILI9341_CYAN); tft.print("Caselle: ");
    for (int i = 0; i < 64; i++) if (occ & BB(i)) { tft.print(sqName(i)); tft.print(" "); }
  }
  bool w = WiFi.status() == WL_CONNECTED;
  tft.setCursor(10, 144); tft.setTextColor(w ? ILI9341_GREEN : ILI9341_ORANGE);
  if (w) tft.printf("Wi-Fi: connesso  %s  %d dBm", WiFi.localIP().toString().c_str(), WiFi.RSSI());
  else   tft.printf("Wi-Fi: non connesso (%s) - offline e 2 giocatori ok", cfgSsid.c_str());
  tft.setCursor(10, 160); tft.setTextColor(cfgToken.length() > 8 ? ILI9341_GREEN : ILI9341_ORANGE);
  tft.print(cfgToken.length() > 8 ? "Token Lichess: inserito" : "Token Lichess: manca (menu OPZIONI)");
}
void apriStato(bool avvio) {
  sim = !initMcp();                                      // riprova i moduli ogni volta
  occ = 0; rawPrev = 0; stableCnt = 0;
  stato = S_STATO; statoAvvio = avvio; statoDa = millis();
  drawStatoFisso(); drawStatoVariabile();
}

// ============================== LED ==============================
int ledIndex(int sq) {
  int f = sq % 8, r = sq / 8;
  if (LED_INV_COL) f = 7 - f;
  if (LED_INV_RIGA) r = 7 - r;
  if (SERPENTINA && (r & 1)) f = 7 - f;
  return r * 8 + f;
}
void updateLeds() {
  static uint32_t prev[64]; static bool first = true;
  uint32_t c[64] = {0};
  const uint32_t BLU = strip.Color(0, 0, 255), VERDE = strip.Color(0, 255, 0), ARANCIO = strip.Color(255, 90, 0),
                 CIANO = strip.Color(0, 200, 255), VIOLA = strip.Color(170, 0, 255), ROSSO = strip.Color(255, 0, 0),
                 BIANCO = strip.Color(255, 255, 255);
  uint64_t exp = occOf(pos);
  const uint32_t BIANCO_TENUE = strip.Color(150, 150, 150);   // bianco piu' basso: consuma meno
  bool blink = (millis() / 300) & 1;
  bool blinkV = (millis() / 120) & 1;                           // lampeggio veloce = errore                            // lampeggio ~1,7 volte al secondo
  static unsigned long fineDa = 0; static bool eraFine = false;     // quando e' finita la partita
  if (stato == S_FINE && !eraFine) fineDa = millis();
  eraFine = stato == S_FINE;
  bool avvio = stato == S_SETUP || (stato == S_GIOCA && millis() < avvioLedFino);   // posizione da disporre
  if (stato == S_STATO) {                                       // diagnosi: traverse per modulo + reed chiusi
    for (int i = 0; i < 64; i++) {
      int m = i / 16;
      c[i] = mcpOk[m] ? strip.Color(0, 60, 0) : (blink ? ROSSO : 0);
      if (occ & BB(i)) c[i] = BIANCO_TENUE;
    }
  } else if (avvio) {
    // traverse 1-2 verdi (Bianco), 3-6 bianche, 7-8 rosse (Nero), da a ad h
    Pos s = setupPos; uint64_t e = occOf(s);
    for (int i = 0; i < 64; i++) {
      int r = i / 8;
      uint32_t zona = r <= 1 ? VERDE : r >= 6 ? ROSSO : BIANCO_TENUE;
      if (modo == M_PROBLEMI) zona = s.b[i] == '.' ? 0 : isW(s.b[i]) ? VERDE : ROSSO;   // problema: verde = pezzo bianco, rosso = nero
      c[i] = zona;
      if (stato != S_SETUP || sim) continue;
      if ((e & BB(i)) && !(occ & BB(i)) && blink) c[i] = 0;          // manca il pezzo: lampeggia
      if (!(e & BB(i)) && (occ & BB(i))) c[i] = blink ? ARANCIO : 0; // pezzo di troppo: arancione lampeggiante
    }
  } else if (stato == S_GIOCA || stato == S_PROMO) {
    Move ml[MAXM]; int n = genLegal(pos, ml);
    int lifted = -1, nl = 0;
    for (int i = 0; i < 64; i++) if ((exp & BB(i)) && !(occ & BB(i)) && own(pos.b[i], pos.white)) { lifted = i; nl++; }
    if (sim) { lifted = selSq; nl = selSq >= 0 ? 1 : 0; }
    if (nl == 1) {
      c[lifted] = BLU;
      for (int i = 0; i < n; i++) if (ml[i].from == lifted) c[ml[i].to] = pos.b[ml[i].to] != '.' ? ARANCIO : VERDE;
    }
    if (!sim) {
      bool sbagliato = false;
      for (int i = 0; i < 64; i++) {
        // MOSSA NON VALIDA: pezzo appoggiato su una casella non consentita -> ROSSO lampeggiante veloce
        if ((occ & BB(i)) && !(exp & BB(i)) && c[i] != VERDE) { c[i] = blinkV ? ROSSO : 0; sbagliato = true; }
        if (!(occ & BB(i)) && (exp & BB(i)) && !own(pos.b[i], pos.white)) c[i] = ARANCIO;   // pezzo da catturare tolto
      }
      if (sbagliato && nl == 1) c[lifted] = blinkV ? BLU : 0;          // ...e la casella da cui riprenderlo lampeggia blu
    }
    if (millis() < hintUntil && hint.from >= 0) { c[hint.from] = CIANO; c[hint.to] = VIOLA; }
  } else if (stato == S_SYNC) {
    uint64_t prima = occOf(posPrima);
    uint64_t daTogliere = prima & ~exp, daRiempire = exp & ~prima;
    daRiempire |= BB(syncMove.to);
    for (int i = 0; i < 64; i++) {
      if ((occ & BB(i)) && !(exp & BB(i)))                    // casella di partenza (ciano) o pezzo messo male (rosso)
        c[i] = (daTogliere & BB(i)) ? CIANO : (blinkV ? ROSSO : 0);
      if (!(occ & BB(i)) && (exp & BB(i)))                    // arrivo (viola) o pezzo tolto per sbaglio (arancione)
        c[i] = (daRiempire & BB(i)) ? VIOLA : (blinkV ? ARANCIO : 0);
    }
    bool arrivoOk = (occ & BB(syncMove.to)) && (!syncCapture || (seenEmpty & BB(syncMove.to)));
    if (!arrivoOk) c[syncMove.to] = VIOLA;                    // per le catture: togli il pezzo e metti il nuovo
  } else if (stato == S_ANALISI) {                              // errore: giallo = giocata, azzurro/viola = migliore
    if (aIdx >= 0) {
      c[errori[aIdx].giocata.from] = c[errori[aIdx].giocata.to] = strip.Color(255, 200, 0);
      c[errori[aIdx].migliore.from] = CIANO; c[errori[aIdx].migliore.to] = VIOLA;
    }
  } else if (stato == S_FINE) {
    // fine partita: tutta la scacchiera VERDE (vince il Bianco), ROSSA (vince il Nero) o BIANCA (patta)
    // per FINE_LED_MS, poi torna ai colori normali: traverse 1-2 verdi, 3-6 bianche, 7-8 rosse
    uint32_t col = (risultato.startsWith("Vince il Bianco") || risultato.startsWith("Risolto")) ? VERDE : risultato.startsWith("Vince il Nero") ? ROSSO : BIANCO_TENUE;
    bool festa = millis() - fineDa < FINE_LED_MS;
    for (int i = 0; i < 64; i++) {
      int r = i / 8;
      c[i] = festa ? col : (r <= 1 ? VERDE : r >= 6 ? ROSSO : BIANCO_TENUE);
    }
  }
  if (millis() < flashFino && flashSq >= 0) c[flashSq] = blink ? flashCol : 0;   // conferma mossa
  bool diff = first;
  for (int i = 0; i < 64; i++) if (c[i] != prev[i]) diff = true;
  if (!diff) return;
  for (int i = 0; i < 64; i++) { strip.setPixelColor(ledIndex(i), c[i]); prev[i] = c[i]; }
  strip.show();
  first = false;
}

// ============================ LICHESS ============================
WiFiClientSecure streamCli;
String gameId = "";
bool   streamOn = false;
String lineBuf = "";
unsigned long lastStreamTry = 0;

bool lichessPost(const String& path, const String& body, String* resp) {
  if (WiFi.status() != WL_CONNECTED) return false;
  WiFiClientSecure cli; cli.setInsecure();
  HTTPClient http;
  if (!http.begin(cli, "https://lichess.org" + path)) return false;
  http.addHeader("Authorization", String("Bearer ") + cfgToken);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  int code = http.POST(body);
  if (resp) *resp = http.getString();
  http.end();
  Serial.printf("POST %s -> %d\n", path.c_str(), code);
  return code == 200 || code == 201;
}
bool lichessNuovaPartita() {
  String r;
  if (!lichessPost("/api/challenge/ai", "level=" + String(livello) + "&color=white", &r)) return false;
  JsonDocument d;
  if (deserializeJson(d, r)) return false;
  gameId = d["id"] | "";
  ultimaPartitaLichess = gameId;                          // per il link all'analisi su lichess.org
  return gameId.length() > 0;
}
void lichessStreamOpen() {
  streamCli.stop();
  streamCli.setInsecure();
  lastStreamTry = millis();
  if (!streamCli.connect("lichess.org", 443)) { streamOn = false; return; }
  streamCli.print("GET /api/board/game/stream/" + gameId + " HTTP/1.0\r\nHost: lichess.org\r\n"
                  "Authorization: Bearer " + cfgToken + "\r\nAccept: application/x-ndjson\r\n\r\n");
  unsigned long t = millis();                           // salta le intestazioni HTTP
  String h;
  while (millis() - t < 5000) {
    if (streamCli.available()) { char ch = streamCli.read(); h += ch; if (h.endsWith("\r\n\r\n")) break; }
    else delay(5);
  }
  streamOn = h.indexOf(" 200") > 0;
  lineBuf = "";
  Serial.println(streamOn ? "Stream Lichess aperto" : "Stream Lichess: errore");
}
void lichessChiudi(bool abbandona) {
  if (abbandona && gameId.length()) lichessPost("/api/board/game/" + gameId + "/resign", "", nullptr);
  streamCli.stop(); streamOn = false; gameId = "";
}
void applicaMossaAvversario(const Move& m);
void fineSePartitaFinita();

void processaRiga(const String& line) {
  JsonDocument filter;
  filter["type"] = true; filter["moves"] = true; filter["status"] = true; filter["winner"] = true;
  filter["state"]["moves"] = true; filter["state"]["status"] = true; filter["state"]["winner"] = true;
  JsonDocument d;
  if (deserializeJson(d, line, DeserializationOption::Filter(filter))) return;
  JsonVariant st = d["type"] == "gameFull" ? d["state"].as<JsonVariant>() : d.as<JsonVariant>();
  if (st.isNull() || st["moves"].isNull()) return;
  String moves = st["moves"] | "";
  String status = st["status"] | "started";
  // conta e applica le mosse nuove (quelle dell'avversario)
  int idx = 0, start = 0;
  moves.trim();
  while (moves.length() && start <= (int)moves.length()) {
    int sp = moves.indexOf(' ', start); if (sp < 0) sp = moves.length();
    String u = moves.substring(start, sp);
    if (idx >= plyCount && u.length() >= 4) {
      Move m;
      if (parseUci(pos, u, m)) applicaMossaAvversario(m);
      else { setMsg("Mossa Lichess non valida: " + u); break; }
    }
    idx++; start = sp + 1;
    if (sp >= (int)moves.length()) break;
  }
  if (status != "started" && status != "created" && stato != S_FINE) {
    String w = st["winner"] | "";
    risultato = w == "white" ? "Vince il Bianco (" + status + ")" : w == "black" ? "Vince il Nero (" + status + ")" : "Patta (" + status + ")";
    stato = S_FINE; drawPanel();
  }
}
void lichessLoop() {
  if (modo != M_LICHESS || gameId.length() == 0 || stato == S_FINE) return;
  if (!streamOn || !streamCli.connected()) {
    streamOn = false;
    if (millis() - lastStreamTry > 5000 && WiFi.status() == WL_CONNECTED) lichessStreamOpen();
    return;
  }
  while (streamCli.available()) {
    char ch = streamCli.read();
    if (ch == '\n') { lineBuf.trim(); if (lineBuf.length()) processaRiga(lineBuf); lineBuf = ""; }
    else if (lineBuf.length() < 6000) lineBuf += ch;
  }
}

// ========================= LOGICA DI GIOCO =========================
bool giocatoreDiTurno() { return modo == M_2P || (modo == M_PROBLEMI ? pos.white == probBianco : pos.white); }   // il giocatore ha i Bianchi (nei problemi: chi muove)

void fineSePartitaFinita() {
  Move ml[MAXM];
  if (genLegal(pos, ml) > 0) {                    // ci sono mosse: controlla le patte automatiche
    const char* perche = pattaAutomatica();
    if (!perche) return;
    risultato = String("Patta (") + perche + ")";
    stato = S_FINE; drawPanel(); Serial.println(risultato); return;
  }
  if (inCheck(pos, pos.white)) risultato = pos.white ? "Vince il Nero (scacco matto)" : "Vince il Bianco (scacco matto)";
  else risultato = "Patta (stallo)";
  stato = S_FINE; drawPanel();
}

String probMossa(int k) {                         // k-esima mossa della soluzione ("" = finita)
  const char* s = probOnline ? probSolOnline.c_str() : PROBLEMI[probIdx].sol; String t = ""; int n = 0;
  for (const char* c = s; ; c++) {
    if (*c == ' ' || *c == 0) {
      if (t.length()) { if (n == k) return t; n++; t = ""; }
      if (*c == 0) break;
    } else t += *c;
  }
  return "";
}
void mossaProblema(const Move& m) {               // controlla la mossa con la soluzione
  Pos dopo = pos; makeMove(dopo, m);
  Move tmp[MAXM];
  bool matto = genLegal(dopo, tmp) == 0 && inCheck(dopo, dopo.white);
  if (uci(m) != probMossa(probPly) && !matto) {   // SBAGLIATA: si rimette il pezzo e si riprova
    selSq = -1; hintUntil = 0;
    lampeggia(m.to, strip.Color(255, 0, 0), 2400);
    if (sim) { drawBoard(); setMsg("Sbagliato, riprova"); return; }
    posPrima = dopo; syncMove = {m.to, m.from, 0}; syncCapture = false; seenEmpty = 0;
    stato = S_SYNC; drawBoard();
    setMsg(pos.b[m.to] != '.' ? "Sbagliato! Rimetti i pezzi come prima e riprova" : "Sbagliato! Rimetti il pezzo e riprova");
    return;
  }
  makeMove(pos, m); plyCount++; lastMove = m; selSq = -1; hintUntil = 0; seenEmpty = 0; probPly++;
  drawBoard(); lampeggia(m.to, strip.Color(0, 255, 0));
  String risp = probMossa(probPly);
  if (matto || risp.length() == 0) {              // RISOLTO
    risultato = "Risolto! Tocca AVANTI per il prossimo problema";
    stato = S_FINE; drawPanel(); Serial.println(risultato); return;
  }
  Move r;                                          // risposta dell'avversario: va rifatta a mano
  if (parseUci(pos, risp, r)) { probPly++; applicaMossaAvversario(r); }
  else { risultato = "Errore nella soluzione del problema"; stato = S_FINE; drawPanel(); }
}

void eseguiMossa(const Move& m) {                 // mossa del giocatore confermata
  if (modo == M_PROBLEMI) { mossaProblema(m); return; }
  if (nStoria < 600) storia[nStoria++] = m;
  makeMove(pos, m); plyCount++; lastMove = m; selSq = -1; hintUntil = 0;
  seenEmpty = 0;
  drawBoard();
  Serial.println("Mossa: " + uci(m));
  if (modo == M_LICHESS) {
    stato = S_ATTESA; setMsg("Invio " + uci(m) + "...");
    if (!lichessPost("/api/board/game/" + gameId + "/move/" + uci(m), "", nullptr)) {
      setMsg("Errore invio mossa a Lichess"); lampeggia(m.to, strip.Color(255, 0, 0), 2400);
    } else { setMsg("Lichess pensa..."); lampeggia(m.to, strip.Color(0, 255, 0)); }
  } else if (modo == M_OFFLINE) {
    stato = S_ATTESA; setMsg("ESP32 pensa..."); lampeggia(m.to, strip.Color(0, 255, 0));
  } else {
    stato = S_GIOCA; setMsg(""); lampeggia(m.to, strip.Color(0, 255, 0));
  }
  fineSePartitaFinita();
}

void applicaMossaAvversario(const Move& m) {     // mossa di Lichess / ESP32: va rifatta a mano
  syncCapture = pos.b[m.to] != '.' || (up(pos.b[m.from]) == 'P' && m.to == pos.ep);
  if (nStoria < 600) storia[nStoria++] = m;
  posPrima = pos;
  makeMove(pos, m); plyCount++; lastMove = m; syncMove = m; seenEmpty = 0;
  drawBoard();
  stato = sim ? S_GIOCA : S_SYNC;
  setMsg(sim ? "Avversario: " + uci(m) : "Muovi " + uci(m) + " sulla scacchiera");
  fineSePartitaFinita();
}

void controllaMossaFisica() {                    // confronta i reed con le mosse legali
  uint64_t exp = occOf(pos);
  seenEmpty |= exp & ~occ;
  Move ml[MAXM]; int n = genLegal(pos, ml);
  nPromo = 0; Move found; bool ok = false;
  for (int i = 0; i < n; i++) {
    Pos q = pos; makeMove(q, ml[i]);
    if (occOf(q) != occ) continue;
    bool cattura = pos.b[ml[i].to] != '.';
    if (cattura && !(seenEmpty & BB(ml[i].to))) continue;          // il pezzo catturato deve essere stato tolto
    if (ml[i].promo) { if (nPromo < 4) promoCand[nPromo++] = ml[i]; }
    else { found = ml[i]; ok = true; }
  }
  if (nPromo > 0) { stato = S_PROMO; drawPanel(); setMsg("Promozione: scegli"); return; }
  if (ok) eseguiMossa(found);
}

void controllaSync() {
  uint64_t exp = occOf(pos);
  seenEmpty |= ~occ;
  if (occ == exp && (!syncCapture || (seenEmpty & BB(syncMove.to)))) {
    stato = S_GIOCA; seenEmpty = 0;                          // azzera: altrimenti sollevare un pezzo che attacca la
    setMsg("Tocca a te");                                    // casella appena occupata veniva preso per una cattura
    lampeggia(syncMove.to, strip.Color(0, 255, 0));          // mossa dell'avversario eseguita bene
  }
}

void annullaMossa() {                            // torna indietro e guida a rimettere i pezzi
  int k = modo == M_OFFLINE ? 2 : 1;
  Move tua = storia[nStoria - k];                 // la mossa da riprendere
  Pos prima = pos;                                // posizione attuale
  nStoria -= k; plyCount = max(0, plyCount - k);
  Pos p; startPos(p);
  for (int i = 0; i < nStoria; i++) makeMove(p, storia[i]);
  pos = p;
  lastMove = nStoria ? storia[nStoria - 1] : Move{-1, -1, 0};
  selSq = -1; hintUntil = 0; seenEmpty = 0;
  String rim = "";                                // pezzi catturati da rimettere
  for (int i = 0; i < 64; i++)
    if (pos.b[i] != '.' && prima.b[i] != '.' && pos.b[i] != prima.b[i]) {
      if (rim.length()) rim += ", ";
      rim += String(nomeLungo(pos.b[i])) + (isW(pos.b[i]) ? " bianco" : " nero") + " in " + sqName(i);
    }
  Serial.println("Mossa annullata: " + uci(tua));
  if (sim) { stato = S_GIOCA; drawBoard(); setMsg("Mossa annullata"); return; }
  posPrima = prima; syncMove = {tua.to, tua.from, 0}; syncCapture = false;
  stato = S_SYNC; drawBoard();
  setMsg(rim.length() ? "Annullata: rimetti " + rim : String("Annullata: rimetti i pezzi come sul display"));
  controllaSync();                                // se e' gia' tutto a posto riparte subito
}

void nuovaPartita() {
  startPos(setupPos);
  startPos(pos); plyCount = 0; lastMove = {-1, -1, 0}; selSq = -1; hintUntil = 0; seenEmpty = 0;
  nStoria = 0; nErrori = 0; ultimaPartitaLichess = "";
  risultato = ""; messaggio = "";
  stato = sim ? S_GIOCA : S_SETUP;
  drawGameScreen();
  if (stato == S_SETUP) setMsg("Disponi i pezzi nella posizione iniziale");
  else iniziaGioco();
}
void iniziaGioco() {
  stato = S_GIOCA;
  avvioLedFino = millis() + 2000;                         // traverse colorate ancora 2 s, poi si spengono
  if (modo == M_LICHESS) {
    setMsg("Creo la partita su Lichess...");
    if (lichessNuovaPartita()) { lichessStreamOpen(); setMsg("Partita Lichess pronta. Tocca a te"); }
    else { modo = M_OFFLINE; livello = 3; drawPanel(); setMsg("Lichess non disponibile: gioco offline"); }
  } else setMsg("Tocca a te");
}
void iniziaProblema() {
  stato = S_GIOCA; avvioLedFino = 0;
  if (probOnline) setMsg(String(probBianco ? "Muove il Bianco" : "Muove il Nero") + ": problema Lichess " + String(probRating));
  else setMsg(String(probBianco ? "Muove il Bianco" : "Muove il Nero") + ": matto in " + String(PROBLEMI[probIdx].liv));
}
void avviaProblema() {                            // la posizione del problema e' in setupPos
  pos = setupPos; probBianco = pos.white; probPly = 0;
  plyCount = 0; lastMove = {-1, -1, 0}; selSq = -1; hintUntil = 0; seenEmpty = 0;
  nStoria = 0; nErrori = 0; risultato = ""; messaggio = "";
  stato = sim ? S_GIOCA : S_SETUP;
  drawGameScreen();
  if (stato == S_SETUP) setMsg("Disponi i pezzi come sul display");
  else iniziaProblema();
}
void nuovoProblema() {                            // prossimo problema del livello scelto
  static int lista[N_PROBLEMI]; int nl = 0, tot = 0;   // problemi del livello non ancora usciti
  for (int i = 0; i < N_PROBLEMI; i++) if (PROBLEMI[i].liv == probLiv) {
    tot++;
    if (!probVisto[i]) lista[nl++] = i;
  }
  if (tot == 0) { stato = S_MENU; drawMenu(); return; }
  if (nl == 0) {                                  // visti tutti: si ricomincia (senza ripetere l'ultimo)
    for (int i = 0; i < N_PROBLEMI; i++) if (PROBLEMI[i].liv == probLiv) probVisto[i] = false;
    for (int i = 0; i < N_PROBLEMI; i++)
      if (PROBLEMI[i].liv == probLiv && (i != probIdx || tot == 1)) lista[nl++] = i;
  }
  int k = lista[random(0, nl)];
  probVisto[k] = true;
  probIdx = k; probOnline = false;
  parseFen(PROBLEMI[k].fen, setupPos);
  avviaProblema();
}

// ---------------------- PROBLEMI DA LICHESS ----------------------
// legge una mossa in notazione SAN (es. "Nf3", "exd5", "O-O", "e8=Q+") tra le mosse legali
bool parseSan(const Pos& p, String t, Move& out) {
  while (t.length() && strchr("+#!?", t[t.length() - 1])) t.remove(t.length() - 1);
  if (t.length() < 2) return false;
  Move ml[MAXM]; int n = genLegal(p, ml);
  if (t == "O-O" || t == "0-0" || t == "O-O-O" || t == "0-0-0") {          // arrocco
    int k = p.white ? 4 : 60, to = k + (t.length() == 5 ? -2 : 2);
    for (int i = 0; i < n; i++) if (ml[i].from == k && ml[i].to == to && up(p.b[k]) == 'K') { out = ml[i]; return true; }
    return false;
  }
  char promo = 0;
  int eq = t.indexOf('=');
  if (eq >= 0) { if (eq + 1 < (int)t.length()) promo = t[eq + 1] + 32; t = t.substring(0, eq); }
  char pez = (t[0] >= 'A' && t[0] <= 'Z') ? t[0] : 'P';
  String c = "";
  for (int i = (pez == 'P' ? 0 : 1); i < (int)t.length(); i++) if (t[i] != 'x') c += t[i];
  if (c.length() < 2) return false;
  char cf = c[c.length() - 2], cr = c[c.length() - 1];
  if (cf < 'a' || cf > 'h' || cr < '1' || cr > '8') return false;
  int to = (cr - '1') * 8 + (cf - 'a'), df = -1, dr = -1;
  for (int i = 0; i + 2 < (int)c.length(); i++) {                         // disambiguazione (colonna/traversa)
    if (c[i] >= 'a' && c[i] <= 'h') df = c[i] - 'a';
    else if (c[i] >= '1' && c[i] <= '8') dr = c[i] - '1';
  }
  for (int i = 0; i < n; i++) {
    const Move& m = ml[i];
    if (m.to != to || up(p.b[m.from]) != pez || m.promo != promo) continue;
    if ((df >= 0 && m.from % 8 != df) || (dr >= 0 && m.from / 8 != dr)) continue;
    out = m; return true;
  }
  return false;
}
bool lichessGet(const String& path, String& resp) {
  if (WiFi.status() != WL_CONNECTED) return false;
  WiFiClientSecure cli; cli.setInsecure();
  HTTPClient http;
  if (!http.begin(cli, "https://lichess.org" + path)) return false;
  http.addHeader("Accept", "application/json");
  http.setTimeout(10000);
  int code = http.GET();
  if (code == 200) resp = http.getString();
  http.end();
  Serial.printf("GET %s -> %d\n", path.c_str(), code);
  return code == 200;
}
// scarica un problema: rigioca la partita da cui e' preso e prepara la soluzione
bool caricaProblemaLichess() {
  String path = "/api/puzzle/next", par = "";
  if (strlen(LICHESS_PROB_TEMA)) par += String("angle=") + LICHESS_PROB_TEMA;
  if (strlen(LICHESS_PROB_DIFF)) par += String(par.length() ? "&" : "") + "difficulty=" + LICHESS_PROB_DIFF;
  if (par.length()) path += "?" + par;
  String r;
  if (!lichessGet(path, r) && !lichessGet("/api/puzzle/daily", r)) return false;   // ripiego: problema del giorno
  JsonDocument filter;
  filter["game"]["pgn"] = true; filter["puzzle"]["solution"] = true; filter["puzzle"]["rating"] = true;
  JsonDocument d;
  if (deserializeJson(d, r, DeserializationOption::Filter(filter))) return false;
  String pgn = d["game"]["pgn"] | "";
  JsonArray sol = d["puzzle"]["solution"].as<JsonArray>();
  if (!pgn.length() || sol.isNull() || sol.size() == 0) return false;
  Pos p; startPos(p);
  int i = 0;
  while (i < (int)pgn.length()) {                 // tutte le mosse della partita fino al problema
    int sp = pgn.indexOf(' ', i); if (sp < 0) sp = pgn.length();
    String t = pgn.substring(i, sp); i = sp + 1;
    if (!t.length() || t.indexOf('.') >= 0 || t == "1-0" || t == "0-1" || t == "1/2-1/2" || t == "*") continue;
    Move m;
    if (!parseSan(p, t, m)) { Serial.println("Mossa della partita non capita: " + t); return false; }
    makeMove(p, m);
  }
  String s = ""; Pos q = p;
  for (JsonVariant v : sol) {                     // soluzione (UCI): controlla che sia valida
    const char* u = v | "";
    Move m;
    if (!parseUci(q, String(u), m)) { Serial.println("Soluzione non valida"); return false; }
    makeMove(q, m);
    if (s.length()) s += " ";
    s += u;
  }
  probSolOnline = s; probRating = d["puzzle"]["rating"] | 0;
  setupPos = p;
  Serial.println("Problema Lichess " + String(probRating) + ": " + s);
  return true;
}
void problemaLichess() {
  if (stato == S_LIVELLO) {
    tft.fillRect(0, 130, 320, 110, ILI9341_BLACK);
    tft.setTextSize(2); tft.setTextColor(ILI9341_YELLOW); tft.setCursor(20, 170); tft.print("Scarico da Lichess...");
  } else setMsg("Scarico da Lichess...");
  if (caricaProblemaLichess()) { probOnline = true; avviaProblema(); return; }
  stato = S_LIVELLO; drawLivelli();
  tft.setTextSize(1); tft.setTextColor(ILI9341_RED); tft.setCursor(20, 126);
  tft.print("Lichess non disponibile: controlla il Wi-Fi");
}


// ===================== OPZIONI: rete Wi-Fi e token Lichess =====================
// Rete, password e token si scelgono dalla scacchiera (tastiera sul display) oppure dal telefono
// (la scacchiera crea la rete AP_NOME con una pagina dove scegliere la rete e incollare il token).
// Vengono salvati nella memoria dell'ESP32 e restano anche da spenta.
Btn bOpRete  = {20, 40, 280, 32, "Scegli rete Wi-Fi"};
Btn bOpToken = {20, 78, 280, 32, "Token Lichess"};
Btn bOpTel   = {20, 116, 280, 32, "Configura col telefono"};
Btn bOpCanc  = {20, 154, 280, 32, "Cancella token"};
Btn bOpInd   = {20, 200, 280, 32, "Indietro"};
Btn bRtPrec  = {4, 208, 40, 30, "<"};
Btn bRtSucc  = {48, 208, 40, 30, ">"};
Btn bRtAgg   = {92, 208, 108, 30, "Aggiorna"};
Btn bRtInd   = {204, 208, 112, 30, "Indietro"};
Btn bTkMaiu  = {2, 196, 58, 40, "Aa"};
Btn bTkSimb  = {62, 196, 58, 40, "#+="};
Btn bTkSpaz  = {122, 196, 76, 40, "spazio"};
Btn bTkCanc  = {200, 196, 56, 40, "<-"};
Btn bTkOk    = {258, 196, 60, 40, "OK"};
Btn bTkEsc   = {266, 20, 52, 30, "Esc"};
Btn bPortEsc = {20, 200, 280, 32, "Annulla"};

const int RETI_PAG = 6;                          // reti per pagina
int  nReti = 0, retiPag = 0, retiOrd[30];
bool retiDaAvvio = false;                        // aperta all'accensione: "Indietro" porta al menu
String reteScelta = "";
Destino tastDest = T_PASS;
String tastTesto = "", tastTitolo = "";
int  tastModo = 0;                               // 0 minuscole, 1 maiuscole, 2 simboli
const char* TASTI[3][4] = {
  {"1234567890", "qwertyuiop", "asdfghjkl-", "zxcvbnm_.@"},
  {"1234567890", "QWERTYUIOP", "ASDFGHJKL-", "ZXCVBNM_.@"},
  {"1234567890", "!#$%&*+=?/", "()[]{}<>:;", ",'\"~^|\\`.@"}};
const int TK_Y0 = 56;                            // prima riga di tasti (righe ogni 34 px, tasti 30x32)

void scritta(const String& s, uint16_t col) {    // messaggio a tutto schermo
  tft.fillScreen(ILI9341_BLACK);
  tft.setTextSize(2); tft.setTextColor(col);
  int y = 90, i = 0;
  while (i < (int)s.length() && y < 200) {       // a capo ogni 25 caratteri circa
    int len = min(25, (int)s.length() - i);
    if (i + len < (int)s.length()) { int sp = s.lastIndexOf(' ', i + len); if (sp > i) len = sp - i; }
    tft.setCursor(10, y); tft.print(s.substring(i, i + len));
    i += len; while (i < (int)s.length() && s[i] == ' ') i++; y += 22;
  }
}

// ---------- schermata OPZIONI ----------
void apriOpzioni() {
  stato = S_IMPOST;
  tft.fillScreen(ILI9341_BLACK);
  tft.setTextSize(2); tft.setTextColor(ILI9341_YELLOW); tft.setCursor(10, 4); tft.print("Opzioni");
  tft.setTextSize(1); tft.setCursor(10, 26);
  bool w = WiFi.status() == WL_CONNECTED;
  tft.setTextColor(w ? ILI9341_GREEN : ILI9341_ORANGE);
  tft.print(cfgSsid.length() ? cfgSsid.substring(0, 22) : String("nessuna rete"));
  tft.print(w ? " (collegata)" : " (non collegata)");
  tft.setCursor(220, 26); tft.setTextColor(cfgToken.length() > 8 ? ILI9341_GREEN : ILI9341_ORANGE);
  tft.print(cfgToken.length() > 8 ? "token: si'" : "token: no");
  drawBtn(bOpRete, 0x0019); drawBtn(bOpToken, 0x0019); drawBtn(bOpTel, 0x0320);
  drawBtn(bOpCanc, 0x8200); drawBtn(bOpInd, 0x4208);
  wifiLast = -2;
}

// ---------- elenco reti ----------
void drawReti() {
  tft.fillScreen(ILI9341_BLACK);
  tft.setTextSize(2); tft.setTextColor(ILI9341_YELLOW); tft.setCursor(6, 4);
  tft.printf("Scegli la rete (%d)", nReti);
  if (nReti == 0) { tft.setTextColor(ILI9341_WHITE); tft.setCursor(6, 100); tft.print("Nessuna rete trovata"); }
  for (int k = 0; k < RETI_PAG; k++) {
    int j = retiPag * RETI_PAG + k; if (j >= nReti) break;
    int i = retiOrd[j], y = 28 + k * 30;
    tft.fillRoundRect(6, y, 308, 28, 5, 0x18C3);
    tft.setTextSize(2); tft.setTextColor(ILI9341_WHITE); tft.setCursor(12, y + 6);
    tft.print(WiFi.SSID(i).substring(0, 18));
    int rssi = WiFi.RSSI(i), t = rssi > -55 ? 4 : rssi > -65 ? 3 : rssi > -75 ? 2 : 1;
    for (int b = 0; b < 4; b++) tft.fillRect(272 + b * 7, y + 22 - (4 + b * 4), 5, 4 + b * 4, b < t ? ILI9341_GREEN : 0x4208);
    if (WiFi.encryptionType(i) != WIFI_AUTH_OPEN) { tft.setTextSize(1); tft.setTextColor(ILI9341_YELLOW); tft.setCursor(256, y + 10); tft.print("*"); }
  }
  if (nReti > RETI_PAG) { drawBtn(bRtPrec, 0x4208); drawBtn(bRtSucc, 0x4208); }
  drawBtn(bRtAgg, 0x0019); drawBtn(bRtInd, 0x4208);
  wifiLast = -2;
}
void apriReti(bool daAvvio) {
  retiDaAvvio = daAvvio; stato = S_RETI; retiPag = 0; nReti = 0;
  scritta("Cerco le reti Wi-Fi...", ILI9341_YELLOW);
  WiFi.disconnect(); delay(100);                  // la ricerca va meglio se non sta provando a collegarsi
  int n = WiFi.scanNetworks();
  int tutte[60], nt = 0;
  for (int i = 0; i < n && nt < 60; i++) if (WiFi.SSID(i).length()) tutte[nt++] = i;
  for (int a = 1; a < nt; a++) {                  // ordina per segnale
    int t = tutte[a], b = a;
    while (b > 0 && WiFi.RSSI(tutte[b - 1]) < WiFi.RSSI(t)) { tutte[b] = tutte[b - 1]; b--; }
    tutte[b] = t;
  }
  for (int a = 0; a < nt && nReti < 30; a++) {    // stessa rete con piu' antenne: una sola volta
    bool dop = false;
    for (int k = 0; k < nReti; k++) if (WiFi.SSID(retiOrd[k]) == WiFi.SSID(tutte[a])) dop = true;
    if (!dop) retiOrd[nReti++] = tutte[a];
  }
  Serial.printf("Reti trovate: %d\n", nReti);
  drawReti();
}
void esciReti() {                                 // torna indietro senza cambiare rete
  connettiWifi();
  if (retiDaAvvio) { stato = S_MENU; drawMenu(); } else apriOpzioni();
}
// prova la rete: se si collega la salva, altrimenti torna a quella di prima
bool provaRete(const String& ssid, const String& pass) {
  scritta("Mi collego a " + ssid + "...", ILI9341_YELLOW);
  WiFi.disconnect(); delay(100);
  WiFi.begin(ssid.c_str(), pass.c_str());
  unsigned long t = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t < 12000) delay(200);
  bool ok = WiFi.status() == WL_CONNECTED;
  if (ok) { cfgSsid = ssid; cfgPass = pass; salvaConfig(); }
  else connettiWifi();
  scritta(ok ? "Collegato! Rete salvata." : "Non riesco a collegarmi: password sbagliata o rete lontana.",
          ok ? ILI9341_GREEN : ILI9341_RED);
  Serial.println(ok ? "Rete salvata: " + ssid : "Collegamento fallito: " + ssid);
  delay(1800);
  return ok;
}

// ---------- tastiera sul display ----------
void drawTastCampo() {
  tft.fillRect(2, 20, 262, 30, ILI9341_BLACK); tft.drawRect(2, 20, 262, 30, ILI9341_WHITE);
  tft.setTextSize(2); tft.setTextColor(ILI9341_WHITE); tft.setCursor(6, 28);
  int da = max(0, (int)tastTesto.length() - 20);  // mostra la parte finale
  tft.print(tastTesto.substring(da)); tft.print("_");
}
void drawTastiera() {
  tft.fillScreen(ILI9341_BLACK);
  tft.setTextSize(1); tft.setTextColor(ILI9341_YELLOW); tft.setCursor(4, 6); tft.print(tastTitolo.substring(0, 46));
  drawTastCampo(); drawBtn(bTkEsc, 0x8000);
  char lbl[2] = {0, 0};
  for (int r = 0; r < 4; r++)
    for (int c = 0; c < 10; c++) {
      lbl[0] = TASTI[tastModo][r][c];
      Btn b = {2 + c * 32, TK_Y0 + r * 34, 30, 32, lbl};
      drawBtn(b, 0x2124);
    }
  drawBtn(bTkMaiu, tastModo == 1 ? 0x0320 : 0x4208); drawBtn(bTkSimb, tastModo == 2 ? 0x0320 : 0x4208);
  drawBtn(bTkSpaz, 0x4208); drawBtn(bTkCanc, 0x8200); drawBtn(bTkOk, 0x0320);
  wifiLast = -2;
}
void apriTastiera(Destino d, const String& titolo) {
  tastDest = d; tastTitolo = titolo; tastTesto = ""; tastModo = 0;
  stato = S_TASTIERA; drawTastiera();
}
void tastieraFinita() {
  if (tastDest == T_PASS) {
    if (provaRete(reteScelta, tastTesto)) { if (retiDaAvvio) { stato = S_MENU; drawMenu(); } else apriOpzioni(); }
    else apriReti(retiDaAvvio);
  } else {
    tastTesto.trim();
    if (tastTesto.length()) { cfgToken = tastTesto; salvaConfig(); scritta("Token salvato.", ILI9341_GREEN); delay(1200); }
    apriOpzioni();
  }
}

// ---------- configurazione col telefono (rete temporanea + pagina web) ----------
WebServer server(80);
DNSServer dns;
String opzioniReti = "";                          // <option> della pagina
String nuovoSsid = "", nuovoPass = "", nuovoToken = ""; bool cancellaToken = false;
unsigned long portaleChiudiDa = 0;
String html(const String& s) {                    // caratteri speciali per la pagina
  String r = "";
  for (int i = 0; i < (int)s.length(); i++) {
    char c = s[i];
    if (c == '&') r += "&amp;"; else if (c == '<') r += "&lt;"; else if (c == '>') r += "&gt;";
    else if (c == '"') r += "&quot;"; else r += c;
  }
  return r;
}
String paginaPortale() {
  String p = "<!DOCTYPE html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
             "<title>Scacchiera</title><style>body{font-family:sans-serif;max-width:480px;margin:auto;padding:12px;background:#f4f4f4}"
             "label{display:block;margin-top:14px;font-weight:bold}input,select{width:100%;padding:10px;font-size:16px;box-sizing:border-box}"
             "button{margin-top:20px;width:100%;padding:14px;font-size:18px;background:#2f6db5;color:#fff;border:0;border-radius:6px}"
             "small{color:#555}</style></head><body><h2>Scacchiera intelligente</h2><form method='POST' action='/salva'>"
             "<label>Rete Wi-Fi</label><select name='ssid'><option value=''>-- non cambiare --</option>";
  p += opzioniReti;
  p += "</select><small>oppure scrivi il nome di un'altra rete:</small><input name='altra' placeholder='nome rete (facoltativo)'>"
       "<label>Password Wi-Fi</label><input name='pass' type='password' placeholder='password della rete'>"
       "<label>Token Lichess</label><input name='token' placeholder='lip_... (vuoto = non cambiare)' autocapitalize='off' autocorrect='off'>"
       "<small>Si crea su lichess.org: Preferenze &gt; Accesso API (token) con i permessi "
       "<b>board:play</b> e <b>challenge:write</b>.</small>"
       "<label><input type='checkbox' name='cancella' style='width:auto'> Cancella il token salvato</label>"
       "<button type='submit'>SALVA</button></form></body></html>";
  return p;
}
void drawPortale() {
  tft.fillScreen(ILI9341_BLACK);
  tft.setTextSize(2); tft.setTextColor(ILI9341_YELLOW); tft.setCursor(10, 4); tft.print("Configura col telefono");
  tft.setTextSize(1); tft.setTextColor(ILI9341_WHITE);
  tft.setCursor(10, 34);  tft.print("1. Sul telefono collegati alla rete Wi-Fi:");
  tft.setTextSize(2); tft.setTextColor(ILI9341_GREEN);
  tft.setCursor(20, 50);  tft.print(AP_NOME);
  tft.setTextSize(1); tft.setTextColor(ILI9341_WHITE);
  tft.setCursor(20, 72);  tft.print("password: "); tft.setTextColor(ILI9341_GREEN); tft.print(AP_PASS);
  tft.setTextColor(ILI9341_WHITE);
  tft.setCursor(10, 96);  tft.print("2. Si apre una pagina (se non si apre da sola,");
  tft.setCursor(10, 108); tft.print("   apri il browser e scrivi 192.168.4.1)");
  tft.setCursor(10, 128); tft.print("3. Scegli la rete, scrivi la password,");
  tft.setCursor(10, 140); tft.print("   incolla il token Lichess e tocca SALVA");
  tft.setCursor(10, 164); tft.setTextColor(0xAD55); tft.print("La scacchiera poi si collega da sola.");
  drawBtn(bPortEsc, 0x8000);
}
void apriPortale() {
  scritta("Preparo la configurazione...", ILI9341_YELLOW);
  WiFi.disconnect(); delay(100);
  int n = WiFi.scanNetworks();                    // reti per il menu della pagina
  opzioniReti = "";
  for (int i = 0; i < n; i++) {
    String s = WiFi.SSID(i); if (!s.length() || opzioniReti.indexOf(">" + html(s) + "<") >= 0) continue;
    opzioniReti += "<option value=\"" + html(s) + "\">" + html(s) + "</option>";
  }
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_NOME, AP_PASS);
  dns.start(53, "*", WiFi.softAPIP());            // ogni indirizzo porta alla pagina (si apre da sola)
  static bool registrato = false;
  if (!registrato) {
    registrato = true;
    server.on("/", []() { server.send(200, "text/html", paginaPortale()); });
    server.on("/salva", []() {
      nuovoSsid = server.arg("altra").length() ? server.arg("altra") : server.arg("ssid");
      nuovoPass = server.arg("pass");
      nuovoToken = server.arg("token"); nuovoToken.trim();
      cancellaToken = server.hasArg("cancella");
      server.send(200, "text/html", "<!DOCTYPE html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width'>"
                  "</head><body style='font-family:sans-serif;padding:20px'><h2>Salvato!</h2>"
                  "<p>Guarda il display della scacchiera. Puoi ricollegare il telefono alla tua rete.</p></body></html>");
      portaleChiudiDa = millis();
    });
    server.onNotFound([]() { server.sendHeader("Location", "http://192.168.4.1/", true); server.send(302, "text/plain", ""); });
  }
  server.begin();
  stato = S_PORTALE; portaleChiudiDa = 0; nuovoSsid = ""; nuovoToken = ""; cancellaToken = false;
  drawPortale();
  Serial.println("Configurazione col telefono: rete " + String(AP_NOME));
}
void chiudiPortale() {
  server.stop(); dns.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA); WiFi.setTxPower(WIFI_POTENZA);
}
void portaleLoop() {                              // chiamata dal loop mentre la pagina e' attiva
  dns.processNextRequest();
  server.handleClient();
  if (portaleChiudiDa && millis() - portaleChiudiDa > 3000) {   // lascia arrivare la risposta al telefono
    portaleChiudiDa = 0;
    chiudiPortale();
    if (cancellaToken) cfgToken = "";
    if (nuovoToken.length()) cfgToken = nuovoToken;
    salvaConfig();
    if (nuovoSsid.length()) provaRete(nuovoSsid, nuovoPass);
    else connettiWifi();
    apriOpzioni();
  }
}

// ---------- tocchi nelle schermate delle opzioni ----------
void toccoOpzioni(int x, int y) {
  if (inBtn(bOpRete, x, y)) apriReti(false);
  else if (inBtn(bOpToken, x, y)) apriTastiera(T_TOKEN, "Token Lichess (lip_...), poi OK");
  else if (inBtn(bOpTel, x, y)) apriPortale();
  else if (inBtn(bOpCanc, x, y)) { cfgToken = ""; salvaConfig(); scritta("Token cancellato.", ILI9341_ORANGE); delay(1200); apriOpzioni(); }
  else if (inBtn(bOpInd, x, y)) { stato = S_MENU; drawMenu(); }
}
void toccoReti(int x, int y) {
  int pag = (nReti + RETI_PAG - 1) / RETI_PAG;
  if (inBtn(bRtInd, x, y)) { esciReti(); return; }
  if (inBtn(bRtAgg, x, y)) { apriReti(retiDaAvvio); return; }
  if (nReti > RETI_PAG && inBtn(bRtPrec, x, y)) { retiPag = (retiPag + pag - 1) % pag; drawReti(); return; }
  if (nReti > RETI_PAG && inBtn(bRtSucc, x, y)) { retiPag = (retiPag + 1) % pag; drawReti(); return; }
  if (x < 6 || x > 314 || y < 28) return;
  int k = (y - 28) / 30, j = retiPag * RETI_PAG + k;
  if (k >= RETI_PAG || j >= nReti || (y - 28) % 30 > 28) return;
  int i = retiOrd[j];
  reteScelta = WiFi.SSID(i);
  if (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) {   // rete aperta: niente password
    if (provaRete(reteScelta, "")) { if (retiDaAvvio) { stato = S_MENU; drawMenu(); } else apriOpzioni(); }
    else apriReti(retiDaAvvio);
  } else apriTastiera(T_PASS, "Password di " + reteScelta + ", poi OK");
}
void toccoTastiera(int x, int y) {
  if (inBtn(bTkEsc, x, y)) {                        // annulla
    if (tastDest == T_PASS) { stato = S_RETI; drawReti(); } else apriOpzioni();
    return;
  }
  if (inBtn(bTkOk, x, y)) { tastieraFinita(); return; }
  if (inBtn(bTkMaiu, x, y)) { tastModo = tastModo == 1 ? 0 : 1; drawTastiera(); return; }
  if (inBtn(bTkSimb, x, y)) { tastModo = tastModo == 2 ? 0 : 2; drawTastiera(); return; }
  if (inBtn(bTkSpaz, x, y)) { if (tastTesto.length() < 100) tastTesto += ' '; drawTastCampo(); return; }
  if (inBtn(bTkCanc, x, y)) { if (tastTesto.length()) tastTesto.remove(tastTesto.length() - 1); drawTastCampo(); return; }
  if (y >= TK_Y0 && y < TK_Y0 + 4 * 34 && x >= 2 && x < 2 + 10 * 32) {
    int r = (y - TK_Y0) / 34, c = (x - 2) / 32;
    if (tastTesto.length() < 100) tastTesto += TASTI[tastModo][r][c];
    drawTastCampo();
  }
}

// ============================= TOUCH =============================
bool leggiTocco(int& x, int& y) {
  static bool premuto = false;
  bool t = ts.touched();
  if (!t) { premuto = false; return false; }
  if (premuto) return false;                     // solo all'inizio del tocco
  premuto = true;
  TS_Point p = ts.getPoint();
  x = constrain(map(p.x, TS_MINX, TS_MAXX, 0, 320), 0, 319);
  y = constrain(map(p.y, TS_MINY, TS_MAXY, 0, 240), 0, 239);
  return true;
}

void gestisciTocco() {
  int x, y;
  if (!leggiTocco(x, y)) return;
  if (stato == S_STATO) { stato = S_MENU; drawMenu(); return; }
  if (stato == S_IMPOST) { toccoOpzioni(x, y); return; }
  if (stato == S_RETI) { toccoReti(x, y); return; }
  if (stato == S_TASTIERA) { toccoTastiera(x, y); return; }
  if (stato == S_PORTALE) { if (inBtn(bPortEsc, x, y)) { chiudiPortale(); connettiWifi(); apriOpzioni(); } return; }
  if (stato == S_MENU) {
    if (inBtn(bStato, x, y)) { apriStato(false); return; }
    if (inBtn(bOpzioni, x, y)) { apriOpzioni(); return; }
    if (inBtn(bLichess, x, y)) { modo = M_LICHESS; livello = 3; stato = S_LIVELLO; drawLivelli(); }
    else if (inBtn(bOffline, x, y)) { modo = M_OFFLINE; livello = 2; stato = S_LIVELLO; drawLivelli(); }
    else if (inBtn(b2p, x, y)) { modo = M_2P; nuovaPartita(); }
    else if (inBtn(bProblemi, x, y)) { modo = M_PROBLEMI; stato = S_LIVELLO; drawLivelli(); }
    return;
  }
  if (stato == S_LIVELLO) {
    if (modo == M_PROBLEMI && inBtn(bProbLichess, x, y)) { problemaLichess(); return; }
    for (int i = 0; i < nLivelli(); i++) if (inBtn(livBtn(i), x, y)) {
      if (modo == M_PROBLEMI) { probLiv = i + 1; nuovoProblema(); }
      else { livello = i + 1; nuovaPartita(); }
      return;
    }
    return;
  }
  if (stato == S_FINE && modo == M_PROBLEMI && inBtn(bAvanti, x, y)) { if (probOnline) problemaLichess(); else nuovoProblema(); return; }
  if (stato == S_FINE && inBtn(bAnalisi, x, y)) {
    if (nStoria == 0) return;
    lichessChiudi(false);                                // partita finita: chiudi solo lo stream
    eseguiAnalisi(); mostraAnalisi(); return;
  }
  if (stato == S_ANALISI) {
    if (inBtn(bNuova, x, y)) { hintUntil = 0; stato = S_MENU; drawMenu(); return; }
    if (nErrori && inBtn(bPrec, x, y)) { aIdx = aIdx <= -1 ? nErrori - 1 : aIdx - 1; mostraAnalisi(); }
    else if (nErrori && inBtn(bSucc, x, y)) { aIdx = aIdx >= nErrori - 1 ? -1 : aIdx + 1; mostraAnalisi(); }
    return;
  }
  if (inBtn(bNuova, x, y)) {
    if (modo == M_LICHESS) lichessChiudi(stato != S_FINE);
    stato = S_MENU; drawMenu(); return;
  }
  if (stato == S_PROMO) {
    const char pr[4] = {'q', 'r', 'b', 'n'};
    for (int i = 0; i < 4; i++) {
      Btn b = {PX, 40 + i * 32, 88, 28, ""};
      if (inBtn(b, x, y)) for (int k = 0; k < nPromo; k++) if (promoCand[k].promo == pr[i]) {
        Move m = promoCand[k]; stato = S_GIOCA; tft.fillRect(PX, 20, 92, 150, ILI9341_BLACK); eseguiMossa(m); return;
      }
    }
    return;
  }
  if (inBtn(bAnnulla, x, y) && puoAnnullare()) { annullaMossa(); return; }
  if (inBtn(bAiuto, x, y) && stato == S_GIOCA && giocatoreDiTurno()) {
    setMsg("Cerco un suggerimento...");
    bool trovato = modo == M_PROBLEMI ? parseUci(pos, probMossa(probPly), hint) : engineMove(pos, 3, hint, false);
    if (trovato) { hintUntil = millis() + 10000; drawBoard(); setMsg("Consiglio: " + uci(hint)); }
    return;
  }
  // mosse col touch (solo in simulazione)
  if (sim && stato == S_GIOCA && giocatoreDiTurno() && x >= X0 && x < X0 + 8 * LATO && y >= Y0 && y < Y0 + 8 * LATO) {
    int sq = (7 - (y - Y0) / LATO) * 8 + (x - X0) / LATO;
    if (selSq < 0) { if (own(pos.b[sq], pos.white)) { selSq = sq; drawBoard(); } return; }
    if (own(pos.b[sq], pos.white)) { selSq = sq; drawBoard(); return; }
    Move ml[MAXM]; int n = genLegal(pos, ml); nPromo = 0; bool ok = false; Move found;
    for (int i = 0; i < n; i++) if (ml[i].from == selSq && ml[i].to == sq) {
      if (ml[i].promo) promoCand[nPromo++] = ml[i]; else { found = ml[i]; ok = true; }
    }
    selSq = -1;
    if (nPromo) { stato = S_PROMO; drawBoard(); drawPanel(); return; }
    if (ok) eseguiMossa(found); else drawBoard();
  }
}

// ============================= SETUP =============================
void setup() {
  Serial.begin(115200);
  randomSeed(esp_random());
  SPI.begin();
  tft.begin(); tft.setRotation(ROTAZIONE);
  ts.begin();  ts.setRotation(ROTAZIONE);
  Wire.begin(21, 22);
  strip.begin(); strip.setBrightness(LUMINOSITA); strip.clear(); strip.show();

  delay(300);                                            // lascia stabilizzare l'alimentazione
  WiFi.mode(WIFI_STA);
  WiFi.setTxPower(WIFI_POTENZA);                         // meno picchi di corrente (brownout)
  WiFi.setAutoReconnect(true);
  caricaConfig();                                        // rete e token salvati
  connettiWifi();

  startPos(pos);
  apriStato(true);                                       // schermata di controllo all'accensione
}

// ============================== LOOP =============================
void loop() {
  static unsigned long lastWifiTry = 0;
  bool inOpzioni = stato == S_RETI || stato == S_TASTIERA || stato == S_PORTALE;   // li' la rete la gestiscono le opzioni
  if (!inOpzioni && WiFi.status() != WL_CONNECTED && millis() - lastWifiTry > 15000) { WiFi.disconnect(); connettiWifi(); lastWifiTry = millis(); }
  if (stato == S_PORTALE) portaleLoop();
  drawWifi();
  readSensors();
  gestisciTocco();
  lichessLoop();

  static unsigned long tMossa = 0; static bool mossaInSospeso = false;
  if (occChanged) {
    occChanged = false;
    if (stato == S_GIOCA && giocatoreDiTurno() && !sim) {
      seenEmpty |= occOf(pos) & ~occ;                   // ricorda le caselle svuotate (catture)
      mossaInSospeso = true; tMossa = millis();         // la mossa si controlla quando i pezzi sono fermi
    }
    else if (stato == S_SYNC) controllaSync();
  }
  unsigned long ritardo = modo == M_LICHESS ? RITARDO_LICHESS_MS : RITARDO_MOSSA_MS;
  if (mossaInSospeso && millis() - tMossa >= ritardo) {
    mossaInSospeso = false;
    if (stato == S_GIOCA && giocatoreDiTurno() && !sim) controllaMossaFisica();
  }
  if (stato == S_STATO) {
    static unsigned long tAgg = 0;
    if (millis() - tAgg > 500) { tAgg = millis(); drawStatoVariabile(); }
    // all'accensione, se e' tutto a posto, passa da solo al menu dopo 6 s
    if (statoAvvio && !sim && millis() - statoDa > 6000) {
      if (WiFi.status() == WL_CONNECTED) { stato = S_MENU; drawMenu(); }
      else if (millis() - statoDa > 10000) { statoAvvio = false; apriReti(true); }   // rete salvata non trovata: scegline una
    }
  }
  if (stato == S_SETUP) {                                 // aspetta la posizione iniziale
    static unsigned long okDa = 0; Pos s = setupPos;
    if (occ == occOf(s)) { if (!okDa) okDa = millis(); if (millis() - okDa > 800) { okDa = 0; if (modo == M_PROBLEMI) iniziaProblema(); else iniziaGioco(); } }
    else okDa = 0;
  }
  if (stato == S_ATTESA && modo == M_OFFLINE) {          // turno dell'ESP32
    Move m;
    if (engineMove(pos, livello, m)) applicaMossaAvversario(m);
  }
  static bool hintWas = false;                            // spegne il suggerimento dopo 10 s
  bool hintNow = millis() < hintUntil;
  if (hintWas && !hintNow && stato != S_ANALISI) drawBoard();
  hintWas = hintNow;

  updateLeds();
  delay(5);
}

// Piu' stack per il motore (livello 4 = ricerca profonda). Deve stare IN FONDO al file:
// la macro crea una funzione e l'IDE Arduino mette le dichiarazioni automatiche prima della prima funzione.
#ifdef SET_LOOP_TASK_STACK_SIZE
SET_LOOP_TASK_STACK_SIZE(32 * 1024);
#endif
