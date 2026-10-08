#include <pebble.h>
#include "digits.h"

// ================================================================ Konstanten

// -- Stile
#define STYLE_MONDRIAN 0
#define STYLE_ALBERS   1
#define STYLE_MOHOLY   2

// -- Paletten
#define PALETTE_PRIMARY 0
#define PALETTE_MUTED   1
#define PALETTE_MONO    2
#define PALETTE_NIGHT   3

// -- Lesbarkeitsstufen
#define READ_SHAPES 0
#define READ_SMALL  1
#define READ_LARGE  2

// -- Wechselrhythmus der Komposition
#define RHYTHM_MINUTE 0
#define RHYTHM_HOUR   1
#define RHYTHM_DAY    2
#define RHYTHM_FIXED  3

// -- Drittes Element
#define EXTRA_NONE    0
#define EXTRA_STEPS   1
#define EXTRA_BATTERY 2

// -- Rasterstaerke
#define GRID_OFF  0
#define GRID_THIN 1
#define GRID_BOLD 2

// -- Layout (durchweg als Prozent der Displaymasse)
#define DIGIT_H_LARGE_PCT   24
#define DIGIT_H_SMALL_PCT   11
#define BLOCK_PAD_PCT       30   // Innenabstand der Minutenflaeche, in % der Ziffernhoehe
#define PLATE_PAD_PCT       13   // Innenabstand der mitwandernden Minutenunterlage
#define BLOCK_GAP            6   // Mindestluft zwischen zwei Ziffernbloecken
#define CIRCLE_MARGIN        8   // Abstand der Stundenscheibe zum Displayrand
#define DISC_MIN_PCT        20   // Mindestradius der Stundenscheibe, in % der Displaybreite
#define RIM_THICKNESS_PCT    4   // Minutenbogen am Rand, in % der Displaybreite
#define RIM_INSET            3
#define EXTRA_BAR_W_PCT     42
#define EXTRA_BAR_H_PCT      3
#define EXTRA_BAR_Y_PCT     89
#define ALBERS_OUTER_PCT    88
// Die drei Baender sind absichtlich ungleich breit -- gleich breite Baender
// lesen sich als ineinandergesetzte Rahmen, nicht als Farbfelder. Ihre Summe
// ist konstant, damit das innerste Feld die Ziffern immer fasst.
// Das gelbe Band ist bewusst das schmalste: Gabbro zeigt Gelb nur als Creme,
// als breites Feld macht es die ganze Komposition blass.
#define ALBERS_BAND1_PM    100   // rot, aussen
#define ALBERS_BAND2_PM     58   // gelb
#define ALBERS_BAND3_PM     91   // blau, innen
#define ALBERS_OFFSET_NUM    7   // Versatz der inneren Quadrate nach unten, in Vierteln der Bandbreite
#define ANIM_DURATION_MS   340   // Dauer des Minutenwechsels
#define MOHOLY_TURN         92   // Abstand der Minutenflaeche vom Mittelpunkt, in % des Scheibenradius
#define MOHOLY_DATE         75   // dasselbe fuer das Datum
#define STEPS_GOAL       10000

// -- Persist
#define PERSIST_STYLE   100
#define PERSIST_PALETTE 101
#define PERSIST_READ    102
#define PERSIST_RHYTHM  103
#define PERSIST_EXTRA   104
#define PERSIST_DATE    105
#define PERSIST_RIM     106
#define PERSIST_GRID    107
#define PERSIST_ANIM    108

// ================================================================ Zustand

typedef struct {
  GColor paper;
  GColor ink;
  GColor red;
  GColor blue;
  GColor yellow;
  // Ziffern, die auf einer Farbflaeche stehen. Rot und Blau sind in jeder
  // Palette mitteldunkel, deshalb ist das nicht dasselbe wie die Papierfarbe --
  // in der Nachtpalette waere das Schwarz auf Blau.
  GColor on_color;
} Palette;

static Window *s_window;
static Layer  *s_canvas;

static int s_style       = STYLE_MONDRIAN;
static int s_palette     = PALETTE_PRIMARY;
static int s_readability = READ_LARGE;
static int s_rhythm      = RHYTHM_DAY;
static int s_extra       = EXTRA_NONE;
static int s_grid        = GRID_BOLD;
static bool s_show_date  = true;
static bool s_rim_arc    = true;
static bool s_animate    = true;

// Der Minutenwechsel wird ueberblendet: s_from ist der Stand davor, s_to der
// aktuelle. s_phase laeuft dazwischen von 0 bis 1000. localtime() gibt einen
// statischen Puffer zurueck, beide Zeiten muessen deshalb Kopien sein.
static struct tm s_from, s_to;
static int s_phase = 1000;

// ================================================================ Zufall
// Eigener Generator, damit dieselbe Zeit immer dieselbe Komposition ergibt --
// unabhaengig davon, wie oft zwischendurch neu gezeichnet wurde.

static uint32_t s_rng;

static void rng_seed(uint32_t seed) {
  s_rng = seed ? seed : 1u;
}

static uint32_t rng_next(void) {
  s_rng ^= s_rng << 13;
  s_rng ^= s_rng >> 17;
  s_rng ^= s_rng << 5;
  return s_rng;
}

static int rng_range(int lo, int hi) {
  if (hi <= lo) return lo;
  return lo + (int)(rng_next() % (uint32_t)(hi - lo + 1));
}

static uint32_t composition_seed(struct tm *t) {
  switch (s_rhythm) {
    case RHYTHM_MINUTE: return (uint32_t)(t->tm_yday * 1440 + t->tm_hour * 60 + t->tm_min) + 1u;
    case RHYTHM_HOUR:   return (uint32_t)(t->tm_yday * 24 + t->tm_hour) + 1u;
    case RHYTHM_DAY:    return (uint32_t)(t->tm_year * 366 + t->tm_yday) + 1u;
    default:            return 1919u;   // Gruendungsjahr, feste Komposition
  }
}

// ================================================================ Uebergang

static void anim_update(Animation *animation, const AnimationProgress progress) {
  s_phase = (int)(progress * 1000 / ANIMATION_NORMALIZED_MAX);
  if (s_phase < 0) s_phase = 0;
  if (s_phase > 1000) s_phase = 1000;
  layer_mark_dirty(s_canvas);
}

static const AnimationImplementation s_anim_impl = { .update = anim_update };

// Die Animation wird nach dem Lauf selbst zerstoert, ihr Zeiger darf also nicht
// aufgehoben werden. Ein laufender Uebergang wird ueber unschedule_all beendet.
static void start_transition(void) {
  animation_unschedule_all();
  Animation *a = animation_create();
  if (!a) { s_phase = 1000; return; }
  animation_set_duration(a, ANIM_DURATION_MS);
  animation_set_curve(a, AnimationCurveEaseInOut);
  animation_set_implementation(a, &s_anim_impl);
  animation_schedule(a);
}

// Minutenstand als Tausendstel, ueber den Stundenwechsel hinweg fortlaufend.
static int blended_minute(void) {
  int from = s_from.tm_min, to = s_to.tm_min;
  if (to < from) to += 60;
  return from * 1000 + (to - from) * s_phase;
}

// Position auf dem Zwoelfstundenzifferblatt, in Minuten seit 12 Uhr.
static int blended_pos12(void) {
  int from = (s_from.tm_hour % 12) * 60 + s_from.tm_min;
  int to   = (s_to.tm_hour   % 12) * 60 + s_to.tm_min;
  if (to < from) to += 720;
  return from + (to - from) * s_phase / 1000;
}

// ================================================================ Palette

static Palette palette_for(int idx) {
  Palette p;
  switch (idx) {
    case PALETTE_MUTED:
      p.paper  = GColorPastelYellow;
      p.ink    = GColorBlack;
      p.red    = GColorDarkCandyAppleRed;
      p.blue   = GColorDukeBlue;
      p.yellow = GColorChromeYellow;
      p.on_color = GColorPastelYellow;
      break;
    case PALETTE_MONO:
      p.paper  = GColorWhite;
      p.ink    = GColorBlack;
      p.red    = GColorBlack;
      p.blue   = GColorDarkGray;
      p.yellow = GColorLightGray;
      p.on_color = GColorWhite;
      break;
    case PALETTE_NIGHT:
      p.paper  = GColorBlack;
      p.ink    = GColorWhite;
      p.red    = GColorRed;
      p.blue   = GColorBlue;
      p.yellow = GColorYellow;
      p.on_color = GColorWhite;
      break;
    default:
      p.paper  = GColorWhite;
      p.ink    = GColorBlack;
      p.red    = GColorRed;
      p.blue   = GColorBlue;
      p.yellow = GColorYellow;
      p.on_color = GColorWhite;
      break;
  }
  return p;
}

// ================================================================ Helfer

static void fill_rect(GContext *ctx, GColor c, int x, int y, int w, int h) {
  if (w <= 0 || h <= 0) return;
  graphics_context_set_fill_color(ctx, c);
  graphics_fill_rect(ctx, GRect(x, y, w, h), 0, GCornerNone);
}

static void fill_poly(GContext *ctx, GColor c, GPoint *pts, int n) {
  GPathInfo info = { .num_points = (uint32_t)n, .points = pts };
  GPath *path = gpath_create(&info);
  if (!path) return;
  graphics_context_set_fill_color(ctx, c);
  gpath_draw_filled(ctx, path);
  gpath_destroy(path);
}

static int digit_height_for(int display_h) {
  if (s_readability == READ_SMALL) return display_h * DIGIT_H_SMALL_PCT / 100;
  return display_h * DIGIT_H_LARGE_PCT / 100;
}

static int grid_weight(int display_w) {
  if (s_grid == GRID_OFF) return 0;
  int t = (s_grid == GRID_BOLD) ? display_w / 56 : display_w / 104;
  return t < 2 ? 2 : t;
}

static int display_hour(struct tm *t) {
  if (clock_is_24h_style()) return t->tm_hour;
  int h = t->tm_hour % 12;
  return h == 0 ? 12 : h;
}

// Zweistelliger Block, mittig um (cx, cy). Aendert sich der Wert, staucht die
// alte Zahl bis auf null zusammen und die neue waechst wieder heraus. Die
// Ziffern-Engine ist ueber die Hoehe parametrisiert, deshalb genuegt dafuer ein
// veraenderter Hoehenwert -- es braucht keine Maske und keinen zweiten Layer.
static void draw_block(GContext *ctx, GColor c, int from_v, int to_v,
                       int cx, int cy, int dh) {
  if (s_readability == READ_SHAPES) return;

  int value = to_v, h = dh;
  if (from_v != to_v) {
    if (s_phase < 500) { value = from_v; h = dh * (500 - s_phase) / 500; }
    else               { value = to_v;   h = dh * (s_phase - 500) / 500; }
  }
  if (h < 3) return;

  const int bw = number_width(2, h);
  graphics_context_set_fill_color(ctx, c);
  draw_number(ctx, value, 2, cx - bw / 2, cy - h / 2, h);
}

// Haelt einen Ziffernblock im sicher sichtbaren Bereich. Auf dem runden Display
// ist das das eingeschriebene Quadrat -- ein Rasterfeld reicht dort bis in die
// Ecken, die der Rand wegschneidet.
static void clamp_block(GRect b, int bw, int bh, int *cx, int *cy) {
#ifdef PBL_ROUND
  const int inset = b.size.w * 146 / 1000;
#else
  // Rand plus die Breite der umlaufenden Minutenlinie.
  const int inset = 12;
#endif
  const int lo_x = inset + bw / 2, hi_x = b.size.w - inset - bw / 2;
  const int lo_y = inset + bh / 2, hi_y = b.size.h - inset - bh / 2;
  if (hi_x > lo_x) { if (*cx < lo_x) *cx = lo_x; if (*cx > hi_x) *cx = hi_x; }
  if (hi_y > lo_y) { if (*cy < lo_y) *cy = lo_y; if (*cy > hi_y) *cy = hi_y; }
}

// Schiebt einen Ziffernblock aus einem anderen heraus. Beide sind achsenparallele
// Rechtecke, es genuegt also der Vergleich der Mittelpunktsabstaende -- kein
// Kreis-Rechteck-Test und keine Wurzel. Zuerst wird waagerecht ausgewichen, weil
// die Felder meist breiter als hoch sind; klemmt es dort am Rand, dann senkrecht.
static void avoid_overlap(GRect b, int ox, int oy, int need_x, int need_y,
                          int bw, int bh, int *cx, int *cy) {
  if (abs(*cx - ox) >= need_x || abs(*cy - oy) >= need_y) return;

  int tx = (*cx >= ox) ? ox + need_x : ox - need_x, ty = *cy;
  clamp_block(b, bw, bh, &tx, &ty);
  if (abs(tx - ox) >= need_x) { *cx = tx; *cy = ty; return; }

  tx = *cx;
  ty = (*cy >= oy) ? oy + need_y : oy - need_y;
  clamp_block(b, bw, bh, &tx, &ty);
  *cx = tx;
  *cy = ty;
}

// ================================================================ Randbogen

// Der Bogen laeuft in der Linienfarbe, nicht in Blau: die Minute hat mit dem
// blauen Feld schon einen Traeger, und zwei blaue Elemente kippen das Bild.
static void draw_rim_arc(GContext *ctx, GRect b, Palette p) {
  if (!s_rim_arc) return;
  const int thick = b.size.w * RIM_THICKNESS_PCT / 100;
  graphics_context_set_fill_color(ctx, p.ink);
#ifdef PBL_ROUND
  graphics_fill_radial(ctx, grect_inset(b, GEdgeInsets(RIM_INSET)),
                       GOvalScaleModeFitCircle, (uint16_t)thick,
                       0, TRIG_MAX_ANGLE / 60 * blended_minute() / 1000);
#else
  // Auf dem eckigen Display laeuft dieselbe Linie einmal am Rand herum: ab der
  // oberen Mitte im Uhrzeigersinn. Ein Balken an nur einer Kante liest sich
  // dort wie ein Fehler im Raster.
  const int W = b.size.w, H = b.size.h;
  int left = 2 * (W + H) / 60 * blended_minute() / 1000;
  int seg, d;

  seg = W / 2; d = left < seg ? left : seg;
  fill_rect(ctx, p.ink, W / 2, 0, d, thick);        left -= d; if (left <= 0) return;
  seg = H;     d = left < seg ? left : seg;
  fill_rect(ctx, p.ink, W - thick, 0, thick, d);    left -= d; if (left <= 0) return;
  seg = W;     d = left < seg ? left : seg;
  fill_rect(ctx, p.ink, W - d, H - thick, d, thick); left -= d; if (left <= 0) return;
  seg = H;     d = left < seg ? left : seg;
  fill_rect(ctx, p.ink, 0, H - d, thick, d);        left -= d; if (left <= 0) return;
  seg = W / 2; d = left < seg ? left : seg;
  fill_rect(ctx, p.ink, 0, 0, d, thick);
#endif
}

// ================================================================ Drittes Element

static int extra_permille(void) {
  if (s_extra == EXTRA_BATTERY) {
    return battery_state_service_peek().charge_percent * 10;
  }
  if (s_extra == EXTRA_STEPS) {
    HealthServiceAccessibilityMask ok =
        health_service_metric_accessible(HealthMetricStepCount,
                                         time_start_of_today(), time(NULL));
    if (ok != HealthServiceAccessibilityMaskAvailable) return 0;
    int pm = (int)health_service_sum_today(HealthMetricStepCount) * 1000 / STEPS_GOAL;
    return pm > 1000 ? 1000 : pm;
  }
  return 0;
}

static void draw_extra(GContext *ctx, GRect b, Palette p) {
  if (s_extra == EXTRA_NONE) return;
  const int bw = b.size.w * EXTRA_BAR_W_PCT / 100;
  const int bh = b.size.h * EXTRA_BAR_H_PCT / 100;
  const int bx = (b.size.w - bw) / 2;
  const int by = b.size.h * EXTRA_BAR_Y_PCT / 100;
  // Umrandeter Balken, gefuellter Teil in der Linienfarbe. Die Fuellung in Gelb
  // waere auf Papier unsichtbar -- ein voller Akku sah aus wie gar kein Balken.
  const int filled = bw * extra_permille() / 1000;
  fill_rect(ctx, p.ink, bx, by, bw, bh);
  fill_rect(ctx, p.paper, bx + filled + 1, by + 1, bw - filled - 2, bh - 2);
}

// ================================================================ Datum

#define DATE_BOX_W 80
#define DATE_BOX_H 28
#define DATE_BOX_H_SMALL 22

// Wird mittig um (cx, cy) gesetzt; die Stile suchen sich die freie Flaeche.
// compact ist fuer schmale Baender gedacht, in die die grosse Schrift nicht passt.
static void draw_date(GContext *ctx, GColor c, struct tm *t, int cx, int cy, bool compact) {
  if (!s_show_date) return;
  static char buf[16];
  const int h = compact ? DATE_BOX_H_SMALL : DATE_BOX_H;
  strftime(buf, sizeof(buf), "%d.%m.", t);
  graphics_context_set_text_color(ctx, c);
  graphics_draw_text(ctx, buf,
                     fonts_get_system_font(compact ? FONT_KEY_GOTHIC_18_BOLD
                                                   : FONT_KEY_GOTHIC_24_BOLD),
                     GRect(cx - DATE_BOX_W / 2, cy - h / 2, DATE_BOX_W, h),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
}

// ================================================================ Stil: Mondrian

// Die Stundenscheibe steht auf ihrer Position im Zifferblatt und ist gerade so
// gross, dass der Ziffernblock hineinpasst. Beide Stile mit Scheibe teilen sich
// diese Rechnung.
static void hour_disc(GRect b, int dh, int *cx, int *cy, int *r) {
  const int W = b.size.w, H = b.size.h;
  const int pad = dh * BLOCK_PAD_PCT / 100;

  // Die Scheibe muss den Ziffernblock fassen -- aber sie ist auch ohne ihn ein
  // Traeger der Komposition. Ohne Untergrenze schrumpft sie bei kleinen Ziffern
  // auf einen Punkt zusammen und die Stundenposition geht als Bild verloren.
  int hr = number_width(2, dh) / 2 + pad;
  const int need = dh / 2 + pad;
  const int floor_r = W * DISC_MIN_PCT / 100;
  if (need > hr) hr = need;
  if (floor_r > hr) hr = floor_r;

  const int32_t angle = TRIG_MAX_ANGLE * blended_pos12() / 720;
  int orbit = (W < H ? W : H) / 2 - hr - CIRCLE_MARGIN;
  if (orbit < 0) orbit = 0;

  *r  = hr;
  *cx = W / 2 + orbit * sin_lookup(angle) / TRIG_MAX_RATIO;
  *cy = H / 2 - orbit * cos_lookup(angle) / TRIG_MAX_RATIO;
}

static void draw_mondrian(GContext *ctx, GRect b, Palette p, struct tm *t) {
  const int W = b.size.w, H = b.size.h;
  const int dh = digit_height_for(H);
  const int bw = number_width(2, dh);
  const int gw = grid_weight(W);

  int cx, cy, hr;
  hour_disc(b, dh, &cx, &cy, &hr);

  // Ein senkrechtes und ein waagerechtes Lineal teilen die Flaeche in vier
  // Felder. Beide bleiben im mittleren Drittel, damit jedes Feld den
  // Ziffernblock noch fassen kann.
  const int vx = W * rng_range(40, 58) / 100;
  const int hy = H * rng_range(38, 58) / 100;

  // Welches der vier Felder die Minute traegt, entscheidet der Seed -- nicht
  // die Scheibe. Haengt es an ihrer Position, springt die ganze Aufteilung in
  // die andere Spalte, sobald sie eine Achse kreuzt, also etwa alle drei
  // Stunden. So steht das Raster den ganzen Tag und die Scheibe wandert darueber.
  const int field = rng_range(0, 3);
  const bool right = (field & 1) != 0;
  const bool below = (field & 2) != 0;
  const int rx = right ? vx : 0,  rw = right ? W - vx : vx;
  const int ry = below ? hy : 0,  rh = below ? H - hy : hy;

  // Die gelbe Flaeche fuellt das Nachbarfeld derselben Zeile nur zum Teil und
  // liegt an der Aussenkante an.
  const int yx = right ? 0 : vx,  yw = right ? vx : W - vx;
  const int yh = rh * rng_range(45, 78) / 100;
  const int yy = below ? H - yh : 0;

  fill_rect(ctx, p.paper,  0,  0,  W,  H);
  fill_rect(ctx, p.yellow, yx, yy, yw, yh);
  fill_rect(ctx, p.blue,   rx, ry, rw, rh);

  if (gw > 0) {
    fill_rect(ctx, p.ink, vx - gw / 2, 0, gw, H);
    fill_rect(ctx, p.ink, 0, hy - gw / 2, W, gw);
    // Volle Kante: Gabbro zeigt Gelb nur als Creme, ohne Linie liest sich das
    // Feld als Papier statt als Farbflaeche.
    fill_rect(ctx, p.ink, yx, below ? yy - gw : yy + yh, yw, gw);
  }

  graphics_context_set_fill_color(ctx, p.red);
  graphics_fill_circle(ctx, GPoint(cx, cy), hr);

  draw_block(ctx, p.on_color, display_hour(&s_from), display_hour(&s_to), cx, cy, dh);

  // Die Minute weicht der Scheibe aus und nimmt ihre blaue Unterlage mit.
  // Ohne sie landen die Ziffern beim Ausweichen auf Papier oder Gelb, und
  // die Ziffernfarbe traegt dort nicht.
  const int plate_pad = dh * PLATE_PAD_PCT / 100;
  int mx = rx + rw / 2, my = ry + rh / 2;
  clamp_block(b, bw, dh, &mx, &my);
  avoid_overlap(b, cx, cy,
                bw + plate_pad + BLOCK_GAP, dh + plate_pad + BLOCK_GAP,
                bw, dh, &mx, &my);
  fill_rect(ctx, p.blue, mx - bw / 2 - plate_pad, my - dh / 2 - plate_pad,
            bw + 2 * plate_pad, dh + 2 * plate_pad);

  // Das Datum sitzt im Feld diagonal gegenueber der Minute und weicht der
  // Scheibe ebenfalls aus.
  int dx = right ? (vx / 2) : (vx + (W - vx) / 2);
  int dy = below ? (hy / 2) : (hy + (H - hy) / 2);
  clamp_block(b, DATE_BOX_W, DATE_BOX_H, &dx, &dy);
  avoid_overlap(b, cx, cy, (DATE_BOX_W + bw) / 2 + BLOCK_GAP,
                (DATE_BOX_H + dh) / 2 + BLOCK_GAP, DATE_BOX_W, DATE_BOX_H, &dx, &dy);
  draw_date(ctx, p.ink, t, dx, dy, false);

  draw_block(ctx, p.on_color, display_hour(&s_from), display_hour(&s_to), cx, cy, dh);
  draw_block(ctx, p.on_color, s_from.tm_min, s_to.tm_min, mx, my, dh);
}

// ================================================================ Stil: Albers

static void draw_albers(GContext *ctx, GRect b, Palette p, struct tm *t) {
  const int W = b.size.w, H = b.size.h;
  const int outer = (W < H ? W : H) * ALBERS_OUTER_PCT / 100;

  // Die Bandbreiten atmen mit der Zeit, ihre Summe bleibt aber konstant --
  // sonst schrumpft das innerste Feld unter die Ziffern weg.
  const int shift_h = (t->tm_hour % 5) * 5 - 10;
  const int shift_m = (t->tm_min % 5) * 5 - 10;
  const int f1 = ALBERS_BAND1_PM + shift_h;
  const int f2 = ALBERS_BAND2_PM + shift_m;
  const int f3 = ALBERS_BAND3_PM - shift_h - shift_m;

  const int step1 = outer * f1 / 1000;
  const int step2 = outer * f2 / 1000;
  const int step3 = outer * f3 / 1000;

  const int s1 = outer;
  const int s2 = s1 - 2 * step1;
  const int s3 = s2 - 2 * step2;
  const int s4 = s3 - 2 * step3;

  const int y1 = (H - s1) / 2 - outer / 40;
  // Albers setzt die inneren Quadrate nicht mittig, sondern nach unten versetzt.
  const int y2 = y1 + step1 * ALBERS_OFFSET_NUM / 4;
  const int y3 = y2 + step2 * ALBERS_OFFSET_NUM / 4;
  const int y4 = y3 + step3 * ALBERS_OFFSET_NUM / 4;

  fill_rect(ctx, p.paper,  0,            0,  W,  H);
  fill_rect(ctx, p.red,    (W - s1) / 2, y1, s1, s1);
  fill_rect(ctx, p.yellow, (W - s2) / 2, y2, s2, s2);
  fill_rect(ctx, p.blue,   (W - s3) / 2, y3, s3, s3);
  fill_rect(ctx, p.paper,  (W - s4) / 2, y4, s4, s4);

  // Weil die inneren Quadrate nach unten versetzt sind, ist das obere rote Band
  // das breiteste -- dort steht das Datum.
  draw_date(ctx, p.on_color, t, W / 2, (y1 + y2) / 2, true);

  if (s_readability == READ_SHAPES || s4 <= 0) return;

  // Stunde ueber Minute, beide mittig im innersten Feld.
  const int gap = s4 / 14;
  const int max_dh = (s4 - s4 / 10 - gap) / 2;
  int dh = digit_height_for(H);
  if (dh > max_dh) dh = max_dh;
  if (dh < 8) return;

  const int top = y4 + (s4 - (2 * dh + gap)) / 2;
  draw_block(ctx, p.ink, display_hour(&s_from), display_hour(&s_to),
             W / 2, top + dh / 2, dh);
  draw_block(ctx, p.ink, s_from.tm_min, s_to.tm_min,
             W / 2, top + dh + gap + dh / 2, dh);
}

// ================================================================ Stil: Moholy-Nagy

static void draw_moholy(GContext *ctx, GRect b, Palette p, struct tm *t) {
  const int W = b.size.w, H = b.size.h;
  const int dh = digit_height_for(H);
  const int bw = number_width(2, dh);
  const int pad = dh * BLOCK_PAD_PCT / 100;

  int cx, cy, hr;
  hour_disc(b, dh, &cx, &cy, &hr);

  // Die Minutenflaeche gegenueberzuspiegeln ergaebe eine achsensymmetrische
  // Hantel. Stattdessen steht sie quer zur Scheibe: Scheibe, Flaeche und Datum
  // verteilen sich so um den Mittelpunkt, und die Diagonale bleibt sichtbar.
  // Die Flaeche steht um 135 Grad gedreht zur Scheibe und etwas weiter aussen.
  // Bei 90 Grad kaeme sie der Scheibe zu nah und deren Rand liefe in die Ziffern.
  const int ox = cx - W / 2, oy = cy - H / 2;
  int mx = W / 2 - (ox + oy) * MOHOLY_TURN / 100;
  int my = H / 2 + (ox - oy) * MOHOLY_TURN / 100;
  clamp_block(b, bw, dh, &mx, &my);

  fill_rect(ctx, p.paper, 0, 0, W, H);

  // Gelber Keil in der Ecke, die der Scheibe gegenueberliegt.
  const bool disc_left = (cx < W / 2);
  const int reach = W * rng_range(68, 96) / 100;
  const int rise  = H * rng_range(52, 84) / 100;
  GPoint tri[3] = {
    GPoint(disc_left ? W : 0, H),
    GPoint(disc_left ? W - reach : reach, H),
    GPoint(disc_left ? W : 0, H - rise)
  };
  fill_poly(ctx, p.yellow, tri, 3);

  // Schwarze Diagonale durch die Mitte.
  const int band = H * rng_range(7, 12) / 100;
  const int slope = H * (rng_range(0, 36) - 18) / 100;
  const int my2 = H / 2 - band / 2;
  GPoint diag[4] = {
    GPoint(0, my2 - slope), GPoint(W, my2 + slope),
    GPoint(W, my2 + band + slope), GPoint(0, my2 + band - slope)
  };
  fill_poly(ctx, p.ink, diag, 4);

  // Minutenflaeche als freistehendes Rechteck.
  fill_rect(ctx, p.blue, mx - bw / 2 - pad, my - dh / 2 - pad, bw + 2 * pad, dh + 2 * pad);

  graphics_context_set_fill_color(ctx, p.red);
  graphics_fill_circle(ctx, GPoint(cx, cy), hr);

  draw_block(ctx, p.on_color, display_hour(&s_from), display_hour(&s_to), cx, cy, dh);
  draw_block(ctx, p.on_color, s_from.tm_min, s_to.tm_min, mx, my, dh);

  // Das Datum nimmt die dritte Richtung. Es bekommt eine Papierflaeche
  // untergelegt, weil die Diagonale sonst je nach Neigung darunterlaeuft.
  int dx = W / 2 + (oy - ox) * MOHOLY_DATE / 100;
  int dy = H / 2 - (ox + oy) * MOHOLY_DATE / 100;
  clamp_block(b, DATE_BOX_W + 8, DATE_BOX_H + 6, &dx, &dy);
  fill_rect(ctx, p.paper, dx - DATE_BOX_W / 2 - 4, dy - (DATE_BOX_H + 6) / 2,
            DATE_BOX_W + 8, DATE_BOX_H + 6);
  draw_date(ctx, p.ink, t, dx, dy, false);
}

// ================================================================ Zeichnen

// #define DEBUG_DIGITS   // zeichnet 0-9 statt der Komposition

#ifdef DEBUG_DIGITS
static void draw_digit_ruler(GContext *ctx, GRect b, Palette p) {
  const int dh = b.size.h / 6;
  const int w = digit_width(dh) + digit_gap(dh);
  fill_rect(ctx, p.paper, 0, 0, b.size.w, b.size.h);
  graphics_context_set_fill_color(ctx, p.ink);
  for (int i = 0; i < 5; i++) {
    draw_number(ctx, i, 1, b.size.w / 2 - (5 * w) / 2 + i * w, b.size.h / 4, dh);
    draw_number(ctx, i + 5, 1, b.size.w / 2 - (5 * w) / 2 + i * w, b.size.h / 2, dh);
  }
}
#endif

static void canvas_update(Layer *layer, GContext *ctx) {
  const GRect b = layer_get_bounds(layer);
  struct tm *t = &s_to;
  const Palette p = palette_for(s_palette);

  graphics_context_set_antialiased(ctx, true);
  rng_seed(composition_seed(t));

#ifdef DEBUG_DIGITS
  draw_digit_ruler(ctx, b, p);
  return;
#endif

  switch (s_style) {
    case STYLE_ALBERS: draw_albers(ctx, b, p, t);  break;
    case STYLE_MOHOLY: draw_moholy(ctx, b, p, t);  break;
    default:           draw_mondrian(ctx, b, p, t); break;
  }

  draw_rim_arc(ctx, b, p);
  draw_extra(ctx, b, p);
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  s_from = s_to;
  s_to = *tick_time;

  if (s_animate) {
    s_phase = 0;
    start_transition();
  } else {
    s_phase = 1000;
    layer_mark_dirty(s_canvas);
  }
}

// ================================================================ Einstellungen
// Jeder Wert liegt unter einem eigenen Persist-Key. Ein gemeinsames Struct waere
// bequemer, aber ein spaeter eingeschobenes Feld verschiebt dort stumm alle
// folgenden Werte.

static void settings_load(void) {
  if (persist_exists(PERSIST_STYLE))   s_style       = persist_read_int(PERSIST_STYLE);
  if (persist_exists(PERSIST_PALETTE)) s_palette     = persist_read_int(PERSIST_PALETTE);
  if (persist_exists(PERSIST_READ))    s_readability = persist_read_int(PERSIST_READ);
  if (persist_exists(PERSIST_RHYTHM))  s_rhythm      = persist_read_int(PERSIST_RHYTHM);
  if (persist_exists(PERSIST_EXTRA))   s_extra       = persist_read_int(PERSIST_EXTRA);
  if (persist_exists(PERSIST_GRID))    s_grid        = persist_read_int(PERSIST_GRID);
  if (persist_exists(PERSIST_DATE))    s_show_date   = persist_read_bool(PERSIST_DATE);
  if (persist_exists(PERSIST_RIM))     s_rim_arc     = persist_read_bool(PERSIST_RIM);
  if (persist_exists(PERSIST_ANIM))    s_animate     = persist_read_bool(PERSIST_ANIM);
}

static void settings_save(void) {
  persist_write_int(PERSIST_STYLE,   s_style);
  persist_write_int(PERSIST_PALETTE, s_palette);
  persist_write_int(PERSIST_READ,    s_readability);
  persist_write_int(PERSIST_RHYTHM,  s_rhythm);
  persist_write_int(PERSIST_EXTRA,   s_extra);
  persist_write_int(PERSIST_GRID,    s_grid);
  persist_write_bool(PERSIST_DATE,   s_show_date);
  persist_write_bool(PERSIST_RIM,    s_rim_arc);
  persist_write_bool(PERSIST_ANIM,   s_animate);
}

static void inbox_received(DictionaryIterator *iter, void *context) {
  Tuple *tp;

  if ((tp = dict_find(iter, MESSAGE_KEY_STYLE)))       s_style       = atoi(tp->value->cstring);
  if ((tp = dict_find(iter, MESSAGE_KEY_PALETTE)))     s_palette     = atoi(tp->value->cstring);
  if ((tp = dict_find(iter, MESSAGE_KEY_READABILITY))) s_readability = atoi(tp->value->cstring);
  if ((tp = dict_find(iter, MESSAGE_KEY_RHYTHM)))      s_rhythm      = atoi(tp->value->cstring);
  if ((tp = dict_find(iter, MESSAGE_KEY_EXTRA)))       s_extra       = atoi(tp->value->cstring);
  if ((tp = dict_find(iter, MESSAGE_KEY_GRID)))        s_grid        = atoi(tp->value->cstring);
  if ((tp = dict_find(iter, MESSAGE_KEY_SHOW_DATE)))   s_show_date   = tp->value->int32 != 0;
  if ((tp = dict_find(iter, MESSAGE_KEY_RIM_ARC)))     s_rim_arc     = tp->value->int32 != 0;
  if ((tp = dict_find(iter, MESSAGE_KEY_ANIMATE)))     s_animate     = tp->value->int32 != 0;

  settings_save();
  layer_mark_dirty(s_canvas);
}

// ================================================================ App

static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  s_canvas = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_canvas, canvas_update);
  layer_add_child(root, s_canvas);
}

static void window_unload(Window *window) {
  layer_destroy(s_canvas);
}

static void init(void) {
  settings_load();

  const time_t now = time(NULL);
  s_to = *localtime(&now);
  s_from = s_to;

  s_window = window_create();
  window_set_window_handlers(s_window, (WindowHandlers){
    .load = window_load,
    .unload = window_unload,
  });
  window_stack_push(s_window, true);

  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
  app_message_register_inbox_received(inbox_received);
  app_message_open(512, 64);
}

static void deinit(void) {
  animation_unschedule_all();
  tick_timer_service_unsubscribe();
  window_destroy(s_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
