#include "common.h"

// ---- shared cache ----
uint16_t g_history[3][NUM_DAYS];

char g_label1[LABEL_BUF_LEN];
char g_label2[LABEL_BUF_LEN];
char g_label3[LABEL_BUF_LEN];
GColor g_color1;
GColor g_color2;
GColor g_color3;

// ---- persistent storage keys ----
#define PKEY_LABEL1 1
#define PKEY_LABEL2 2
#define PKEY_LABEL3 3
#define PKEY_COLOR1 4
#define PKEY_COLOR2 5
#define PKEY_COLOR3 6

// ---- main window widgets ----
static Window *s_main_window;
static ActionBarLayer *s_action_bar;

static Layer *s_label1_bg, *s_label2_bg, *s_label3_bg;
static TextLayer *s_label1, *s_label2, *s_label3;
static Layer *s_price1, *s_price2, *s_price3;

static GBitmap *s_icon_refresh;
static GBitmap *s_icon_graph;

#define PRICE_UNIT_TEXT "\u20AC/L"
#define PRICE_FONT_BIG FONT_KEY_BITHAM_30_BLACK
#define PRICE_FONT_UNIT FONT_KEY_GOTHIC_18_BOLD

// ---- persisted settings ----

static void load_persisted_settings(void) {
  if (persist_exists(PKEY_LABEL1)) {
    persist_read_string(PKEY_LABEL1, g_label1, LABEL_BUF_LEN);
  } else {
    strncpy(g_label1, DEFAULT_LABEL_1, LABEL_BUF_LEN - 1);
    g_label1[LABEL_BUF_LEN - 1] = '\0';
  }
  if (persist_exists(PKEY_LABEL2)) {
    persist_read_string(PKEY_LABEL2, g_label2, LABEL_BUF_LEN);
  } else {
    strncpy(g_label2, DEFAULT_LABEL_2, LABEL_BUF_LEN - 1);
    g_label2[LABEL_BUF_LEN - 1] = '\0';
  }
  if (persist_exists(PKEY_LABEL3)) {
    persist_read_string(PKEY_LABEL3, g_label3, LABEL_BUF_LEN);
  } else {
    strncpy(g_label3, DEFAULT_LABEL_3, LABEL_BUF_LEN - 1);
    g_label3[LABEL_BUF_LEN - 1] = '\0';
  }

  g_color1 = persist_exists(PKEY_COLOR1)
      ? (GColor){ .argb = (uint8_t)persist_read_int(PKEY_COLOR1) } : DEFAULT_COLOR_1;
  g_color2 = persist_exists(PKEY_COLOR2)
      ? (GColor){ .argb = (uint8_t)persist_read_int(PKEY_COLOR2) } : DEFAULT_COLOR_2;
  g_color3 = persist_exists(PKEY_COLOR3)
      ? (GColor){ .argb = (uint8_t)persist_read_int(PKEY_COLOR3) } : DEFAULT_COLOR_3;
}

// ---- helpers ----

// Formats a price stored as (euros * 1000) into "1,879" style text (Dutch decimal
// comma), just the number - the €/L unit is drawn separately, smaller. A value
// of 0 means "no data yet".
static void format_price(uint16_t value, char *buf, size_t buf_len) {
  if (value == 0) {
    snprintf(buf, buf_len, "--,---");
    return;
  }
  int euros = value / PRICE_SCALE;
  int frac = value % PRICE_SCALE;
  snprintf(buf, buf_len, "%d,%03d", euros, frac);
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

// Draws "<number>" big & bold, followed immediately by a smaller bold
// "€/L", the pair centered together as one group within the row.
static void price_row_update_proc(Layer *layer, GContext *ctx) {
  char *num_str = (char *)layer_get_data(layer);
  GRect bounds = layer_get_bounds(layer);

  GFont big_font = fonts_get_system_font(PRICE_FONT_BIG);
  GFont unit_font = fonts_get_system_font(PRICE_FONT_UNIT);

  GRect measure_box = GRect(0, 0, 300, 60);
  GSize num_size = graphics_text_layout_get_content_size(
      num_str, big_font, measure_box, GTextOverflowModeFill, GTextAlignmentLeft);
  GSize unit_size = graphics_text_layout_get_content_size(
      PRICE_UNIT_TEXT, unit_font, measure_box, GTextOverflowModeFill, GTextAlignmentLeft);

  int gap = 4;
  int total_w = num_size.w + gap + unit_size.w;
  int start_x = (bounds.size.w - total_w) / 2;
  int num_y = (bounds.size.h - num_size.h) / 2;
  int unit_y = num_y + num_size.h - unit_size.h; // sit on the same baseline as the number

  graphics_context_set_text_color(ctx, GColorBlack);

  GRect num_rect = GRect(start_x, num_y, num_size.w, num_size.h);
  graphics_draw_text(ctx, num_str, big_font, num_rect, GTextOverflowModeFill, GTextAlignmentLeft, NULL);

  GRect unit_rect = GRect(start_x + num_size.w + gap, unit_y, unit_size.w, unit_size.h);
  graphics_draw_text(ctx, PRICE_UNIT_TEXT, unit_font, unit_rect, GTextOverflowModeFill, GTextAlignmentLeft, NULL);
}

static Layer *make_price(Layer *parent, GRect frame) {
  Layer *l = layer_create_with_data(frame, 16);
  strcpy((char *)layer_get_data(l), "--,---");
  layer_set_update_proc(l, price_row_update_proc);
  layer_add_child(parent, l);
  return l;
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
  s_label1_bg = make_colored_bg(root, f1, g_color1);
  s_label1 = make_label(s_label1_bg, GRect(0, 0, content_width, label_h), g_label1);
  y += label_h;

  GRect fp1 = GRect(0, y, content_width, price_h);
  s_price1 = make_price(root, fp1);
  y += price_h;

  GRect f2 = GRect(0, y, content_width, label_h);
  s_label2_bg = make_colored_bg(root, f2, g_color2);
  s_label2 = make_label(s_label2_bg, GRect(0, 0, content_width, label_h), g_label2);
  y += label_h;

  GRect fp2 = GRect(0, y, content_width, price_h);
  s_price2 = make_price(root, fp2);
  y += price_h;

  GRect f3 = GRect(0, y, content_width, label_h);
  s_label3_bg = make_colored_bg(root, f3, g_color3);
  s_label3 = make_label(s_label3_bg, GRect(0, 0, content_width, label_h), g_label3);
  y += label_h;

  GRect fp3 = GRect(0, y, content_width, price_h);
  s_price3 = make_price(root, fp3);
}

static void main_window_unload(Window *window) {
  text_layer_destroy(s_label1);
  text_layer_destroy(s_label2);
  text_layer_destroy(s_label3);
  layer_destroy(s_price1);
  layer_destroy(s_price2);
  layer_destroy(s_price3);
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
  char buf[16];

  format_price(g_history[0][NUM_DAYS - 1], buf, sizeof(buf));
  strncpy((char *)layer_get_data(s_price1), buf, 15);

  format_price(g_history[1][NUM_DAYS - 1], buf, sizeof(buf));
  strncpy((char *)layer_get_data(s_price2), buf, 15);

  format_price(g_history[2][NUM_DAYS - 1], buf, sizeof(buf));
  strncpy((char *)layer_get_data(s_price3), buf, 15);
}

// Pushes the current g_label*/g_color* values onto the already-built UI -
// used when new settings arrive from the phone after the window has loaded.
static void apply_dynamic_settings(void) {
  text_layer_set_text(s_label1, g_label1);
  text_layer_set_text(s_label2, g_label2);
  text_layer_set_text(s_label3, g_label3);

  *(GColor *)layer_get_data(s_label1_bg) = g_color1;
  *(GColor *)layer_get_data(s_label2_bg) = g_color2;
  *(GColor *)layer_get_data(s_label3_bg) = g_color3;
  layer_mark_dirty(s_label1_bg);
  layer_mark_dirty(s_label2_bg);
  layer_mark_dirty(s_label3_bg);
}

// ---- AppMessage ----

void request_refresh(void) {
  DictionaryIterator *iter;
  if (app_message_outbox_begin(&iter) == APP_MSG_OK) {
    dict_write_uint8(iter, MESSAGE_KEY_REFRESH, 1);
    app_message_outbox_send();
  }
}

static void update_label(Tuple *t, char *dest, int pkey) {
  if (!t) return;
  strncpy(dest, t->value->cstring, LABEL_BUF_LEN - 1);
  dest[LABEL_BUF_LEN - 1] = '\0';
  persist_write_string(pkey, dest);
}

static void update_color(Tuple *t, GColor *dest, int pkey) {
  if (!t) return;
  dest->argb = t->value->uint8;
  persist_write_int(pkey, (int)dest->argb);
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

  update_label(dict_find(iterator, MESSAGE_KEY_LABEL1), g_label1, PKEY_LABEL1);
  update_label(dict_find(iterator, MESSAGE_KEY_LABEL2), g_label2, PKEY_LABEL2);
  update_label(dict_find(iterator, MESSAGE_KEY_LABEL3), g_label3, PKEY_LABEL3);
  update_color(dict_find(iterator, MESSAGE_KEY_COLOR1), &g_color1, PKEY_COLOR1);
  update_color(dict_find(iterator, MESSAGE_KEY_COLOR2), &g_color2, PKEY_COLOR2);
  update_color(dict_find(iterator, MESSAGE_KEY_COLOR3), &g_color3, PKEY_COLOR3);

  apply_dynamic_settings();
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
  load_persisted_settings();

  s_main_window = window_create();
  window_set_click_config_provider(s_main_window, click_config_provider);
  window_set_window_handlers(s_main_window, (WindowHandlers) {
    .load = main_window_load,
    .unload = main_window_unload,
  });

  app_message_register_inbox_received(inbox_received_handler);
  app_message_register_inbox_dropped(inbox_dropped_callback);
  app_message_register_outbox_failed(outbox_failed_callback);
  // Bumped from 256: label text + colours added to every refresh payload
  // alongside the three 60-byte history arrays.
  app_message_open(512, 64);

  main_window_push();

  // No need to request a refresh here: the phone's PebbleKit JS fires its
  // own fetch-and-send as soon as it becomes ready. Requesting one here too
  // caused two AppMessage sends to race each other (APP_MSG_BUSY / nack).
}

static void deinit(void) {
  window_destroy(s_main_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}