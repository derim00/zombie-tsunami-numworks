/*
 * ZOMBIE TSUNAMI - version NumWorks (EADK, C99)
 *
 * Reecriture complete (le depot fourni est un loader PS Vita du binaire
 * Android, il ne contient pas le code du jeu). Boucle de gameplay d'origine :
 *   - une horde de zombies court automatiquement vers la droite ;
 *   - on convertit les humains en les touchant (+1 zombie) ;
 *   - on saute les trous, caisses, tonneaux, pointes et voitures ;
 *   - une voiture ne se retourne que si la horde est assez nombreuse
 *     (le chiffre au-dessus de la voiture), sinon elle decime la horde.
 *
 * Controles : OK / EXE / HAUT = sauter (maintenir = saut plus haut)
 *             BACK = pause / quitter
 *
 * Aucun float, aucun malloc, aucune dependance libc : tout est statique et
 * initialise explicitement au demarrage (pas de .data/.bss suppose a zero).
 * Rendu par bandes de 24 lignes (buffer de 15 Ko) -> pas de framebuffer
 * complet, aucun scintillement.
 */
#include "eadk.h"
#include <stdbool.h>
#include <stdint.h>

const char eadk_app_name[] __attribute__((section(".rodata.eadk_app_name"))) = "Zombie Tsunami";
const uint32_t eadk_api_level __attribute__((section(".rodata.eadk_api_level"))) = 0;

/* ------------------------------------------------------------------ */
/* Constantes                                                          */
/* ------------------------------------------------------------------ */
#define SW 320
#define SH 240
#define STRIP_H 24
#define NSTRIPS (SH / STRIP_H)
#define GROUND_Y 176            /* haut de la route */
#define BASE_Y (GROUND_Y + 8)   /* pieds des entites */
#define FRAME_MS 33             /* ~30 images/s */
#define HX 120                  /* x ecran du zombie de tete */
#define MAXV 15                 /* zombies affiches (3 rangees x 5) */
#define NSEL 3
#define NENT 30
#define NPART 40
#define HIST 32
#define MAXTOTAL 99

#define JUMP_V 128              /* 8 px/frame en 1/16 px */
#define GRAV 14
#define GRAV_HOLD 9
#define SPEED_MIN 60            /* 3.75 px/frame en 1/16 px */
#define SPEED_MAX 120           /* 7.5 px/frame */

#define RGB(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

/* ------------------------------------------------------------------ */
/* Utilitaires                                                         */
/* ------------------------------------------------------------------ */
static uint32_t rng_state;

static uint32_t rnd(void) {
  uint32_t x = rng_state;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  rng_state = x;
  return x;
}

static uint32_t hash32(uint32_t x) {
  x ^= x >> 16;
  x *= 0x7feb352dU;
  x ^= x >> 15;
  x *= 0x846ca68bU;
  x ^= x >> 16;
  return x;
}

static int imin(int a, int b) { return a < b ? a : b; }
static int iabs(int a) { return a < 0 ? -a : a; }

/* ------------------------------------------------------------------ */
/* Police 5x7 (ASCII 32..90, colonnes, bit0 = haut)                    */
/* ------------------------------------------------------------------ */
static const uint8_t font5x7[59][5] = {
  {0x00,0x00,0x00,0x00,0x00}, {0x00,0x00,0x5F,0x00,0x00}, {0x00,0x07,0x00,0x07,0x00}, {0x14,0x7F,0x14,0x7F,0x14},
  {0x24,0x2A,0x7F,0x2A,0x12}, {0x23,0x13,0x08,0x64,0x62}, {0x36,0x49,0x56,0x20,0x50}, {0x00,0x08,0x07,0x03,0x00},
  {0x00,0x1C,0x22,0x41,0x00}, {0x00,0x41,0x22,0x1C,0x00}, {0x2A,0x1C,0x7F,0x1C,0x2A}, {0x08,0x08,0x3E,0x08,0x08},
  {0x00,0x80,0x70,0x30,0x00}, {0x08,0x08,0x08,0x08,0x08}, {0x00,0x00,0x60,0x60,0x00}, {0x20,0x10,0x08,0x04,0x02},
  {0x3E,0x51,0x49,0x45,0x3E}, {0x00,0x42,0x7F,0x40,0x00}, {0x72,0x49,0x49,0x49,0x46}, {0x21,0x41,0x49,0x4D,0x33},
  {0x18,0x14,0x12,0x7F,0x10}, {0x27,0x45,0x45,0x45,0x39}, {0x3C,0x4A,0x49,0x49,0x31}, {0x41,0x21,0x11,0x09,0x07},
  {0x36,0x49,0x49,0x49,0x36}, {0x46,0x49,0x49,0x29,0x1E}, {0x00,0x00,0x14,0x00,0x00}, {0x00,0x40,0x34,0x00,0x00},
  {0x00,0x08,0x14,0x22,0x41}, {0x14,0x14,0x14,0x14,0x14}, {0x00,0x41,0x22,0x14,0x08}, {0x02,0x01,0x59,0x09,0x06},
  {0x3E,0x41,0x5D,0x59,0x4E}, {0x7C,0x12,0x11,0x12,0x7C}, {0x7F,0x49,0x49,0x49,0x36}, {0x3E,0x41,0x41,0x41,0x22},
  {0x7F,0x41,0x41,0x41,0x3E}, {0x7F,0x49,0x49,0x49,0x41}, {0x7F,0x09,0x09,0x09,0x01}, {0x3E,0x41,0x41,0x51,0x73},
  {0x7F,0x08,0x08,0x08,0x7F}, {0x00,0x41,0x7F,0x41,0x00}, {0x20,0x40,0x41,0x3F,0x01}, {0x7F,0x08,0x14,0x22,0x41},
  {0x7F,0x40,0x40,0x40,0x40}, {0x7F,0x02,0x1C,0x02,0x7F}, {0x7F,0x04,0x08,0x10,0x7F}, {0x3E,0x41,0x41,0x41,0x3E},
  {0x7F,0x09,0x09,0x09,0x06}, {0x3E,0x41,0x51,0x21,0x5E}, {0x7F,0x09,0x19,0x29,0x46}, {0x26,0x49,0x49,0x49,0x32},
  {0x03,0x01,0x7F,0x01,0x03}, {0x3F,0x40,0x40,0x40,0x3F}, {0x1F,0x20,0x40,0x20,0x1F}, {0x3F,0x40,0x38,0x40,0x3F},
  {0x63,0x14,0x08,0x14,0x63}, {0x03,0x04,0x78,0x04,0x03}, {0x61,0x59,0x49,0x4D,0x43}
};

/* ------------------------------------------------------------------ */
/* Sprites pixel art (chaque ligne = w caracteres)                      */
/* . transparent  G vert fonce  g vert  w blanc  k noir  S chemise      */
/* P pantalon  m bouche  f peau  h cheveux  y jaune  o orange          */
/* ------------------------------------------------------------------ */
#define ZW 14
#define ZH 15
static const char *const zombie_a[ZH] = {
  "....GGGGGG....", "...GgggggggG..", "..GgggggggggG.", ".GgggwwwwgggG.", ".GggwwwwwwggG.",
  ".GggwwwkkwggG.", ".GggwwwkkwggG.", ".GggwwwwwwggG.", "..GgggggggkkG.", "...GGGGGGGGG..",
  "...SSSSSSSS...", "..gSSSSSSSSg..", "...SSSSSSSS...", "...PP....PP...", "..kkk....kkk.."
};
static const char *const zombie_b[ZH] = {
  "....GGGGGG....", "...GgggggggG..", "..GgggggggggG.", ".GgggwwwwgggG.", ".GggwwwwwwggG.",
  ".GggwwwkkwggG.", ".GggwwwkkwggG.", ".GggwwwwwwggG.", "..GgggggggkkG.", "...GGGGGGGGG..",
  "...SSSSSSSS...", "..gSSSSSSSSg..", "...SSSSSSSS...", "....PPPPPP....", "...kkkkkkkk..."
};
static const char *const zombie_j[ZH] = {
  "....GGGGGG....", "...GgggggggG..", "..GgggggggggG.", ".GgggwwwwgggG.", ".GggwwwwwwggG.",
  ".GggwwwkkwggG.", ".GggwwwkkwggG.", ".GggwwwwwwggG.", "..GgggggggkkG.", "...GGGGGGGGG..",
  "g..SSSSSSSS..g", "g..SSSSSSSS..g", "...SSSSSSSS...", "....PP..PP....", "...kkk..kkk..."
};

#define HW 10
#define HH 14
static const char *const human_a[HH] = {
  "...hhhh...", "..hhhhhh..", "..ffffff..", "..fkffkf..", "..ffffff..",
  "...ffff...", "..SSSSSS..", ".fSSSSSSf.", ".fSSSSSSf.", "..SSSSSS..",
  "..PPPPPP..", "..PP..PP..", "..PP..PP..", ".kkk..kkk."
};
static const char *const human_b[HH] = {
  "...hhhh...", "..hhhhhh..", "..ffffff..", "..fkffkf..", "..ffffff..",
  "...ffff...", "..SSSSSS..", ".fSSSSSSf.", ".fSSSSSSf.", "..SSSSSS..",
  "..PPPPPP..", "...PPPP...", "...PPPP...", "..kkkkkk.."
};
static const char *const human_p[HH] = {  /* panique : bras en l'air, bouche ouverte */
  "...hhhh...", "..hhhhhh..", "..ffffff..", "..fwfwff..", "..ffffff..",
  "...fkkf...", "f.SSSSSS.f", "f.SSSSSS.f", "..SSSSSS..", "..SSSSSS..",
  "..PPPPPP..", "..PP..PP..", "..PP..PP..", ".kkk..kkk."
};

#define BW 12
#define BH 10
static const char *const brain_spr[BH] = {
  "...pppppp...", "..pqppqppp..", ".ppqpppqppp.", "pppqppqpppqp", "pqppppqpppqp",
  "pppqppqpppqp", ".ppqpppqppp.", "..ppqppqpp..", "...pppqpp...", "....pppp...."
};

static uint16_t pal(char c, uint16_t shirt, uint16_t skin) {
  switch (c) {
    case 'G': return (uint16_t)((skin >> 1) & 0x7BEF);
    case 'g': return skin;
    case 'w': return RGB(250, 250, 250);
    case 'k': return RGB(20, 20, 30);
    case 'S': return shirt;
    case 'P': return RGB(84, 66, 52);
    case 'm': return RGB(130, 24, 34);
    case 'f': return RGB(240, 190, 150);
    case 'h': return RGB(110, 70, 30);
    case 'p': return RGB(246, 150, 176);
    case 'q': return RGB(198, 86, 120);
    case 'y': return RGB(255, 210, 40);
    case 'o': return RGB(255, 140, 30);
    default: return 0;
  }
}

/* ------------------------------------------------------------------ */
/* Etat du jeu                                                         */
/* ------------------------------------------------------------------ */
enum { ST_MENU, ST_PLAY, ST_PAUSE, ST_OVER, ST_MISSIONS, ST_RECORDS };

enum { E_NONE, E_HUMAN, E_COIN, E_HOLE, E_BARREL, E_CRATE, E_SPIKES, E_DUMP, E_CAR };

typedef struct {
  uint8_t type;
  uint8_t hp;     /* points de vie de l'obstacle */
  uint8_t aux;    /* voiture: zombies requis / humain: couleur */
  uint8_t w, h;
  int16_t y;      /* hauteur au-dessus du sol (humains / pieces) */
  int32_t x;      /* x monde (pixels) */
} Ent;

enum { P_BIT, P_FALL, P_TXT };

typedef struct {
  uint8_t kind;
  uint8_t life;
  uint16_t color;
  int32_t x, y, vx, vy;  /* ecran, en 1/16 px */
  char t[4];
} Part;

static Ent ents[NENT];
static Part parts[NPART];

static uint8_t state;
static uint32_t frame;
static int32_t cam16;           /* defilement en 1/16 px */
static int speed16;
static int32_t spawn_x;

/* horde */
static uint8_t alive[MAXV];
static int8_t slot_wait[MAXV];  /* >0: zombie en route, 0: vivant/vide selon alive, -1: vide */
static int reserve_cnt;         /* zombies caches au-dela des MAXV places */
static int slot_dx[MAXV];
static int slot_row[MAXV];
static int slot_jit[MAXV];
static uint16_t slot_shirt[MAXV];
static uint16_t slot_skin[MAXV];
static uint8_t slot_hat[MAXV];

/* saut */
static int jy16, jv16, jump_buf;
static int16_t hist[HIST];
static int hist_head;

/* score */
static int bonus, coins, converted, best;
static int hurt_flash;
static int over_t;
static int sel;                 /* bouton selectionne (menu / pause / fin) */
static int score_disp;          /* score affiche (anime) */
static int games_played;
static int best_conv, best_coins, best_dist_m, best_horde, run_horde;
static int tot_conv, tot_coins;
static bool new_record;
static bool k_ok, k_ok_prev, k_up, k_up_prev, k_down, k_down_prev;
static bool k_jump, k_jump_prev, k_back, k_back_prev;

static const uint16_t skin_tab[4] = {
  RGB(124, 196, 60), RGB(98, 190, 138), RGB(172, 204, 72), RGB(138, 176, 96)
};
static const uint16_t shirt_tab[6] = {
  RGB(60, 90, 200), RGB(200, 60, 60), RGB(230, 170, 40), RGB(150, 70, 170), RGB(40, 150, 150), RGB(220, 120, 160)
};

/* ------------------------------------------------------------------ */
/* Dessin dans la bande courante                                        */
/* ------------------------------------------------------------------ */
static uint16_t strip[SW * STRIP_H];
static int strip_y;

static void fill(int x, int y, int w, int h, uint16_t c) {
  int x0 = x < 0 ? 0 : x;
  int x1 = (x + w > SW) ? SW : x + w;
  int y0 = y < strip_y ? strip_y : y;
  int y1 = (y + h > strip_y + STRIP_H) ? strip_y + STRIP_H : y + h;
  if (x0 >= x1 || y0 >= y1) return;
  for (int yy = y0; yy < y1; yy++) {
    uint16_t *p = &strip[(yy - strip_y) * SW + x0];
    for (int xx = x0; xx < x1; xx++) *p++ = c;
  }
}

static void spr(const char *const *rows, int w, int h, int x, int y, uint16_t shirt, uint16_t skin, bool flip) {
  if (y >= strip_y + STRIP_H || y + h <= strip_y || x >= SW || x + w <= 0) return;
  for (int r = 0; r < h; r++) {
    int yy = y + r;
    if (yy < strip_y || yy >= strip_y + STRIP_H) continue;
    const char *row = rows[r];
    uint16_t *line = &strip[(yy - strip_y) * SW];
    for (int c = 0; c < w; c++) {
      char ch = row[flip ? (w - 1 - c) : c];
      if (ch == '.') continue;
      int xx = x + c;
      if (xx < 0 || xx >= SW) continue;
      line[xx] = pal(ch, shirt, skin);
    }
  }
}

static void text(int x, int y, const char *s, int sc, uint16_t col) {
  if (y >= strip_y + STRIP_H || y + 7 * sc <= strip_y) return;
  for (; *s; s++) {
    int ch = (unsigned char)*s;
    if (ch >= 'a' && ch <= 'z') ch -= 32;
    if (ch < 32 || ch > 90) ch = 32;
    const uint8_t *g = font5x7[ch - 32];
    if (x < SW && x + 5 * sc > 0) {
      for (int cx = 0; cx < 5; cx++) {
        uint8_t b = g[cx];
        if (!b) continue;
        for (int ry = 0; ry < 7; ry++)
          if ((b >> ry) & 1) fill(x + cx * sc, y + ry * sc, sc, sc, col);
      }
    }
    x += 6 * sc;
  }
}

static int text_w(const char *s, int sc) {
  int n = 0;
  while (s[n]) n++;
  return n ? n * 6 * sc - sc : 0;
}

static void text_sh(int x, int y, const char *s, int sc, uint16_t col, uint16_t shadow) {
  int o = sc > 2 ? 2 : 1;  /* ombre fine, meme pour les gros titres */
  text(x + o, y + o, s, sc, shadow);
  text(x, y, s, sc, col);
}

static void text_center(int y, const char *s, int sc, uint16_t col, uint16_t shadow) {
  text_sh((SW - text_w(s, sc)) / 2, y, s, sc, col, shadow);
}

static void text_ol(int x, int y, const char *s, int sc, uint16_t col, uint16_t ol) {
  int o = sc >= 3 ? 2 : 1;
  for (int dy = -o; dy <= o; dy += o)
    for (int dx = -o; dx <= o; dx += o)
      if (dx || dy) text(x + dx, y + dy, s, sc, ol);
  text(x, y, s, sc, col);
}

static void text_ol_center(int y, const char *s, int sc, uint16_t col, uint16_t ol) {
  text_ol((SW - text_w(s, sc)) / 2, y, s, sc, col, ol);
}

/* parallelogramme (lignes r0..r1), pente = decalage horizontal du haut */
static void para_rows(int x, int y, int w, int h, int slant, uint16_t c, int r0, int r1) {
  for (int r = r0; r < r1; r++) {
    int off = h > 1 ? slant * (h - 1 - r) / (h - 1) : 0;
    fill(x + off, y + r, w, 1, c);
  }
}

static void button(int x, int y, int w, int h, const char *label, int sc, uint16_t c1, uint16_t c2, uint16_t txt, bool selected) {
  int sl = h / 3;
  if (y >= strip_y + STRIP_H || y + h <= strip_y) {
    if (selected ? (y - 4 >= strip_y + STRIP_H || y + h + 4 <= strip_y) : true) return;
  }
  uint16_t edge = selected ? RGB(255, 255, 255) : RGB(30, 12, 40);
  int e = selected ? 3 : 2;
  for (int k = 1; k <= e; k++) {
    para_rows(x - k, y - k, w + 2 * k, h + 2 * k, sl + k, edge, 0, h + 2 * k);
  }
  para_rows(x, y, w, h, sl, c2, 0, h);
  para_rows(x, y, w, h, sl, c1, 0, h / 2);
  int tw = text_w(label, sc);
  text_ol(x + (w + sl - tw) / 2, y + (h - 7 * sc) / 2, label, sc, txt, RGB(20, 10, 30));
}

static char *itoa_u(int v, char *buf) {  /* buf >= 12 octets, retourne le debut */
  char *p = buf + 11;
  *p = 0;
  if (v < 0) v = 0;
  do {
    *--p = (char)('0' + v % 10);
    v /= 10;
  } while (v > 0);
  return p;
}

/* ------------------------------------------------------------------ */
/* Decor                                                               */
/* ------------------------------------------------------------------ */
static void draw_buildings(int32_t cam_px, int div, int cell, uint16_t col, uint16_t win, bool windows, int minh, int varh, uint32_t seed) {
  int32_t off = cam_px / div;
  int c0 = (int)(off / cell);
  int c1 = (int)((off + SW) / cell) + 1;
  for (int c = c0 - 1; c <= c1; c++) {
    uint32_t h = hash32((uint32_t)c * 2654435761U + seed);
    int bw = cell * 2 / 3 + (int)(h & 7);
    int bh = minh + (int)((h >> 4) % (uint32_t)varh);
    int bx = c * cell + (int)((h >> 12) % (uint32_t)(cell / 3)) - (int)off;
    int by = GROUND_Y - bh;
    fill(bx, by, bw, bh, col);
    if (h & 0x100000) fill(bx + bw / 2 - 1, by - 14, 2, 14, col);  /* antenne */
    if (windows) {
      for (int wy = by + 6; wy < GROUND_Y - 8; wy += 9)
        for (int wx = bx + 4; wx < bx + bw - 5; wx += 8)
          if ((hash32((uint32_t)(wx + off) * 31U + (uint32_t)wy * 17U + seed) & 3U) != 0) fill(wx, wy, 4, 5, win);
    }
  }
}

/* themes : jour -> crepuscule -> nuit (transition progressive).
 * 6 couleurs x RGB : ciel haut, ciel bas, immeubles loin, immeubles pres, fenetres, nuages */
static const uint8_t TH[3][18] = {
  {88,160,226, 214,230,242, 132,152,188, 92,104,140, 150,164,196, 245,250,255},
  {110,70,150, 252,176,110, 150,104,140, 100,66,100, 255,206,120, 255,196,176},
  {12,16,44,   62,66,112,   42,50,90,    28,32,66,   255,230,140, 74,80,126}
};
static int thc[18];

static void theme_update(int32_t dist_px) {
  int a = (int)((dist_px / 3000) % 3);
  int b = (a + 1) % 3;
  int u = (int)(dist_px % 3000) - 2600;
  u = u < 0 ? 0 : (u > 400 ? 400 : u);
  u = u * 256 / 400;
  for (int k = 0; k < 18; k++) thc[k] = TH[a][k] + ((int)TH[b][k] - (int)TH[a][k]) * u / 256;
}

static uint16_t thcol(int k) { return RGB(thc[k * 3], thc[k * 3 + 1], thc[k * 3 + 2]); }

static void draw_background(int32_t cam_px) {
  /* ciel en degrade */
  for (int y = 0; y < GROUND_Y; y += 8) {
    int t = y * 256 / GROUND_Y;
    fill(0, y, SW, 8, RGB(thc[0] + (thc[3] - thc[0]) * t / 256, thc[1] + (thc[4] - thc[1]) * t / 256, thc[2] + (thc[5] - thc[2]) * t / 256));
  }
  /* etoiles (visibles quand le ciel est sombre) */
  if (thc[0] + thc[1] + thc[2] < 150) {
    for (int c = 0; c < 26; c++) {
      uint32_t h = hash32((uint32_t)c * 7919U);
      fill((int)(h % SW), (int)((h >> 9) % 110U), 1, 1, RGB(230, 230, 255));
    }
  }
  /* nuages */
  {
    int32_t off = cam_px / 10;
    int c0 = (int)(off / 150);
    uint16_t cc = thcol(5);
    for (int c = c0 - 1; c <= c0 + 3; c++) {
      uint32_t h = hash32((uint32_t)c + 77U);
      if (h & 1U) {
        int cx = c * 150 + (int)((h >> 3) % 60U) - (int)off;
        int cy = 18 + (int)((h >> 9) % 50U);
        fill(cx + 6, cy, 30, 8, cc);
        fill(cx, cy + 5, 44, 8, cc);
        fill(cx + 12, cy - 4, 18, 6, cc);
      }
    }
  }
  draw_buildings(cam_px, 5, 46, thcol(2), 0, false, 40, 70, 11U);
  draw_buildings(cam_px, 3, 40, thcol(3), thcol(4), true, 36, 70, 5U);
  draw_buildings(cam_px, 2, 52, thcol(3), thcol(4), true, 20, 40, 23U);
  /* lampadaires */
  {
    int c0 = (int)(cam_px / 150);
    for (int c = c0 - 1; c <= c0 + SW / 150 + 1; c++) {
      int lx = c * 150 + 40 - (int)cam_px;
      fill(lx, GROUND_Y - 44, 3, 44, RGB(46, 48, 60));
      fill(lx, GROUND_Y - 44, 14, 3, RGB(46, 48, 60));
      fill(lx + 10, GROUND_Y - 41, 6, 3, RGB(255, 235, 150));
    }
  }
  /* route */
  fill(0, GROUND_Y, SW, 16, RGB(70, 72, 84));
  fill(0, GROUND_Y, SW, 2, RGB(140, 142, 156));
  {
    int base = -(int)(cam_px % 36);
    for (int x = base; x < SW; x += 36) fill(x, GROUND_Y + 9, 18, 2, RGB(230, 230, 210));
    int c0 = (int)(cam_px / 48);
    for (int c = c0 - 1; c <= c0 + SW / 48 + 1; c++) {  /* fissures */
      uint32_t h = hash32((uint32_t)c * 31U + 5U);
      if ((h & 3U) == 0) {
        int fx = c * 48 + (int)((h >> 4) % 30U) - (int)cam_px;
        fill(fx, GROUND_Y + 4 + (int)((h >> 12) % 3U), 5, 1, RGB(48, 50, 60));
        fill(fx + 4, GROUND_Y + 5 + (int)((h >> 12) % 3U), 4, 1, RGB(48, 50, 60));
      }
    }
  }
  /* terre */
  fill(0, GROUND_Y + 16, SW, SH - GROUND_Y - 16, RGB(96, 62, 42));
  fill(0, GROUND_Y + 16, SW, 3, RGB(60, 40, 30));
  {
    int c0 = (int)(cam_px / 30);
    for (int r = 0; r < 3; r++) {
      for (int c = c0 - 1; c <= c0 + SW / 30 + 1; c++) {
        uint32_t h = hash32((uint32_t)c * 97U + (uint32_t)r * 1013U);
        int sx = c * 30 + (int)((h >> 8) % 14U) - (int)cam_px;
        if ((h % 3U) == 0) fill(sx, GROUND_Y + 26 + r * 14, 8 + (int)((h >> 16) % 6U), 4, RGB(74, 48, 34));
        if ((h % 5U) == 1) {  /* fissure en zigzag */
          int fy = GROUND_Y + 22 + r * 14;
          fill(sx, fy, 2, 3, RGB(58, 36, 26));
          fill(sx + 2, fy + 3, 2, 3, RGB(58, 36, 26));
          fill(sx, fy + 6, 2, 3, RGB(58, 36, 26));
        }
      }
    }
  }
}

/* ------------------------------------------------------------------ */
/* Entites                                                             */
/* ------------------------------------------------------------------ */
static Ent *ent_alloc(void) {
  for (int i = 0; i < NENT; i++)
    if (ents[i].type == E_NONE) return &ents[i];
  return 0;
}

static int ents_free(void) {
  int n = 0;
  for (int i = 0; i < NENT; i++)
    if (ents[i].type == E_NONE) n++;
  return n;
}

static Ent *ent_add(int type, int32_t x, int w, int h, int y, int hp, int aux) {
  Ent *e = ent_alloc();
  if (!e) return 0;
  e->type = (uint8_t)type;
  e->x = x;
  e->w = (uint8_t)w;
  e->h = (uint8_t)h;
  e->y = (int16_t)y;
  e->hp = (uint8_t)hp;
  e->aux = (uint8_t)aux;
  return e;
}

static int total_zombies(void) {
  int n = reserve_cnt;
  for (int i = 0; i < MAXV; i++)
    if (alive[i] || slot_wait[i] > 0) n++;
  return n;
}

static int car_need_now(void);

static void draw_entities(int32_t cam_px) {
  for (int i = 0; i < NENT; i++) {
    const Ent *e = &ents[i];
    if (e->type == E_NONE) continue;
    int sx = (int)(e->x - cam_px);
    if (sx > SW + 8 || sx + e->w < -8) continue;
    switch (e->type) {
      case E_HOLE:
        fill(sx, GROUND_Y, e->w, SH - GROUND_Y, RGB(18, 8, 24));
        fill(sx, GROUND_Y + 16, e->w, 3, RGB(8, 2, 12));
        fill(sx - 2, GROUND_Y, 2, 16, RGB(40, 40, 50));
        fill(sx + e->w, GROUND_Y, 2, 16, RGB(40, 40, 50));
        break;
      case E_HUMAN: {
        int top = BASE_Y - e->y - HH + 2;
        if (e->y > 0) {
          fill(sx - 8, BASE_Y - e->y + 2, 26, 4, RGB(150, 150, 165));
          fill(sx - 4, BASE_Y - e->y + 6, 3, e->y - 6 + 4, RGB(110, 110, 125));
          fill(sx + 11, BASE_Y - e->y + 6, 3, e->y - 6 + 4, RGB(110, 110, 125));
        } else {
          fill(sx + 1, BASE_Y + 1, 9, 2, RGB(40, 40, 52));
        }
        {
          const char *const *hs = (sx < HX + 140) ? human_p : (((frame / 5 + (uint32_t)i) & 1U) ? human_a : human_b);
          spr(hs, HW, HH, sx, top, shirt_tab[e->aux % 6], 0, false);
        }
        break;
      }
      case E_COIN: {
        int cy = BASE_Y - e->y - 8;
        fill(sx + 2, cy, 4, 8, RGB(255, 205, 30));
        fill(sx, cy + 2, 8, 4, RGB(255, 205, 30));
        fill(sx + 3, cy + 2, 2, 4, RGB(255, 240, 150));
        break;
      }
      case E_BARREL:
        fill(sx, BASE_Y - e->h + 2, e->w, e->h, RGB(150, 90, 40));
        fill(sx, BASE_Y - e->h + 6, e->w, 2, RGB(70, 70, 76));
        fill(sx, BASE_Y - 4, e->w, 2, RGB(70, 70, 76));
        fill(sx + 3, BASE_Y - e->h + 2, 2, e->h, RGB(185, 120, 60));
        break;
      case E_CRATE:
        fill(sx, BASE_Y - e->h + 2, e->w, e->h, RGB(190, 140, 70));
        fill(sx, BASE_Y - e->h + 2, e->w, 2, RGB(110, 76, 36));
        fill(sx, BASE_Y, e->w, 2, RGB(110, 76, 36));
        fill(sx, BASE_Y - e->h + 2, 2, e->h, RGB(110, 76, 36));
        fill(sx + e->w - 2, BASE_Y - e->h + 2, 2, e->h, RGB(110, 76, 36));
        for (int d = 2; d < e->w - 2 && d < e->h - 2; d++) fill(sx + d, BASE_Y - e->h + 2 + d, 2, 2, RGB(130, 92, 46));
        break;
      case E_DUMP:
        fill(sx, BASE_Y - e->h + 2, e->w, e->h, RGB(58, 112, 86));
        fill(sx - 1, BASE_Y - e->h + 2, e->w + 2, 4, RGB(36, 78, 60));
        fill(sx + 3, BASE_Y - e->h + 8, e->w - 6, 2, RGB(40, 90, 66));
        fill(sx + 3, BASE_Y - e->h + 13, e->w - 6, 2, RGB(40, 90, 66));
        fill(sx + 2, BASE_Y, 4, 2, RGB(20, 20, 26));
        fill(sx + e->w - 6, BASE_Y, 4, 2, RGB(20, 20, 26));
        break;
      case E_SPIKES:
        fill(sx, BASE_Y - 0, e->w, 2, RGB(90, 90, 100));
        for (int k = 0; k + 6 <= e->w; k += 6) {
          fill(sx + k + 2, BASE_Y - 8, 2, 8, RGB(215, 215, 225));
          fill(sx + k + 1, BASE_Y - 4, 4, 4, RGB(190, 190, 205));
          fill(sx + k, BASE_Y - 1, 6, 1, RGB(150, 150, 165));
        }
        break;
      case E_CAR: {
        int top = BASE_Y + 2 - e->h;
        uint16_t body = (e->aux & 1) ? RGB(100, 134, 160) : RGB(174, 82, 66);
        fill(sx, top + 9, e->w, 10, body);
        fill(sx + 8, top, e->w - 18, 10, body);
        fill(sx + 11, top + 2, 10, 6, RGB(190, 225, 245));
        fill(sx + 23, top + 2, 10, 6, RGB(190, 225, 245));
        fill(sx + 25, top + 3, 6, 5, RGB(240, 190, 150));  /* conducteur terrorise */
        fill(sx + 27, top + 5, 2, 3, RGB(20, 20, 30));
        fill(sx + 24, top + 2, 8, 1, RGB(110, 70, 30));
        fill(sx, top + 15, e->w, 2, RGB(60, 60, 70));
        fill(sx + e->w - 4, top + 11, 4, 3, RGB(255, 235, 140));
        fill(sx + 5, top + 15, 9, 7, RGB(20, 20, 26));
        fill(sx + e->w - 15, top + 15, 9, 7, RGB(20, 20, 26));
        fill(sx + 8, top + 18, 3, 3, RGB(150, 150, 160));
        fill(sx + e->w - 12, top + 18, 3, 3, RGB(150, 150, 160));
        /* bulle "zombies requis" */
        {
          char nb[12], nb2[12], rb[28];
          int have = imin(total_zombies(), 99);
          char *a = itoa_u(have, nb), *b = itoa_u(e->aux, nb2);
          int n = 0;
          while (*a) rb[n++] = *a++;
          rb[n++] = '/';
          while (*b) rb[n++] = *b++;
          rb[n] = 0;
          int enough = have >= (int)e->aux;
          int bw = text_w(rb, 2) + 10;
          fill(sx + 22 - bw / 2, top - 24, bw, 18, RGB(20, 20, 28));
          fill(sx + 18, top - 7, 6, 4, RGB(20, 20, 28));
          text(sx + 27 - bw / 2, top - 20, rb, 2, enough ? RGB(120, 255, 90) : RGB(255, 140, 50));
        }
        break;
      }
      default: break;
    }
  }
}

/* ------------------------------------------------------------------ */
/* Particules                                                          */
/* ------------------------------------------------------------------ */
static void part_add(int kind, int px, int py, int vx16, int vy16, uint16_t col, int life, const char *t) {
  for (int i = 0; i < NPART; i++) {
    if (parts[i].life == 0) {
      Part *p = &parts[i];
      p->kind = (uint8_t)kind;
      p->x = px * 16;
      p->y = py * 16;
      p->vx = vx16;
      p->vy = vy16;
      p->color = col;
      p->life = (uint8_t)life;
      p->t[0] = p->t[1] = p->t[2] = p->t[3] = 0;
      if (t) {
        for (int k = 0; k < 3 && t[k]; k++) p->t[k] = t[k];
      }
      return;
    }
  }
}

static void burst(int px, int py, uint16_t c1, uint16_t c2, int n) {
  for (int i = 0; i < n; i++) {
    int vx = (int)(rnd() % 80U) - 40;
    int vy = -(int)(rnd() % 90U) - 20;
    part_add(P_BIT, px, py, vx, vy, (i & 1) ? c1 : c2, 14 + (int)(rnd() % 10U), 0);
  }
}

static void update_particles(void) {
  for (int i = 0; i < NPART; i++) {
    Part *p = &parts[i];
    if (!p->life) continue;
    p->life--;
    p->x += p->vx - speed16;
    p->y += p->vy;
    if (p->kind == P_FALL) p->vy += 6;
    else if (p->kind == P_BIT) p->vy += 8;
    if ((p->x >> 4) < -30 || (p->y >> 4) > SH + 20) p->life = 0;
  }
}

static void draw_particles(void) {
  for (int i = 0; i < NPART; i++) {
    const Part *p = &parts[i];
    if (!p->life) continue;
    int x = p->x >> 4, y = p->y >> 4;
    if (p->kind == P_BIT) fill(x, y, 2, 2, p->color);
    else if (p->kind == P_FALL) spr(zombie_j, ZW, ZH, x, y, p->color, RGB(124, 196, 60), false);
    else text_sh(x, y, p->t, 1, p->color, RGB(20, 20, 28));
  }
}

/* ------------------------------------------------------------------ */
/* Horde                                                               */
/* ------------------------------------------------------------------ */
static int slot_jy(int i) {
  int d = slot_dx[i] * 16 / speed16 + slot_jit[i];
  if (d < 0) d = 0;
  if (d > HIST - 1) d = HIST - 1;
  return hist[(hist_head - 1 - d) & (HIST - 1)];
}

static void zombie_join(int px, int py) {
  if (total_zombies() >= MAXTOTAL) return;
  for (int i = 0; i < MAXV; i++) {
    if (!alive[i] && slot_wait[i] < 0) {
      alive[i] = 1;
      return;
    }
  }
  reserve_cnt++;
  (void)px;
  (void)py;
}

static void zombie_die(int i) {
  alive[i] = 0;
  /* Les 15 zombies visibles representent toute la horde : quand l'un d'eux
   * tombe, une part proportionnelle des zombies caches disparait avec lui.
   * Rater un trou ou un obstacle coute donc d'autant plus cher que la horde
   * est grosse (effet "tsunami"). */
  {
    int na = 0;
    for (int k = 0; k < MAXV; k++) na += alive[k];
    (void)na;
    reserve_cnt -= (reserve_cnt * 6 + 99) / 100;
  }
  if (reserve_cnt > 0) {
    reserve_cnt--;
    slot_wait[i] = 24;  /* un zombie du fond vient combler la place */
  } else {
    slot_wait[i] = -1;
  }
}

static void draw_hat(int kind, int x, int y) {
  switch (kind) {
    case 1: fill(x + 4, y - 1, 7, 3, RGB(210, 50, 50)); fill(x + 9, y + 2, 5, 1, RGB(160, 30, 30)); break;  /* casquette */
    case 2: fill(x + 6, y - 3, 2, 3, RGB(70, 50, 30)); fill(x + 8, y - 2, 2, 2, RGB(70, 50, 30)); break;      /* meche */
    case 3: fill(x + 2, y + 2, 11, 1, RGB(240, 200, 40)); break;                                              /* bandeau */
    default: break;
  }
}

static void draw_horde(void) {
  for (int row = 0; row < 3; row++) {
    for (int i = 0; i < MAXV; i++) {
      if (slot_row[i] != row || !alive[i]) continue;
      int jy = slot_jy(i) >> 4;
      int x = HX - slot_dx[i];
      int feet = GROUND_Y + 5 + row * 4;
      int w = 12 - imin(jy / 8, 6);
      fill(x + 1 + (12 - w) / 2, feet - 1, w, 2, RGB(36, 38, 48));
      const char *const *f;
      if (jy > 2) f = zombie_j;
      else f = (((frame / 3) + (uint32_t)i) & 1U) ? zombie_a : zombie_b;
      int top = feet - ZH - jy;
      spr(f, ZW, ZH, x, top, slot_shirt[i], slot_skin[i], false);
      draw_hat(slot_hat[i], x, top);
    }
  }
}

/* ------------------------------------------------------------------ */
/* Generation du niveau                                                */
/* ------------------------------------------------------------------ */
static int car_need_now(void) {
  int n = 2 + (int)(rnd() % 3U) + (int)((cam16 >> 4) / 1500);
  return imin(n, 30);
}

static void spawn_humans(int32_t x, int n) {
  for (int k = 0; k < n; k++) ent_add(E_HUMAN, x + k * 22, HW, HH, 0, 1, (int)(rnd() % 6U));
}

static void spawn_arc(int32_t x, int n, int spacing, int base_h) {
  for (int k = 0; k < n; k++) {
    int hgt = base_h + ((k == 0 || k == n - 1) ? 0 : 12);
    ent_add(E_COIN, x + k * spacing, 8, 8, hgt, 1, 0);
  }
}

static void spawn_pattern(void) {
  int32_t dist = cam16 >> 4;
  bool easy = dist < 700;
  int32_t x0 = spawn_x;
  int32_t end = x0;
  int r = (int)(rnd() % 100U);
  if (easy && r >= 55) r = (int)(rnd() % 55U);

  if (r < 24) {  /* groupe d'humains */
    int n = 2 + (int)(rnd() % 3U);
    spawn_humans(x0, n);
    end = x0 + n * 22;
  } else if (r < 44) {  /* trou */
    int w = 30 + (int)(rnd() % 16U) + (speed16 - SPEED_MIN) / 2;
    if (easy) w = imin(w, 40);
    ent_add(E_HOLE, x0, w, 0, 0, 1, 0);
    spawn_arc(x0 + w / 4, 3, w / 4, 34);
    end = x0 + w;
    if (rnd() % 100U < 50U) {
      spawn_humans(end + 40, 2);
      end += 40 + 44;
    }
  } else if (r < 60) {  /* tonneaux / caisses */
    int n = 1 + (int)(rnd() % 2U);
    int32_t x = x0;
    if (!easy && (rnd() % 3U) == 0) {
      ent_add(E_DUMP, x, 30, 22, 0, 4, 0);
      x += 30;
    } else {
      for (int k = 0; k < n; k++) {
        if (rnd() & 1U) { ent_add(E_BARREL, x, 14, 16, 0, 2, 0); x += 14 + 3; }
        else { ent_add(E_CRATE, x, 18, 18, 0, 3, 0); x += 18 + 3; }
      }
    }
    spawn_arc(x0, 3, 12, 40);
    end = x;
  } else if (r < 70) {  /* pointes */
    int n = 1 + (int)(rnd() % 2U);
    ent_add(E_SPIKES, x0, 12 * n + 6, 10, 0, 3, 0);
    end = x0 + 12 * n + 6;
  } else if (r < 84) {  /* voiture avec passagers */
    int need = car_need_now();
    if (easy) need = 2;
    ent_add(E_CAR, x0, 44, 22, 0, need, need);
    spawn_humans(x0 + 44 + 30, 2);
    end = x0 + 44 + 30 + 44;
  } else if (r < 92 && !easy) {  /* combo : trou puis obstacle juste apres l'atterrissage */
    int w = 30 + (int)(rnd() % 12U) + (speed16 - SPEED_MIN) / 3;
    ent_add(E_HOLE, x0, w, 0, 0, 1, 0);
    int32_t bx = x0 + w + 46 + speed16 / 2;
    if (rnd() & 1U) ent_add(E_BARREL, bx, 14, 16, 0, 2, 0);
    else ent_add(E_SPIKES, bx, 18, 10, 0, 3, 0);
    spawn_humans(bx + 60, 2);
    end = bx + 60 + 44;
  } else if (r < 96 && !easy) {  /* deux obstacles espaces : un seul grand saut */
    ent_add(E_CRATE, x0, 18, 18, 0, 3, 0);
    ent_add(E_BARREL, x0 + 18 + 26 + speed16 / 2, 14, 16, 0, 2, 0);
    spawn_arc(x0 + 10, 4, 14, 44);
    end = x0 + 18 + 26 + speed16 / 2 + 14;
  } else {  /* humains en hauteur + pieces */
    int n = 1 + (int)(rnd() % 2U);
    for (int k = 0; k < n; k++) ent_add(E_HUMAN, x0 + 12 + k * 34, HW, HH, 38 + (int)(rnd() % 8U), 1, (int)(rnd() % 6U));
    spawn_arc(x0 + 80, 4, 12, 24);
    end = x0 + 12 + n * 34 + 20;
  }
  spawn_x = end + 40 + speed16 * 2 + (int32_t)(rnd() % 70U);
}

static void spawn_level(void) {
  int guard = 0;
  while (spawn_x < (cam16 >> 4) + SW + 120 && guard++ < 6) {
    if (ents_free() < 10) break;
    spawn_pattern();
  }
}

/* ------------------------------------------------------------------ */
/* Initialisation                                                      */
/* ------------------------------------------------------------------ */
static void clear_world(void) {
  for (int i = 0; i < NENT; i++) ents[i].type = E_NONE;
  for (int i = 0; i < NPART; i++) parts[i].life = 0;
  for (int i = 0; i < HIST; i++) hist[i] = 0;
  hist_head = 0;
  cam16 = 0;
  speed16 = SPEED_MIN;
  jy16 = jv16 = jump_buf = 0;
  bonus = coins = converted = 0;
  hurt_flash = 0;
  reserve_cnt = 0;
  for (int i = 0; i < MAXV; i++) {
    alive[i] = 0;
    slot_wait[i] = -1;
  }
}

static void init_slots(void) {
  for (int i = 0; i < MAXV; i++) {
    int col = i % 5, row = i / 5;
    uint32_t h = hash32((uint32_t)i + 1234U);
    slot_row[i] = row;
    slot_dx[i] = col * 10 + row * 5 + (int)(h & 3U);
    slot_jit[i] = (int)((h >> 4) % 3U);
    slot_shirt[i] = shirt_tab[(h >> 8) % 6U];
    slot_skin[i] = skin_tab[(h >> 12) % 4U];
    slot_hat[i] = (uint8_t)((h >> 16) % 4U);
  }
}

static void start_game(void) {
  clear_world();
  for (int i = 0; i < 3; i++) alive[i] = 1;
  spawn_x = SW + 160;
  score_disp = 0;
  run_horde = 3;
  new_record = false;
  games_played++;
  sel = 0;
  state = ST_PLAY;
}

static void start_title(void) {
  clear_world();
  for (int i = 0; i < 7; i++) alive[i] = 1;
  sel = 0;
  state = ST_MENU;
}

/* ------------------------------------------------------------------ */
/* Mise a jour                                                         */
/* ------------------------------------------------------------------ */
static int score_now(void) { return (int)((cam16 >> 4) / 10) + bonus; }

static void update_horde_physics(void) {
  if (k_jump && !k_jump_prev) jump_buf = 7;
  if (jy16 == 0 && jv16 == 0 && jump_buf > 0) {
    jv16 = JUMP_V;
    jump_buf = 0;
  }
  if (jump_buf > 0) jump_buf--;
  if (jy16 > 0 || jv16 > 0) {
    jy16 += jv16;
    jv16 -= (jv16 > 0 && k_jump) ? GRAV_HOLD : GRAV;
    if (jy16 <= 0) {
      jy16 = 0;
      jv16 = 0;
    }
  }
  hist[hist_head & (HIST - 1)] = (int16_t)jy16;
  hist_head = (hist_head + 1) & (HIST - 1);
}

static void kill_zombie_effect(int i, bool fall) {
  int x = HX - slot_dx[i];
  int feet = GROUND_Y + 5 + slot_row[i] * 4;
  int jy = slot_jy(i) >> 4;
  if (fall) {
    part_add(P_FALL, x, feet - ZH - jy, 0, 16, slot_shirt[i], 40, 0);
  } else {
    burst(x + 7, feet - 8 - jy, RGB(124, 196, 60), RGB(130, 24, 34), 7);
  }
  hurt_flash = 6;
}

static void update_collisions(void) {
  int32_t cam_px = cam16 >> 4;
  for (int i = 0; i < MAXV; i++) {
    if (!alive[i]) {
      if (slot_wait[i] > 0 && --slot_wait[i] == 0) alive[i] = 1;
      continue;
    }
    int zx = HX - slot_dx[i] + ZW / 2;
    int jy = slot_jy(i) >> 4;
    for (int k = 0; k < NENT && alive[i]; k++) {
      Ent *e = &ents[k];
      if (e->type == E_NONE) continue;
      int ex = (int)(e->x - cam_px);
      if (ex > zx + 40 || ex + e->w < zx - 40) continue;
      switch (e->type) {
        case E_HUMAN:
          if (iabs(zx - (ex + 5)) < 9 && jy >= e->y - 10 && jy <= e->y + 12) {
            e->type = E_NONE;
            zombie_join(ex, 0);
            converted++;
            bonus += 10;
            burst(ex + 5, BASE_Y - e->y - 8, RGB(124, 196, 60), RGB(255, 150, 190), 8);
            part_add(P_TXT, ex, BASE_Y - e->y - 22, 0, -12, RGB(255, 235, 90), 22, "+1");
          }
          break;
        case E_COIN:
          if (iabs(zx - (ex + 4)) < 9 && jy >= e->y - 12 && jy <= e->y + 12) {
            e->type = E_NONE;
            coins++;
            bonus += 2;
            burst(ex + 4, BASE_Y - e->y - 4, RGB(255, 210, 40), RGB(255, 245, 160), 3);
          }
          break;
        case E_HOLE:
          if (jy < 3 && zx > ex + 4 && zx < ex + e->w - 4) {
            kill_zombie_effect(i, true);
            zombie_die(i);
          }
          break;
        case E_BARREL:
        case E_CRATE:
        case E_DUMP:
        case E_SPIKES:
          if (jy < e->h - 3 && zx > ex - 2 && zx < ex + e->w + 2) {
            kill_zombie_effect(i, false);
            zombie_die(i);
            if (e->hp > 0) e->hp--;
            if (e->hp == 0) {
              burst(ex + e->w / 2, BASE_Y - e->h / 2, e->type == E_DUMP ? RGB(58, 112, 86) : RGB(190, 140, 70), RGB(110, 76, 36), 10);
              e->type = E_NONE;
            }
          }
          break;
        case E_CAR:
          if (jy < e->h - 3 && zx > ex && zx < ex + e->w) {
            if (total_zombies() >= (int)e->aux) {
              burst(ex + e->w / 2, BASE_Y - e->h, RGB(180, 180, 195), RGB(255, 150, 40), 16);
              part_add(P_TXT, ex + 8, BASE_Y - e->h - 20, 0, -12, RGB(255, 235, 90), 26, "+25");
              e->type = E_NONE;
              bonus += 25;
              zombie_join(ex, 0);
            } else {
              kill_zombie_effect(i, false);
              zombie_die(i);
              if (e->hp > 0) e->hp--;
              if (e->hp == 0) {
                burst(ex + e->w / 2, BASE_Y - e->h, RGB(180, 180, 195), RGB(255, 150, 40), 16);
                e->type = E_NONE;
              }
            }
          }
          break;
        default: break;
      }
    }
  }
}

static void update_entities(void) {
  int32_t cam_px = cam16 >> 4;
  for (int i = 0; i < NENT; i++) {
    Ent *e = &ents[i];
    if (e->type == E_NONE) continue;
    if (e->type == E_HUMAN && e->y == 0) {  /* ils fuient, plus vite quand la horde approche */
      bool near = (int)(e->x - cam_px) < HX + 140;
      if (near || (frame & 1U)) e->x += 1;
    }
    if (e->x + e->w + 20 < cam_px) e->type = E_NONE;
  }
}

static int dist_m(void) { return (int)((cam16 >> 4) / 12); }

static void end_run(void) {
  int sc = score_now();
  new_record = sc > best;
  if (sc > best) best = sc;
  if (converted > best_conv) best_conv = converted;
  if (coins > best_coins) best_coins = coins;
  if (dist_m() > best_dist_m) best_dist_m = dist_m();
  if (run_horde > best_horde) best_horde = run_horde;
  tot_conv += converted;
  tot_coins += coins;
  over_t = 0;
  sel = 0;
  state = ST_OVER;
}

static void update_play(void) {
  frame++;
  speed16 = imin(SPEED_MIN + (int)((cam16 >> 4) / 60), SPEED_MAX);
  update_horde_physics();
  cam16 += speed16;
  spawn_level();
  update_entities();
  update_collisions();
  update_particles();
  if (hurt_flash > 0) hurt_flash--;
  {
    int sc = score_now();
    if (score_disp < sc) score_disp += (sc - score_disp + 7) / 8;
    else score_disp = sc;
    int t = total_zombies();
    if (t > run_horde) run_horde = t;
  }
  if (total_zombies() == 0) end_run();
}

static void update_menu(void) {
  frame++;
  speed16 = SPEED_MIN;
  cam16 += speed16;
  update_particles();
  jy16 = 0;
  if ((frame % 90U) > 60U) {
    int t = (int)((frame % 90U) - 60U);
    jy16 = t * (30 - t) * 2;
  }
  hist[hist_head & (HIST - 1)] = (int16_t)jy16;
  hist_head = (hist_head + 1) & (HIST - 1);
}

/* ------------------------------------------------------------------ */
/* Rendu des interfaces                                                */
/* ------------------------------------------------------------------ */
#define COL_ORANGE RGB(255, 160, 30)
#define COL_OUTLINE RGB(60, 22, 6)
#define COL_WHITE RGB(255, 255, 255)

static void draw_coin_icon(int x, int y) {
  fill(x + 2, y, 6, 10, RGB(255, 205, 30));
  fill(x, y + 2, 10, 6, RGB(255, 205, 30));
  fill(x + 3, y + 2, 4, 6, RGB(255, 240, 150));
  fill(x + 4, y + 3, 2, 4, RGB(235, 160, 20));
}

static void draw_brain_icon(int x, int y) { spr(brain_spr, BW, BH, x, y, 0, 0, false); }

static void draw_hud(void) {
  char nb[12];
  fill(4, 4, 72, 62, RGB(18, 22, 30));
  draw_coin_icon(9, 8);
  text_ol(26, 7, itoa_u(coins, nb), 2, COL_ORANGE, COL_OUTLINE);
  draw_brain_icon(8, 28);
  text_ol(26, 26, itoa_u(converted, nb), 2, COL_ORANGE, COL_OUTLINE);
  spr(hurt_flash ? zombie_j : zombie_a, ZW, ZH, 8, 46, hurt_flash ? RGB(255, 60, 60) : RGB(60, 90, 200), RGB(124, 196, 60), false);
  text_ol(26, 46, itoa_u(total_zombies(), nb), 2, hurt_flash ? RGB(255, 110, 80) : RGB(150, 255, 100), RGB(10, 40, 10));
  /* score au centre */
  {
    const char *sc = itoa_u(score_disp, nb);
    text_ol_center(5, "SCORE", 1, RGB(210, 225, 245), RGB(20, 24, 40));
    text_ol_center(15, sc, 2, COL_WHITE, RGB(20, 24, 40));
  }
  /* bouton pause */
  fill(SW - 32, 4, 28, 26, RGB(18, 22, 30));
  fill(SW - 24, 9, 6, 16, COL_WHITE);
  fill(SW - 14, 9, 6, 16, COL_WHITE);
}

static void slime_panel(int x, int y, int w, int h) {
  fill(x - 2, y - 2, w + 4, h + 4, RGB(20, 40, 14));
  fill(x, y, w, h, RGB(130, 214, 50));
  fill(x + 3, y + 3, w - 6, h - 6, RGB(172, 236, 80));
  for (int k = 6; k < w - 10; k += 22) fill(x + k, y + h - 2, 7, 5 + (k % 3) * 2, RGB(130, 214, 50));  /* gouttes */
}

static void dark_panel(int x, int y, int w, int h) {
  fill(x - 2, y - 2, w + 4, h + 4, RGB(20, 26, 20));
  fill(x, y, w, h, RGB(56, 140, 48));
  fill(x + 3, y + 3, w - 6, h - 6, RGB(26, 30, 40));
}

static void draw_menu(void) {
  char nb[12];
  /* logo */
  text_ol(10, 14, "ZOMBIE", 4, RGB(150, 255, 70), RGB(20, 60, 10));
  text_ol(10, 52, "TSUNAMI", 4, RGB(255, 190, 50), RGB(110, 30, 10));
  /* boutons */
  button(194, 36, 104, 30, "RECORDS", 2, RGB(214, 90, 220), RGB(150, 36, 160), COL_WHITE, sel == 2);
  button(194, 76, 104, 30, "MISSIONS", 2, RGB(214, 90, 220), RGB(150, 36, 160), COL_WHITE, sel == 1);
  button(164, 128, 134, 60, "JOUER", 4, RGB(255, 236, 70), RGB(255, 132, 20), RGB(255, 255, 255), sel == 0);
  /* meilleurs resultats */
  fill(0, 200, SW, 40, RGB(30, 20, 34));
  text_ol(8, 204, "MEILLEUR SCORE:", 1, COL_WHITE, RGB(10, 6, 12));
  draw_brain_icon(8, 218);
  text_ol(24, 218, itoa_u(best_conv, nb), 1, COL_ORANGE, COL_OUTLINE);
  draw_coin_icon(64, 217);
  text_ol(80, 218, itoa_u(best_coins, nb), 1, COL_ORANGE, COL_OUTLINE);
  text_ol(176, 208, "HAUT/BAS + OK", 1, RGB(210, 200, 220), RGB(10, 6, 12));
  text_ol(176, 222, "BACK : QUITTER", 1, RGB(210, 200, 220), RGB(10, 6, 12));
}

static void draw_pause(void) {
  dark_panel(80, 56, 160, 130);
  text_ol_center(66, "PAUSE", 3, COL_WHITE, RGB(20, 70, 20));
  button(100, 106, 112, 30, "REPRENDRE", 2, RGB(170, 240, 80), RGB(80, 176, 40), RGB(255, 255, 255), sel == 0);
  button(100, 148, 112, 30, "MENU", 2, RGB(214, 90, 220), RGB(150, 36, 160), COL_WHITE, sel == 1);
}

static void draw_hint(void) {
  if (games_played > 1 || frame > 240) return;
  slime_panel(60, 190, 200, 38);
  text_center(198, "OK / EXE : SAUTER", 1, RGB(20, 50, 10), RGB(190, 245, 110));
  text_center(212, "ATTRAPEZ LES HUMAINS !", 1, RGB(20, 50, 10), RGB(190, 245, 110));
}

static void draw_over(void) {
  char nb[12];
  fill(0, 0, SW, SH, RGB(54, 46, 82));
  draw_buildings(cam16 >> 4, 6, 44, RGB(70, 62, 104), 0, false, 50, 80, 3U);
  fill(0, GROUND_Y, SW, SH - GROUND_Y, RGB(38, 32, 58));
  /* haut : cerveaux convertis */
  text_ol(8, 6, "CERVEAUX CONVERTIS:", 1, COL_WHITE, RGB(10, 6, 12));
  {
    const char *cs = itoa_u(converted, nb);
    text_ol(8, 18, cs, 4, COL_ORANGE, COL_OUTLINE);
    draw_brain_icon(8 + text_w(cs, 4) + 12, 28);
  }
  /* etagere */
  fill(6, 52, 200, 108, RGB(112, 68, 38));
  fill(9, 55, 194, 102, RGB(30, 20, 20));
  for (int r = 0; r < 5; r++) {
    for (int c = 0; c < 10; c++) {
      int x = 9 + c * 19 + 1, y = 55 + r * 20 + 1;
      fill(x + 18, y - 1, 1, 21, RGB(112, 68, 38));
      if (r * 10 + c < converted) draw_brain_icon(x + 3, y + 5);
    }
    fill(9, 55 + r * 20 + 20, 194, 2, RGB(112, 68, 38));
  }
  /* pieces / score */
  text_ol(8, 168, "PIECES RECOLTEES:", 1, COL_WHITE, RGB(10, 6, 12));
  draw_coin_icon(8, 182);
  text_ol(24, 180, itoa_u(coins, nb), 2, COL_ORANGE, COL_OUTLINE);
  text_ol(8, 202, "SCORE", 1, RGB(210, 225, 245), RGB(10, 6, 12));
  text_ol(8, 212, itoa_u(score_now(), nb), 2, COL_WHITE, RGB(10, 6, 12));
  text_ol(110, 202, "RECORD", 1, RGB(210, 225, 245), RGB(10, 6, 12));
  text_ol(110, 212, itoa_u(best, nb), 2, new_record ? RGB(120, 255, 90) : COL_ORANGE, COL_OUTLINE);
  if (new_record && ((frame / 8U) & 1U)) text_ol(214, 8, "NOUVEAU RECORD !", 1, RGB(120, 255, 90), RGB(10, 40, 10));
  /* boutons */
  button(214, 60, 96, 32, "MENU", 2, RGB(214, 90, 220), RGB(150, 36, 160), COL_WHITE, sel == 1);
  button(204, 136, 104, 62, "REJOUER", 2, RGB(170, 240, 80), RGB(70, 170, 40), RGB(255, 255, 255), sel == 0);
}

static void draw_sub_screen(bool missions) {
  char nb[12], nb2[12];
  dark_panel(16, 14, 288, 212);
  text_ol_center(22, missions ? "MISSIONS" : "RECORDS", 3, COL_ORANGE, COL_OUTLINE);
  if (missions) {
    static const char *const lab[3] = {"CONVERTIR 40 HUMAINS", "RAMASSER 60 PIECES", "COURIR 1000 M"};
    int goal[3] = {40, 60, 1000};
    int val[3] = {tot_conv, tot_coins, best_dist_m};
    for (int k = 0; k < 3; k++) {
      int y = 66 + k * 46;
      int v = imin(val[k], goal[k]);
      text(32, y, lab[k], 1, COL_WHITE);
      fill(32, y + 14, 200, 12, RGB(10, 12, 16));
      fill(33, y + 15, 198 * v / goal[k], 10, v >= goal[k] ? RGB(120, 230, 70) : RGB(240, 150, 40));
      char *a = itoa_u(v, nb), *b = itoa_u(goal[k], nb2);
      char rb[28];
      int n = 0;
      while (*a) rb[n++] = *a++;
      rb[n++] = '/';
      while (*b) rb[n++] = *b++;
      rb[n] = 0;
      text(240, y + 16, rb, 1, COL_WHITE);
      if (v >= goal[k]) text(262, y, "OK !", 1, RGB(120, 255, 90));
    }
  } else {
    static const char *const lab[5] = {"MEILLEUR SCORE", "DISTANCE MAX (M)", "HORDE MAX", "HUMAINS CONVERTIS", "PIECES RAMASSEES"};
    int val[5] = {best, best_dist_m, best_horde, tot_conv, tot_coins};
    for (int k = 0; k < 5; k++) {
      int y = 66 + k * 28;
      text(32, y + 3, lab[k], 1, RGB(210, 225, 245));
      char *a = itoa_u(val[k], nb);
      text_ol(300 - 12 - text_w(a, 2), y, a, 2, COL_ORANGE, COL_OUTLINE);
      fill(32, y + 20, 256, 1, RGB(60, 90, 60));
    }
  }
  text_center(212, "OK / BACK : RETOUR", 1, RGB(200, 220, 200), RGB(10, 20, 10));
}

static void draw_scene(void) {
  if (state == ST_OVER) { draw_over(); return; }
  int32_t cam_px = cam16 >> 4;
  draw_background(cam_px);
  draw_entities(cam_px);
  draw_horde();
  draw_particles();
  if (state == ST_MENU) draw_menu();
  else if (state == ST_MISSIONS) draw_sub_screen(true);
  else if (state == ST_RECORDS) draw_sub_screen(false);
  else {
    draw_hud();
    if (state == ST_PLAY) draw_hint();
    if (state == ST_PAUSE) draw_pause();
  }
}

static void render(void) {
  theme_update(cam16 >> 4);
  eadk_display_wait_for_vblank();
  for (int s = 0; s < NSTRIPS; s++) {
    strip_y = s * STRIP_H;
    draw_scene();
    eadk_display_push_rect((eadk_rect_t){0, (uint16_t)strip_y, SW, STRIP_H}, strip);
  }
}

/* ------------------------------------------------------------------ */
/* Programme principal                                                 */
/* ------------------------------------------------------------------ */
int main(int argc, char *argv[]) {
  (void)argc;
  (void)argv;
  rng_state = (uint32_t)eadk_random() ^ (uint32_t)eadk_timing_millis() ^ 0x9E3779B9U;
  if (rng_state == 0) rng_state = 0x1234567U;
  best = best_conv = best_coins = best_dist_m = best_horde = run_horde = 0;
  tot_conv = tot_coins = games_played = score_disp = 0;
  new_record = false;
  frame = 0;
  over_t = 0;
  sel = 0;
  k_jump = k_jump_prev = k_back = k_back_prev = false;
  k_ok = k_ok_prev = k_up = k_up_prev = k_down = k_down_prev = false;
  init_slots();
  start_title();

  uint64_t frame_start = eadk_timing_millis();
  while (true) {
    eadk_keyboard_state_t ks = eadk_keyboard_scan();
    k_jump_prev = k_jump; k_back_prev = k_back; k_ok_prev = k_ok; k_up_prev = k_up; k_down_prev = k_down;
    k_ok = eadk_keyboard_key_down(ks, eadk_key_ok) || eadk_keyboard_key_down(ks, eadk_key_exe);
    k_up = eadk_keyboard_key_down(ks, eadk_key_up);
    k_down = eadk_keyboard_key_down(ks, eadk_key_down);
    k_jump = k_ok || k_up;
    k_back = eadk_keyboard_key_down(ks, eadk_key_back);
    bool ok_p = k_ok && !k_ok_prev;
    bool up_p = k_up && !k_up_prev;
    bool down_p = k_down && !k_down_prev;
    bool back_p = k_back && !k_back_prev;

    switch (state) {
      case ST_MENU:
        if (back_p) return 0;
        if (up_p) sel = (sel + 2) % NSEL;
        if (down_p) sel = (sel + 1) % NSEL;
        if (ok_p) {
          if (sel == 0) start_game();
          else state = sel == 1 ? ST_MISSIONS : ST_RECORDS;
        } else if (state == ST_MENU) update_menu();
        break;
      case ST_MISSIONS:
      case ST_RECORDS:
        frame++;
        update_menu();
        if (ok_p || back_p) { state = ST_MENU; }
        break;
      case ST_PLAY:
        if (back_p) { sel = 0; state = ST_PAUSE; }
        else update_play();
        break;
      case ST_PAUSE:
        if (up_p || down_p) sel = 1 - sel;
        if (back_p) state = ST_PLAY;
        else if (ok_p) {
          if (sel == 0) state = ST_PLAY;
          else start_title();
        }
        break;
      case ST_OVER:
        frame++;
        if (over_t < 1000) over_t++;
        update_particles();
        if (up_p || down_p) sel = 1 - sel;
        if (back_p && over_t > 10) start_title();
        else if (ok_p && over_t > 24) {
          if (sel == 0) start_game();
          else start_title();
        }
        break;
      default: start_title(); break;
    }

    render();

    uint64_t now = eadk_timing_millis();
    uint64_t target = frame_start + FRAME_MS;
    if (now < target) {
      eadk_timing_msleep((uint32_t)(target - now));
      frame_start = target;
    } else {
      frame_start = now;
    }
  }
}
