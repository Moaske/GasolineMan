#include "common.h"

static Window *s_graph_window;
static Layer *s_graph_layer;

#define MARGIN_LEFT 34
#define MARGIN_TOP 6
#define MARGIN_BOTTOM 16
#define MARGIN_RIGHT 6

// Fallback range shown before any data has arrived at all.
#define DEFAULT_Y_MIN 1000  // €1,00 * PRICE_SCALE
#define DEFAULT_Y_MAX 3000  // €3,00 * PRICE_SCALE

// Minimum half-height of the range around the data, and the step we round
// the padded range out to, so the axis doesn't look weirdly precise.
#define MIN_HALF_SPAN 150  // €0,15
#define ROUND_STEP 50      // €0,05

static GColor sensor_color(int idx) {
  switch (idx) {
    case 0: return g_color1;
    case 1: return g_color2;
    default: return g_color3;
  }
}

// Scans all three sensors' history and works out a sensible y-axis range:
// tight enough around the real data to make day-to-day movement visible,
// but padded and rounded so it doesn't look jittery between refreshes.
static void compute_y_range(uint16_t *out_min, uint16_t *out_max) {
  bool found = false;
  uint16_t data_min = 0, data_max = 0;

  for (int s = 0; s < 3; s++) {
    for (int d = 0; d < NUM_DAYS; d++) {
      uint16_t v = g_history[s][d];
      if (v == 0) continue;
      if (!found || v < data_min) data_min = v;
      if (!found || v > data_max) data_max = v;
      found = true;
    }
  }

  if (!found) {
    *out_min = DEFAULT_Y_MIN;
    *out_max = DEFAULT_Y_MAX;
    return;
  }

  int32_t span = data_max - data_min;
  int32_t half_pad = span / 5; // ~10% total padding
  if (half_pad < MIN_HALF_SPAN) half_pad = MIN_HALF_SPAN;

  int32_t min_v = (int32_t)data_min - half_pad;
  int32_t max_v = (int32_t)data_max + half_pad;
  if (min_v < 0) min_v = 0;

  // Round outward to a clean step so the axis labels look tidy.
  min_v = (min_v / ROUND_STEP) * ROUND_STEP;
  max_v = ((max_v + ROUND_STEP - 1) / ROUND_STEP) * ROUND_STEP;

  *out_min = (uint16_t)min_v;
  *out_max = (uint16_t)max_v;
}

static int16_t y_for_price(uint16_t price, uint16_t y_min, uint16_t y_max, GRect plot_area) {
  if (price < y_min) price = y_min;
  if (price > y_max) price = y_max;
  int32_t range = y_max - y_min;
  if (range <= 0) range = 1;
  int32_t offset = price - y_min;
  int32_t h = plot_area.size.h;
  // Higher price -> higher on screen (smaller y).
  return plot_area.origin.y + h - (int16_t)((offset * h) / range);
}

static int16_t x_for_day(int day_index, GRect plot_area) {
  if (NUM_DAYS <= 1) return plot_area.origin.x;
  int32_t w = plot_area.size.w;
  return plot_area.origin.x + (int16_t)(((int32_t)day_index * w) / (NUM_DAYS - 1));
}

// Formats a price (euros * 1000) as "X,YY" (2 decimals) for axis labels.
static void format_axis_price(uint16_t value, char *buf, size_t buf_len) {
  int rounded = ((value + 5) / 10) * 10; // round to nearest cent
  int euros = rounded / 1000;
  int cents = (rounded % 1000) / 10;
  snprintf(buf, buf_len, "%d,%02d", euros, cents);
}

static void draw_axes(GContext *ctx, GRect plot_area, uint16_t y_min, uint16_t y_max) {
  graphics_context_set_stroke_color(ctx, GColorBlack);
  graphics_context_set_stroke_width(ctx, 1);

  // Y axis + X axis
  graphics_draw_line(ctx, GPoint(plot_area.origin.x, plot_area.origin.y),
                           GPoint(plot_area.origin.x, plot_area.origin.y + plot_area.size.h));
  graphics_draw_line(ctx, GPoint(plot_area.origin.x, plot_area.origin.y + plot_area.size.h),
                           GPoint(plot_area.origin.x + plot_area.size.w, plot_area.origin.y + plot_area.size.h));

  // Three evenly spaced tick labels across the current dynamic range, with
  // light gridlines.
  uint16_t ticks[3] = { y_min, (uint16_t)((y_min + y_max) / 2), y_max };
  GFont font = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);

  for (int i = 0; i < 3; i++) {
    int16_t y = y_for_price(ticks[i], y_min, y_max, plot_area);
    graphics_context_set_stroke_color(ctx, GColorLightGray);
    graphics_draw_line(ctx, GPoint(plot_area.origin.x, y), GPoint(plot_area.origin.x + plot_area.size.w, y));

    char label[8];
    format_axis_price(ticks[i], label, sizeof(label));

    GRect label_rect = GRect(0, y - 8, MARGIN_LEFT - 4, 16);
    graphics_context_set_text_color(ctx, GColorBlack);
    graphics_draw_text(ctx, label, font, label_rect, GTextOverflowModeTrailingEllipsis,
                        GTextAlignmentRight, NULL);
  }
}

// Fixed reference line at a specific price (e.g. €2,00), regardless of the
// dynamic y-axis range. Skipped if that price falls outside the current range.
static void draw_reference_line(GContext *ctx, GRect plot_area, uint16_t price, uint16_t y_min, uint16_t y_max) {
  if (price < y_min || price > y_max) return;

  int16_t y = y_for_price(price, y_min, y_max, plot_area);
  graphics_context_set_stroke_color(ctx, GColorRed);
  graphics_context_set_stroke_width(ctx, 1);
  graphics_draw_line(ctx, GPoint(plot_area.origin.x, y), GPoint(plot_area.origin.x + plot_area.size.w, y));
}

static void draw_sensor_line(GContext *ctx, GRect plot_area, int sensor_idx, uint16_t y_min, uint16_t y_max) {
  graphics_context_set_stroke_color(ctx, sensor_color(sensor_idx));
  graphics_context_set_stroke_width(ctx, 2);
  graphics_context_set_fill_color(ctx, sensor_color(sensor_idx));

  bool have_prev = false;
  GPoint prev = GPointZero;

  for (int day = 0; day < NUM_DAYS; day++) {
    uint16_t price = g_history[sensor_idx][day];
    if (price == 0) {
      // No data for this day: break the line here.
      have_prev = false;
      continue;
    }
    GPoint pt = GPoint(x_for_day(day, plot_area), y_for_price(price, y_min, y_max, plot_area));
    if (have_prev) {
      graphics_draw_line(ctx, prev, pt);
    }
    // Always mark the point itself too - a sensor with only a day or two of
    // history so far would otherwise have nothing to connect a line to and
    // render as nothing at all.
    graphics_fill_circle(ctx, pt, 2);
    prev = pt;
    have_prev = true;
  }
}

static void graph_layer_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);

  GRect plot_area = GRect(
    MARGIN_LEFT,
    MARGIN_TOP,
    bounds.size.w - MARGIN_LEFT - MARGIN_RIGHT,
    bounds.size.h - MARGIN_TOP - MARGIN_BOTTOM
  );

  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);

  uint16_t y_min, y_max;
  compute_y_range(&y_min, &y_max);

  draw_axes(ctx, plot_area, y_min, y_max);
  
  draw_reference_line(ctx, plot_area, 2000, y_min, y_max); // fixed €2,00 marker

  for (int s = 0; s < 3; s++) {
    draw_sensor_line(ctx, plot_area, s, y_min, y_max);
  }

  // X axis caption
  graphics_context_set_text_color(ctx, GColorBlack);
  GRect caption = GRect(plot_area.origin.x, bounds.size.h - MARGIN_BOTTOM + 1, plot_area.size.w, MARGIN_BOTTOM - 1);
  graphics_draw_text(ctx, "last 30 days", fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
                      caption, GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
}

static void graph_window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(root);

  s_graph_layer = layer_create(bounds);
  layer_set_update_proc(s_graph_layer, graph_layer_update_proc);
  layer_add_child(root, s_graph_layer);
}

static void graph_window_unload(Window *window) {
  layer_destroy(s_graph_layer);
}

void graph_window_push(void) {
  if (!s_graph_window) {
    s_graph_window = window_create();
    window_set_window_handlers(s_graph_window, (WindowHandlers) {
      .load = graph_window_load,
      .unload = graph_window_unload,
    });
  }
  window_stack_push(s_graph_window, true);
}