#pragma once
#include <pebble.h>

// Number of days of history we keep / plot
#define NUM_DAYS 30

// Colours, shared between main window labels and graph lines.
// Order matches the sensor order: 1 = Euro95 CBS-NL, 2 = Euro95 VanRaak, 3 = Super+ VanRaak
#define COLOR_SENSOR_1 GColorOxfordBlue        // dark blue
#define COLOR_SENSOR_2 GColorDarkGreen         // dark green
#define COLOR_SENSOR_3 GColorBulgarianRose     // dark red

// Prices are transported and cached as whole eurocents (e.g. 187 = €1,87 / 1879 not used,
// we keep 3 decimals so it's actually deci-millicents: see PRICE_SCALE below).
// A litre price like €1,879 is stored as the integer 1879 (i.e. price * 1000).
#define PRICE_SCALE 1000
// 0 means "no data for this day"

// Shared history cache, filled by the AppMessage inbox handler in main.c,
// read by graph_window.c when the graph is drawn.
extern uint16_t g_history[3][NUM_DAYS];

void main_window_push(void);
void graph_window_push(void);
void request_refresh(void);
