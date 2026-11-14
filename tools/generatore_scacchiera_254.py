#!/usr/bin/env python3
"""Scacchiera intelligente v13 - VERSIONE 254 mm (piatto 256 mm, es. Bambu) - NON PROVATA dall'autore.
Caselle 31 mm, striscia LED 144 LED/m (33 LED per traversa, centrata nella scanalatura).
Tutto unito con 4 VITI laterali (niente colla / nastro).
La cassettiera ha le pareti laterali alte fino al piano di gioco; il separatore ha le pareti
davanti/dietro alte. Dentro si infilano dall'alto separatore, vassoio LED e piano di gioco.
4 viti M3 (sinistra, destra, davanti, dietro) entrano nelle torrette della gonna del piano:
il vassoio resta stretto tra piano e separatore, il separatore tra vassoio e cassettiera.
Si rifanno: 1a/1b/1c piano, 2 vassoio LED, 3 separatore, 4 cassettiera.
Restano: cassetti, coperchio elettronica, box display (vecchio, senza rialzo).
Nuova anche la cornice display (7b), adattata al box vecchio.
Coordinate: x = colonne a->h, y = traverse 1->8, z verso l'alto. Misure in mm."""
import math, os, struct, zipfile

OUT = "stl"
ASM = "asm"
os.makedirs(OUT, exist_ok=True); os.makedirs(ASM, exist_ok=True)

# ===================== PARAMETRI =====================
SQ, B = 31.0, 3.0             # <<< MODIFICA: casella, bordo (versione 254: 31 + 3)
W = 8 * SQ + 2 * B            # 254
T_PLATE, SKIN = 1.2, 0.6      # piano / plastica sopra il reed (provato 0,6)
COORD_D = 0.4                 # profondita' lettere nere
H_GAP = 8.0                   # tubi luce (piano -> vassoio)
SKIRT = 2.0                   # gonna perimetrale del piano
# modulo HW-483 (misurato col calibro)
MOD_L, MOD_T = 14.6, 6.9      # larghezza (lungo il reed) x spessore
MOD_H, PIN_L = 20.7, 5.3      # corpo (reed compreso) e pin
GAP_MOD = 0.05                # <<< MODIFICA: gioco taschina per lato (prova: 0,05 perfetto)
REED_OFF = 6.5                # <<< MODIFICA: reed spostato verso la traversa precedente (magnete sull'estremita')
SL_WALL = 1.2                 # parete taschina
SL_DEPTH = H_GAP - 0.5        # taschina: si ferma 0,5 sopra il vassoio
HOLE_EXTRA = 0.4              # foro nel vassoio piu' largo della taschina (passa anche il dupont)
# luce: striscia 144 LED/m (di solito larga 12 mm)
STRIP_W = 12.0                # <<< MODIFICA: larghezza della tua striscia 144 LED/m (10 o 12 mm)
TUBE_W, STRIP_NOTCH = 0.8, 2.0
TUBE_IN_X = 12.0
TUBE_Y1 = SQ / 2 - TUBE_W - 0.1           # 14,6: il tubo resta dentro la casella
TUBE_Y0 = TUBE_Y1 - (STRIP_W + 0.6)       # scanalatura = striscia + 0,6
# vassoio LED
RISE = 23.0                   # <<< scacchiera piu' alta di 23 mm (= passo fori del box display)
T_TRAY = 2.0
H_WIRE = 10.0 + RISE          # spazio sotto il vassoio: moduli + dupont
WALL_FB, WALL_LR, RIB = 2.0, 3.0, 1.6
# incastri
PIN_W, PIN_L_, PIN_H, PIN_GAP = 1.2, 4.0, 1.5, 0.2   # perni 1,2 x 4 x 1,5 ; fori +0,2 per lato
# separatore / cassettiera (cassettiera INVARIATA)
T_SEP, H_CASS, CW = 1.6, 32.0, 4.0
BW = 3.0
BAND = (W / 2 - 16, W / 2 + 16)          # vano elettronica centrato (32 mm come prima)
TAB_D, TAB_T, TAB_GAP = 6.0, 2.0, 0.05  # <<< MODIFICA: linguette separatore (gioco 0,05 come la taschina)
# v13: pareti esterne e viti
T_UP = 1.6                    # <<< MODIFICA: spessore pareti alte (cassettiera ai lati, separatore davanti/dietro)
GAP_IN = 0.25                 # <<< MODIFICA: gioco tra pareti e pezzi interni
O = T_UP + GAP_IN             # i pezzi interni vanno da O a W-O (264,3 mm): le caselle restano 33 mm
FLOOR = 2.0
SCREW_HOLE, SCREW_PILOT = 3.4, 2.5   # foro passante parete / foro torretta (vite autofilettante M3)
BOSS_L, BOSS_IN = 8.0, 7.0    # torrette nella gonna del piano: lunghezza / sporgenza verso l'interno
# posizione delle 4 viti (assolute): la destra e' spostata fuori dal box display
Y_SX = B + SQ / 2 + 3 * SQ + 23.8        # viti tra due strisce: sinistra tra traversa 4 e 5
Y_DX = B + SQ / 2 + 23.8                 # destra tra traversa 1 e 2 (fuori dal box display)
X_FB = W / 2                              # davanti / dietro tra colonna d ed e
NOTCH_BK = 2.5                # gonna dietro rialzata: sotto passa la striscia della traversa 8
# display (box INVARIATO)
D_H, L_H, H_F = 70.0, 120.0, 30.0   # box display che usi (versione vecchia, appoggiato sul tavolo, senza rialzo)
H_BOX = 74.8                  # altezza del box dietro (circa a filo del piano)
OLD_ZTOP = 54.8
DISP_PCB = (90.5, 60.0)       # <<< MODIFICA: sede scheda 89 x 59 + gioco (prima 57: troppo stretta)
DISP_WIN = (78.8, 55.2)       # <<< MODIFICA: finestra (lato lungo y, lato corto u) richiesta da Antonio
GAP_BZ = 0.5
# =====================================================

Z_SEP = H_CASS
Z_VAS = Z_SEP + T_SEP
Z_TRAY = Z_VAS + H_WIRE + T_TRAY
Z_TOP = Z_TRAY + H_GAP + T_PLATE
CEN = [B + SQ / 2 + SQ * k for k in range(8)]
BND = [B + SQ * k for k in range(1, 8)]
Y0 = W / 2 - L_H / 2


# ---------------- primitive ----------------
def box(x0, x1, y0, y1, z0, z1):
    if x1 - x0 < 1e-6 or y1 - y0 < 1e-6 or z1 - z0 < 1e-6:
        return []
    v = [(x0, y0, z0), (x1, y0, z0), (x1, y1, z0), (x0, y1, z0),
         (x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)]
    f = [(0, 3, 2), (0, 2, 1), (4, 5, 6), (4, 6, 7), (0, 1, 5), (0, 5, 4),
         (1, 2, 6), (1, 6, 5), (2, 3, 7), (2, 7, 6), (3, 0, 4), (3, 4, 7)]
    return [(v[a], v[b], v[c]) for a, b, c in f]


def mapbox(ax, u0, u1, v0, v1, w0, w1):
    r = {ax[0]: (u0, u1), ax[1]: (v0, v1), ax[2]: (w0, w1)}
    return box(*r["x"], *r["y"], *r["z"])


def fill(u0, u1, v0, v1, holes, w0, w1, ax="xyz"):
    """Rettangolo pieno meno i fori rettangolari (hu0,hu1,hv0,hv1), spessore w0..w1."""
    vs = sorted({v0, v1, *[min(max(h[k], v0), v1) for h in holes for k in (2, 3)]})
    rows, out = [], []
    for a, b in zip(vs, vs[1:]):
        m = (a + b) / 2
        segs = sorted((max(h[0], u0), min(h[1], u1)) for h in holes if h[2] < m < h[3] and h[1] > u0 and h[0] < u1)
        gaps, x = [], u0
        for s0, s1 in segs:
            if s0 > x:
                gaps.append((x, s0))
            x = max(x, s1)
        if x < u1:
            gaps.append((x, u1))
        if rows and rows[-1][2] == gaps and abs(rows[-1][1] - a) < 1e-9:
            rows[-1][1] = b
        else:
            rows.append([a, b, gaps])
    for a, b, gaps in rows:
        for g0, g1 in gaps:
            out += mapbox(ax, g0, g1, a, b, w0, w1)
    return out


def prism_xz(pts, y0, y1):
    t, n = [], len(pts)
    P = lambda p, y: (p[0], y, p[1])
    for i in range(1, n - 1):
        t.append((P(pts[0], y0), P(pts[i + 1], y0), P(pts[i], y0)))
        t.append((P(pts[0], y1), P(pts[i], y1), P(pts[i + 1], y1)))
    for i in range(n):
        a, b = pts[i], pts[(i + 1) % n]
        t += [(P(a, y0), P(b, y0), P(b, y1)), (P(a, y0), P(b, y1), P(a, y1))]
    return t


def shell_vol(tris):
    return sum(a[0] * (b[1] * c[2] - b[2] * c[1]) - a[1] * (b[0] * c[2] - b[2] * c[0])
               + a[2] * (b[0] * c[1] - b[1] * c[0]) for a, b, c in tris) / 6.0


def fix(tris):
    return tris if shell_vol(tris) >= 0 else [(a, c, b) for a, b, c in tris]


def move(tris, dx=0, dy=0, dz=0):
    return [tuple((p[0] + dx, p[1] + dy, p[2] + dz) for p in t) for t in tris]


def flip_x(tris, ymax, zmax):
    """Rotazione 180 gradi attorno a X (stampa a faccia in giu')."""
    return [tuple((p[0], ymax - p[1], zmax - p[2]) for p in t) for t in tris]


def to_floor(tris):
    zmin = min(p[2] for t in tris for p in t)
    return move(tris, dz=-zmin)


def write_stl(path, tris):
    with open(path, "wb") as f:
        f.write(os.path.basename(path).encode()[:80].ljust(80, b" "))
        f.write(struct.pack("<I", len(tris)))
        for a, b, c in tris:
            u = [b[k] - a[k] for k in range(3)]; v = [c[k] - a[k] for k in range(3)]
            n = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
            l = math.sqrt(sum(x * x for x in n)) or 1.0
            f.write(struct.pack("<12fH", *(x / l for x in n), *a, *b, *c, 0))


def pin_rect(px, py, ax, w, l):
    return (px - w / 2, px + w / 2, py - l / 2, py + l / 2) if ax == "y" else (px - l / 2, px + l / 2, py - w / 2, py + w / 2)


parts, assembly = {}, {}

# font a pixel 5x7 per le coordinate
FONT = {
 'a': [".....",".....",".###.","....#",".####","#...#",".####"],
 'b': ["#....","#....","#.##.","##..#","#...#","#...#","####."],
 'c': [".....",".....",".###.","#....","#....","#...#",".###."],
 'd': ["....#","....#",".##.#","#..##","#...#","#...#",".####"],
 'e': [".....",".....",".###.","#...#","#####","#....",".###."],
 'f': ["..##.",".#..#",".#...","###..",".#...",".#...",".#..."],
 'g': [".....",".####","#...#","#...#",".####","....#",".###."],
 'h': ["#....","#....","#.##.","##..#","#...#","#...#","#...#"],
 '1': ["..#..",".##..","..#..","..#..","..#..","..#..",".###."],
 '2': [".###.","#...#","....#","...#.","..#..",".#...","#####"],
 '3': ["####.","....#","....#",".###.","....#","....#","####."],
 '4': ["...#.","..##.",".#.#.","#..#.","#####","...#.","...#."],
 '5': ["#####","#....","####.","....#","....#","#...#",".###."],
 '6': ["..##.",".#...","#....","####.","#...#","#...#",".###."],
 '7': ["#####","....#","...#.","..#..",".#...",".#...",".#..."],
 '8': [".###.","#...#","#...#",".###.","#...#","#...#",".###."],
}
PIX = 0.9


def glyph(ch, x0, ytop, ruota=False):
    out = []
    rows = [r[::-1] for r in FONT[ch][::-1]] if ruota else FONT[ch]
    for i, row in enumerate(rows):
        j = 0
        while j < 5:
            if row[j] == '#':
                k = j
                while k < 5 and row[k] == '#': k += 1
                out.append((x0 + j * PIX, x0 + k * PIX, ytop - (i + 1) * PIX, ytop - i * PIX))
                j = k
            else:
                j += 1
    return out


def pocket(cx, cy, g):
    ry = cy - REED_OFF
    return (cx - MOD_T / 2 - g, cx + MOD_T / 2 + g, ry - MOD_L / 2 - g, ry + MOD_L / 2 + g)


# perni (asimmetrici: il pezzo entra in un solo verso) - coordinate assolute
PY1, PY2 = W * 90 / 268, W * 178 / 268
PIN_PLATE = [(O + SKIRT / 2, PY1, "y"), (W - O - SKIRT / 2, PY1, "y"), (PY1, O + SKIRT / 2, "x")]   # dietro niente perno (striscia)
PIN_TRAY = [(O + WALL_LR / 2, PY2, "y"), (W - O - WALL_LR / 2, PY2, "y"), (PY2, O + WALL_FB / 2, "x"), (PY2, W - O - WALL_FB / 2, "x")]
Z_SCREW = Z_TRAY + H_GAP / 2          # altezza delle 4 viti (meta' gonna del piano)
IN0, IN1 = O, W - O                    # ingombro dei pezzi interni

# =========================================================
# 1) PIANO DI GIOCO  (z locale 0 = sopra del vassoio LED)
#    NERO: bordo, lettere, tubi luce, taschine moduli, gonna, perni, 4 torrette viti
# =========================================================
zp0, zp1 = H_GAP, H_GAP + T_PLATE
zs, zc = zp1 - SKIN, zp1 - COORD_D
zsc = Z_SCREW - Z_TRAY
white, blue, black = [], [], []
for r in range(8):
    for f in range(8):
        cx, cy = CEN[f], CEN[r]
        x0, x1, y0, y1 = cx - SQ / 2, cx + SQ / 2, cy - SQ / 2, cy + SQ / 2
        pk = pocket(cx, cy, GAP_MOD)
        win = (cx - TUBE_IN_X / 2, cx + TUBE_IN_X / 2, cy + TUBE_Y0, cy + TUBE_Y1)
        body = white if (f + r) % 2 else blue          # a1 scura (blu)
        coord = []
        if r == 0: coord += glyph('abcdefgh'[f], x1 - 1.2 - 5 * PIX, y0 + 1.2 + 7 * PIX)
        if f == 0: coord += glyph('12345678'[r], x0 + 1.2, y1 - 1.2)
        if r == 7: coord += glyph('abcdefgh'[f], x0 + 1.2, y1 - 1.2 - (7 * PIX + 1.2 if f == 0 else 0), True)
        if f == 7: coord += glyph('12345678'[r], x1 - 1.2 - 5 * PIX, y0 + 1.2 + 7 * PIX + (7 * PIX + 1.2 if r == 0 else 0), True)
        body += fill(x0, x1, y0, y1, [win] + coord, zc, zp1)
        body += fill(x0, x1, y0, y1, [win], zs, zc)
        body += fill(x0, x1, y0, y1, [win, pk], zp0, zs)
        white += box(*win, zp0, zp1)                             # finestra LED bianca
        for c_ in coord:
            black += box(*c_, zc, zp1)
        wx0, wx1 = win[0] - TUBE_W, win[1] + TUBE_W
        black += box(wx0, wx1, win[2] - TUBE_W, win[2], 0, zp0)
        black += box(wx0, wx1, win[3], win[3] + TUBE_W, 0, zp0)
        black += box(wx0, win[0], win[2], win[3], STRIP_NOTCH, zp0)
        black += box(win[1], wx1, win[2], win[3], STRIP_NOTCH, zp0)
        black += fill(pk[0] - SL_WALL, pk[1] + SL_WALL, pk[2] - SL_WALL, pk[3] + SL_WALL, [pk], zp0 - SL_DEPTH, zp0)
black += fill(IN0, IN1, IN0, IN1, [(B, W - B, B, W - B)], zp0, zp1)                       # bordo sottile
SK0, SK1 = IN0 + SKIRT, IN1 - SKIRT
pilot = lambda u, v: (u - SCREW_PILOT / 2, u + SCREW_PILOT / 2, v - SCREW_PILOT / 2, v + SCREW_PILOT / 2)
# gonna con i fori delle viti, poi torrette (foro pilota orizzontale per vite M3 autofilettante)
skirt_sx = fill(IN0, IN1, 0, zp0, [pilot(Y_SX, zsc)], IN0, SK0, ax="yzx")
skirt_dx = fill(IN0, IN1, 0, zp0, [pilot(Y_DX, zsc)], SK1, IN1, ax="yzx")
skirt_fr = fill(SK0, SK1, 0, zp0, [pilot(X_FB, zsc)], IN0, SK0, ax="xzy")
skirt_bk = fill(SK0, SK1, NOTCH_BK, zp0, [pilot(X_FB, zsc)], SK1, IN1, ax="xzy")
black += skirt_sx + skirt_dx + skirt_fr + skirt_bk
L2 = BOSS_L / 2
black += fill(Y_SX - L2, Y_SX + L2, 0, zp0, [pilot(Y_SX, zsc)], SK0, SK0 + BOSS_IN, ax="yzx")   # sinistra
black += fill(Y_DX - L2, Y_DX + L2, 0, zp0, [pilot(Y_DX, zsc)], SK1 - BOSS_IN, SK1, ax="yzx")      # destra
black += fill(X_FB - L2, X_FB + L2, 0, zp0, [pilot(X_FB, zsc)], SK0, SK0 + BOSS_IN, ax="xzy")   # davanti
black += fill(X_FB - L2, X_FB + L2, NOTCH_BK, zp0, [pilot(X_FB, zsc)], SK1 - BOSS_IN, SK1, ax="xzy")   # dietro (sopra la striscia)
for px, py, ax in PIN_PLATE:
    black += box(*pin_rect(px, py, ax, PIN_W, PIN_L_), -PIN_H, 0)

for nm, t in (("1a_piano_NERO.stl", black), ("1b_piano_BIANCO.stl", white), ("1c_piano_BLU.stl", blue)):
    parts[nm] = flip_x(t, W, zp1)
    assembly[nm] = move(t, dz=Z_TRAY)

# =========================================================
# 2) VASSOIO LED  (z locale 0 = appoggio sul separatore)
# =========================================================
vas = []
zt0, zt1 = H_WIRE, H_WIRE + T_TRAY
holes_t, holes_g = [], []
LR0, LR1 = IN0 + WALL_LR, IN1 - WALL_LR
for r in range(8):
    cy = CEN[r]
    for f in range(8):
        holes_t.append(pocket(CEN[f], cy, GAP_MOD + HOLE_EXTRA))     # passaggio modulo + dupont
    sy0, sy1 = cy + TUBE_Y0, cy + TUBE_Y1
    holes_g.append((LR0, LR1, sy0, sy1))                             # scanalatura striscia
    holes_t.append((LR0 + 0.5, LR0 + 4.5, sy0, sy1))                 # asole fine striscia
    holes_t.append((LR1 - 4.5, LR1 - 0.5, sy0, sy1))
for px, py, ax in PIN_PLATE:
    holes_t.append(pin_rect(px, py, ax, PIN_W + 2 * PIN_GAP, PIN_L_ + 2 * PIN_GAP))
vas += fill(IN0, IN1, IN0, IN1, holes_t, zt0, zt1 - 0.6)
vas += fill(IN0, IN1, IN0, IN1, holes_t + holes_g, zt1 - 0.6, zt1)



vas += box(IN0, LR0, IN0, IN1, 0, zt0)                                  # sinistra
vas += box(LR1, IN1, IN0, IN1, 0, zt0)                                  # destra
vas += box(LR0, LR1, IN0, IN0 + WALL_FB, 0, zt0)                        # davanti
vas += box(LR0, LR1, IN1 - WALL_FB, IN1, 0, zt0)                        # dietro
for xb in BND:
    vas += box(xb - RIB / 2, xb + RIB / 2, IN0 + WALL_FB, IN1 - WALL_FB, zt0 - 6, zt0)
for px, py, ax in PIN_TRAY:
    vas += box(*pin_rect(px, py, ax, PIN_W, PIN_L_), -PIN_H, 0)
parts["2_vassoio_LED.stl"] = flip_x(vas, W, zt1)
assembly["2_vassoio_LED.stl"] = move(vas, dz=Z_VAS)

# =========================================================
# 3) SEPARATORE con pareti DAVANTI e DIETRO alte (fori viti)  z locale 0 = appoggio sulla cassettiera
# =========================================================
b0, b1 = BAND
H_SEPW = Z_TOP - Z_SEP                       # pareti fino a filo del piano
zss = Z_SCREW - Z_SEP
hole = lambda u, v: (u - SCREW_HOLE / 2, u + SCREW_HOLE / 2, v - SCREW_HOLE / 2, v + SCREW_HOLE / 2)
sep_holes = [(45, W - 45, b0 + 2, b1 - 2)]
sep_holes += [pin_rect(px, py, ax, PIN_W + 2 * PIN_GAP, PIN_L_ + 2 * PIN_GAP) for px, py, ax in PIN_TRAY]
sep = fill(IN0, IN1, 0, W, sep_holes, 0, T_SEP)
sep += fill(IN0, IN1, T_SEP, H_SEPW, [hole(X_FB, zss)], 0, T_UP, ax="xzy")          # parete davanti
sep += fill(IN0, IN1, T_SEP, H_SEPW, [hole(X_FB, zss)], W - T_UP, W, ax="xzy")      # parete dietro
parts["3_separatore.stl"] = sep                                    # si stampa cosi': piano sul piatto, pareti in alto
assembly["3_separatore.stl"] = move(sep, dz=Z_SEP)

# =========================================================
# 4) CASSETTIERA: parte bassa uguale a prima + pareti laterali alte (fori viti e display)
# =========================================================
cas = []
bay_f, bay_b = (0, b0 - BW), (b1 + BW, W)
feet = [(8, 19, 6, 17), (W - 19, W - 8, 6, 17), (8, 19, W - 17, W - 6), (W - 19, W - 8, W - 17, W - 6)]
for ya, yb in (bay_f, bay_b):                                   # fondi cassetti con sedi piedini
    cas += fill(CW, W - CW, ya, yb, feet, 0, 1.0)
    cas += box(CW, W - CW, ya, yb, 1.0, FLOOR)
cas += box(0, CW, 0, W, 0, H_CASS)                              # parete sinistra (bassa)
DC = W / 2                                                       # centro box display
wall_holes = [(DC - 5 - 1.7, DC - 5 + 1.7, 6 - 1.7, 6 + 1.7), (DC + 5 - 1.7, DC + 5 + 1.7, 6 - 1.7, 6 + 1.7),
              (DC - 5 - 1.7, DC - 5 + 1.7, 29 - 1.7, 29 + 1.7), (DC + 5 - 1.7, DC + 5 + 1.7, 29 - 1.7, 29 + 1.7),
              (DC - 10, DC + 10, 12, 24)]
cas += fill(0, W, 0, H_CASS, wall_holes, W - CW, W, ax="yzx")  # parete destra (bassa, lato display)
cas += box(CW, W - CW, b0 - BW, b0, 0, H_CASS)                  # pareti vano
cas += box(CW, W - CW, b1, b1 + BW, 0, H_CASS)
BLOCKS = [(CW + 4, b0 + 4), (W - CW - 4, b0 + 4), (CW + 4, b1 - 4), (W - CW - 4, b1 - 4)]
for bx, by in BLOCKS:                                           # blocchetti viti coperchio
    cas += fill(bx - 4, bx + 4, by - 4, by + 4, [(bx - 1.3, bx + 1.3, by - 1.3, by + 1.3)], FLOOR, FLOOR + 10)
    cas += box(bx - 4, bx + 4, by - 4, by + 4, FLOOR + 10, H_CASS)
# pareti alte (v13)
up_sx = [hole(Y_SX, Z_SCREW)]
up_dx = [hole(Y_DX, Z_SCREW)]       # il box display si avvita nella parte bassa come prima
cas += fill(0, W, H_CASS, Z_TOP, up_sx, 0, T_UP, ax="yzx")
cas += fill(0, W, H_CASS, Z_TOP, up_dx, W - T_UP, W, ax="yzx")
parts["4_cassettiera.stl"] = cas
assembly["4_cassettiera.stl"] = cas

# =========================================================
# 5) COPERCHIO ELETTRONICA e 6) CASSETTO (stesse regole di prima, ricalcolati per la nuova misura)
# =========================================================
cop = fill(CW + 0.3, W - CW - 0.3, b0 + 0.3, b1 - 0.3, [(bx - 1.7, bx + 1.7, by - 1.7, by + 1.7) for bx, by in BLOCKS], 0, 2.0)
parts["5_coperchio_elettronica.stl"] = cop
assembly["5_coperchio_elettronica.stl"] = move(cop, dz=H_CASS - 2.0)
DW, DD, DHt, DT = W - 2 * CW - 1.0, (b0 - BW) - 1.0, H_CASS - FLOOR - 0.6, 1.6   # cassetto: largo, profondo, alto, pareti
NW = 40.0                                                        # tacca maniglia
cass = box(0, DW, 0, DD, 0, DT)                                  # fondo
cass += box(0, DT, 3.0, DD - DT, DT, DHt) + box(DW - DT, DW, 3.0, DD - DT, DT, DHt)   # fianchi
cass += box(0, DW, DD - DT, DD, DT, DHt)                         # dietro
cass += box(0, DW / 2 - NW / 2, 0, 3.0, DT, DHt) + box(DW / 2 + NW / 2, DW, 0, 3.0, DT, DHt)   # frontale
cass += box(DW / 2 - NW / 2, DW / 2 + NW / 2, 0, 3.0, DT, DHt - 10.0)
parts["6_cassetto_x2.stl"] = cass
assembly["6_cassetto_x2.stl"] = move(cass, CW + 0.5, 0.5, FLOOR)

# =========================================================
# 7) CORNICE DISPLAY per il box vecchio (70 mm, davanti 30, dietro 74,8)
# =========================================================
H_B = H_BOX
L_IN = math.hypot(D_H, H_B - H_F)
ang = math.atan2(H_B - H_F, D_H)
U0 = (2.5 + GAP_BZ) / math.cos(ang)
UC = (U0 + L_IN) / 2
py0, py1 = L_H / 2 - DISP_PCB[0] / 2, L_H / 2 + DISP_PCB[0] / 2
pu0, pu1 = UC - DISP_PCB[1] / 2, UC + DISP_PCB[1] / 2
win = (UC - DISP_WIN[1] / 2, UC + DISP_WIN[1] / 2, L_H / 2 - DISP_WIN[0] / 2, L_H / 2 + DISP_WIN[0] / 2)
bz = fill(U0, L_IN, 0, L_H, [win], 0, 2.0)
yr = 2.0 + GAP_BZ
bz += box(U0 + 6, L_IN - 8, yr, yr + 1.2, 2.0, 5.0) + box(U0 + 6, L_IN - 8, L_H - yr - 1.2, L_H - yr, 2.0, 5.0)
for (u, y, du, dy) in ((pu0, py0, 1, 1), (pu1, py0, -1, 1), (pu0, py1, 1, -1), (pu1, py1, -1, -1)):
    bz += box(*sorted((u - 1.2 * du, u + 6 * du)), *sorted((y - 1.2 * dy, y)), 2.0, 5.0)
    bz += box(*sorted((u - 1.2 * du, u)), *sorted((y - 1.2 * dy, y + 6 * dy)), 2.0, 5.0)
parts["7b_cornice_display.stl"] = bz
def place_bezel(p):
    u, y, z = p
    return (W + u * math.cos(ang) + z * math.sin(ang), Y0 + y, H_B - u * math.sin(ang) + z * math.cos(ang))
assembly["7b_cornice_display.stl"] = [tuple(place_bezel((p[0], p[1], 2.0 - p[2])) for p in tr) for tr in bz]
assert pu0 - 1.2 > U0 and pu1 + 1.2 < L_IN, "cornice: la sede non ci sta"

# =========================================================
# (solo anteprima) BOX DISPLAY vecchio, appoggiato sul tavolo
# =========================================================
H_B7 = H_BOX
hs = box(0, D_H, 0, L_H, 0, 2)
wall_h = wall_holes
hs += fill(0, L_H, 0, H_B7, [(h[0] - Y0, h[1] - Y0, h[2], h[3]) for h in wall_h], 0, 2.5, ax="yzx")
hs += fill(0, L_H, 0, H_F, [(14, 34.7, 20, 35.2), (50, 70.4, 25, 31), (86, 102, 5, 15)], D_H - 2.0, D_H, ax="yzx")
prof = [(0, 0), (D_H, 0), (D_H, H_F), (0, H_B7)]
hs += fix(prism_xz(prof, 0, 2)) + fix(prism_xz(prof, L_H - 2, L_H))
assembly["7a_supporto_display.stl"] = move(hs, W, Y0, 0)

# ---------------- scrittura ----------------
for nm, tr in parts.items():
    tr = to_floor(tr)
    write_stl(os.path.join(OUT, nm), tr)
    xs = [p[0] for t_ in tr for p in t_]; ys = [p[1] for t_ in tr for p in t_]; zz = [p[2] for t_ in tr for p in t_]
    print(f"{nm:28s} {max(xs)-min(xs):6.1f} x {max(ys)-min(ys):6.1f} x {max(zz):5.1f} mm  tri={len(tr)}")
for nm, tr in assembly.items():
    write_stl(os.path.join(ASM, nm), tr)
print(f"Superficie di gioco: {Z_TOP:.1f} mm, box display alto {H_BOX} mm, angolo {math.degrees(ang):.1f} gradi, discesa {L_IN:.1f} mm")
print(f"Viti laterali a z = {Z_SCREW:.1f} mm; pezzi interni {IN1-IN0:.1f} mm")
