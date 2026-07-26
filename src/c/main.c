#include "common.h"

// ---- shared cache ----
uint16_t g_history[3][NUM_DAYS];

// ---- main window widgets ----
static Window *s_main_window;
static ActionBarLayer *s_action_bar;

static Layer *s_label1_bg, *s_label2_bg, *s_label3_bg;
static TextLayer *s_label1, *s_label2, *s_label3;
static TextLayer *s_price1, *s_price2, *s_price3;

static char s_price1_buf[16];
static char s_price2_buf[16];
static char s_price3_buf[16];

static GBitmap *s_icon_refresh;
static GBitmap *s_icon_graph;

// ---- helpers ----

// Formats a price stored as (euros * 1000) into "€1,879" style text (Dutch decimal comma).
// A value of 0 means "no data yet".
static void format_price(uint16_t value, char *buf, size_t buf_len) {
  if (value == 0) {
    snprintf(buf, buf_len, "--,---");
    return;
  }
  int euros = value / PRICE_SCALE;
  int frac = value % PRICE_SCALE;
  snprintf(buf, buf_len, "\u20AC%d,%03d", euros, frac);
}

static void colored_bg_update_proc(Layer *layer, GContext *ctx) {
  GColor color = *(GColor *)layer_get_data(layer);
  graphics_context_set_fill_color(ctx, color);
  graphics_fill_rect(ctx, layer_get_bounds(layer), 0, GCornerNone);
}

static Layer *make_colored_bg(Layer *parent, GRect frame, GColor color) {
  Layer *bg = layer_create_with_data(frame, sizeof(GColor));
  *(GColor *)layer_get_data(bg) = color;
  layer_set_update_proc(bg, colored_bg_update_proc);
  layer_add_child(parent, bg);
  return bg;
}

static TextLayer *make_label(Layer *bg, GRect frame, const char *text) {
  TextLayer *tl = text_layer_create(frame);
  text_layer_set_text(tl, text);
  text_layer_set_background_color(tl, GColorClear);
  text_layer_set_text_color(tl, GColorWhite);
  text_layer_set_font(tl, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
  text_layer_set_text_alignment(tl, GTextAlignmentCenter);
  layer_add_child(bg, text_layer_get_layer(tl));
  return tl;
}

static TextLayer *make_price(Layer *parent, GRect frame) {
  TextLayer *tl = text_layer_create(frame);
  text_layer_set_text(tl, "--,---");
  text_layer_set_background_color(tl, GColorClear);
  text_layer_set_text_color(tl, GColorBlack);
  text_layer_set_font(tl, fonts_get_system_font(FONT_KEY_LECO_32_BOLD_NUMBERS));
  text_layer_set_text_alignment(tl, GTextAlignmentCenter);
  layer_add_child(parent, text_layer_get_layer(tl));
  return tl;
}

// ---- click handlers ----

static void up_click_handler(ClickRecognizerRef recognizer, void *context) {
  graph_window_push();
}

static void select_click_handler(ClickRecognizerRef recognizer, void *context) {
  request_refresh();
}

// Down button intentionally does nothing for now.

static void click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_UP, up_click_handler);
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click_handler);
}

// ---- window lifecycle ----

static void main_window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(root);

  s_action_bar = action_bar_layer_create();
  action_bar_layer_add_to_window(s_action_bar, window);
  action_bar_layer_set_click_config_provider(s_action_bar, click_config_provider);

  s_icon_graph = gbitmap_create_with_resource(RESOURCE_ID_ICON_GRAPH);
  s_icon_refresh = gbitmap_create_with_resource(RESOURCE_ID_ICON_REFRESH);
  action_bar_layer_set_icon(s_action_bar, BUTTON_ID_UP, s_icon_graph);
  action_bar_layer_set_icon(s_action_bar, BUTTON_ID_SELECT, s_icon_refresh);

  // Content area excludes the action bar strip on the right.
  int content_width = bounds.size.w - ACTION_BAR_WIDTH;
  int label_h = 30;
  int price_h = (bounds.size.h - (label_h * 3)) / 3;
  int y = 0;

  GRect f1 = GRect(0, y, content_width, label_h);
  s_label1_bg = make_colored_bg(root, f1, COLOR_SENSOR_1);
  s_label1 = make_label(s_label1_bg, GRect(0, 0, content_width, label_h), "Euro95 - CBS-NL");
  y += label_h;

  GRect fp1 = GRect(0, y, content_width, price_h);
  s_price1 = make_price(root, fp1);
  y += price_h;

  GRect f2 = GRect(0, y, content_width, label_h);
  s_label2_bg = make_colored_bg(root, f2, COLOR_SENSOR_2);
  s_label2 = make_label(s_label2_bg, GRect(0, 0, content_width, label_h), "Euro95 - VanRaak");
  y += label_h;

  GRect fp2 = GRect(0, y, content_width, price_h);
  s_price2 = make_price(root, fp2);
  y += price_h;

  GRect f3 = GRect(0, y, content_width, label_h);
  s_label3_bg = make_colored_bg(root, f3, COLOR_SENSOR_3);
  s_label3 = make_label(s_label3_bg, GRect(0, 0, content_width, label_h), "Super+ - VanRaak");
  y += label_h;

  GRect fp3 = GRect(0, y, content_width, price_h);
  s_price3 = make_price(root, fp3);
}

static void main_window_unload(Window *window) {
  text_layer_destroy(s_label1);
  text_layer_destroy(s_label2);
  text_layer_destroy(s_label3);
  text_layer_destroy(s_price1);
  text_layer_destroy(s_price2);
  text_layer_destroy(s_price3);
  layer_destroy(s_label1_bg);
  layer_destroy(s_label2_bg);
  layer_destroy(s_label3_bg);
  action_bar_layer_destroy(s_action_bar);
  gbitmap_destroy(s_icon_graph);
  gbitmap_destroy(s_icon_refresh);
}

void main_window_push(void) {
  window_stack_push(s_main_window, true);
}

static void refresh_price_labels(void) {
  format_price(g_history[0][NUM_DAYS - 1], s_price1_buf, sizeof(s_price1_buf));
  format_price(g_history[1][NUM_DAYS - 1], s_price2_buf, sizeof(s_price2_buf));
  format_price(g_history[2][NUM_DAYS - 1], s_price3_buf, sizeof(s_price3_buf));
  text_layer_set_text(s_price1, s_price1_buf);
  text_layer_set_text(s_price2, s_price2_buf);
  text_layer_set_text(s_price3, s_price3_buf);
}

// ---- AppMessage ----

void request_refresh(void) {
  DictionaryIterator *iter;
  if (app_message_outbox_begin(&iter) == APP_MSG_OK) {
    dict_write_uint8(iter, MESSAGE_KEY_REFRESH, 1);
    app_message_outbox_send();
  }
}

static void inbox_received_handler(DictionaryIterator *iterator, void *context) {
  Tuple *t;

  t = dict_find(iterator, MESSAGE_KEY_HIST1);
  if (t && t->length >= NUM_DAYS * sizeof(uint16_t)) {
    memcpy(g_history[0], t->value->data, NUM_DAYS * sizeof(uint16_t));
  }
  t = dict_find(iterator, MESSAGE_KEY_HIST2);
  if (t && t->length >= NUM_DAYS * sizeof(uint16_t)) {
    memcpy(g_history[1], t->value->data, NUM_DAYS * sizeof(uint16_t));
  }
  t = dict_find(iterator, MESSAGE_KEY_HIST3);
  if (t && t->length >= NUM_DAYS * sizeof(uint16_t)) {
    memcpy(g_history[2], t->value->data, NUM_DAYS * sizeof(uint16_t));
  }

  refresh_price_labels();
  layer_mark_dirty(window_get_root_layer(s_main_window));
}

static void inbox_dropped_callback(AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_ERROR, "Inbox dropped: %d", (int)reason);
}

static void outbox_failed_callback(DictionaryIterator *iterator, AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_ERROR, "Outbox failed: %d", (int)reason);
}

// ---- init ----

static void init(void) {
  memset(g_history, 0, sizeof(g_history));

  s_main_window = window_create();
  window_set_click_config_provider(s_main_window, click_config_provider);
  window_set_window_handlers(s_main_window, (WindowHandlers) {
    .load = main_window_load,
    .unload = main_window_unload,
  });

  app_message_register_inbox_received(inbox_received_handler);
  app_message_register_inbox_dropped(inbox_dropped_callback);
  app_message_register_outbox_failed(outbox_failed_callback);
  app_message_open(256, 64);

  main_window_push();

  // Kick off an initial fetch so data appears without waiting for a manual refresh.
  request_refresh();
}

static void deinit(void) {
  window_destroy(s_main_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
