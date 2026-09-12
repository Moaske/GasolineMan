#pragma once
#include <pebble.h>

// Number of days of history we keep / plot
#define NUM_DAYS 30

// Prices are transported and cached as whole eurocents (e.g. 187 = €1,87 / 1879 not used,
// we keep 3 decimals so it's actually deci-millicents: see PRICE_SCALE below).
// A litre price like €1,879 is stored as the integer 1879 (i.e. price * 1000).
#define PRICE_SCALE 1000
// 0 means "no data for this day"

#define LABEL_BUF_LEN 24

// Defaults used until the phone has ever sent configured labels/colours, and
// until anything's been persisted from a previous run.
#define DEFAULT_LABEL_1 "Euro95 - CBS-NL"
#define DEFAULT_LABEL_2 "Euro95 - VanRaak"
#define DEFAULT_LABEL_3 "Super+ - VanRaak"
#define DEFAULT_COLOR_1 GColorOxfordBlue     // dark blue
#define DEFAULT_COLOR_2 GColorDarkGreen      // dark green
#define DEFAULT_COLOR_3 GColorBulgarianRose  // dark red

// Shared history cache, filled by the AppMessage inbox handler in main.c,
// read by graph_window.c when the graph is drawn.
extern uint16_t g_history[3][NUM_DAYS];

// Per-row label text and banner/line colour, configurable from the phone
// settings page, persisted on the watch, and shared between the main window
// (banner text + background) and the graph window (line colours).
extern char g_label1[LABEL_BUF_LEN];
extern char g_label2[LABEL_BUF_LEN];
extern char g_label3[LABEL_BUF_LEN];
extern GColor g_color1;
extern GColor g_color2;
extern GColor g_color3;

void main_window_push(void);
void graph_window_push(void);
void request_refresh(void);