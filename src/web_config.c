#include "web_config.h"

#include <errno.h>
#include <netinet/in.h>
#include <openssl/sha.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <psp2/kernel/threadmgr.h>

#define WEB_THREAD_PRIORITY 0x10000100
#define WEB_THREAD_STACK_SIZE (64U * 1024U)
#define WEB_REQUEST_CAPACITY 12288
#define WEB_PAGE_CAPACITY 16384
#define WEB_VALUE_CAPACITY 1024

static SceUID web_mutex = -1;
static SceUID web_thread = -1;
static int web_server_socket = -1;
static int web_stop_requested;
static int web_running;
static int web_pending;
static char web_ip[WEB_CONFIG_IP_CAPACITY];
static char web_endpoint[WEB_CONFIG_ENDPOINT_CAPACITY + 1];
static char web_api_key[WEB_CONFIG_API_KEY_CAPACITY + 1];
static unsigned char web_password_hash[WEB_CONFIG_PASSWORD_HASH_BYTES];
static int web_password_configured;

static void web_lock(void) {
	if (web_mutex >= 0) {
		sceKernelLockMutex(web_mutex, 1, NULL);
	}
}

static void web_unlock(void) {
	if (web_mutex >= 0) {
		sceKernelUnlockMutex(web_mutex, 1);
	}
}

static void copy_text(char *target, size_t capacity, const char *source) {
	if (capacity == 0) return;
	size_t length = source == NULL ? 0 : strlen(source);
	if (length >= capacity) length = capacity - 1;
	if (length > 0) memcpy(target, source, length);
	target[length] = '\0';
}

int web_config_hash_password(const char *password, unsigned char output[WEB_CONFIG_PASSWORD_HASH_BYTES]) {
	if (password == NULL || output == NULL || password[0] == '\0') return -1;
	SHA256((const unsigned char *)password, strlen(password), output);
	return 0;
}

static int constant_time_equal(const unsigned char *left, const unsigned char *right, size_t length) {
	unsigned char difference = 0;
	for (size_t index = 0; index < length; ++index) {
		difference |= left[index] ^ right[index];
	}
	return difference == 0;
}

static void append_html(char *output, size_t capacity, int *length, const char *format, ...) {
	if (*length >= (int)capacity - 1) return;
	va_list arguments;
	va_start(arguments, format);
	int written = vsnprintf(output + *length, capacity - (size_t)*length, format, arguments);
	va_end(arguments);
	if (written > 0) {
		*length += written < (int)(capacity - (size_t)*length) ? written : (int)(capacity - (size_t)*length - 1);
	}
}

static void html_escape(char *output, size_t capacity, const char *source) {
	int length = 0;
	output[0] = '\0';
	for (size_t index = 0; source != NULL && source[index] != '\0' && length < (int)capacity - 1; ++index) {
		const char *replacement = NULL;
		switch (source[index]) {
		case '&': replacement = "&amp;"; break;
		case '<': replacement = "&lt;"; break;
		case '>': replacement = "&gt;"; break;
		case '"': replacement = "&quot;"; break;
		case '\'': replacement = "&#39;"; break;
		default:
			output[length++] = source[index];
			output[length] = '\0';
			continue;
		}
		int replacement_length = (int)strlen(replacement);
		if (length + replacement_length >= (int)capacity) break;
		memcpy(output + length, replacement, replacement_length);
		length += replacement_length;
		output[length] = '\0';
	}
}

static int send_all(int socket, const char *data, int length) {
	int sent = 0;
	while (sent < length) {
		int result = (int)send(socket, data + sent, (size_t)(length - sent), 0);
		if (result <= 0) return -1;
		sent += result;
	}
	return 0;
}

static int send_response(int socket, const char *status, const char *content_type, const char *body) {
	char header[512];
	int body_length = (int)strlen(body);
	int header_length = snprintf(header, sizeof(header),
		"HTTP/1.1 %s\r\nContent-Type: %s; charset=utf-8\r\nContent-Length: %d\r\nConnection: close\r\nCache-Control: no-store\r\n\r\n",
		status, content_type, body_length);
	if (header_length <= 0 || header_length >= (int)sizeof(header)) return -1;
	if (send_all(socket, header, header_length) < 0) return -1;
	return send_all(socket, body, body_length);
}

static int receive_request(int socket, char *request, int capacity, int *body_offset, int *body_length) {
	int length = 0;
	int content_length = 0;
	int header_length = -1;
	while (length < capacity - 1) {
		int received = (int)recv(socket, request + length, (size_t)(capacity - length - 1), 0);
		if (received <= 0) return -1;
		length += received;
		request[length] = '\0';
		if (header_length < 0) {
			char *header_end = strstr(request, "\r\n\r\n");
			if (header_end == NULL) continue;
			header_length = (int)(header_end - request) + 4;
			char *content_header = strstr(request, "Content-Length:");
			if (content_header != NULL) content_length = atoi(content_header + 15);
		}
		if (header_length >= 0 && length >= header_length + content_length) {
			*body_offset = header_length;
			*body_length = content_length;
			return length;
		}
	}
	return -1;
}

static int hex_value(char character) {
	if (character >= '0' && character <= '9') return character - '0';
	if (character >= 'a' && character <= 'f') return character - 'a' + 10;
	if (character >= 'A' && character <= 'F') return character - 'A' + 10;
	return -1;
}

static void url_decode(char *output, size_t capacity, const char *source, int length) {
	int output_length = 0;
	for (int index = 0; index < length && output_length < (int)capacity - 1; ++index) {
		if (source[index] == '+' ) {
			output[output_length++] = ' ';
		} else if (source[index] == '%' && index + 2 < length) {
			int high = hex_value(source[index + 1]);
			int low = hex_value(source[index + 2]);
			if (high >= 0 && low >= 0) {
				output[output_length++] = (char)((high << 4) | low);
				index += 2;
			} else {
				output[output_length++] = source[index];
			}
		} else {
			output[output_length++] = source[index];
		}
	}
	output[output_length] = '\0';
}

static int form_value(const char *body, int body_length, const char *key, char *output, size_t capacity) {
	int key_length = (int)strlen(key);
	int position = 0;
	output[0] = '\0';
	while (position < body_length) {
		int end = position;
		while (end < body_length && body[end] != '&') ++end;
		int equals = position;
		while (equals < end && body[equals] != '=') ++equals;
		if (equals - position == key_length && strncmp(body + position, key, key_length) == 0) {
			url_decode(output, capacity, body + equals + (equals < end ? 1 : 0), end - equals - (equals < end ? 1 : 0));
			return 1;
		}
		position = end + 1;
	}
	return 0;
}

static int render_page(char *page, int capacity, const char *message) {
	char endpoint[WEB_CONFIG_ENDPOINT_CAPACITY + 1];
	int password_configured;
	web_lock();
	copy_text(endpoint, sizeof(endpoint), web_endpoint);
	password_configured = web_password_configured;
	web_unlock();
	char escaped_endpoint[WEB_CONFIG_ENDPOINT_CAPACITY * 2 + 1];
	char escaped_message[256];
	html_escape(escaped_endpoint, sizeof(escaped_endpoint), endpoint);
	html_escape(escaped_message, sizeof(escaped_message), message == NULL ? "" : message);
	int length = 0;
	append_html(page, capacity, &length,
		"<!doctype html><html><head><meta name=viewport content=\"width=device-width,initial-scale=1\"><title>VagaRoute AI</title>"
		"<style>body{font-family:system-ui,sans-serif;background:#10121e;color:#edf0ff;max-width:620px;margin:40px auto;padding:0 18px}"
		"main{background:#191c2d;border:1px solid #383d5d;border-radius:14px;padding:26px}h1{font-size:24px;margin-top:0}"
		"label{display:block;color:#a5abc5;font-size:14px;margin:18px 0 6px}input{box-sizing:border-box;width:100%%;padding:12px;border:1px solid #4a5074;border-radius:8px;background:#0c0f1c;color:#fff;font-size:16px}"
		"button{margin-top:22px;padding:12px 18px;border:0;border-radius:8px;background:#c78b5b;color:#201812;font-weight:700;font-size:16px}"
		".notice{color:#6fcaa5}.error{color:#e0706f}.hint{color:#9299b8;font-size:13px}</style></head><body><main>"
		"<h1>Configuracion web</h1>");
	if (escaped_message[0] != '\0') {
		append_html(page, capacity, &length, "<p class=\"%s\">%s</p>",
			strstr(message, "correctamente") != NULL ? "notice" : "error", escaped_message);
	}
	if (!password_configured) {
		append_html(page, capacity, &length,
			"<p>Define una contrasena para bloquear esta configuracion.</p>"
			"<form method=post action=/save><label>Endpoint</label><input name=endpoint value=\"%s\" required>"
			"<label>API key</label><input name=api_key type=password autocomplete=off>"
			"<label>Nueva contrasena</label><input name=password type=password minlength=4 required>"
			"<label>Repite la contrasena</label><input name=password_confirm type=password minlength=4 required>"
			"<button type=submit>Guardar configuracion</button></form>", escaped_endpoint);
	} else {
		append_html(page, capacity, &length,
			"<p class=hint>La API key actual no se muestra. Deja el campo vacio para conservarla.</p>"
			"<form method=post action=/save><label>Endpoint</label><input name=endpoint value=\"%s\" required>"
			"<label>API key</label><input name=api_key type=password autocomplete=off placeholder=\"Sin cambios\">"
			"<label>Contrasena</label><input name=password type=password autocomplete=current-password required>"
			"<button type=submit>Guardar configuracion</button></form>", escaped_endpoint);
	}
	append_html(page, capacity, &length, "</main></body></html>");
	return length;
}

static int handle_request(int socket, const char *request, int body_offset, int body_length) {
	char method[8] = { 0 };
	char path[64] = { 0 };
	if (sscanf(request, "%7s %63s", method, path) != 2) return -1;
	char page[WEB_PAGE_CAPACITY];
	if (strcmp(method, "GET") == 0 && strcmp(path, "/") == 0) {
		render_page(page, sizeof(page), NULL);
		return send_response(socket, "200 OK", "text/html", page);
	}
	if (strcmp(method, "POST") != 0 || strcmp(path, "/save") != 0) {
		return send_response(socket, "404 Not Found", "text/plain", "Not found");
	}

	char password[WEB_VALUE_CAPACITY];
	char password_confirm[WEB_VALUE_CAPACITY];
	char endpoint[WEB_CONFIG_ENDPOINT_CAPACITY + 1];
	char api_key[WEB_CONFIG_API_KEY_CAPACITY + 1];
	form_value(request + body_offset, body_length, "password", password, sizeof(password));
	form_value(request + body_offset, body_length, "password_confirm", password_confirm, sizeof(password_confirm));
	form_value(request + body_offset, body_length, "endpoint", endpoint, sizeof(endpoint));
	form_value(request + body_offset, body_length, "api_key", api_key, sizeof(api_key));
	unsigned char password_hash[WEB_CONFIG_PASSWORD_HASH_BYTES];
	if (web_config_hash_password(password, password_hash) < 0) {
		render_page(page, sizeof(page), "La contrasena es obligatoria.");
		return send_response(socket, "400 Bad Request", "text/html", page);
	}

	web_lock();
	int configured = web_password_configured;
	int valid_password = !configured || constant_time_equal(password_hash, web_password_hash, sizeof(password_hash));
	if (!valid_password) {
		web_unlock();
		render_page(page, sizeof(page), "Contrasena incorrecta.");
		return send_response(socket, "403 Forbidden", "text/html", page);
	}
	if (!configured && (strlen(password) < 4 || strcmp(password, password_confirm) != 0)) {
		web_unlock();
		render_page(page, sizeof(page), "La contrasena debe tener al menos 4 caracteres y coincidir.");
		return send_response(socket, "400 Bad Request", "text/html", page);
	}
	if (endpoint[0] == '\0') {
		web_unlock();
		render_page(page, sizeof(page), "El endpoint es obligatorio.");
		return send_response(socket, "400 Bad Request", "text/html", page);
	}
	copy_text(web_endpoint, sizeof(web_endpoint), endpoint);
	if (api_key[0] != '\0') copy_text(web_api_key, sizeof(web_api_key), api_key);
	if (!configured) {
		memcpy(web_password_hash, password_hash, sizeof(web_password_hash));
		web_password_configured = 1;
	}
	web_pending = 1;
	web_unlock();
	render_page(page, sizeof(page), "Configuracion guardada correctamente.");
	return send_response(socket, "200 OK", "text/html", page);
}

static int web_thread_entry(unsigned int argc, void *arg) {
	(void)argc;
	(void)arg;
	for (;;) {
		web_lock();
		int stop = web_stop_requested;
		int server = web_server_socket;
		web_unlock();
		if (stop || server < 0) break;
		int client = accept(server, NULL, NULL);
		if (client < 0) {
			if (errno == EINTR) continue;
			break;
		}
		char request[WEB_REQUEST_CAPACITY];
		int body_offset = 0;
		int body_length = 0;
		if (receive_request(client, request, sizeof(request), &body_offset, &body_length) >= 0) {
			handle_request(client, request, body_offset, body_length);
		}
		close(client);
	}
	web_lock();
	web_running = 0;
	web_unlock();
	return 0;
}

int web_config_start(const char *ip, const char *endpoint, const char *api_key,
	const unsigned char password_hash[WEB_CONFIG_PASSWORD_HASH_BYTES], int password_configured) {
	if (web_thread >= 0) return 0;
	web_mutex = sceKernelCreateMutex("VagaWebConfig", 0, 0, NULL);
	if (web_mutex < 0) return -1;
	int server = socket(AF_INET, SOCK_STREAM, 0);
	if (server < 0) {
		sceKernelDeleteMutex(web_mutex);
		web_mutex = -1;
		return -1;
	}
	int reuse = 1;
	setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
	struct sockaddr_in address = { 0 };
	address.sin_family = AF_INET;
	address.sin_port = htons(WEB_CONFIG_PORT);
	address.sin_addr.s_addr = htonl(INADDR_ANY);
	if (bind(server, (struct sockaddr *)&address, sizeof(address)) < 0 || listen(server, 1) < 0) {
		close(server);
		sceKernelDeleteMutex(web_mutex);
		web_mutex = -1;
		return -1;
	}
	web_lock();
	web_server_socket = server;
	web_stop_requested = 0;
	web_pending = 0;
	web_running = 1;
	copy_text(web_ip, sizeof(web_ip), ip);
	copy_text(web_endpoint, sizeof(web_endpoint), endpoint);
	copy_text(web_api_key, sizeof(web_api_key), api_key);
	if (password_hash != NULL) memcpy(web_password_hash, password_hash, sizeof(web_password_hash));
	else memset(web_password_hash, 0, sizeof(web_password_hash));
	web_password_configured = password_configured;
	web_unlock();
	web_thread = sceKernelCreateThread("VagaWebConfig", web_thread_entry,
		WEB_THREAD_PRIORITY, WEB_THREAD_STACK_SIZE, 0,
		SCE_KERNEL_THREAD_CPU_AFFINITY_MASK_DEFAULT, NULL);
	if (web_thread < 0 || sceKernelStartThread(web_thread, 0, NULL) < 0) {
		if (web_thread >= 0) sceKernelDeleteThread(web_thread);
		web_thread = -1;
		close(server);
		web_server_socket = -1;
		web_running = 0;
		sceKernelDeleteMutex(web_mutex);
		web_mutex = -1;
		return -1;
	}
	return 0;
}

void web_config_stop(void) {
	if (web_mutex < 0) return;
	web_lock();
	web_stop_requested = 1;
	int server = web_server_socket;
	web_server_socket = -1;
	SceUID thread = web_thread;
	web_unlock();
	if (server >= 0) {
		shutdown(server, SHUT_RDWR);
		close(server);
	}
	if (thread >= 0) {
		sceKernelWaitThreadEnd(thread, NULL, NULL);
		sceKernelDeleteThread(thread);
	}
	web_thread = -1;
	sceKernelDeleteMutex(web_mutex);
	web_mutex = -1;
	web_running = 0;
}

int web_config_is_running(void) {
	if (web_mutex < 0) return 0;
	web_lock();
	int running = web_running;
	web_unlock();
	return running;
}

int web_config_copy_ip(char *output, size_t capacity) {
	if (output == NULL || capacity == 0 || web_mutex < 0) return -1;
	web_lock();
	copy_text(output, capacity, web_ip);
	web_unlock();
	return 0;
}

int web_config_take_pending(char *endpoint, size_t endpoint_capacity, char *api_key,
	size_t api_key_capacity, unsigned char password_hash[WEB_CONFIG_PASSWORD_HASH_BYTES],
	int *password_configured) {
	if (web_mutex < 0) return 0;
	web_lock();
	if (!web_pending) {
		web_unlock();
		return 0;
	}
	copy_text(endpoint, endpoint_capacity, web_endpoint);
	copy_text(api_key, api_key_capacity, web_api_key);
	memcpy(password_hash, web_password_hash, sizeof(web_password_hash));
	*password_configured = web_password_configured;
	web_pending = 0;
	web_unlock();
	return 1;
}
