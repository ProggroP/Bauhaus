#pragma once
#include <pebble.h>

// Geometrische Stencil-Ziffern im Geist von Albers' Kombinationsschrift.
// Zusammengesetzt aus Halbkreisbogen, Balken und Stegen -- keine Font-Resource,
// dadurch kantengeglaettet (Pebble-Fonts sind 1-Bit) und frei skalierbar.

// Alle Proportionen als Bruchteil der Ziffernhoehe.
#define DIGIT_W_NUM      68
#define DIGIT_W_DEN     100
#define DIGIT_STROKE_NUM 15
#define DIGIT_STROKE_DEN 100
#define DIGIT_GAP_NUM    10
#define DIGIT_GAP_DEN   100

int digit_width(int height);
int digit_gap(int height);
int number_width(int digits, int height);

// value wird rechtsbuendig mit fuehrenden Nullen auf digits Stellen gezeichnet.
void draw_number(GContext *ctx, int value, int digits, int x, int y, int height);
void draw_colon(GContext *ctx, int x, int y, int height);
int colon_width(int height);
