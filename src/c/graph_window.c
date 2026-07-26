#include "common.h"

static Window *s_graph_window;
static Layer *s_graph_layer;

// Chart bounds, in €, matching the requested fixed axis range.
#define Y_MIN_PRICE 1000  // €1,00 * PRICE_SCALE
#define Y_MAX_PRICE 3000  // €3,00 * PRICE_SCALE

#define MARGIN_LEFT 34
#define MARGIN_TOP 6
#define MARGIN_BOTTOM 16
#define MARGIN_RIGHT 6

static GColor sensor_color(int idx) {
  switch (idx) {
    case 0: return COLOR_SENSOR_1;
    case 1: return COLOR_SENSOR_2;
    default: return COLOR_SENSOR_3;
  }
}

static int16_t y_for_price(uint16_t price, GRect plot_area) {
  if (price < Y_MIN_PRICE) price = Y_MIN_PRICE;
  if (price > Y_MAX_PRICE) price = Y_MAX_PRICE;
  int32_t range = Y_MAX_PRICE - Y_MIN_PRICE;
  int32_t offset = price - Y_MIN_PRICE;
  int32_t h = plot_area.size.h;
  // Higher price -> higher on screen (smaller y).
  return plot_area.origin.y + h - (int16_t)((offset * h) / range);
}

static int16_t x_for_day(int day_index, GRect plot_area) {
  if (NUM_DAYS <= 1) return plot_area.origin.x;
  int32_t w = plot_area.size.w;
  return plot_area.origin.x + (int16_t)(((int32_t)day_index * w) / (NUM_DAYS - 1));
}

static void draw_axes(GContext *ctx, GRect plot_area) {
  graphics_context_set_stroke_color(ctx, GColorBlack);
  graphics_context_set_stroke_width(ctx, 1);

  // Y axis + X axis
  graphics_draw_line(ctx, GPoint(plot_area.origin.x, plot_area.origin.y),
                           GPoint(plot_area.origin.x, plot_area.origin.y + plot_area.size.h));
  graphics_draw_line(ctx, GPoint(plot_area.origin.x, plot_area.origin.y + plot_area.size.h),
                           GPoint(plot_area.origin.x + plot_area.size.w, plot_area.origin.y + plot_area.size.h));

  // Y axis labels: €1,00 / €2,00 / €3,00 with light gridlines
  const uint16_t ticks[3] = {1000, 2000, 3000};
  const char *labels[3] = {"€1,00", "€2,00", "€3,00"};
  GFont font = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);

  for (int i = 0; i < 3; i++) {
    int16_t y = y_for_price(ticks[i], plot_area);
    graphics_context_set_stroke_color(ctx, GColorLightGray);
    graphics_draw_line(ctx, GPoint(plot_area.origin.x, y), GPoint(plot_area.origin.x + plot_area.size.w, y));

    GRect label_rect = GRect(0, y - 8, MARGIN_LEFT - 4, 16);
    graphics_context_set_text_color(ctx, GColorBlack);
    graphics_draw_text(ctx, labels[i], font, label_rect, GTextOverflowModeTrailingEllipsis,
                        GTextAlignmentRight, NULL);
  }
}

static void draw_sensor_line(GContext *ctx, GRect plot_area, int sensor_idx) {
  graphics_context_set_stroke_color(ctx, sensor_color(sensor_idx));
  graphics_context_set_stroke_width(ctx, 2);

  bool have_prev = false;
  GPoint prev = GPointZero;

  for (int day = 0; day < NUM_DAYS; day++) {
    uint16_t price = g_history[sensor_idx][day];
    if (price == 0) {
      // No data for this day: break the line here.
      have_prev = false;
      continue;
    }
    GPoint pt = GPoint(x_for_day(day, plot_area), y_for_price(price, plot_area));
    if (have_prev) {
      graphics_draw_line(ctx, prev, pt);
    }
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

  draw_axes(ctx, plot_area);

  for (int s = 0; s < 3; s++) {
    draw_sensor_line(ctx, plot_area, s);
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
