#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <curl/curl.h>

#include <psp2/ctrl.h>
#include <psp2/ime_dialog.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/net/net.h>
#include <psp2/net/netctl.h>
#include <psp2/sysmodule.h>
#include <psp2/touch.h>

#include <vita2d.h>

#include "chat.h"

#define SCREEN_WIDTH 960
#define SCREEN_HEIGHT 544

#define CARD_X 46.0f
#define CARD_Y 24.0f
#define CARD_WIDTH 868.0f
#define CARD_HEIGHT 496.0f
#define CARD_RADIUS 18.0f
#define SPLIT_X 480.0f

#define CONFIG_DIRECTORY "ux0:data/VagaRouteAI"
#define CONFIG_FILE "ux0:data/VagaRouteAI/config.ini"
#define NAME_CAPACITY 31
#define MESSAGE_CAPACITY 127
#define HISTORY_CAPACITY 4
#define ENDPOINT_CAPACITY 255
#define API_KEY_CAPACITY 255
#define MODEL_CAPACITY 63
#define MODEL_COUNT 8
#define CONFIG_BUFFER_CAPACITY 4096
#define NETWORK_MEMORY_SIZE (1024 * 1024)
#define CA_CERTIFICATE_FILE "app0:assets/cacert.pem"

#define KEYBOARD_ROWS 7
#define KEYBOARD_COLUMNS 6
#define KEY_BACKSPACE '\b'
#define KEY_DONE '\n'

#define COLOR_BACKGROUND_TOP RGBA8(7, 9, 17, 255)
#define COLOR_BACKGROUND_BOTTOM RGBA8(11, 14, 26, 255)
#define COLOR_CARD_LEFT RGBA8(22, 24, 41, 255)
#define COLOR_CARD_RIGHT RGBA8(15, 17, 30, 255)
#define COLOR_CARD_EDGE RGBA8(37, 40, 61, 255)
#define COLOR_FIELD RGBA8(11, 13, 24, 255)
#define COLOR_KEY RGBA8(25, 28, 46, 255)
#define COLOR_TEXT RGBA8(235, 239, 255, 255)
#define COLOR_MUTED RGBA8(145, 153, 183, 255)
#define COLOR_SUBTLE RGBA8(78, 85, 113, 255)
#define COLOR_ACCENT RGBA8(199, 139, 91, 255)
#define COLOR_ACCENT_SOFT RGBA8(111, 91, 153, 255)
#define COLOR_SUCCESS RGBA8(91, 201, 161, 255)
#define COLOR_ERROR RGBA8(224, 112, 111, 255)

static const char keyboard[KEYBOARD_ROWS][KEYBOARD_COLUMNS] = {
	{ 'A', 'B', 'C', 'D', 'E', 'F' },
	{ 'G', 'H', 'I', 'J', 'K', 'L' },
	{ 'M', 'N', 'O', 'P', 'Q', 'R' },
	{ 'S', 'T', 'U', 'V', 'W', 'X' },
	{ 'Y', 'Z', '0', '1', '2', '3' },
	{ '4', '5', '6', '7', '8', '9' },
	{ ' ', KEY_BACKSPACE, KEY_DONE, 0, 0, 0 },
};

static vita2d_font *ui_font;
static vita2d_texture *app_logo;
static char user_name[NAME_CAPACITY + 1];
static char endpoint_url[ENDPOINT_CAPACITY + 1];
static char api_key[API_KEY_CAPACITY + 1];
static char models[MODEL_COUNT][MODEL_CAPACITY + 1];
static char selected_model[MODEL_CAPACITY + 1];
static int model_count;
static int config_needs_rewrite;
static char message_text[MESSAGE_CAPACITY + 1];
static char status_message[64];
static int status_is_error;
static int screen;
static int focus;
static int keyboard_open;
static int keyboard_row;
static int keyboard_column;
static int keyboard_target;
static int chat_focus;
static int input_mode;
static int history_modal;
static int history_selection;
static int chat_scroll_offset;
static int chat_scroll_max;
static int chat_follow_bottom = 1;
static int chat_analog_delay;
static int info_modal;
static int side_menu_open;
static int side_menu_selection;
static int model_selector_open;
static int model_cursor;
static int model_analog_delay;
static int connection_state;
static int network_ready;
static int net_initialized;
static int netctl_initialized;
static int curl_initialized;
static int net_module_loaded;
static unsigned char network_memory[NETWORK_MEMORY_SIZE];
static SceTouchPanelInfo touch_panel;
static int touch_down;
static SceWChar16 ime_input[SCE_IME_DIALOG_MAX_TEXT_LENGTH + 1];
static SceWChar16 ime_initial_text[SCE_IME_DIALOG_MAX_TEXT_LENGTH + 1];
static int ime_open;
static int ime_target;
static int ime_origin_screen;
static int ime_module_loaded;
static int settings_focus;

enum {
	SCREEN_NAME,
	SCREEN_CHAT,
	SCREEN_SETTINGS
};

enum {
	CONNECTION_OFFLINE,
	CONNECTION_VERIFYING,
	CONNECTION_ONLINE
};

enum {
	KEYBOARD_NAME,
	KEYBOARD_MESSAGE,
	KEYBOARD_ENDPOINT,
	KEYBOARD_API_KEY
};

static void send_message(void);
static void draw_menu_icon(float x, float y, uint32_t color);

static void set_status(const char *message, int is_error) {
	strncpy(status_message, message, sizeof(status_message) - 1);
	status_message[sizeof(status_message) - 1] = '\0';
	status_is_error = is_error;
}

static unsigned int text_size(float scale) {
	unsigned int size;
	if (scale <= 0.65f) {
		size = 14;
	} else if (scale <= 0.95f) {
		size = 16;
	} else if (scale <= 1.10f) {
		size = 18;
	} else if (scale <= 1.20f) {
		size = 22;
	} else if (scale <= 1.60f) {
		size = 20;
	} else if (scale <= 2.10f) {
		size = 26;
	} else {
		size = 28;
	}
	return size > 0 ? size : 1;
}

static int text_width(float scale, const char *text) {
	return vita2d_font_text_width(ui_font, text_size(scale), text);
}

static void draw_text(float x, float baseline, float scale, uint32_t color, const char *text) {
	vita2d_font_draw_text(ui_font, (int)x, (int)baseline, color, text_size(scale), text);
}

static void draw_text_centered(float x, float baseline, float width, float scale, uint32_t color, const char *text) {
	int centered_x = (int)(x + (width - text_width(scale, text)) * 0.5f + 0.5f);
	draw_text((float)centered_x, baseline, scale, color, text);
}

static void draw_text_fit(float x, float baseline, float width, float scale, uint32_t color, const char *text) {
	const char *visible = text;
	while (*visible != '\0' && text_width(scale, visible) > width) {
		++visible;
	}
	draw_text(x, baseline, scale, color, visible);
}

static void draw_round_rect(float x, float y, float width, float height, float radius, uint32_t color) {
	vita2d_draw_rectangle(x + radius, y, width - radius * 2.0f, height, color);
	vita2d_draw_rectangle(x, y + radius, width, height - radius * 2.0f, color);
	vita2d_draw_fill_circle(x + radius, y + radius, radius, color);
	vita2d_draw_fill_circle(x + width - radius, y + radius, radius, color);
	vita2d_draw_fill_circle(x + radius, y + height - radius, radius, color);
	vita2d_draw_fill_circle(x + width - radius, y + height - radius, radius, color);
}

static void draw_left_panel(float x, float y, float width, float height, float radius, uint32_t color) {
	vita2d_draw_rectangle(x + radius, y, width - radius, height, color);
	vita2d_draw_rectangle(x, y + radius, width, height - radius * 2.0f, color);
	vita2d_draw_fill_circle(x + radius, y + radius, radius, color);
	vita2d_draw_fill_circle(x + radius, y + height - radius, radius, color);
}

static void draw_border_width(float x, float y, float width, float height, float radius, float thickness, uint32_t border, uint32_t inside) {
	draw_round_rect(x, y, width, height, radius, border);
	draw_round_rect(x + thickness, y + thickness, width - thickness * 2.0f, height - thickness * 2.0f, radius - thickness, inside);
}

static void draw_border(float x, float y, float width, float height, float radius, uint32_t border, uint32_t inside) {
	draw_border_width(x, y, width, height, radius, 1.0f, border, inside);
}

static void draw_logo(float x, float y) {
	if (app_logo != NULL) {
		vita2d_draw_texture(app_logo, x, y);
		return;
	}

	uint32_t primary = COLOR_ACCENT;
	uint32_t secondary = RGBA8(170, 91, 50, 255);

	vita2d_draw_line(x, y + 22.0f, x + 20.0f, y + 22.0f, primary);
	vita2d_draw_line(x + 20.0f, y + 22.0f, x + 43.0f, y + 4.0f, secondary);
	vita2d_draw_line(x + 20.0f, y + 22.0f, x + 43.0f, y + 40.0f, primary);
	vita2d_draw_line(x + 17.0f, y + 22.0f, x + 37.0f, y + 22.0f, secondary);
	vita2d_draw_line(x + 37.0f, y + 22.0f, x + 58.0f, y + 8.0f, primary);
	vita2d_draw_line(x + 37.0f, y + 22.0f, x + 58.0f, y + 36.0f, secondary);
	vita2d_draw_line(x + 34.0f, y + 22.0f, x + 53.0f, y + 22.0f, primary);
	vita2d_draw_line(x + 53.0f, y + 22.0f, x + 73.0f, y + 11.0f, secondary);
	vita2d_draw_line(x + 53.0f, y + 22.0f, x + 73.0f, y + 33.0f, primary);
	vita2d_draw_fill_circle(x, y + 22.0f, 3.0f, COLOR_ACCENT);
}

static void draw_background(void) {
	const int stripes = 32;
	for (int index = 0; index < stripes; ++index) {
		float amount = (float)index / (float)(stripes - 1);
		unsigned char red = (unsigned char)(7.0f + amount * 4.0f);
		unsigned char green = (unsigned char)(9.0f + amount * 5.0f);
		unsigned char blue = (unsigned char)(17.0f + amount * 9.0f);
		float y = (float)SCREEN_HEIGHT * amount;
		vita2d_draw_rectangle(0.0f, y, SCREEN_WIDTH, (float)SCREEN_HEIGHT / stripes + 1.0f, RGBA8(red, green, blue, 255));
	}

	static const uint16_t stars[][3] = {
		{ 64, 62, 2 }, { 178, 120, 1 }, { 302, 74, 1 }, { 413, 151, 2 },
		{ 555, 83, 1 }, { 710, 48, 1 }, { 828, 118, 2 }, { 900, 72, 1 },
		{ 116, 430, 1 }, { 244, 480, 2 }, { 361, 402, 1 }, { 466, 468, 1 },
		{ 624, 493, 1 }, { 756, 423, 2 }, { 876, 471, 1 }, { 914, 350, 1 },
		{ 34, 280, 1 }, { 932, 246, 1 }, { 145, 232, 1 }, { 801, 278, 1 },
	};

	for (size_t index = 0; index < sizeof(stars) / sizeof(stars[0]); ++index) {
		uint32_t color = stars[index][2] == 2 ? RGBA8(111, 119, 160, 255) : RGBA8(67, 74, 107, 255);
		vita2d_draw_fill_circle(stars[index][0], stars[index][1], stars[index][2], color);
	}
}

static void draw_badge(float x, float y, float width, const char *text) {
	draw_border(x, y, width, 25.0f, 5.0f, COLOR_CARD_EDGE, RGBA8(25, 27, 45, 255));
	draw_text_centered(x, y + 17.0f, width, 0.72f, COLOR_MUTED, text);
}

static void copy_config_value(char *target, int capacity, const char *value) {
	int length = 0;
	while (value[length] != '\0' && value[length] != '\r' && value[length] != '\n' && length < capacity - 1) {
		target[length] = value[length];
		++length;
	}
	target[length] = '\0';
}

static int hex_value(char character) {
	if (character >= '0' && character <= '9') {
		return character - '0';
	}
	if (character >= 'A' && character <= 'F') {
		return character - 'A' + 10;
	}
	if (character >= 'a' && character <= 'f') {
		return character - 'a' + 10;
	}
	return -1;
}

static void decode_api_key(const char *encoded) {
	static const char mask[] = "VagaRouteAI";
	int length = 0;
	while (encoded[length * 2] != '\0' && encoded[length * 2 + 1] != '\0' && length < API_KEY_CAPACITY) {
		int high = hex_value(encoded[length * 2]);
		int low = hex_value(encoded[length * 2 + 1]);
		if (high < 0 || low < 0) {
			api_key[0] = '\0';
			return;
		}
		api_key[length] = (char)((high << 4 | low) ^ mask[length % (sizeof(mask) - 1)]);
		++length;
	}
	api_key[length] = '\0';
}

static void append_config_text(char *buffer, int *length, const char *text) {
	while (*text != '\0' && *length < CONFIG_BUFFER_CAPACITY - 1) {
		buffer[(*length)++] = *text++;
	}
}

static void append_config_line(char *buffer, int *length, const char *key, const char *value) {
	append_config_text(buffer, length, key);
	if (*length < CONFIG_BUFFER_CAPACITY - 1) {
		buffer[(*length)++] = '=';
	}
	append_config_text(buffer, length, value);
	if (*length < CONFIG_BUFFER_CAPACITY - 1) {
		buffer[(*length)++] = '\n';
	}
}

static int save_config(void) {
	char buffer[CONFIG_BUFFER_CAPACITY] = { 0 };
	int length = 0;
	append_config_line(buffer, &length, "name", user_name);
	append_config_line(buffer, &length, "endpoint_url", endpoint_url);
	append_config_line(buffer, &length, "api_key", api_key);
	append_config_line(buffer, &length, "selected_model", selected_model);
	append_config_text(buffer, &length, "[models]\n");
	for (int index = 0; index < model_count && index < MODEL_COUNT; ++index) {
		char key[16] = "model_";
		int key_length = 6;
		if (index >= 10) {
			key[key_length++] = (char)('0' + index / 10);
		}
		key[key_length++] = (char)('0' + index % 10);
		key[key_length] = '\0';
		append_config_line(buffer, &length, key, models[index]);
	}

	sceIoMkdir(CONFIG_DIRECTORY, 0777);
	SceUID file = sceIoOpen(CONFIG_FILE, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
	if (file < 0) {
		return -1;
	}
	int written = sceIoWrite(file, buffer, length);
	sceIoClose(file);
	return written == length ? 0 : -1;
}

static void load_config(void) {
	SceUID file = sceIoOpen(CONFIG_FILE, SCE_O_RDONLY, 0);
	if (file < 0) {
		return;
	}

	char buffer[CONFIG_BUFFER_CAPACITY] = { 0 };
	int bytes_read = sceIoRead(file, buffer, sizeof(buffer) - 1);
	sceIoClose(file);
	if (bytes_read <= 0) {
		return;
	}
	model_count = 0;
	char *line = buffer;
	char *end = buffer + bytes_read;
	while (line < end) {
		char *line_end = line;
		while (line_end < end && *line_end != '\n') {
			++line_end;
		}
		if (strncmp(line, "name=", 5) == 0) {
			char *embedded_endpoint = strstr(line + 5, "endpoint_url=");
			if (embedded_endpoint != NULL && embedded_endpoint >= line_end) {
				embedded_endpoint = NULL;
			}
			if (embedded_endpoint != NULL) {
				int name_length = (int)(embedded_endpoint - (line + 5));
				if (name_length >= (int)sizeof(user_name)) {
					name_length = sizeof(user_name) - 1;
				}
				memcpy(user_name, line + 5, name_length);
				user_name[name_length] = '\0';
				copy_config_value(endpoint_url, sizeof(endpoint_url), embedded_endpoint + 13);
				config_needs_rewrite = 1;
			} else {
				copy_config_value(user_name, sizeof(user_name), line + 5);
			}
		} else if (strncmp(line, "endpoint_url=", 13) == 0) {
			copy_config_value(endpoint_url, sizeof(endpoint_url), line + 13);
		} else if (strncmp(line, "api_key=", 8) == 0) {
			copy_config_value(api_key, sizeof(api_key), line + 8);
		} else if (strncmp(line, "api_key_enc=", 12) == 0 && api_key[0] == '\0') {
			char encoded[API_KEY_CAPACITY * 2 + 1] = { 0 };
			copy_config_value(encoded, sizeof(encoded), line + 12);
			decode_api_key(encoded);
		} else if (strncmp(line, "selected_model=", 15) == 0) {
			copy_config_value(selected_model, sizeof(selected_model), line + 15);
		} else if (strncmp(line, "model_", 6) == 0 && model_count < MODEL_COUNT) {
			char *equals = line + 6;
			while (equals < line_end && *equals != '=') {
				++equals;
			}
			if (equals < line_end) {
				copy_config_value(models[model_count], sizeof(models[model_count]), equals + 1);
				if (models[model_count][0] != '\0') {
					++model_count;
				}
			}
		}
		line = line_end < end ? line_end + 1 : end;
	}
	int selected_exists = 0;
	for (int index = 0; index < model_count; ++index) {
		if (strcmp(selected_model, models[index]) == 0) {
			selected_exists = 1;
			break;
		}
	}
	if (!selected_exists) {
		if (model_count > 0) {
			strncpy(selected_model, models[0], sizeof(selected_model) - 1);
			selected_model[sizeof(selected_model) - 1] = '\0';
		} else {
			selected_model[0] = '\0';
		}
	}
}

static int save_name(void) {
	return save_config();
}

static void append_character(char *text, int capacity, char character) {
	int length = (int)strlen(text);
	if (length < capacity && character >= 32 && character <= 126) {
		text[length] = character;
		text[length + 1] = '\0';
	}
}

static void remove_last_character(char *text) {
	int length = (int)strlen(text);
	if (length > 0) {
		text[length - 1] = '\0';
	}
}

static void move_keyboard(int delta_row, int delta_column) {
	keyboard_row += delta_row;
	keyboard_column += delta_column;
	if (keyboard_row < 0) {
		keyboard_row = 0;
	}
	if (keyboard_row >= KEYBOARD_ROWS) {
		keyboard_row = KEYBOARD_ROWS - 1;
	}
	if (keyboard_column < 0) {
		keyboard_column = 0;
	}
	if (keyboard_column >= KEYBOARD_COLUMNS) {
		keyboard_column = KEYBOARD_COLUMNS - 1;
	}
}

static char *keyboard_target_text(void) {
	if (keyboard_target == KEYBOARD_NAME) {
		return user_name;
	}
	if (keyboard_target == KEYBOARD_ENDPOINT) {
		return endpoint_url;
	}
	if (keyboard_target == KEYBOARD_API_KEY) {
		return api_key;
	}
	return message_text;
}

static int keyboard_target_capacity(void) {
	if (keyboard_target == KEYBOARD_NAME) {
		return NAME_CAPACITY;
	}
	if (keyboard_target == KEYBOARD_ENDPOINT) {
		return ENDPOINT_CAPACITY;
	}
	if (keyboard_target == KEYBOARD_API_KEY) {
		return API_KEY_CAPACITY;
	}
	return MESSAGE_CAPACITY;
}

static void activate_keyboard_key(void) {
	char key = keyboard[keyboard_row][keyboard_column];
	char *target = keyboard_target_text();
	int capacity = keyboard_target_capacity();
	if (key == 0) {
		return;
	}
	if (key == KEY_BACKSPACE) {
		remove_last_character(target);
	} else if (key == KEY_DONE) {
		keyboard_open = 0;
		if (keyboard_target != KEYBOARD_MESSAGE) {
			if (screen == SCREEN_SETTINGS) {
				settings_focus = keyboard_target == KEYBOARD_NAME ? 1 : keyboard_target == KEYBOARD_ENDPOINT ? 2 : 3;
			} else {
				focus = 1;
			}
		}
	} else {
		append_character(target, capacity, key);
	}
}

static void draw_input(void) {
	uint32_t border = focus == 0 && !keyboard_open ? COLOR_ACCENT : COLOR_CARD_EDGE;
	draw_border_width(526.0f, 230.0f, 350.0f, 39.0f, 7.0f, 2.0f, border, COLOR_FIELD);

	if (user_name[0] != '\0') {
		draw_text(540.0f, 257.0f, 1.55f, COLOR_TEXT, user_name);
	}
	if (focus == 0 && !keyboard_open) {
		int cursor_x = 540 + text_width(1.55f, user_name);
		if (cursor_x > 860) {
			cursor_x = 860;
		}
		vita2d_draw_rectangle(cursor_x, 241.0f, 2.0f, 21.0f, COLOR_ACCENT);
	}
}

static void draw_keyboard(void) {
	draw_round_rect(505.0f, 292.0f, 380.0f, 211.0f, 12.0f, RGBA8(12, 14, 26, 255));
	draw_text(526.0f, 315.0f, 1.1f, COLOR_TEXT,
		keyboard_target == KEYBOARD_NAME ? "EDITA TU NOMBRE" : keyboard_target == KEYBOARD_ENDPOINT ? "EDITA ENDPOINT" : keyboard_target == KEYBOARD_API_KEY ? "EDITA API KEY" : "ESCRIBE TU MENSAJE");

	const float key_width = 51.0f;
	const float key_height = 24.0f;
	const float gap = 5.0f;
	const float start_x = 523.0f;
	const float start_y = 333.0f;

	for (int row = 0; row < KEYBOARD_ROWS; ++row) {
		for (int column = 0; column < KEYBOARD_COLUMNS; ++column) {
			char key = keyboard[row][column];
			if (key == 0) {
				continue;
			}

			float x = start_x + column * (key_width + gap);
			float y = start_y + row * (key_height + gap);
			uint32_t border = row == keyboard_row && column == keyboard_column ? COLOR_ACCENT : COLOR_CARD_EDGE;
			draw_border(x, y, key_width, key_height, 5.0f, border, COLOR_KEY);

			const char *label = NULL;
			char single[2] = { key, '\0' };
			if (key == ' ') {
				label = "ESP";
			} else if (key == KEY_BACKSPACE) {
				label = "BOR";
			} else if (key == KEY_DONE) {
				label = "OK";
			} else {
				label = single;
			}

			draw_text_centered(x, y + 17.0f, key_width, 0.85f, COLOR_TEXT, label);
		}
	}

	draw_text(526.0f, 494.0f, 0.72f, COLOR_MUTED, "CRUZ: ELEGIR   CIRCULO: CERRAR");
}

static void draw_name_interface(void) {
	draw_background();
	draw_round_rect(CARD_X, CARD_Y, CARD_WIDTH, CARD_HEIGHT, CARD_RADIUS, COLOR_CARD_RIGHT);
	draw_left_panel(CARD_X, CARD_Y, SPLIT_X - CARD_X, CARD_HEIGHT, CARD_RADIUS, COLOR_CARD_LEFT);
	vita2d_draw_rectangle(SPLIT_X, CARD_Y + CARD_RADIUS, 1.0f, CARD_HEIGHT - CARD_RADIUS * 2.0f, COLOR_CARD_EDGE);
	draw_menu_icon(77.0f, 64.0f, COLOR_TEXT);

	draw_badge(79.0f, 53.0f, 128.0f, "ROUTER SEGURO");
	draw_text(79.0f, 103.0f, 2.55f, COLOR_TEXT, "TU ESPACIO");
	draw_text(79.0f, 135.0f, 2.55f, COLOR_TEXT, "EN VAGAROUTE");
	draw_text(79.0f, 190.0f, 0.92f, COLOR_MUTED, "TU NOMBRE PERSONALIZA");
	draw_text(79.0f, 207.0f, 0.92f, COLOR_MUTED, "LA EXPERIENCIA EN VAGAROUTE.");
	draw_text(79.0f, 224.0f, 0.92f, COLOR_MUTED, "TODO SE GUARDA EN TU VITA.");

	vita2d_draw_line(81.0f, 276.0f, 94.0f, 276.0f, COLOR_ACCENT);
	vita2d_draw_line(91.0f, 272.0f, 96.0f, 276.0f, COLOR_ACCENT);
	vita2d_draw_line(91.0f, 280.0f, 96.0f, 276.0f, COLOR_ACCENT);
	draw_text(104.0f, 281.0f, 0.92f, COLOR_MUTED, "CONFIGURACION LOCAL.");
	vita2d_draw_line(81.0f, 321.0f, 94.0f, 321.0f, COLOR_ACCENT);
	vita2d_draw_line(91.0f, 317.0f, 96.0f, 321.0f, COLOR_ACCENT);
	vita2d_draw_line(91.0f, 325.0f, 96.0f, 321.0f, COLOR_ACCENT);
	draw_text(104.0f, 326.0f, 0.92f, COLOR_MUTED, "INICIO RAPIDO.");
	vita2d_draw_line(81.0f, 366.0f, 94.0f, 366.0f, COLOR_ACCENT);
	vita2d_draw_line(91.0f, 362.0f, 96.0f, 366.0f, COLOR_ACCENT);
	vita2d_draw_line(91.0f, 370.0f, 96.0f, 366.0f, COLOR_ACCENT);
	draw_text(104.0f, 371.0f, 0.92f, COLOR_MUTED, "DATOS BAJO TU CONTROL.");

	draw_logo(530.0f, 52.0f);
	draw_badge(526.0f, 105.0f, 122.0f, "VAGAROUTE AI");
	draw_text(526.0f, 158.0f, 1.9f, COLOR_TEXT, "CUAL ES TU NOMBRE?");
	draw_text(526.0f, 190.0f, 0.9f, COLOR_MUTED, "COMENZAREMOS PERSONALIZANDO");
	draw_text(526.0f, 207.0f, 0.9f, COLOR_MUTED, "TU ESPACIO LOCAL.");

	draw_text(526.0f, 226.0f, 0.72f, COLOR_MUTED, "NOMBRE");
	draw_input();

	draw_border_width(526.0f, 286.0f, 350.0f, 38.0f, 7.0f, 2.0f, COLOR_ACCENT, COLOR_ACCENT);
	draw_text_centered(526.0f, 311.0f, 350.0f, 0.95f, RGBA8(25, 19, 16, 255), user_name[0] == '\0' ? "CONTINUAR" : "GUARDAR CAMBIOS");

	draw_text(526.0f, 350.0f, 0.72f, COLOR_SUBTLE, "LA CONFIGURACION SE GUARDA SOLO");
	draw_text(526.0f, 365.0f, 0.72f, COLOR_SUBTLE, "EN UX0:DATA/VAGAROUTEAI.");
	if (status_message[0] != '\0') {
		draw_text(526.0f, 393.0f, 0.72f, status_is_error ? COLOR_ERROR : COLOR_SUCCESS, status_message);
	}

	draw_text(526.0f, 468.0f, 0.72f, COLOR_MUTED, "CRUZ: EDITAR   ARRIBA/ABAJO: CAMBIAR");
	draw_text(526.0f, 486.0f, 0.72f, COLOR_MUTED, "START: SALIR");

	if (keyboard_open) {
		draw_keyboard();
	}
}

static void draw_icon_stroke(float x0, float y0, float x1, float y1, uint32_t color) {
	vita2d_draw_line(x0, y0, x1, y1, color);
	if (x0 == x1) {
		vita2d_draw_line(x0 + 1.0f, y0, x1 + 1.0f, y1, color);
	} else {
		vita2d_draw_line(x0, y0 + 1.0f, x1, y1 + 1.0f, color);
	}
}

static void draw_menu_icon(float x, float y, uint32_t color) {
	draw_icon_stroke(x, y, x + 28.0f, y, color);
	draw_icon_stroke(x, y + 9.0f, x + 28.0f, y + 9.0f, color);
	draw_icon_stroke(x, y + 18.0f, x + 28.0f, y + 18.0f, color);
}

static void draw_icon_ring(float x, float y, uint32_t color) {
	static const uint8_t points[][2] = {
		{ 12, 1 }, { 20, 4 }, { 23, 12 }, { 20, 20 },
		{ 12, 23 }, { 4, 20 }, { 1, 12 }, { 4, 4 }
	};
	for (size_t index = 0; index < 8; ++index) {
		const uint8_t *from = points[index];
		const uint8_t *to = points[(index + 1) % 8];
		draw_icon_stroke(x + from[0], y + from[1], x + to[0], y + to[1], color);
	}
}

static void draw_sidebar_icon(float x, float y, int type, uint32_t color) {
	if (type == 0) {
		draw_icon_stroke(x + 3.0f, y + 4.0f, x + 20.0f, y + 4.0f, color);
		draw_icon_stroke(x + 20.0f, y + 4.0f, x + 23.0f, y + 7.0f, color);
		draw_icon_stroke(x + 23.0f, y + 7.0f, x + 23.0f, y + 17.0f, color);
		draw_icon_stroke(x + 23.0f, y + 17.0f, x + 20.0f, y + 20.0f, color);
		draw_icon_stroke(x + 20.0f, y + 20.0f, x + 7.0f, y + 20.0f, color);
		draw_icon_stroke(x + 7.0f, y + 20.0f, x + 3.0f, y + 24.0f, color);
		draw_icon_stroke(x + 4.0f, y + 20.0f, x + 3.0f, y + 7.0f, color);
		draw_icon_stroke(x + 3.0f, y + 7.0f, x + 3.0f, y + 4.0f, color);
		vita2d_draw_fill_circle(x + 9.0f, y + 12.0f, 1.0f, color);
		vita2d_draw_fill_circle(x + 14.0f, y + 12.0f, 1.0f, color);
		draw_icon_stroke(x + 25.0f, y + 3.0f, x + 25.0f, y + 11.0f, color);
		draw_icon_stroke(x + 22.0f, y + 7.0f, x + 28.0f, y + 7.0f, color);
	} else if (type == 1) {
		draw_icon_ring(x, y, color);
		draw_icon_stroke(x + 12.0f, y + 12.0f, x + 12.0f, y + 6.0f, color);
		draw_icon_stroke(x + 12.0f, y + 12.0f, x + 17.0f, y + 15.0f, color);
		draw_icon_stroke(x + 19.0f, y + 2.0f, x + 23.0f, y + 2.0f, color);
		draw_icon_stroke(x + 23.0f, y + 2.0f, x + 23.0f, y + 6.0f, color);
	} else if (type == 2) {
		draw_icon_stroke(x + 3.0f, y + 5.0f, x + 21.0f, y + 5.0f, color);
		draw_icon_stroke(x + 21.0f, y + 5.0f, x + 23.0f, y + 7.0f, color);
		draw_icon_stroke(x + 23.0f, y + 7.0f, x + 23.0f, y + 21.0f, color);
		draw_icon_stroke(x + 23.0f, y + 21.0f, x + 3.0f, y + 21.0f, color);
		draw_icon_stroke(x + 3.0f, y + 21.0f, x + 3.0f, y + 5.0f, color);
		vita2d_draw_fill_circle(x + 17.0f, y + 10.0f, 2.0f, color);
		draw_icon_stroke(x + 5.0f, y + 19.0f, x + 10.0f, y + 13.0f, color);
		draw_icon_stroke(x + 10.0f, y + 13.0f, x + 15.0f, y + 18.0f, color);
		draw_icon_stroke(x + 15.0f, y + 18.0f, x + 19.0f, y + 14.0f, color);
		draw_icon_stroke(x + 25.0f, y + 2.0f, x + 25.0f, y + 8.0f, color);
		draw_icon_stroke(x + 22.0f, y + 5.0f, x + 28.0f, y + 5.0f, color);
	} else {
		draw_icon_ring(x, y, color);
		vita2d_draw_fill_circle(x + 12.0f, y + 12.0f, 4.0f, color);
		vita2d_draw_fill_circle(x + 12.0f, y + 12.0f, 2.0f, RGBA8(17, 19, 35, 255));
		draw_icon_stroke(x + 12.0f, y + 0.0f, x + 12.0f, y + 3.0f, color);
		draw_icon_stroke(x + 12.0f, y + 21.0f, x + 12.0f, y + 24.0f, color);
		draw_icon_stroke(x + 0.0f, y + 12.0f, x + 3.0f, y + 12.0f, color);
		draw_icon_stroke(x + 21.0f, y + 12.0f, x + 24.0f, y + 12.0f, color);
	}
}

static void draw_model_icon(float x, float y, uint32_t color) {
	draw_icon_stroke(x + 5.0f, y + 4.0f, x + 19.0f, y + 4.0f, color);
	draw_icon_stroke(x + 19.0f, y + 4.0f, x + 19.0f, y + 20.0f, color);
	draw_icon_stroke(x + 19.0f, y + 20.0f, x + 5.0f, y + 20.0f, color);
	draw_icon_stroke(x + 5.0f, y + 20.0f, x + 5.0f, y + 4.0f, color);
	for (int offset = 0; offset < 3; ++offset) {
		draw_icon_stroke(x + 1.0f, y + 7.0f + offset * 5.0f, x + 5.0f, y + 7.0f + offset * 5.0f, color);
		draw_icon_stroke(x + 19.0f, y + 7.0f + offset * 5.0f, x + 23.0f, y + 7.0f + offset * 5.0f, color);
	}
	vita2d_draw_fill_circle(x + 12.0f, y + 12.0f, 2.0f, color);
}

static void draw_options_icon(float x, float y, uint32_t color) {
	draw_icon_stroke(x + 1.0f, y + 4.0f, x + 23.0f, y + 4.0f, color);
	draw_icon_stroke(x + 1.0f, y + 12.0f, x + 23.0f, y + 12.0f, color);
	draw_icon_stroke(x + 1.0f, y + 20.0f, x + 23.0f, y + 20.0f, color);
	vita2d_draw_fill_circle(x + 8.0f, y + 4.0f, 2.0f, color);
	vita2d_draw_fill_circle(x + 17.0f, y + 12.0f, 2.0f, color);
	vita2d_draw_fill_circle(x + 10.0f, y + 20.0f, 2.0f, color);
}

static void draw_send_icon(float x, float y, uint32_t color) {
	draw_icon_stroke(x + 2.0f, y + 10.0f, x + 25.0f, y + 4.0f, color);
	draw_icon_stroke(x + 25.0f, y + 4.0f, x + 15.0f, y + 20.0f, color);
	draw_icon_stroke(x + 15.0f, y + 20.0f, x + 16.0f, y + 12.0f, color);
	draw_icon_stroke(x + 16.0f, y + 12.0f, x + 2.0f, y + 10.0f, color);
}

static void draw_close_icon(float x, float y, uint32_t color) {
	draw_icon_stroke(x + 3.0f, y + 3.0f, x + 21.0f, y + 21.0f, color);
	draw_icon_stroke(x + 21.0f, y + 3.0f, x + 3.0f, y + 21.0f, color);
}

static void draw_quick_card(float x, uint32_t icon_color, const char *title, const char *line_one, const char *line_two) {
	draw_border(x, 278.0f, 160.0f, 126.0f, 9.0f, COLOR_CARD_EDGE, RGBA8(19, 22, 40, 255));
	draw_sidebar_icon(x + 29.0f, 294.0f, 0, icon_color);
	draw_text(x + 17.0f, 348.0f, 0.88f, COLOR_TEXT, title);
	draw_text(x + 17.0f, 370.0f, 0.7f, COLOR_MUTED, line_one);
	draw_text(x + 17.0f, 390.0f, 0.7f, COLOR_MUTED, line_two);
}

static void draw_chat_header(void) {
	draw_round_rect(28.0f, 22.0f, 904.0f, 510.0f, 18.0f, COLOR_CARD_RIGHT);
	vita2d_draw_rectangle(29.0f, 101.0f, 902.0f, 1.0f, COLOR_CARD_EDGE);
	draw_menu_icon(77.0f, 64.0f, COLOR_TEXT);
	draw_logo(177.0f, 42.0f);
	draw_text(262.0f, 78.0f, 1.55f, COLOR_TEXT, "VagaChatVITA");

	uint32_t model_border = screen == SCREEN_CHAT && chat_focus == 2 ? COLOR_ACCENT : COLOR_CARD_EDGE;
	draw_border_width(480.0f, 50.0f, 340.0f, 43.0f, 9.0f, 2.0f, model_border, RGBA8(14, 17, 33, 255));
	draw_model_icon(500.0f, 59.0f, COLOR_MUTED);
	draw_text(530.0f, 78.0f, 0.82f, COLOR_MUTED, "Modelo");
	vita2d_draw_line(605.0f, 58.0f, 605.0f, 85.0f, COLOR_CARD_EDGE);
	draw_text_fit(642.0f, 78.0f, 135.0f, 0.82f, selected_model[0] == '\0' ? COLOR_MUTED : COLOR_TEXT, selected_model[0] == '\0' ? "No hay modelos" : selected_model);
	if (model_selector_open) {
		vita2d_draw_line(795.0f, 75.0f, 802.0f, 68.0f, COLOR_MUTED);
		vita2d_draw_line(802.0f, 68.0f, 809.0f, 75.0f, COLOR_MUTED);
	} else {
		vita2d_draw_line(795.0f, 68.0f, 802.0f, 75.0f, COLOR_MUTED);
		vita2d_draw_line(802.0f, 75.0f, 809.0f, 68.0f, COLOR_MUTED);
	}

	uint32_t connection_color = connection_state == CONNECTION_ONLINE ? COLOR_SUCCESS : connection_state == CONNECTION_VERIFYING ? COLOR_ACCENT : COLOR_SUBTLE;
	const char *connection_text = connection_state == CONNECTION_ONLINE ? "En linea" : connection_state == CONNECTION_VERIFYING ? "Verificando..." : "Sin conexion";
	vita2d_draw_fill_circle(844.0f, 71.0f, 4.0f, connection_color);
	draw_text(856.0f, 78.0f, 0.78f, COLOR_MUTED, connection_text);
}

static void draw_chat_sidebar(int selected) {
	vita2d_draw_rectangle(29.0f, 102.0f, 100.0f, 430.0f, RGBA8(14, 17, 33, 255));
	static const float selection_y[] = { 126.0f, 206.0f, 286.0f, 374.0f };
	if (selected >= 0 && selected < 4) {
		draw_border_width(45.0f, selection_y[selected], 68.0f, 68.0f, 12.0f, 2.0f, COLOR_ACCENT, RGBA8(25, 27, 46, 255));
	}
	draw_sidebar_icon(65.0f, 148.0f, 0, selected == 0 ? COLOR_ACCENT : COLOR_MUTED);

	vita2d_draw_line(48.0f, 200.0f, 110.0f, 200.0f, COLOR_CARD_EDGE);
	draw_sidebar_icon(67.0f, 228.0f, 1, selected == 1 ? COLOR_ACCENT : COLOR_MUTED);
	vita2d_draw_line(48.0f, 280.0f, 110.0f, 280.0f, COLOR_CARD_EDGE);
	draw_sidebar_icon(67.0f, 308.0f, 2, selected == 2 ? COLOR_ACCENT : COLOR_MUTED);
	vita2d_draw_line(48.0f, 360.0f, 110.0f, 360.0f, COLOR_CARD_EDGE);
	draw_sidebar_icon(67.0f, 388.0f, 3, selected == 3 ? COLOR_ACCENT : COLOR_MUTED);
}

static void draw_chat_landing(void) {
	draw_border_width(145.0f, 121.0f, 775.0f, 300.0f, 17.0f, 2.0f, COLOR_CARD_EDGE, RGBA8(11, 15, 30, 255));
	draw_logo(492.0f, 143.0f);
	draw_text_centered(145.0f, 226.0f, 775.0f, 2.05f, COLOR_TEXT, "Nueva conversacion");
	draw_text_centered(145.0f, 253.0f, 775.0f, 0.9f, COLOR_MUTED, "Escribe una pregunta, pide ayuda o genera ideas.");

	draw_quick_card(174.0f, COLOR_ACCENT_SOFT, "Explicame este", "Entiende y", "soluciona errores.");
	draw_quick_card(360.0f, RGBA8(35, 133, 255, 255), "Resume un texto", "Obten un resumen", "claro y conciso.");
	draw_quick_card(546.0f, COLOR_ACCENT, "Genera ideas", "Brainstorm de", "ideas utiles.");
	draw_quick_card(732.0f, COLOR_SUCCESS, "Crear imagen", "Genera imagenes", "(beta).");
}

static int next_chat_line(const char *text, int *offset, char *line, int capacity, float width) {
	int source = *offset;
	int start = source;
	int line_length = 0;
	int last_space = -1;
	if (text[source] == '\0') {
		return 0;
	}
	if (text[source] == '\n') {
		line[0] = '\0';
		*offset = source + 1;
		return 1;
	}
	while (text[source] != '\0' && line_length < capacity - 1) {
		if (text[source] == '\n') {
			++source;
			break;
		}
		int previous_source = source;
		int sequence = ((unsigned char)text[source] & 0x80) == 0 ? 1 :
			(((unsigned char)text[source] & 0xE0) == 0xC0 ? 2 :
			((unsigned char)text[source] & 0xF0) == 0xE0 ? 3 : 4);
		if (line_length + sequence >= capacity) {
			break;
		}
		for (int byte = 0; byte < sequence && text[source] != '\0'; ++byte) {
			line[line_length++] = text[source++];
		}
		line[line_length] = '\0';
		if (line[line_length - 1] == ' ') {
			last_space = line_length - 1;
		}
		if (text_width(0.70f, line) > width) {
			if (last_space > 0) {
				line_length = last_space;
				source = start + last_space + 1;
			} else if (line_length > sequence) {
				line_length -= sequence;
				source = previous_source;
			}
			break;
		}
	}
	while (line_length > 0 && line[line_length - 1] == ' ') {
		--line_length;
	}
	line[line_length] = '\0';
	if (source == start) {
		source += 1;
	}
	*offset = source;
	return 1;
}

static int chat_line_count(const char *text, float width) {
	char line[192];
	int offset = 0;
	int count = 0;
	while (next_chat_line(text, &offset, line, sizeof(line), width)) {
		++count;
	}
	return count > 0 ? count : 1;
}

static void draw_chat_message(int index, int count, int y, int height) {
	ChatRole role = CHAT_ROLE_USER;
	char content[CHAT_MAX_MESSAGE_BYTES + 1];
	if (chat_copy_message(index, &role, content, sizeof(content)) < 0) {
		return;
	}
	const int bubble_width = role == CHAT_ROLE_USER ? 500 : 610;
	const int bubble_x = role == CHAT_ROLE_USER ? 397 : 166;
	uint32_t bubble_color = role == CHAT_ROLE_USER ? RGBA8(49, 38, 58, 255) : RGBA8(26, 46, 47, 255);
	uint32_t role_color = role == CHAT_ROLE_USER ? COLOR_ACCENT : COLOR_SUCCESS;
	draw_round_rect((float)bubble_x, (float)y, (float)bubble_width, (float)height, 12.0f, bubble_color);
	draw_text((float)bubble_x + 16.0f, (float)y + 21.0f, 0.66f, role_color,
		role == CHAT_ROLE_USER ? "Tu" : "VagaRoute AI");

	char line[192];
	int source = 0;
	int line_number = 0;
	const float text_width_limit = (float)bubble_width - 32.0f;
	while (next_chat_line(content, &source, line, sizeof(line), text_width_limit)) {
		draw_text((float)bubble_x + 16.0f, (float)y + 43.0f + line_number * 20.0f,
			0.70f, COLOR_TEXT, line[0] != '\0' ? line : " ");
		++line_number;
	}
	if (line_number == 0) {
		draw_text((float)bubble_x + 16.0f, (float)y + 43.0f, 0.70f, COLOR_MUTED, "...");
	}

	char counter[24];
	int counter_length = 0;
	if (index + 1 >= 10) {
		counter[counter_length++] = (char)('0' + (index + 1) / 10);
	}
	counter[counter_length++] = (char)('0' + (index + 1) % 10);
	counter[counter_length++] = '/';
	if (count >= 10) {
		counter[counter_length++] = (char)('0' + count / 10);
	}
	counter[counter_length++] = (char)('0' + count % 10);
	counter[counter_length] = '\0';
	draw_text((float)(bubble_x + bubble_width - 49), (float)y + 21.0f, 0.60f, COLOR_MUTED, counter);
}

static void draw_chat_history(void) {
	const int view_left = 158;
	const int view_top = 115;
	const int view_right = 907;
	const int view_bottom = 405;
	const int view_height = view_bottom - view_top;
	const int message_gap = 10;
	int heights[CHAT_MAX_MESSAGES] = { 0 };
	int count = chat_message_count();
	int total_height = 0;

	draw_border_width(145.0f, 110.0f, 775.0f, 310.0f, 17.0f, 2.0f, COLOR_CARD_EDGE, RGBA8(11, 15, 30, 255));
	for (int index = 0; index < count && index < CHAT_MAX_MESSAGES; ++index) {
		ChatRole role = CHAT_ROLE_USER;
		char content[CHAT_MAX_MESSAGE_BYTES + 1];
		if (chat_copy_message(index, &role, content, sizeof(content)) < 0) {
			content[0] = '\0';
		}
		int bubble_width = role == CHAT_ROLE_USER ? 500 : 610;
		heights[index] = 34 + chat_line_count(content, (float)bubble_width - 32.0f) * 20;
		if (heights[index] < 54) {
			heights[index] = 54;
		}
		total_height += heights[index];
	}
	if (count > 1) {
		total_height += (count - 1) * message_gap;
	}
	chat_scroll_max = total_height > view_height ? total_height - view_height : 0;
	if (chat_follow_bottom) {
		chat_scroll_offset = chat_scroll_max;
	}
	if (chat_scroll_offset > chat_scroll_max) {
		chat_scroll_offset = chat_scroll_max;
	}
	if (chat_scroll_offset < 0) {
		chat_scroll_offset = 0;
	}
	int content_y = total_height < view_height ? view_top + 12 : view_top - chat_scroll_offset;

	vita2d_set_clip_rectangle(view_left, view_top, view_right, view_bottom);
	vita2d_enable_clipping();
	for (int index = 0; index < count && index < CHAT_MAX_MESSAGES; ++index) {
		draw_chat_message(index, count, content_y, heights[index]);
		content_y += heights[index] + message_gap;
	}
	vita2d_disable_clipping();

	if (chat_scroll_max > 0) {
		uint32_t scroll_color = COLOR_MUTED;
		if (chat_scroll_offset > 0) {
			vita2d_draw_line(894.0f, 128.0f, 889.0f, 134.0f, scroll_color);
			vita2d_draw_line(889.0f, 134.0f, 899.0f, 134.0f, scroll_color);
		}
		if (chat_scroll_offset < chat_scroll_max) {
			vita2d_draw_line(889.0f, 389.0f, 899.0f, 389.0f, scroll_color);
			vita2d_draw_line(899.0f, 389.0f, 894.0f, 395.0f, scroll_color);
		}
	}
}

static void draw_history_modal(void) {
	vita2d_draw_rectangle(0.0f, 0.0f, SCREEN_WIDTH, SCREEN_HEIGHT, RGBA8(3, 5, 12, 190));
	draw_border_width(170.0f, 86.0f, 620.0f, 374.0f, 18.0f, 2.0f, COLOR_CARD_EDGE, RGBA8(13, 17, 34, 255));
	draw_text(205.0f, 139.0f, 1.55f, COLOR_TEXT, "Historial");
	draw_text(205.0f, 164.0f, 0.76f, COLOR_MUTED, "Conversaciones guardadas en esta sesion.");
	draw_close_icon(744.0f, 109.0f, COLOR_MUTED);

	int conversation_count = chat_conversation_count();
	if (conversation_count == 0) {
		draw_text(205.0f, 245.0f, 0.9f, COLOR_MUTED, "Aun no hay conversaciones.");
	} else {
		for (int row = 0; row < conversation_count; ++row) {
			int history_index = conversation_count - row - 1;
			float y = 184.0f + row * 64.0f;
			uint32_t border = history_index == history_selection ? COLOR_ACCENT : COLOR_CARD_EDGE;
			draw_border(205.0f, y, 550.0f, 52.0f, 9.0f, border, RGBA8(19, 23, 43, 255));
			draw_sidebar_icon(222.0f, y + 13.0f, 0, COLOR_ACCENT_SOFT);
			char title[CHAT_MAX_TITLE_BYTES + 1];
			chat_copy_conversation_title(history_index, title, sizeof(title));
			draw_text(262.0f, y + 22.0f, 0.72f, COLOR_MUTED, history_index == chat_active_conversation() ? "Conversacion activa" : "Conversacion guardada");
			draw_text_fit(262.0f, y + 42.0f, 450.0f, 0.76f, COLOR_TEXT, title);
		}
	}
}

static void draw_info_modal(void) {
	vita2d_draw_rectangle(0.0f, 0.0f, SCREEN_WIDTH, SCREEN_HEIGHT, RGBA8(3, 5, 12, 190));
	draw_border_width(210.0f, 155.0f, 540.0f, 190.0f, 18.0f, 2.0f, COLOR_CARD_EDGE, RGBA8(13, 17, 34, 255));
	draw_text(245.0f, 210.0f, 1.2f, COLOR_TEXT, "Imagenes beta");
	draw_close_icon(700.0f, 178.0f, COLOR_MUTED);
	draw_text(245.0f, 247.0f, 0.82f, COLOR_MUTED, "Este modulo estara disponible proximamente.");
	draw_text(245.0f, 273.0f, 0.78f, COLOR_SUBTLE, "La navegacion ya esta preparada para esta seccion.");
}

static void draw_side_menu_overlay(void) {
	draw_round_rect(20.0f, 22.0f, 120.0f, 510.0f, 18.0f, RGBA8(11, 14, 29, 255));
	draw_chat_sidebar(side_menu_selection);
	vita2d_draw_rectangle(129.0f, 102.0f, 1.0f, 430.0f, COLOR_CARD_EDGE);
}

static int sidebar_item_at(int x, int y) {
	if (x < 45 || x >= 113) {
		return -1;
	}
	if (y >= 126 && y < 194) {
		return 0;
	}
	if (y >= 206 && y < 274) {
		return 1;
	}
	if (y >= 286 && y < 354) {
		return 2;
	}
	if (y >= 374 && y < 442) {
		return 3;
	}
	return -1;
}

static void activate_side_menu(void) {
	side_menu_open = 0;
	history_modal = 0;
	info_modal = 0;
	status_message[0] = '\0';

	if (side_menu_selection == 0) {
		message_text[0] = '\0';
		if (chat_new_conversation() < 0) {
			set_status("CANCELA LA RESPUESTA ANTES DE CREAR OTRO CHAT.", 1);
			screen = SCREEN_CHAT;
			return;
		}
		chat_follow_bottom = 1;
		chat_scroll_offset = 0;
		screen = user_name[0] == '\0' ? SCREEN_NAME : SCREEN_CHAT;
	} else if (side_menu_selection == 1) {
		if (user_name[0] != '\0') {
			screen = SCREEN_CHAT;
		}
		history_modal = 1;
		history_selection = chat_active_conversation();
	} else if (side_menu_selection == 2) {
		info_modal = 1;
	} else {
		screen = SCREEN_SETTINGS;
		settings_focus = 0;
	}
}

static void draw_chat_composer(void) {
	draw_border_width(145.0f, 432.0f, 775.0f, 70.0f, 15.0f, 2.0f, COLOR_CARD_EDGE, RGBA8(17, 21, 43, 255));
	draw_border(160.0f, 443.0f, 54.0f, 48.0f, 10.0f, COLOR_CARD_EDGE, RGBA8(30, 36, 67, 255));
	vita2d_draw_line(178.0f, 475.0f, 195.0f, 458.0f, COLOR_TEXT);
	vita2d_draw_line(195.0f, 458.0f, 203.0f, 466.0f, COLOR_TEXT);
	vita2d_draw_line(186.0f, 467.0f, 194.0f, 475.0f, COLOR_TEXT);

	uint32_t input_border = chat_focus == 0 ? COLOR_ACCENT_SOFT : COLOR_CARD_EDGE;
	draw_border_width(226.0f, 443.0f, 390.0f, 48.0f, 10.0f, 2.0f, input_border, RGBA8(15, 19, 39, 255));
	if (message_text[0] == '\0') {
		draw_text(246.0f, 474.0f, 0.9f, COLOR_MUTED, "Escribe tu mensaje...");
	} else {
		draw_text_fit(246.0f, 474.0f, 350.0f, 0.82f, COLOR_TEXT, message_text);
	}

	uint32_t mode_color = input_mode == 0 ? COLOR_ACCENT : COLOR_SUCCESS;
	draw_border_width(627.0f, 443.0f, 118.0f, 48.0f, 10.0f, 2.0f, mode_color, RGBA8(20, 24, 48, 255));
	draw_text_centered(627.0f, 473.0f, 118.0f, 0.82f, mode_color, input_mode == 0 ? "Texto" : "Imagen");

	uint32_t send_border = chat_focus == 1 ? COLOR_TEXT : COLOR_ACCENT;
	draw_border_width(773.0f, 443.0f, 132.0f, 48.0f, 10.0f, 2.0f, send_border, COLOR_ACCENT);
	draw_send_icon(816.0f, 456.0f, RGBA8(25, 19, 16, 255));
}

static void draw_settings_interface(void) {
	draw_background();
	draw_chat_header();
	draw_chat_sidebar(3);

	draw_border_width(145.0f, 110.0f, 775.0f, 385.0f, 17.0f, 2.0f, COLOR_CARD_EDGE, RGBA8(11, 15, 30, 255));
	draw_text(173.0f, 150.0f, 1.9f, COLOR_TEXT, "Ajustes");
	draw_text(173.0f, 176.0f, 0.78f, COLOR_MUTED, "Configura tu conexion y tu perfil local.");
	draw_close_icon(865.0f, 125.0f, COLOR_MUTED);

	draw_text(173.0f, 193.0f, 0.78f, COLOR_MUTED, "NOMBRE DE USUARIO");
	uint32_t name_border = settings_focus == 0 ? COLOR_ACCENT : COLOR_CARD_EDGE;
	draw_border_width(173.0f, 199.0f, 700.0f, 38.0f, 8.0f, 2.0f, name_border, COLOR_FIELD);
	if (user_name[0] == '\0') {
		draw_text(194.0f, 225.0f, 0.82f, COLOR_MUTED, "Sin nombre configurado");
	} else {
		draw_text_fit(194.0f, 225.0f, 650.0f, 0.82f, COLOR_TEXT, user_name);
	}

	draw_text(173.0f, 250.0f, 0.78f, COLOR_MUTED, "ENDPOINT URL");
	uint32_t endpoint_border = settings_focus == 1 ? COLOR_ACCENT : COLOR_CARD_EDGE;
	draw_border_width(173.0f, 256.0f, 700.0f, 38.0f, 8.0f, 2.0f, endpoint_border, COLOR_FIELD);
	draw_text_fit(194.0f, 282.0f, 650.0f, 0.82f, endpoint_url[0] == '\0' ? COLOR_MUTED : COLOR_TEXT, endpoint_url[0] == '\0' ? "https://ejemplo.com/v1" : endpoint_url);

	draw_text(173.0f, 307.0f, 0.78f, COLOR_MUTED, "API KEY");
	uint32_t api_border = settings_focus == 2 ? COLOR_ACCENT : COLOR_CARD_EDGE;
	draw_border_width(173.0f, 313.0f, 700.0f, 38.0f, 8.0f, 2.0f, api_border, COLOR_FIELD);
	if (api_key[0] == '\0') {
		draw_text(194.0f, 339.0f, 0.82f, COLOR_MUTED, "Sin API Key configurada");
	} else {
		char masked[33];
		int mask_length = (int)strlen(api_key);
		if (mask_length > (int)sizeof(masked) - 1) {
			mask_length = sizeof(masked) - 1;
		}
		for (int index = 0; index < mask_length; ++index) {
			masked[index] = '*';
		}
		masked[mask_length] = '\0';
		draw_text(194.0f, 339.0f, 0.82f, COLOR_TEXT, masked);
	}

	uint32_t verify_border = settings_focus == 3 ? COLOR_TEXT : COLOR_ACCENT;
	draw_border_width(173.0f, 372.0f, 220.0f, 42.0f, 9.0f, 2.0f, verify_border, COLOR_ACCENT);
	draw_text_centered(173.0f, 399.0f, 220.0f, 0.76f, RGBA8(25, 19, 16, 255), "Verificar conexion");

	uint32_t back_border = settings_focus == 4 ? COLOR_TEXT : COLOR_CARD_EDGE;
	draw_border_width(413.0f, 372.0f, 180.0f, 42.0f, 9.0f, 2.0f, back_border, RGBA8(20, 24, 48, 255));
	draw_text_centered(413.0f, 399.0f, 180.0f, 0.76f, COLOR_TEXT, "Volver al chat");

	draw_text(173.0f, 440.0f, 0.7f, connection_state == CONNECTION_ONLINE ? COLOR_SUCCESS : COLOR_MUTED, connection_state == CONNECTION_ONLINE ? "CONEXION DISPONIBLE" : connection_state == CONNECTION_VERIFYING ? "VERIFICANDO CONEXION..." : "SIN CONEXION");
	if (status_message[0] != '\0') {
		draw_text(173.0f, 463.0f, 0.7f, status_is_error ? COLOR_ERROR : COLOR_SUCCESS, status_message);
	}
	draw_text(500.0f, 463.0f, 0.64f, COLOR_SUBTLE, "CONFIG LOCAL");

	vita2d_draw_rectangle(29.0f, 502.0f, 902.0f, 30.0f, RGBA8(12, 15, 30, 255));
	draw_text(48.0f, 524.0f, 0.6f, COLOR_MUTED, "SELECT  Menu");
	draw_text(225.0f, 524.0f, 0.6f, COLOR_MUTED, "D-PAD  Mover");
	draw_options_icon(495.0f, 506.0f, COLOR_MUTED);
	draw_text(526.0f, 524.0f, 0.6f, COLOR_MUTED, "TRIANGULO  Opciones");
	draw_send_icon(735.0f, 505.0f, COLOR_MUTED);
	draw_text(765.0f, 524.0f, 0.6f, COLOR_MUTED, "CRUZ  Aceptar");

	if (keyboard_open) {
		draw_keyboard();
	}
}

static void draw_chat_interface(void) {
	draw_background();
	draw_chat_header();
	draw_chat_sidebar(0);
	if (chat_message_count() > 0) {
		draw_chat_history();
	} else {
		chat_scroll_offset = 0;
		chat_scroll_max = 0;
		draw_chat_landing();
	}
	if (status_message[0] != '\0') {
		draw_text(173.0f, 412.0f, 0.7f, status_is_error ? COLOR_ERROR : COLOR_SUCCESS, status_message);
	}
	draw_chat_composer();
	vita2d_draw_rectangle(29.0f, 502.0f, 902.0f, 30.0f, RGBA8(12, 15, 30, 255));
	draw_text(48.0f, 524.0f, 0.6f, COLOR_MUTED, "SELECT  Menu");
	draw_text(225.0f, 524.0f, 0.6f, COLOR_MUTED, "D-PAD  Mover");
	draw_options_icon(495.0f, 506.0f, COLOR_MUTED);
	draw_text(526.0f, 524.0f, 0.6f, COLOR_MUTED, "TRIANGULO  Opciones");
	draw_send_icon(735.0f, 505.0f, COLOR_MUTED);
	draw_text(765.0f, 524.0f, 0.6f, COLOR_MUTED, "CRUZ  Enviar");
	if (keyboard_open) {
		draw_keyboard();
	}
}

static int selected_model_index(void) {
	for (int index = 0; index < model_count; ++index) {
		if (strcmp(selected_model, models[index]) == 0) {
			return index;
		}
	}
	return model_count > 0 ? 0 : -1;
}

static void select_model(int index) {
	if (index < 0 || index >= model_count) {
		return;
	}
	strncpy(selected_model, models[index], sizeof(selected_model) - 1);
	selected_model[sizeof(selected_model) - 1] = '\0';
	model_cursor = index;
	model_selector_open = 0;
	save_config();
}

static void open_model_selector(void) {
	model_selector_open = 1;
	model_cursor = selected_model_index();
	if (model_cursor < 0) {
		model_cursor = 0;
	}
}

static void move_model_cursor(int delta) {
	if (model_count <= 0) {
		model_cursor = 0;
		return;
	}
	model_cursor += delta;
	if (model_cursor < 0) {
		model_cursor = model_count - 1;
	} else if (model_cursor >= model_count) {
		model_cursor = 0;
	}
}

static int model_item_at(int x, int y) {
	if (x < 480 || x >= 820 || y < 96 || model_count <= 0) {
		return -1;
	}
	int index = (y - 103) / 36;
	if (index < 0 || index >= model_count || y >= 103 + model_count * 36) {
		return -1;
	}
	return index;
}

static void draw_model_dropdown(void) {
	if (!model_selector_open) {
		return;
	}
	int height = model_count > 0 ? model_count * 36 + 14 : 62;
	draw_border_width(480.0f, 96.0f, 340.0f, (float)height, 12.0f, 2.0f, COLOR_CARD_EDGE, RGBA8(13, 17, 34, 255));
	if (model_count == 0) {
		draw_text(502.0f, 132.0f, 0.78f, COLOR_MUTED, "No hay modelos disponibles");
		return;
	}
	for (int index = 0; index < model_count; ++index) {
		float y = 103.0f + index * 36.0f;
		if (index == model_cursor) {
			draw_round_rect(490.0f, y, 320.0f, 32.0f, 7.0f, RGBA8(35, 29, 52, 255));
		}
		draw_text_fit(505.0f, y + 22.0f, 290.0f, 0.78f, index == model_cursor ? COLOR_ACCENT : COLOR_TEXT, models[index]);
	}
}

static void copy_utf8_to_utf16(const char *source, SceWChar16 *target) {
	size_t source_index = 0;
	size_t target_index = 0;
	while (source[source_index] != '\0' && target_index < SCE_IME_DIALOG_MAX_TEXT_LENGTH) {
		const unsigned char first = (unsigned char)source[source_index++];
		uint32_t codepoint = first;
		int continuation_count = 0;
		if ((first & 0xE0) == 0xC0) {
			codepoint = first & 0x1F;
			continuation_count = 1;
		} else if ((first & 0xF0) == 0xE0) {
			codepoint = first & 0x0F;
			continuation_count = 2;
		} else if ((first & 0xF8) == 0xF0) {
			codepoint = first & 0x07;
			continuation_count = 3;
		}

		for (int continuation = 0; continuation < continuation_count; ++continuation) {
			unsigned char value = (unsigned char)source[source_index++];
			if ((value & 0xC0) != 0x80) {
				codepoint = '?';
				break;
			}
			codepoint = (codepoint << 6) | (value & 0x3F);
		}

		if (codepoint <= 0xFFFF) {
			target[target_index++] = (SceWChar16)codepoint;
		} else if (target_index + 1 < SCE_IME_DIALOG_MAX_TEXT_LENGTH) {
			codepoint -= 0x10000;
			target[target_index++] = (SceWChar16)(0xD800 | (codepoint >> 10));
			target[target_index++] = (SceWChar16)(0xDC00 | (codepoint & 0x3FF));
		}
	}
	target[target_index] = 0;
}

static void copy_utf16_to_utf8(const SceWChar16 *source, char *target, int capacity) {
	int output = 0;
	for (int index = 0; source[index] != 0 && output < capacity - 1; ++index) {
		uint32_t codepoint = source[index];
		if (codepoint >= 0xD800 && codepoint <= 0xDBFF && source[index + 1] >= 0xDC00 && source[index + 1] <= 0xDFFF) {
			codepoint = 0x10000 + ((codepoint - 0xD800) << 10) + (source[++index] - 0xDC00);
		}

		if (codepoint <= 0x7F) {
			if (output + 1 >= capacity) {
				break;
			}
			target[output++] = (char)codepoint;
		} else if (codepoint <= 0x7FF) {
			if (output + 2 >= capacity) {
				break;
			}
			target[output++] = (char)(0xC0 | (codepoint >> 6));
			target[output++] = (char)(0x80 | (codepoint & 0x3F));
		} else if (codepoint <= 0xFFFF) {
			if (output + 3 >= capacity) {
				break;
			}
			target[output++] = (char)(0xE0 | (codepoint >> 12));
			target[output++] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
			target[output++] = (char)(0x80 | (codepoint & 0x3F));
		} else {
			if (output + 4 >= capacity) {
				break;
			}
			target[output++] = (char)(0xF0 | (codepoint >> 18));
			target[output++] = (char)(0x80 | ((codepoint >> 12) & 0x3F));
			target[output++] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
			target[output++] = (char)(0x80 | (codepoint & 0x3F));
		}
	}
	target[output] = '\0';
}

static int open_ime_dialog(int target) {
	static const SceWChar16 name_title[] = { 'N', 'o', 'm', 'b', 'r', 'e', 0 };
	static const SceWChar16 message_title[] = { 'M', 'e', 'n', 's', 'a', 'j', 'e', 0 };
	static const SceWChar16 endpoint_title[] = { 'E', 'n', 'd', 'p', 'o', 'i', 'n', 't', 0 };
	static const SceWChar16 api_key_title[] = { 'A', 'P', 'I', ' ', 'K', 'e', 'y', 0 };
	SceImeDialogParam param;
	if (!ime_module_loaded) {
		return 0;
	}

	memset(ime_input, 0, sizeof(ime_input));
	copy_utf8_to_utf16(keyboard_target_text(), ime_initial_text);
	sceImeDialogParamInit(&param);
	param.supportedLanguages = SCE_IME_LANGUAGE_SPANISH | SCE_IME_LANGUAGE_ENGLISH;
	param.languagesForced = 0;
	param.type = target == KEYBOARD_ENDPOINT ? SCE_IME_TYPE_URL : SCE_IME_TYPE_DEFAULT;
	param.option = 0;
	param.dialogMode = SCE_IME_DIALOG_DIALOG_MODE_WITH_CANCEL;
	param.textBoxMode = target == KEYBOARD_API_KEY ? SCE_IME_DIALOG_TEXTBOX_MODE_PASSWORD : SCE_IME_DIALOG_TEXTBOX_MODE_WITH_CLEAR;
	param.title = target == KEYBOARD_NAME ? name_title : target == KEYBOARD_ENDPOINT ? endpoint_title : target == KEYBOARD_API_KEY ? api_key_title : message_title;
	param.maxTextLength = target == KEYBOARD_NAME ? NAME_CAPACITY : target == KEYBOARD_ENDPOINT ? ENDPOINT_CAPACITY : target == KEYBOARD_API_KEY ? API_KEY_CAPACITY : MESSAGE_CAPACITY;
	param.initialText = ime_initial_text;
	param.inputTextBuffer = ime_input;
	param.enterLabel = target == KEYBOARD_MESSAGE ? SCE_IME_ENTER_LABEL_SEND : SCE_IME_ENTER_LABEL_DEFAULT;

	if (sceImeDialogInit(&param) < 0) {
		return 0;
	}
	ime_target = target;
	ime_origin_screen = screen;
	ime_open = 1;
	return 1;
}

static void update_ime_dialog(void) {
	if (sceImeDialogGetStatus() != SCE_COMMON_DIALOG_STATUS_FINISHED) {
		return;
	}

	SceImeDialogResult result = { 0 };
	sceImeDialogGetResult(&result);
	if (result.button == SCE_IME_DIALOG_BUTTON_ENTER) {
		if (ime_target != KEYBOARD_MESSAGE) {
			copy_utf16_to_utf8(ime_input, keyboard_target_text(), keyboard_target_capacity() + 1);
			if (ime_origin_screen == SCREEN_SETTINGS) {
				settings_focus = ime_target == KEYBOARD_NAME ? 1 : ime_target == KEYBOARD_ENDPOINT ? 2 : 3;
			} else {
				focus = 1;
			}
			save_config();
		} else {
			copy_utf16_to_utf8(ime_input, message_text, sizeof(message_text));
			send_message();
		}
	}
	sceImeDialogTerm();
	ime_open = 0;
}

static void open_keyboard(int target) {
	keyboard_target = target;
	if (open_ime_dialog(target)) {
		return;
	}
	keyboard_open = 1;
	keyboard_row = 0;
	keyboard_column = 0;
}

static int save_name_and_open_chat(void) {
	if (user_name[0] == '\0') {
		set_status("ESCRIBE TU NOMBRE.", 1);
		focus = 0;
		return -1;
	}
	if (save_name() < 0) {
		set_status("NO SE PUDO GUARDAR.", 1);
		return -1;
	}

	screen = SCREEN_CHAT;
	chat_focus = 0;
	status_message[0] = '\0';
	return 0;
}

static int point_in_rect(int x, int y, float left, float top, float width, float height) {
	return x >= left && x < left + width && y >= top && y < top + height;
}

static int read_touch_tap(int *x, int *y) {
	SceTouchData data = { 0 };
	int has_touch = 0;
	if (sceTouchPeek(SCE_TOUCH_PORT_FRONT, &data, 1) >= 0 && data.reportNum > 0) {
		int width = touch_panel.maxAaX - touch_panel.minAaX;
		int height = touch_panel.maxAaY - touch_panel.minAaY;
		if (width <= 0) {
			width = 1920;
		}
		if (height <= 0) {
			height = 1088;
		}

		*x = (data.report[0].x - touch_panel.minAaX) * SCREEN_WIDTH / width;
		*y = (data.report[0].y - touch_panel.minAaY) * SCREEN_HEIGHT / height;
		if (*x < 0) {
			*x = 0;
		} else if (*x >= SCREEN_WIDTH) {
			*x = SCREEN_WIDTH - 1;
		}
		if (*y < 0) {
			*y = 0;
		} else if (*y >= SCREEN_HEIGHT) {
			*y = SCREEN_HEIGHT - 1;
		}
		has_touch = 1;
	}

	int tap = has_touch && !touch_down;
	touch_down = has_touch;
	return tap;
}

static int touch_keyboard_key(int x, int y, int *row, int *column) {
	const int key_width = 51;
	const int key_height = 24;
	const int gap = 5;
	const int start_x = 523;
	const int start_y = 333;
	int relative_x = x - start_x;
	int relative_y = y - start_y;
	if (relative_x < 0 || relative_y < 0) {
		return 0;
	}

	int selected_column = relative_x / (key_width + gap);
	int selected_row = relative_y / (key_height + gap);
	if (selected_column >= KEYBOARD_COLUMNS || selected_row >= KEYBOARD_ROWS) {
		return 0;
	}
	if (relative_x % (key_width + gap) >= key_width || relative_y % (key_height + gap) >= key_height) {
		return 0;
	}

	*row = selected_row;
	*column = selected_column;
	return 1;
}

static void draw_frame(void) {
	vita2d_start_drawing();
	vita2d_clear_screen();
	if (screen == SCREEN_NAME) {
		draw_name_interface();
	} else if (screen == SCREEN_SETTINGS) {
		draw_settings_interface();
	} else {
		draw_chat_interface();
	}
	if (screen == SCREEN_CHAT) {
		draw_model_dropdown();
	}
	if (history_modal) {
		draw_history_modal();
	}
	if (info_modal) {
		draw_info_modal();
	}
	if (side_menu_open) {
		draw_side_menu_overlay();
	}
	vita2d_end_drawing();
	vita2d_swap_buffers();
}

static void send_message(void) {
	if (message_text[0] == '\0') {
		set_status("ESCRIBE UN MENSAJE.", 1);
		return;
	}

	if (selected_model[0] == '\0') {
		set_status("SELECCIONA UN MODELO.", 1);
		return;
	}
	if (input_mode != 0) {
		set_status("LA GENERACION DE IMAGENES SIGUE EN BETA.", 1);
		return;
	}
	if (chat_send(endpoint_url, api_key, selected_model, message_text) < 0) {
		char chat_status[64];
		chat_copy_status(chat_status, sizeof(chat_status));
		set_status(chat_request_state() == CHAT_REQUEST_STREAMING ? "ESPERA O CANCELA LA RESPUESTA." :
			chat_status[0] != '\0' ? chat_status : "NO SE PUDO ENVIAR.", 1);
		return;
	}
	message_text[0] = '\0';
	chat_follow_bottom = 1;
	chat_scroll_offset = 0;
	set_status("GENERANDO RESPUESTA...", 0);
}

static int build_models_url(char *output, int capacity) {
	int length = (int)strlen(endpoint_url);
	while (length > 0 && endpoint_url[length - 1] == '/') {
		--length;
	}
	if (length <= 0 || length + 7 >= capacity) {
		return -1;
	}
	memcpy(output, endpoint_url, length);
	memcpy(output + length, "/models", 7);
	output[length + 7] = '\0';
	return 0;
}

static int parse_models_json(const char *json, char output[MODEL_COUNT][MODEL_CAPACITY + 1]) {
	if (strstr(json, "\"data\"") == NULL) {
		return -1;
	}
	int count = 0;
	const char *cursor = json;
	while (count < MODEL_COUNT && (cursor = strstr(cursor, "\"id\"")) != NULL) {
		const char *colon = strchr(cursor + 4, ':');
		if (colon == NULL) {
			break;
		}
		const char *quote = strchr(colon + 1, '"');
		if (quote == NULL) {
			break;
		}
		++quote;
		int length = 0;
		while (quote[length] != '\0' && quote[length] != '"' && length < MODEL_CAPACITY) {
			if (quote[length] == '\\' && quote[length + 1] != '\0') {
				++length;
			}
			++length;
		}
		if (quote[length] != '"' || length == 0 || length > MODEL_CAPACITY) {
			cursor = quote + 1;
			continue;
		}
		int output_length = 0;
		for (int index = 0; index < length && output_length < MODEL_CAPACITY; ++index) {
			if (quote[index] == '\\' && index + 1 < length) {
				++index;
			}
			output[count][output_length++] = quote[index];
		}
		output[count][output_length] = '\0';
		int duplicate = 0;
		for (int index = 0; index < count; ++index) {
			if (strcmp(output[index], output[count]) == 0) {
				duplicate = 1;
				break;
			}
		}
		if (!duplicate) {
			++count;
		}
		cursor = quote + length + 1;
	}
	return count;
}

typedef struct {
	char *data;
	size_t capacity;
	size_t length;
} HttpResponse;

static size_t write_http_response(char *data, size_t size, size_t count, void *user_data) {
	HttpResponse *response = user_data;
	size_t bytes = size * count;
	if (bytes > response->capacity - response->length - 1) {
		return 0;
	}
	memcpy(response->data + response->length, data, bytes);
	response->length += bytes;
	response->data[response->length] = '\0';
	return bytes;
}

static int http_get_models(const char *url, const char *key, char *response, int capacity, int *status_code) {
	CURL *curl = curl_easy_init();
	if (curl == NULL) {
		return CURLE_FAILED_INIT;
	}

	char authorization[API_KEY_CAPACITY + 23] = "Authorization: Bearer ";
	size_t authorization_length = strlen(authorization);
	size_t key_length = strlen(key);
	if (key_length > sizeof(authorization) - authorization_length - 1) {
		key_length = sizeof(authorization) - authorization_length - 1;
	}
	memcpy(authorization + authorization_length, key, key_length);
	authorization[authorization_length + key_length] = '\0';
	struct curl_slist *headers = NULL;
	headers = curl_slist_append(headers, authorization);
	if (headers == NULL) {
		curl_easy_cleanup(curl);
		return CURLE_OUT_OF_MEMORY;
	}
	struct curl_slist *updated_headers = curl_slist_append(headers, "Accept: application/json");
	if (updated_headers == NULL) {
		curl_slist_free_all(headers);
		curl_easy_cleanup(curl);
		return CURLE_OUT_OF_MEMORY;
	}
	headers = updated_headers;

	HttpResponse body = { response, (size_t)capacity, 0 };
	response[0] = '\0';
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "VagaChatVITA/1.1");
	curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
	curl_easy_setopt(curl, CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_2);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
	curl_easy_setopt(curl, CURLOPT_CAINFO, CA_CERTIFICATE_FILE);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_http_response);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);

	CURLcode result = curl_easy_perform(curl);
	long response_code = 0;
	if (result == CURLE_OK) {
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
	}
	*status_code = (int)response_code;
	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);

	if (result != CURLE_OK) {
		return result;
	}
	if (*status_code < 200 || *status_code >= 300) {
		return CURLE_HTTP_RETURNED_ERROR;
	}
	return body.length > 0 ? CURLE_OK : CURLE_GOT_NOTHING;
}

static const char *connection_error_message(int error) {
	if (error == CURLE_PEER_FAILED_VERIFICATION) {
		return "CERTIFICADO TLS INVALIDO.";
	}
	if (error == CURLE_SSL_CONNECT_ERROR) {
		return "FALLO HANDSHAKE TLS.";
	}
	if (error == CURLE_COULDNT_RESOLVE_HOST) {
		return "NO SE RESOLVIO EL HOST.";
	}
	if (error == CURLE_COULDNT_CONNECT) {
		return "NO SE PUDO CONECTAR.";
	}
	if (error == CURLE_OPERATION_TIMEDOUT) {
		return "TIEMPO DE CONEXION AGOTADO.";
	}
	if (error == CURLE_WRITE_ERROR) {
		return "RESPUESTA DEMASIADO GRANDE.";
	}
	return "ERROR DE CONEXION.";
}

static int verify_connection(void) {
	connection_state = CONNECTION_VERIFYING;
	if (endpoint_url[0] == '\0' || api_key[0] == '\0') {
		connection_state = CONNECTION_OFFLINE;
		set_status("CONFIGURA ENDPOINT Y API KEY.", 1);
		return -1;
	}
	if (strncmp(endpoint_url, "https://", 8) != 0) {
		connection_state = CONNECTION_OFFLINE;
		set_status("EL ENDPOINT DEBE USAR HTTPS.", 1);
		return -1;
	}
	if (!network_ready) {
		connection_state = CONNECTION_OFFLINE;
		set_status("RED NO DISPONIBLE.", 1);
		return -1;
	}
	int network_state = SCE_NETCTL_STATE_DISCONNECTED;
	if (sceNetCtlInetGetState(&network_state) >= 0 && network_state != SCE_NETCTL_STATE_CONNECTED) {
		connection_state = CONNECTION_OFFLINE;
		set_status("RED NO CONECTADA.", 1);
		return -1;
	}
	char url[ENDPOINT_CAPACITY + 8];
	char response[16384];
	int status_code = 0;
	if (build_models_url(url, sizeof(url)) < 0) {
		connection_state = CONNECTION_OFFLINE;
		set_status("ENDPOINT INVALIDO.", 1);
		return -1;
	}
	int request_error = http_get_models(url, api_key, response, sizeof(response), &status_code);
	if (request_error != CURLE_OK) {
		connection_state = CONNECTION_OFFLINE;
		set_status(status_code >= 400 ? "API KEY O ENDPOINT RECHAZADO." : connection_error_message(request_error), 1);
		return -1;
	}
	char new_models[MODEL_COUNT][MODEL_CAPACITY + 1] = { 0 };
	int new_count = parse_models_json(response, new_models);
	if (new_count < 0) {
		connection_state = CONNECTION_OFFLINE;
		set_status("RESPUESTA INVALIDA.", 1);
		return -1;
	}
	model_count = new_count;
	for (int index = 0; index < model_count; ++index) {
		size_t model_length = strlen(new_models[index]);
		if (model_length > MODEL_CAPACITY) {
			model_length = MODEL_CAPACITY;
		}
		memcpy(models[index], new_models[index], model_length);
		models[index][model_length] = '\0';
	}
	int selected_exists = 0;
	for (int index = 0; index < model_count; ++index) {
		if (strcmp(selected_model, models[index]) == 0) {
			selected_exists = 1;
			break;
		}
	}
	if (!selected_exists) {
		if (model_count > 0) {
			strncpy(selected_model, models[0], sizeof(selected_model) - 1);
			selected_model[sizeof(selected_model) - 1] = '\0';
		} else {
			selected_model[0] = '\0';
		}
	}
	save_config();
	connection_state = CONNECTION_ONLINE;
	set_status("CONEXION VERIFICADA.", 0);
	return 0;
}

int main(void) {
	load_config();
	if (config_needs_rewrite) {
		save_config();
	}
	screen = user_name[0] == '\0' ? SCREEN_NAME : SCREEN_CHAT;

	if (vita2d_init() < 0) {
		sceKernelExitProcess(1);
		return 1;
	}
	vita2d_set_vblank_wait(1);
	vita2d_set_clear_color(COLOR_BACKGROUND_TOP);
	ui_font = vita2d_load_font_file("app0:assets/Manrope.ttf");
	if (ui_font == NULL) {
		vita2d_fini();
		sceKernelExitProcess(1);
		return 1;
	}
	app_logo = vita2d_load_PNG_file("app0:assets/vagaroute-logo.png");
	SceCommonDialogConfigParam common_dialog_config;
	sceCommonDialogConfigParamInit(&common_dialog_config);
	sceCommonDialogSetConfigParam(&common_dialog_config);
	ime_module_loaded = sceSysmoduleLoadModule(SCE_SYSMODULE_IME) >= 0;
	if (sceSysmoduleLoadModule(SCE_SYSMODULE_NET) >= 0) {
		net_module_loaded = 1;
		SceNetInitParam net_param = { network_memory, sizeof(network_memory), 0 };
		net_initialized = sceNetInit(&net_param) >= 0;
		if (net_initialized) {
			netctl_initialized = sceNetCtlInit() >= 0;
		}
		if (netctl_initialized) {
			curl_initialized = curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK;
		}
		network_ready = curl_initialized;
	}

	sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
	memset(&touch_panel, 0, sizeof(touch_panel));
	sceTouchGetPanelInfo(SCE_TOUCH_PORT_FRONT, &touch_panel);
	sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
	if (chat_init() < 0) {
		set_status("NO SE PUDO CARGAR EL HISTORIAL.", 1);
	} else {
		chat_follow_bottom = 1;
		chat_scroll_offset = 0;
	}
	uint32_t previous_buttons = 0;
	ChatRequestState previous_chat_state = chat_request_state();

	for (;;) {
		SceCtrlData controller = { 0 };
		sceCtrlPeekBufferPositive(0, &controller, 1);
		uint32_t pressed = controller.buttons & ~previous_buttons;
		previous_buttons = controller.buttons;
		int touch_x = 0;
		int touch_y = 0;
		int touch_tapped = read_touch_tap(&touch_x, &touch_y);
		chat_update();
		ChatRequestState current_chat_state = chat_request_state();
		if (current_chat_state != previous_chat_state) {
			char chat_status[64];
			char chat_error[128];
			chat_copy_status(chat_status, sizeof(chat_status));
			chat_copy_error(chat_error, sizeof(chat_error));
			if (current_chat_state == CHAT_REQUEST_ERROR && chat_error[0] != '\0') {
				set_status(chat_error, 1);
			} else if (chat_status[0] != '\0') {
				set_status(chat_status, current_chat_state == CHAT_REQUEST_ERROR);
			}
			previous_chat_state = current_chat_state;
		}

		if (ime_open) {
			update_ime_dialog();
			if (ime_open) {
				vita2d_common_dialog_update();
				vita2d_swap_buffers();
				sceKernelDelayThread(16666);
				continue;
			}
		}

		if ((pressed & SCE_CTRL_START) != 0) {
			if (user_name[0] != '\0') {
				save_name();
			}
			break;
		}

		if (keyboard_open) {
			if (touch_tapped) {
				int row = 0;
				int column = 0;
				if (touch_keyboard_key(touch_x, touch_y, &row, &column)) {
					keyboard_row = row;
					keyboard_column = column;
					activate_keyboard_key();
				} else if (!point_in_rect(touch_x, touch_y, 500.0f, 286.0f, 390.0f, 220.0f)) {
					keyboard_open = 0;
				}
			} else if ((pressed & SCE_CTRL_UP) != 0) {
				move_keyboard(-1, 0);
			} else if ((pressed & SCE_CTRL_DOWN) != 0) {
				move_keyboard(1, 0);
			} else if ((pressed & SCE_CTRL_LEFT) != 0) {
				move_keyboard(0, -1);
			} else if ((pressed & SCE_CTRL_RIGHT) != 0) {
				move_keyboard(0, 1);
			} else if ((pressed & SCE_CTRL_CROSS) != 0) {
				activate_keyboard_key();
			} else if ((pressed & SCE_CTRL_CIRCLE) != 0) {
				keyboard_open = 0;
			} else if ((pressed & SCE_CTRL_TRIANGLE) != 0) {
				remove_last_character(keyboard_target_text());
			}
		} else if (side_menu_open) {
			if (touch_tapped) {
				int item = sidebar_item_at(touch_x, touch_y);
				if (item >= 0) {
					side_menu_selection = item;
					activate_side_menu();
				} else {
					side_menu_open = 0;
				}
			} else if ((pressed & SCE_CTRL_UP) != 0) {
				side_menu_selection = (side_menu_selection + 3) % 4;
			} else if ((pressed & SCE_CTRL_DOWN) != 0) {
				side_menu_selection = (side_menu_selection + 1) % 4;
			} else if ((pressed & SCE_CTRL_CROSS) != 0) {
				activate_side_menu();
			} else if ((pressed & SCE_CTRL_CIRCLE) != 0 || (pressed & SCE_CTRL_SELECT) != 0) {
				side_menu_open = 0;
			}
		} else if (model_selector_open) {
			if (touch_tapped) {
				int item = model_item_at(touch_x, touch_y);
				if (item >= 0) {
					select_model(item);
				} else {
					model_selector_open = 0;
				}
			} else if ((pressed & SCE_CTRL_UP) != 0) {
				move_model_cursor(-1);
			} else if ((pressed & SCE_CTRL_DOWN) != 0) {
				move_model_cursor(1);
			} else if ((pressed & SCE_CTRL_CROSS) != 0) {
				select_model(model_cursor);
			} else if ((pressed & SCE_CTRL_CIRCLE) != 0 || (pressed & SCE_CTRL_TRIANGLE) != 0) {
				model_selector_open = 0;
			} else {
				int analog_delta = (int)controller.ly - 128;
				if (analog_delta < -32 || analog_delta > 32) {
					if (model_analog_delay <= 0) {
						move_model_cursor(analog_delta < 0 ? -1 : 1);
						model_analog_delay = abs(analog_delta) > 80 ? 1 : 4;
					} else {
						--model_analog_delay;
					}
				} else {
					model_analog_delay = 0;
				}
			}
		} else if ((pressed & SCE_CTRL_SELECT) != 0) {
			side_menu_open = 1;
			side_menu_selection = screen == SCREEN_SETTINGS ? 3 : 0;
			history_modal = 0;
			info_modal = 0;
		} else if (history_modal) {
			if (touch_tapped) {
				if (point_in_rect(touch_x, touch_y, 730.0f, 98.0f, 54.0f, 54.0f) ||
					!point_in_rect(touch_x, touch_y, 170.0f, 86.0f, 620.0f, 374.0f)) {
					history_modal = 0;
				} else if (touch_x >= 205 && touch_x < 755 && touch_y >= 184) {
					int row = (touch_y - 184) / 64;
					int index = chat_conversation_count() - row - 1;
					if (index >= 0 && chat_select_conversation(index) == 0) {
						history_selection = index;
						history_modal = 0;
						chat_follow_bottom = 1;
						chat_scroll_offset = 0;
					}
				}
			} else if ((pressed & SCE_CTRL_UP) != 0 && chat_conversation_count() > 0) {
				history_selection = (history_selection + 1) % chat_conversation_count();
			} else if ((pressed & SCE_CTRL_DOWN) != 0 && chat_conversation_count() > 0) {
				history_selection = (history_selection + chat_conversation_count() - 1) % chat_conversation_count();
			} else if ((pressed & SCE_CTRL_CROSS) != 0) {
				if (chat_select_conversation(history_selection) == 0) {
					history_modal = 0;
					chat_follow_bottom = 1;
					chat_scroll_offset = 0;
				}
			} else if ((pressed & SCE_CTRL_CIRCLE) != 0 || (pressed & SCE_CTRL_TRIANGLE) != 0) {
				history_modal = 0;
			}
		} else if (info_modal) {
			if (touch_tapped) {
				if (point_in_rect(touch_x, touch_y, 685.0f, 160.0f, 54.0f, 54.0f) ||
					!point_in_rect(touch_x, touch_y, 210.0f, 155.0f, 540.0f, 190.0f)) {
					info_modal = 0;
				}
			} else if ((pressed & SCE_CTRL_CIRCLE) != 0 || (pressed & SCE_CTRL_TRIANGLE) != 0) {
				info_modal = 0;
			}
		} else if (screen == SCREEN_NAME) {
			if (touch_tapped) {
				int item = sidebar_item_at(touch_x, touch_y);
				if (point_in_rect(touch_x, touch_y, 55.0f, 45.0f, 55.0f, 40.0f)) {
					side_menu_open = 1;
					side_menu_selection = 0;
				} else if (item >= 0) {
					side_menu_selection = item;
					activate_side_menu();
				} else if (point_in_rect(touch_x, touch_y, 526.0f, 230.0f, 350.0f, 39.0f)) {
					focus = 0;
					open_keyboard(KEYBOARD_NAME);
				} else if (point_in_rect(touch_x, touch_y, 526.0f, 286.0f, 350.0f, 38.0f)) {
					save_name_and_open_chat();
				}
			} else if ((pressed & SCE_CTRL_UP) != 0 || (pressed & SCE_CTRL_DOWN) != 0) {
				focus = focus == 0 ? 1 : 0;
			} else if ((pressed & SCE_CTRL_CROSS) != 0) {
				if (focus == 0) {
					open_keyboard(KEYBOARD_NAME);
				} else {
					save_name_and_open_chat();
				}
			} else if ((pressed & SCE_CTRL_TRIANGLE) != 0 && focus == 0) {
				user_name[0] = '\0';
				set_status("NOMBRE BORRADO.", 0);
			}
		} else if (screen == SCREEN_SETTINGS) {
			if (touch_tapped) {
				int item = sidebar_item_at(touch_x, touch_y);
				if (point_in_rect(touch_x, touch_y, 55.0f, 45.0f, 55.0f, 40.0f)) {
					side_menu_open = 1;
					side_menu_selection = 3;
				} else if (item >= 0) {
					side_menu_selection = item;
					activate_side_menu();
				} else if (point_in_rect(touch_x, touch_y, 840.0f, 108.0f, 60.0f, 55.0f)) {
					save_config();
					screen = SCREEN_CHAT;
					status_message[0] = '\0';
				} else if (point_in_rect(touch_x, touch_y, 173.0f, 199.0f, 700.0f, 38.0f)) {
					settings_focus = 0;
					open_keyboard(KEYBOARD_NAME);
				} else if (point_in_rect(touch_x, touch_y, 173.0f, 256.0f, 700.0f, 38.0f)) {
					settings_focus = 1;
					open_keyboard(KEYBOARD_ENDPOINT);
				} else if (point_in_rect(touch_x, touch_y, 173.0f, 313.0f, 700.0f, 38.0f)) {
					settings_focus = 2;
					open_keyboard(KEYBOARD_API_KEY);
				} else if (point_in_rect(touch_x, touch_y, 173.0f, 372.0f, 220.0f, 42.0f)) {
					settings_focus = 3;
					verify_connection();
				} else if (point_in_rect(touch_x, touch_y, 413.0f, 372.0f, 180.0f, 42.0f)) {
					save_config();
					screen = SCREEN_CHAT;
					status_message[0] = '\0';
				}
			} else if ((pressed & SCE_CTRL_UP) != 0 || (pressed & SCE_CTRL_DOWN) != 0) {
				settings_focus = (settings_focus + 1) % 5;
			} else if ((pressed & SCE_CTRL_CROSS) != 0) {
				if (settings_focus == 0) {
					open_keyboard(KEYBOARD_NAME);
				} else if (settings_focus == 1) {
					open_keyboard(KEYBOARD_ENDPOINT);
				} else if (settings_focus == 2) {
					open_keyboard(KEYBOARD_API_KEY);
				} else if (settings_focus == 3) {
					verify_connection();
				} else {
					save_config();
					screen = SCREEN_CHAT;
					status_message[0] = '\0';
				}
			} else if ((pressed & SCE_CTRL_CIRCLE) != 0) {
				save_config();
				screen = SCREEN_CHAT;
				status_message[0] = '\0';
			}
		} else {
			if (touch_tapped) {
				int item = sidebar_item_at(touch_x, touch_y);
				if (point_in_rect(touch_x, touch_y, 55.0f, 45.0f, 55.0f, 40.0f)) {
					side_menu_open = 1;
					side_menu_selection = 0;
				} else if (item >= 0) {
					side_menu_selection = item;
					activate_side_menu();
				} else if (point_in_rect(touch_x, touch_y, 226.0f, 443.0f, 390.0f, 48.0f)) {
					chat_focus = 0;
					open_keyboard(KEYBOARD_MESSAGE);
				} else if (point_in_rect(touch_x, touch_y, 627.0f, 443.0f, 118.0f, 48.0f)) {
					input_mode = input_mode == 0 ? 1 : 0;
				} else if (point_in_rect(touch_x, touch_y, 773.0f, 443.0f, 132.0f, 48.0f)) {
					chat_focus = 1;
					send_message();
				} else if (point_in_rect(touch_x, touch_y, 480.0f, 45.0f, 340.0f, 50.0f)) {
					open_model_selector();
				}
			} else if ((pressed & SCE_CTRL_CIRCLE) != 0 &&
				(chat_request_state() == CHAT_REQUEST_CONNECTING || chat_request_state() == CHAT_REQUEST_STREAMING)) {
				chat_cancel();
				set_status("CANCELANDO RESPUESTA...", 0);
			} else if ((pressed & SCE_CTRL_LTRIGGER) != 0) {
				chat_follow_bottom = 0;
				chat_scroll_offset -= 220;
				if (chat_scroll_offset < 0) chat_scroll_offset = 0;
			} else if ((pressed & SCE_CTRL_RTRIGGER) != 0) {
				chat_follow_bottom = 0;
				chat_scroll_offset += 220;
				if (chat_scroll_offset > chat_scroll_max) chat_scroll_offset = chat_scroll_max;
				if (chat_scroll_offset == chat_scroll_max) chat_follow_bottom = 1;
			} else if ((pressed & SCE_CTRL_LEFT) != 0) {
				chat_follow_bottom = 0;
				chat_scroll_offset -= 80;
				if (chat_scroll_offset < 0) chat_scroll_offset = 0;
			} else if ((pressed & SCE_CTRL_RIGHT) != 0) {
				chat_follow_bottom = 0;
				chat_scroll_offset += 80;
				if (chat_scroll_offset > chat_scroll_max) chat_scroll_offset = chat_scroll_max;
				if (chat_scroll_offset == chat_scroll_max) chat_follow_bottom = 1;
			} else if ((pressed & SCE_CTRL_UP) != 0 || (pressed & SCE_CTRL_DOWN) != 0) {
				chat_focus = (chat_focus + 1) % 3;
			} else if ((pressed & SCE_CTRL_CROSS) != 0) {
				if (chat_focus == 0) {
					open_keyboard(KEYBOARD_MESSAGE);
				} else if (chat_focus == 1) {
					send_message();
				} else {
					open_model_selector();
				}
			} else if ((pressed & SCE_CTRL_TRIANGLE) != 0) {
				open_model_selector();
			} else {
				int analog_delta = (int)controller.ly - 128;
				if (analog_delta < -32 || analog_delta > 32) {
					if (chat_analog_delay <= 0) {
						chat_follow_bottom = 0;
						chat_scroll_offset += analog_delta > 0 ? 60 : -60;
						if (chat_scroll_offset < 0) chat_scroll_offset = 0;
						if (chat_scroll_offset > chat_scroll_max) chat_scroll_offset = chat_scroll_max;
						if (chat_scroll_offset == chat_scroll_max) chat_follow_bottom = 1;
						chat_analog_delay = 3;
					} else {
						--chat_analog_delay;
					}
				} else {
					chat_analog_delay = 0;
				}
			}
		}

		if (ime_open) {
			vita2d_common_dialog_update();
			vita2d_swap_buffers();
		} else {
			draw_frame();
		}
		sceKernelDelayThread(16666);
	}

	if (ime_open) {
		sceImeDialogAbort();
		sceImeDialogTerm();
	}
	if (ime_module_loaded) {
		sceSysmoduleUnloadModule(SCE_SYSMODULE_IME);
	}
	chat_shutdown();
	if (curl_initialized) {
		curl_global_cleanup();
	}
	if (netctl_initialized) {
		sceNetCtlTerm();
	}
	if (net_initialized) {
		sceNetTerm();
	}
	if (net_module_loaded) {
		sceSysmoduleUnloadModule(SCE_SYSMODULE_NET);
	}
	if (app_logo != NULL) {
		vita2d_free_texture(app_logo);
	}
	vita2d_free_font(ui_font);
	vita2d_fini();
	sceKernelExitProcess(0);
	return 0;
}
