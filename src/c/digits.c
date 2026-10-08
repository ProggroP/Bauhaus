#include "digits.h"

// ---------------------------------------------------------------- Segmente
// Eine Ziffer sitzt in einer Box (x, y, w, h). Der obere und der untere
// Abschluss ist entweder ein Halbkreis (SEG_ARC_*), ein gerader Balken
// (SEG_BAR_*) oder offen. Dazwischen laufen vier halbhohe Stege.

#define SEG_ARC_TOP  (1 << 0)
#define SEG_ARC_BOT  (1 << 1)
#define SEG_BAR_TOP  (1 << 2)
#define SEG_BAR_BOT  (1 << 3)
#define SEG_BAR_MID  (1 << 4)
#define SEG_LEFT_UP  (1 << 5)
#define SEG_LEFT_LO  (1 << 6)
#define SEG_RIGHT_UP (1 << 7)
#define SEG_RIGHT_LO (1 << 8)
#define SEG_STEM     (1 << 9)   // mittiger Steg ueber die volle Hoehe, nur fuer die 1
#define SEG_MID_R    (1 << 10)  // Mittelbalken nur rechts, sonst schliesst er die 3 zu

static const uint16_t DIGIT_SEGMENTS[10] = {
  /* 0 */ SEG_ARC_TOP | SEG_ARC_BOT | SEG_LEFT_UP | SEG_LEFT_LO | SEG_RIGHT_UP | SEG_RIGHT_LO,
  /* 1 */ SEG_STEM,
  /* 2 */ SEG_ARC_TOP | SEG_RIGHT_UP | SEG_BAR_MID | SEG_LEFT_LO | SEG_BAR_BOT,
  /* 3 */ SEG_ARC_TOP | SEG_RIGHT_UP | SEG_MID_R   | SEG_RIGHT_LO | SEG_ARC_BOT,
  /* 4 */ SEG_LEFT_UP | SEG_BAR_MID | SEG_RIGHT_UP | SEG_RIGHT_LO,
  /* 5 */ SEG_BAR_TOP | SEG_LEFT_UP | SEG_BAR_MID | SEG_RIGHT_LO | SEG_ARC_BOT,
  /* 6 */ SEG_ARC_TOP | SEG_LEFT_UP | SEG_BAR_MID | SEG_LEFT_LO | SEG_RIGHT_LO | SEG_ARC_BOT,
  /* 7 */ SEG_BAR_TOP | SEG_RIGHT_UP | SEG_RIGHT_LO,
  /* 8 */ SEG_ARC_TOP | SEG_LEFT_UP | SEG_RIGHT_UP | SEG_BAR_MID | SEG_LEFT_LO | SEG_RIGHT_LO | SEG_ARC_BOT,
  /* 9 */ SEG_ARC_TOP | SEG_LEFT_UP | SEG_RIGHT_UP | SEG_BAR_MID | SEG_RIGHT_LO | SEG_ARC_BOT,
};

// ---------------------------------------------------------------- Metrik

int digit_width(int height) {
  return height * DIGIT_W_NUM / DIGIT_W_DEN;
}

int digit_gap(int height) {
  int g = height * DIGIT_GAP_NUM / DIGIT_GAP_DEN;
  return g < 2 ? 2 : g;
}

static int digit_stroke(int height) {
  int t = height * DIGIT_STROKE_NUM / DIGIT_STROKE_DEN;
  return t < 2 ? 2 : t;
}

int number_width(int digits, int height) {
  if (digits < 1) return 0;
  return digits * digit_width(height) + (digits - 1) * digit_gap(height);
}

int colon_width(int height) {
  return digit_stroke(height);
}

// ---------------------------------------------------------------- Primitive

static void bar(GContext *ctx, int x, int y, int w, int h) {
  if (w <= 0 || h <= 0) return;
  graphics_fill_rect(ctx, GRect(x, y, w, h), 0, GCornerNone);
}

static void half_arc(GContext *ctx, int cx, int cy, int r, int t, int deg_from, int deg_to) {
  if (r <= 0 || t <= 0) return;
  graphics_fill_radial(ctx, GRect(cx - r, cy - r, 2 * r, 2 * r),
                       GOvalScaleModeFitCircle, (uint16_t)t,
                       DEG_TO_TRIGANGLE(deg_from), DEG_TO_TRIGANGLE(deg_to));
}

// ---------------------------------------------------------------- Ziffer

static void draw_digit(GContext *ctx, int d, int x, int y, int height) {
  if (d < 0 || d > 9) return;

  const int w = digit_width(height);
  const int t = digit_stroke(height);
  const int r = w / 2;
  const uint16_t seg = DIGIT_SEGMENTS[d];

  const int mid_y  = y + (height - t) / 2;
  const int join_y = y + height / 2;

  if (seg & SEG_STEM) {
    bar(ctx, x + (w - t) / 2, y, t, height);
    return;
  }

  // Die 4 ist der einzige Sonderfall: ihr Querbalken sitzt tiefer als die Mitte,
  // sonst liest sie sich wie ein kyrillisches Ч.
  if (d == 4) {
    const int by = y + height * 62 / 100;
    bar(ctx, x,         y,  t, by + t - y);
    bar(ctx, x,         by, w, t);
    bar(ctx, x + w - t, y,  t, height);
    return;
  }

  // Bei der 3 enden beide Schalen weiter oben bzw. unten. Laufen sie wie bei
  // 0 und 8 bis zur groessten Breite, schliesst sich die Form optisch und die
  // Ziffer liest sich als gespiegeltes E.
  const int arc_top_from = (d == 3) ? 315 : 270;
  const int arc_bot_to   = (d == 3) ? 225 : 270;

  if (seg & SEG_ARC_TOP) half_arc(ctx, x + w / 2, y + r, r, t, arc_top_from, 450);
  if (seg & SEG_ARC_BOT) half_arc(ctx, x + w / 2, y + height - r, r, t, 90, arc_bot_to);
  if (seg & SEG_BAR_TOP) bar(ctx, x, y, w, t);
  if (seg & SEG_BAR_BOT) bar(ctx, x, y + height - t, w, t);
  if (seg & SEG_BAR_MID) bar(ctx, x, mid_y, w, t);
  if (seg & SEG_MID_R)   bar(ctx, x + w / 3, mid_y, w - w / 3, t);

  const int up_top = (seg & SEG_ARC_TOP) ? y + r : y;
  const int lo_bot = (seg & SEG_ARC_BOT) ? y + height - r : y + height;

  if (seg & SEG_LEFT_UP)  bar(ctx, x,         up_top,  t, join_y - up_top);
  if (seg & SEG_RIGHT_UP) bar(ctx, x + w - t, up_top,  t, join_y - up_top);
  if (seg & SEG_LEFT_LO)  bar(ctx, x,         join_y,  t, lo_bot - join_y);
  if (seg & SEG_RIGHT_LO) bar(ctx, x + w - t, join_y,  t, lo_bot - join_y);
}

// ---------------------------------------------------------------- Zahl

void draw_number(GContext *ctx, int value, int digits, int x, int y, int height) {
  const int w = digit_width(height);
  const int gap = digit_gap(height);
  int v = value;

  if (v < 0) v = 0;
  for (int i = digits - 1; i >= 0; i--) {
    draw_digit(ctx, v % 10, x + i * (w + gap), y, height);
    v /= 10;
  }
}

void draw_colon(GContext *ctx, int x, int y, int height) {
  const int t = digit_stroke(height);
  const int r = (t + 1) / 2;
  graphics_fill_circle(ctx, GPoint(x + r, y + height / 3), r);
  graphics_fill_circle(ctx, GPoint(x + r, y + height * 2 / 3), r);
}
