#include "chat.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include <curl/curl.h>

#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/rtc.h>

#define CHAT_DIRECTORY "ux0:data/VagaRouteAI"
#define CHAT_HISTORY_FILE CHAT_DIRECTORY "/chat_history.dat"
#define CHAT_HISTORY_TEMP CHAT_DIRECTORY "/chat_history.tmp"
#define CHAT_HISTORY_BACKUP CHAT_DIRECTORY "/chat_history.bak"
#define CHAT_LOG_DIRECTORY "ux0:data/vagachatvita/logs"
#define CHAT_CA_FILE "app0:assets/cacert.pem"

#define CHAT_HISTORY_VERSION 1U
#define CHAT_HISTORY_HEADER_SIZE 28U
#define CHAT_HISTORY_BUFFER_SIZE 180000U
#define CHAT_REQUEST_BUFFER_SIZE 65536U
#define CHAT_SSE_LINE_SIZE 16384U
#define CHAT_SSE_EVENT_SIZE 16384U
#define CHAT_STATUS_SIZE 96U
#define CHAT_ERROR_SIZE 128U
#define CHAT_THREAD_PRIORITY 0x10000100
#define CHAT_THREAD_STACK_SIZE (512U * 1024U)
#define CHAT_JSON_MAX_DEPTH 24

typedef struct ChatMessage {
	uint16_t length;
	uint8_t role;
	char text[CHAT_MAX_MESSAGE_BYTES + 1];
} ChatMessage;

typedef struct ChatConversation {
	uint8_t message_count;
	ChatMessage messages[CHAT_MAX_MESSAGES];
} ChatConversation;

typedef struct ChatSseParser {
	char line[CHAT_SSE_LINE_SIZE];
	char event[CHAT_SSE_EVENT_SIZE];
	size_t line_length;
	size_t event_length;
	int failed;
	int done;
	int received_content;
	int truncated;
} ChatSseParser;

typedef struct ChatWorker {
	char endpoint[CHAT_MAX_ENDPOINT_BYTES + 1];
	char api_key[CHAT_MAX_API_KEY_BYTES + 1];
	char model[CHAT_MAX_MODEL_BYTES + 1];
	char url[CHAT_MAX_ENDPOINT_BYTES + 32];
	char authorization[CHAT_MAX_API_KEY_BYTES + 24];
	char request[CHAT_REQUEST_BUFFER_SIZE];
	int conversation_index;
	int assistant_index;
	int cancel_requested;
	int accept_cancel;
	int finished;
	ChatSseParser sse;
} ChatWorker;

typedef struct JsonCursor {
	const char *current;
	const char *end;
} JsonCursor;

static const unsigned char chat_history_magic[8] = { 'V', 'C', 'H', 'A', 'T', '1', '1', 0 };
static ChatConversation chat_conversations[CHAT_MAX_CONVERSATIONS];
static unsigned char chat_history_buffer[CHAT_HISTORY_BUFFER_SIZE];
static ChatWorker chat_worker;
static SceUID chat_mutex = -1;
static SceUID chat_thread = -1;
static int chat_initialized;
static int chat_conversation_total;
static int chat_active_index = -1;
static ChatRequestState chat_state = CHAT_REQUEST_IDLE;
static long chat_http_status;
static char chat_status[CHAT_STATUS_SIZE];
static char chat_error[CHAT_ERROR_SIZE];
static SceUID chat_log_file = -1;

static size_t utf8_prefix(const char *text, size_t length, size_t maximum);

static void chat_lock(void) {
	sceKernelLockMutex(chat_mutex, 1, NULL);
}

static void chat_unlock(void) {
	sceKernelUnlockMutex(chat_mutex, 1);
}

static void chat_log_locked(const char *format, ...) {
	if (chat_log_file < 0 || format == NULL) {
		return;
	}
	char line[640];
	SceDateTime date = { 0 };
	int prefix_length;
	if (sceRtcGetCurrentClockLocalTime(&date) >= 0) {
		prefix_length = snprintf(line, sizeof(line), "%04d-%02d-%02dT%02d:%02d:%02d ",
			(int)date.year, (int)date.month, (int)date.day, (int)date.hour,
			(int)date.minute, (int)date.second);
	} else {
		prefix_length = snprintf(line, sizeof(line), "unknown-time ");
	}
	if (prefix_length < 0 || prefix_length >= (int)sizeof(line)) {
		return;
	}
	size_t body_capacity = sizeof(line) - (size_t)prefix_length - 2;
	va_list arguments;
	va_start(arguments, format);
	int body_length = vsnprintf(line + prefix_length, body_capacity + 1, format, arguments);
	va_end(arguments);
	if (body_length < 0) {
		return;
	}
	if ((size_t)body_length > body_capacity) {
		body_length = (int)body_capacity;
	}
	line[prefix_length + body_length] = '\n';
	sceIoWrite(chat_log_file, line, (SceSize)(prefix_length + body_length + 1));
}

static void chat_open_log(void) {
	SceRtcTick tick = { 0 };
	char path[160];
	sceIoMkdir("ux0:data/vagachatvita", 0777);
	sceIoMkdir(CHAT_LOG_DIRECTORY, 0777);
	if (sceRtcGetCurrentTick(&tick) >= 0) {
		snprintf(path, sizeof(path), "%s/session-%llu.log", CHAT_LOG_DIRECTORY,
			(unsigned long long)tick.tick);
	} else {
		snprintf(path, sizeof(path), "%s/session-unknown.log", CHAT_LOG_DIRECTORY);
	}
	chat_log_file = sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
}

static size_t bounded_length(const char *text, size_t maximum) {
	size_t length = 0;
	if (text == NULL) {
		return 0;
	}
	while (length <= maximum && text[length] != '\0') {
		++length;
	}
	return length;
}

static void copy_text(char *target, size_t capacity, const char *source) {
	size_t length;
	if (capacity == 0) {
		return;
	}
	if (source == NULL) {
		target[0] = '\0';
		return;
	}
	length = strlen(source);
	if (length >= capacity) {
		length = capacity - 1;
	}
	memcpy(target, source, length);
	target[length] = '\0';
}

static int copy_out(const char *source, char *output, size_t capacity) {
	size_t length = strlen(source);
	if (output == NULL || capacity == 0) {
		return (int)length;
	}
	size_t copy_length = utf8_prefix(source, length, capacity - 1);
	memcpy(output, source, copy_length);
	output[copy_length] = '\0';
	return (int)length;
}

static int request_is_active(void) {
	return chat_state == CHAT_REQUEST_CONNECTING || chat_state == CHAT_REQUEST_STREAMING;
}

static int utf8_validate(const char *text, size_t length) {
	size_t index = 0;
	while (index < length) {
		unsigned char first = (unsigned char)text[index++];
		uint32_t codepoint;
		int remaining;
		uint32_t minimum;
		if (first <= 0x7F) {
			continue;
		}
		if (first >= 0xC2 && first <= 0xDF) {
			codepoint = first & 0x1F;
			remaining = 1;
			minimum = 0x80;
		} else if (first >= 0xE0 && first <= 0xEF) {
			codepoint = first & 0x0F;
			remaining = 2;
			minimum = 0x800;
		} else if (first >= 0xF0 && first <= 0xF4) {
			codepoint = first & 0x07;
			remaining = 3;
			minimum = 0x10000;
		} else {
			return 0;
		}
		if (index + (size_t)remaining > length) {
			return 0;
		}
		while (remaining-- > 0) {
			unsigned char next = (unsigned char)text[index++];
			if ((next & 0xC0) != 0x80) {
				return 0;
			}
			codepoint = (codepoint << 6) | (next & 0x3F);
		}
		if (codepoint < minimum || codepoint > 0x10FFFF ||
			(codepoint >= 0xD800 && codepoint <= 0xDFFF)) {
			return 0;
		}
	}
	return 1;
}

static size_t utf8_prefix(const char *text, size_t length, size_t maximum) {
	size_t index = 0;
	size_t last = 0;
	while (index < length && index < maximum) {
		unsigned char first = (unsigned char)text[index];
		size_t bytes = first < 0x80 ? 1 : (first & 0xE0) == 0xC0 ? 2 :
			(first & 0xF0) == 0xE0 ? 3 : 4;
		if (index + bytes > length || index + bytes > maximum) {
			break;
		}
		index += bytes;
		last = index;
	}
	return last;
}

static uint32_t read_u32(const unsigned char *data) {
	return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
		((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static uint16_t read_u16(const unsigned char *data) {
	return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static void write_u32(unsigned char *data, uint32_t value) {
	data[0] = (unsigned char)value;
	data[1] = (unsigned char)(value >> 8);
	data[2] = (unsigned char)(value >> 16);
	data[3] = (unsigned char)(value >> 24);
}

static void write_u16(unsigned char *data, uint16_t value) {
	data[0] = (unsigned char)value;
	data[1] = (unsigned char)(value >> 8);
}

static uint32_t crc32_bytes(const unsigned char *data, size_t length) {
	uint32_t crc = 0xFFFFFFFFU;
	for (size_t index = 0; index < length; ++index) {
		crc ^= data[index];
		for (int bit = 0; bit < 8; ++bit) {
			uint32_t mask = (uint32_t)-(int32_t)(crc & 1U);
			crc = (crc >> 1) ^ (0xEDB88320U & mask);
		}
	}
	return ~crc;
}

static size_t serialize_history_locked(void) {
	size_t position = CHAT_HISTORY_HEADER_SIZE;
	for (int conversation = 0; conversation < chat_conversation_total; ++conversation) {
		const ChatConversation *item = &chat_conversations[conversation];
		if (position + 4 > sizeof(chat_history_buffer)) {
			return 0;
		}
		write_u32(chat_history_buffer + position, item->message_count);
		position += 4;
		for (int message = 0; message < item->message_count; ++message) {
			const ChatMessage *entry = &item->messages[message];
			if (position + 4 + entry->length > sizeof(chat_history_buffer)) {
				return 0;
			}
			chat_history_buffer[position++] = entry->role;
			chat_history_buffer[position++] = 0;
			write_u16(chat_history_buffer + position, entry->length);
			position += 2;
			memcpy(chat_history_buffer + position, entry->text, entry->length);
			position += entry->length;
		}
	}
	memcpy(chat_history_buffer, chat_history_magic, sizeof(chat_history_magic));
	write_u32(chat_history_buffer + 8, CHAT_HISTORY_VERSION);
	write_u32(chat_history_buffer + 12, (uint32_t)(position - CHAT_HISTORY_HEADER_SIZE));
	write_u32(chat_history_buffer + 16, (uint32_t)chat_conversation_total);
	write_u32(chat_history_buffer + 20, chat_active_index < 0 ? UINT32_MAX : (uint32_t)chat_active_index);
	write_u32(chat_history_buffer + 24,
		crc32_bytes(chat_history_buffer + CHAT_HISTORY_HEADER_SIZE, position - CHAT_HISTORY_HEADER_SIZE));
	return position;
}

static int write_all(SceUID file, const unsigned char *data, size_t length) {
	size_t position = 0;
	while (position < length) {
		int written = sceIoWrite(file, data + position, (SceSize)(length - position));
		if (written <= 0) {
			return -1;
		}
		position += (size_t)written;
	}
	return 0;
}

static int write_history_file(size_t length) {
	SceUID file;
	int result;
	if (length == 0) {
		return -1;
	}
	sceIoMkdir(CHAT_DIRECTORY, 0777);
	file = sceIoOpen(CHAT_HISTORY_TEMP, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
	if (file < 0) {
		return -1;
	}
	result = write_all(file, chat_history_buffer, length);
	if (result == 0 && sceIoSyncByFd(file, 0) < 0) {
		result = -1;
	}
	if (sceIoClose(file) < 0) {
		result = -1;
	}
	if (result < 0) {
		sceIoRemove(CHAT_HISTORY_TEMP);
		return -1;
	}
	sceIoRemove(CHAT_HISTORY_BACKUP);
	sceIoRename(CHAT_HISTORY_FILE, CHAT_HISTORY_BACKUP);
	if (sceIoRename(CHAT_HISTORY_TEMP, CHAT_HISTORY_FILE) < 0) {
		sceIoRename(CHAT_HISTORY_BACKUP, CHAT_HISTORY_FILE);
		sceIoRemove(CHAT_HISTORY_TEMP);
		return -1;
	}
	sceIoRemove(CHAT_HISTORY_BACKUP);
	sceIoSync("ux0:", 0);
	return 0;
}

static int load_history_file(void) {
	SceUID file = sceIoOpen(CHAT_HISTORY_FILE, SCE_O_RDONLY, 0);
	size_t length = 0;
	unsigned char extra;
	if (file < 0) {
		if (sceIoRename(CHAT_HISTORY_BACKUP, CHAT_HISTORY_FILE) < 0) {
			sceIoRename(CHAT_HISTORY_TEMP, CHAT_HISTORY_FILE);
		}
		file = sceIoOpen(CHAT_HISTORY_FILE, SCE_O_RDONLY, 0);
		if (file < 0) {
			return 1;
		}
	}
	while (length < sizeof(chat_history_buffer)) {
		int count = sceIoRead(file, chat_history_buffer + length,
			(SceSize)(sizeof(chat_history_buffer) - length));
		if (count < 0) {
			sceIoClose(file);
			return -1;
		}
		if (count == 0) {
			break;
		}
		length += (size_t)count;
	}
	if (length == sizeof(chat_history_buffer) && sceIoRead(file, &extra, 1) != 0) {
		sceIoClose(file);
		return -1;
	}
	sceIoClose(file);
	if (length < CHAT_HISTORY_HEADER_SIZE ||
		memcmp(chat_history_buffer, chat_history_magic, sizeof(chat_history_magic)) != 0 ||
		read_u32(chat_history_buffer + 8) != CHAT_HISTORY_VERSION) {
		return -1;
	}
	uint32_t payload_length = read_u32(chat_history_buffer + 12);
	uint32_t conversation_count = read_u32(chat_history_buffer + 16);
	uint32_t active = read_u32(chat_history_buffer + 20);
	if (payload_length != length - CHAT_HISTORY_HEADER_SIZE ||
		conversation_count > CHAT_MAX_CONVERSATIONS ||
		(active != UINT32_MAX && active >= conversation_count) ||
		read_u32(chat_history_buffer + 24) !=
			crc32_bytes(chat_history_buffer + CHAT_HISTORY_HEADER_SIZE, payload_length)) {
		return -1;
	}
	size_t position = CHAT_HISTORY_HEADER_SIZE;
	for (uint32_t conversation = 0; conversation < conversation_count; ++conversation) {
		if (position + 4 > length) {
			return -1;
		}
		uint32_t message_count = read_u32(chat_history_buffer + position);
		position += 4;
		if (message_count > CHAT_MAX_MESSAGES) {
			return -1;
		}
		for (uint32_t message = 0; message < message_count; ++message) {
			if (position + 4 > length) {
				return -1;
			}
			uint8_t role = chat_history_buffer[position];
			uint16_t message_length = read_u16(chat_history_buffer + position + 2);
			if (chat_history_buffer[position + 1] != 0) {
				return -1;
			}
			position += 4;
			if ((role != CHAT_ROLE_USER && role != CHAT_ROLE_ASSISTANT) ||
				message_length > CHAT_MAX_MESSAGE_BYTES || position + message_length > length ||
				memchr(chat_history_buffer + position, 0, message_length) != NULL ||
				!utf8_validate((const char *)chat_history_buffer + position, message_length)) {
				return -1;
			}
			position += message_length;
		}
	}
	if (position != length) {
		return -1;
	}
	position = CHAT_HISTORY_HEADER_SIZE;
	memset(chat_conversations, 0, sizeof(chat_conversations));
	for (uint32_t conversation = 0; conversation < conversation_count; ++conversation) {
		ChatConversation *item = &chat_conversations[conversation];
		item->message_count = (uint8_t)read_u32(chat_history_buffer + position);
		position += 4;
		for (int message = 0; message < item->message_count; ++message) {
			ChatMessage *entry = &item->messages[message];
			entry->role = chat_history_buffer[position];
			entry->length = read_u16(chat_history_buffer + position + 2);
			position += 4;
			memcpy(entry->text, chat_history_buffer + position, entry->length);
			entry->text[entry->length] = '\0';
			position += entry->length;
		}
	}
	chat_conversation_total = (int)conversation_count;
	chat_active_index = active == UINT32_MAX ? -1 : (int)active;
	return 0;
}

static void remove_oldest_messages(ChatConversation *conversation, int keep) {
	int remove_count = conversation->message_count - keep;
	if (remove_count <= 0) {
		return;
	}
	memmove(conversation->messages, conversation->messages + remove_count,
		(size_t)keep * sizeof(conversation->messages[0]));
	memset(conversation->messages + keep, 0,
		(size_t)remove_count * sizeof(conversation->messages[0]));
	conversation->message_count = (uint8_t)keep;
}

static void remove_message(ChatConversation *conversation, int index) {
	if (index < 0 || index >= conversation->message_count) {
		return;
	}
	if (index + 1 < conversation->message_count) {
		memmove(conversation->messages + index, conversation->messages + index + 1,
			(size_t)(conversation->message_count - index - 1) * sizeof(conversation->messages[0]));
	}
	--conversation->message_count;
	memset(&conversation->messages[conversation->message_count], 0,
		sizeof(conversation->messages[0]));
}

static size_t json_escaped_length(const char *text, size_t length) {
	size_t total = 0;
	for (size_t index = 0; index < length; ++index) {
		unsigned char value = (unsigned char)text[index];
		size_t addition = (value == '"' || value == '\\' || value == '\b' || value == '\f' ||
			value == '\n' || value == '\r' || value == '\t') ? 2 : value < 0x20 ? 6 : 1;
		if (total > SIZE_MAX - addition) {
			return SIZE_MAX;
		}
		total += addition;
	}
	return total;
}

static int append_bytes(char *buffer, size_t capacity, size_t *length,
	const char *data, size_t data_length) {
	if (*length >= capacity || data_length > capacity - *length - 1) {
		return 0;
	}
	memcpy(buffer + *length, data, data_length);
	*length += data_length;
	buffer[*length] = '\0';
	return 1;
}

static int append_json_string(char *buffer, size_t capacity, size_t *length,
	const char *text, size_t text_length) {
	static const char hex[] = "0123456789abcdef";
	if (!append_bytes(buffer, capacity, length, "\"", 1)) {
		return 0;
	}
	for (size_t index = 0; index < text_length; ++index) {
		unsigned char value = (unsigned char)text[index];
		const char *escape = NULL;
		char unicode[6];
		if (value == '"') escape = "\\\"";
		else if (value == '\\') escape = "\\\\";
		else if (value == '\b') escape = "\\b";
		else if (value == '\f') escape = "\\f";
		else if (value == '\n') escape = "\\n";
		else if (value == '\r') escape = "\\r";
		else if (value == '\t') escape = "\\t";
		if (escape != NULL) {
			if (!append_bytes(buffer, capacity, length, escape, 2)) return 0;
		} else if (value < 0x20) {
			unicode[0] = '\\'; unicode[1] = 'u'; unicode[2] = '0'; unicode[3] = '0';
			unicode[4] = hex[value >> 4]; unicode[5] = hex[value & 15];
			if (!append_bytes(buffer, capacity, length, unicode, sizeof(unicode))) return 0;
		} else if (!append_bytes(buffer, capacity, length, (const char *)&text[index], 1)) {
			return 0;
		}
	}
	return append_bytes(buffer, capacity, length, "\"", 1);
}

static int build_request_locked(void) {
	ChatConversation *conversation = &chat_conversations[chat_worker.conversation_index];
	size_t model_length = strlen(chat_worker.model);
	size_t base = strlen("{\"model\":,\"stream\":true,\"messages\":[]}") +
		json_escaped_length(chat_worker.model, model_length) + 2;
	int first = conversation->message_count;
	for (int index = conversation->message_count - 1; index >= 0; --index) {
		ChatMessage *message = &conversation->messages[index];
		if (message->role == CHAT_ROLE_ASSISTANT && message->length == 0) {
			continue;
		}
		size_t item_length = strlen("{\"role\":\"assistant\",\"content\":}") +
			json_escaped_length(message->text, message->length) + 2;
		if (first < conversation->message_count) {
			++item_length;
		}
		if (item_length >= CHAT_REQUEST_BUFFER_SIZE || base > CHAT_REQUEST_BUFFER_SIZE - item_length - 1) {
			break;
		}
		base += item_length;
		first = index;
	}
	while (first < conversation->message_count &&
		conversation->messages[first].role != CHAT_ROLE_USER) {
		++first;
	}
	if (first >= conversation->message_count ||
		conversation->messages[conversation->message_count - 2].role != CHAT_ROLE_USER) {
		return 0;
	}
	size_t length = 0;
	if (!append_bytes(chat_worker.request, sizeof(chat_worker.request), &length, "{\"model\":", 9) ||
		!append_json_string(chat_worker.request, sizeof(chat_worker.request), &length,
			chat_worker.model, model_length) ||
		!append_bytes(chat_worker.request, sizeof(chat_worker.request), &length,
			",\"stream\":true,\"messages\":[", 27)) {
		return 0;
	}
	int written = 0;
	for (int index = first; index < conversation->message_count; ++index) {
		ChatMessage *message = &conversation->messages[index];
		if (message->role == CHAT_ROLE_ASSISTANT && message->length == 0) {
			continue;
		}
		const char *role = message->role == CHAT_ROLE_USER ? "user" : "assistant";
		if ((written && !append_bytes(chat_worker.request, sizeof(chat_worker.request), &length, ",", 1)) ||
			!append_bytes(chat_worker.request, sizeof(chat_worker.request), &length, "{\"role\":", 8) ||
			!append_json_string(chat_worker.request, sizeof(chat_worker.request), &length, role, strlen(role)) ||
			!append_bytes(chat_worker.request, sizeof(chat_worker.request), &length, ",\"content\":", 11) ||
			!append_json_string(chat_worker.request, sizeof(chat_worker.request), &length,
				message->text, message->length) ||
			!append_bytes(chat_worker.request, sizeof(chat_worker.request), &length, "}", 1)) {
			return 0;
		}
		written = 1;
	}
	return written && append_bytes(chat_worker.request, sizeof(chat_worker.request), &length, "]}", 2);
}

static void json_skip_space(JsonCursor *cursor) {
	while (cursor->current < cursor->end && (*cursor->current == ' ' || *cursor->current == '\t' ||
		*cursor->current == '\r' || *cursor->current == '\n')) {
		++cursor->current;
	}
}

static int hex_digit(char value) {
	if (value >= '0' && value <= '9') return value - '0';
	if (value >= 'a' && value <= 'f') return value - 'a' + 10;
	if (value >= 'A' && value <= 'F') return value - 'A' + 10;
	return -1;
}

static int json_skip_string(JsonCursor *cursor) {
	if (cursor->current >= cursor->end || *cursor->current++ != '"') return 0;
	while (cursor->current < cursor->end) {
		unsigned char value = (unsigned char)*cursor->current++;
		if (value == '"') return 1;
		if (value < 0x20) return 0;
		if (value == '\\') {
			if (cursor->current >= cursor->end) return 0;
			char escape = *cursor->current++;
			if (escape == 'u') {
				if (cursor->end - cursor->current < 4) return 0;
				for (int index = 0; index < 4; ++index) {
					if (hex_digit(cursor->current[index]) < 0) return 0;
				}
				cursor->current += 4;
			} else if (strchr("\"\\/bfnrt", escape) == NULL) {
				return 0;
			}
		}
	}
	return 0;
}

static int json_skip_value(JsonCursor *cursor, int depth) {
	if (depth > CHAT_JSON_MAX_DEPTH) return 0;
	json_skip_space(cursor);
	if (cursor->current >= cursor->end) return 0;
	if (*cursor->current == '"') return json_skip_string(cursor);
	if (*cursor->current == '{') {
		++cursor->current;
		json_skip_space(cursor);
		if (cursor->current < cursor->end && *cursor->current == '}') {
			++cursor->current;
			return 1;
		}
		for (;;) {
			if (!json_skip_string(cursor)) return 0;
			json_skip_space(cursor);
			if (cursor->current >= cursor->end || *cursor->current++ != ':') return 0;
			if (!json_skip_value(cursor, depth + 1)) return 0;
			json_skip_space(cursor);
			if (cursor->current >= cursor->end) return 0;
			if (*cursor->current == '}') {
				++cursor->current;
				return 1;
			}
			if (*cursor->current++ != ',') return 0;
			json_skip_space(cursor);
		}
	}
	if (*cursor->current == '[') {
		++cursor->current;
		json_skip_space(cursor);
		if (cursor->current < cursor->end && *cursor->current == ']') {
			++cursor->current;
			return 1;
		}
		for (;;) {
			if (!json_skip_value(cursor, depth + 1)) return 0;
			json_skip_space(cursor);
			if (cursor->current >= cursor->end) return 0;
			if (*cursor->current == ']') {
				++cursor->current;
				return 1;
			}
			if (*cursor->current++ != ',') return 0;
		}
	}
	const char *start = cursor->current;
	while (cursor->current < cursor->end && strchr(" \t\r\n,}]", *cursor->current) == NULL) {
		++cursor->current;
	}
	if (cursor->current == start) return 0;
	size_t length = (size_t)(cursor->current - start);
	if ((length == 4 && (memcmp(start, "true", 4) == 0 || memcmp(start, "null", 4) == 0)) ||
		(length == 5 && memcmp(start, "false", 5) == 0)) return 1;
	JsonCursor number = { start, cursor->current };
	if (*number.current == '-') ++number.current;
	if (number.current >= number.end) return 0;
	if (*number.current == '0') ++number.current;
	else {
		if (*number.current < '1' || *number.current > '9') return 0;
		while (number.current < number.end && *number.current >= '0' && *number.current <= '9') ++number.current;
	}
	if (number.current < number.end && *number.current == '.') {
		++number.current;
		if (number.current >= number.end || *number.current < '0' || *number.current > '9') return 0;
		while (number.current < number.end && *number.current >= '0' && *number.current <= '9') ++number.current;
	}
	if (number.current < number.end && (*number.current == 'e' || *number.current == 'E')) {
		++number.current;
		if (number.current < number.end && (*number.current == '+' || *number.current == '-')) ++number.current;
		if (number.current >= number.end || *number.current < '0' || *number.current > '9') return 0;
		while (number.current < number.end && *number.current >= '0' && *number.current <= '9') ++number.current;
	}
	return number.current == number.end;
}

static int decode_json_string(const char *start, const char *end, char *output,
	size_t capacity, size_t *output_length, int *truncated) {
	JsonCursor cursor = { start, end };
	size_t length = 0;
	if (capacity == 0) return 0;
	if (cursor.current >= cursor.end || *cursor.current++ != '"') return 0;
	while (cursor.current < cursor.end) {
		uint32_t codepoint;
		unsigned char value = (unsigned char)*cursor.current++;
		if (value == '"') {
			if (cursor.current != cursor.end) return 0;
			output[length] = '\0';
			*output_length = length;
			return 1;
		}
		if (value < 0x20) return 0;
		if (value != '\\') {
			size_t bytes = value < 0x80 ? 1 : (value & 0xE0) == 0xC0 ? 2 :
				(value & 0xF0) == 0xE0 ? 3 : 4;
			const char *sequence = cursor.current - 1;
			if (cursor.end - sequence < (ptrdiff_t)bytes || !utf8_validate(sequence, bytes)) return 0;
			cursor.current += bytes - 1;
			if (!*truncated && length + bytes < capacity) {
				memcpy(output + length, sequence, bytes);
				length += bytes;
			} else {
				*truncated = 1;
			}
			continue;
		}
		if (cursor.current >= cursor.end) return 0;
		char escape = *cursor.current++;
		if (escape == '"' || escape == '\\' || escape == '/') codepoint = (unsigned char)escape;
		else if (escape == 'b') codepoint = '\b';
		else if (escape == 'f') codepoint = '\f';
		else if (escape == 'n') codepoint = '\n';
		else if (escape == 'r') codepoint = '\r';
		else if (escape == 't') codepoint = '\t';
		else if (escape == 'u') {
			if (cursor.end - cursor.current < 4) return 0;
			codepoint = 0;
			for (int index = 0; index < 4; ++index) {
				int digit = hex_digit(*cursor.current++);
				if (digit < 0) return 0;
				codepoint = (codepoint << 4) | (uint32_t)digit;
			}
			if (codepoint >= 0xD800 && codepoint <= 0xDBFF) {
				if (cursor.end - cursor.current < 6 || cursor.current[0] != '\\' || cursor.current[1] != 'u') return 0;
				cursor.current += 2;
				uint32_t low = 0;
				for (int index = 0; index < 4; ++index) {
					int digit = hex_digit(*cursor.current++);
					if (digit < 0) return 0;
					low = (low << 4) | (uint32_t)digit;
				}
				if (low < 0xDC00 || low > 0xDFFF) return 0;
				codepoint = 0x10000 + ((codepoint - 0xD800) << 10) + (low - 0xDC00);
			} else if (codepoint >= 0xDC00 && codepoint <= 0xDFFF) return 0;
		} else return 0;
		if (codepoint == 0) codepoint = 0xFFFD;
		char encoded[4];
		size_t bytes;
		if (codepoint <= 0x7F) { encoded[0] = (char)codepoint; bytes = 1; }
		else if (codepoint <= 0x7FF) {
			encoded[0] = (char)(0xC0 | (codepoint >> 6));
			encoded[1] = (char)(0x80 | (codepoint & 0x3F)); bytes = 2;
		} else if (codepoint <= 0xFFFF) {
			encoded[0] = (char)(0xE0 | (codepoint >> 12));
			encoded[1] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
			encoded[2] = (char)(0x80 | (codepoint & 0x3F)); bytes = 3;
		} else {
			encoded[0] = (char)(0xF0 | (codepoint >> 18));
			encoded[1] = (char)(0x80 | ((codepoint >> 12) & 0x3F));
			encoded[2] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
			encoded[3] = (char)(0x80 | (codepoint & 0x3F)); bytes = 4;
		}
		if (!*truncated && length + bytes < capacity) {
			memcpy(output + length, encoded, bytes);
			length += bytes;
		} else {
			*truncated = 1;
		}
	}
	return 0;
}

static int json_key_equals(const char *start, const char *end, const char *key) {
	char decoded[32];
	size_t length = 0;
	int truncated = 0;
	return decode_json_string(start, end, decoded, sizeof(decoded), &length, &truncated) &&
		!truncated && strlen(key) == length && memcmp(decoded, key, length) == 0;
}

static int json_find_member(const char *start, const char *end, const char *key,
	const char **value_start, const char **value_end) {
	JsonCursor cursor = { start, end };
	json_skip_space(&cursor);
	if (cursor.current >= cursor.end || *cursor.current++ != '{') return -1;
	json_skip_space(&cursor);
	if (cursor.current < cursor.end && *cursor.current == '}') return 0;
	for (;;) {
		const char *key_start = cursor.current;
		if (!json_skip_string(&cursor)) return -1;
		const char *key_end = cursor.current;
		json_skip_space(&cursor);
		if (cursor.current >= cursor.end || *cursor.current++ != ':') return -1;
		json_skip_space(&cursor);
		const char *member_start = cursor.current;
		if (!json_skip_value(&cursor, 1)) return -1;
		const char *member_end = cursor.current;
		if (json_key_equals(key_start, key_end, key)) {
			*value_start = member_start;
			*value_end = member_end;
			return 1;
		}
		json_skip_space(&cursor);
		if (cursor.current >= cursor.end) return -1;
		if (*cursor.current == '}') return 0;
		if (*cursor.current++ != ',') return -1;
		json_skip_space(&cursor);
	}
}

static int extract_delta_content(const char *json, size_t json_length, char *output,
	size_t capacity, size_t *output_length, int *truncated) {
	const char *choices_start;
	const char *choices_end;
	const char *delta_start;
	const char *delta_end;
	const char *content_start;
	const char *content_end;
	int found = json_find_member(json, json + json_length, "choices", &choices_start, &choices_end);
	if (found <= 0) return found;
	JsonCursor choices = { choices_start, choices_end };
	json_skip_space(&choices);
	if (choices.current >= choices.end || *choices.current++ != '[') return -1;
	json_skip_space(&choices);
	if (choices.current >= choices.end || *choices.current == ']') return 0;
	const char *first_start = choices.current;
	if (!json_skip_value(&choices, 1)) return -1;
	const char *first_end = choices.current;
	found = json_find_member(first_start, first_end, "delta", &delta_start, &delta_end);
	if (found <= 0) return found;
	found = json_find_member(delta_start, delta_end, "content", &content_start, &content_end);
	if (found <= 0) return found;
	JsonCursor content = { content_start, content_end };
	json_skip_space(&content);
	if (content.end - content.current == 4 && memcmp(content.current, "null", 4) == 0) return 0;
	return decode_json_string(content.current, content.end, output, capacity, output_length, truncated) ? 1 : -1;
}

static int append_assistant_content(const char *text, size_t length) {
	int appended = 0;
	chat_lock();
	if (chat_worker.conversation_index >= 0 && chat_worker.conversation_index < chat_conversation_total) {
		ChatConversation *conversation = &chat_conversations[chat_worker.conversation_index];
		if (chat_worker.assistant_index >= 0 && chat_worker.assistant_index < conversation->message_count) {
			ChatMessage *message = &conversation->messages[chat_worker.assistant_index];
			size_t available = CHAT_MAX_MESSAGE_BYTES - message->length;
			size_t copy_length = utf8_prefix(text, length, available);
			if (copy_length > 0) {
				memcpy(message->text + message->length, text, copy_length);
				message->length = (uint16_t)(message->length + copy_length);
				message->text[message->length] = '\0';
				appended = 1;
			}
			if (copy_length < length) chat_worker.sse.truncated = 1;
		}
	}
	if (appended) {
		chat_state = CHAT_REQUEST_STREAMING;
		copy_text(chat_status, sizeof(chat_status), "Recibiendo respuesta...");
	}
	chat_unlock();
	return appended;
}

static int process_sse_event(ChatSseParser *parser) {
	char decoded[CHAT_MAX_MESSAGE_BYTES + 1];
	size_t decoded_length = 0;
	int truncated = 0;
	while (parser->event_length > 0 &&
		(parser->event[parser->event_length - 1] == '\r' || parser->event[parser->event_length - 1] == '\n')) {
		--parser->event_length;
	}
	parser->event[parser->event_length] = '\0';
	if (parser->event_length == 0) return 1;
	if (parser->event_length == 6 && memcmp(parser->event, "[DONE]", 6) == 0) {
		parser->done = 1;
		return 1;
	}
	int result = extract_delta_content(parser->event, parser->event_length, decoded,
		sizeof(decoded), &decoded_length, &truncated);
	if (result < 0) return 0;
	if (result > 0 && decoded_length > 0) {
		append_assistant_content(decoded, decoded_length);
		parser->received_content = 1;
	}
	if (truncated) parser->truncated = 1;
	return 1;
}

static int finish_sse_line(ChatSseParser *parser) {
	if (parser->line_length > 0 && parser->line[parser->line_length - 1] == '\r') {
		--parser->line_length;
	}
	parser->line[parser->line_length] = '\0';
	if (parser->line_length == 0) {
		int result = process_sse_event(parser);
		parser->event_length = 0;
		parser->event[0] = '\0';
		return result;
	}
	if (parser->line[0] == ':') return 1;
	if (parser->line_length >= 5 && memcmp(parser->line, "data:", 5) == 0) {
		size_t start = 5;
		if (start < parser->line_length && parser->line[start] == ' ') ++start;
		size_t data_length = parser->line_length - start;
		if (parser->event_length > 0) {
			if (parser->event_length + 1 >= sizeof(parser->event)) return 0;
			parser->event[parser->event_length++] = '\n';
		}
		if (data_length > sizeof(parser->event) - parser->event_length - 1) return 0;
		memcpy(parser->event + parser->event_length, parser->line + start, data_length);
		parser->event_length += data_length;
		parser->event[parser->event_length] = '\0';
	}
	return 1;
}

static size_t write_sse_data(char *data, size_t size, size_t count, void *user_data) {
	ChatSseParser *parser = user_data;
	if (count != 0 && size > SIZE_MAX / count) return 0;
	size_t bytes = size * count;
	for (size_t index = 0; index < bytes; ++index) {
		if (parser->done) continue;
		char value = data[index];
		if (value == '\n') {
			if (!finish_sse_line(parser)) {
				parser->failed = 1;
				return 0;
			}
			parser->line_length = 0;
		} else {
			if (parser->line_length + 1 >= sizeof(parser->line)) {
				parser->failed = 1;
				return 0;
			}
			parser->line[parser->line_length++] = value;
		}
	}
	return bytes;
}

static int transfer_progress(void *user_data, curl_off_t download_total, curl_off_t download_now,
	curl_off_t upload_total, curl_off_t upload_now) {
	ChatWorker *worker = user_data;
	(void)download_total;
	(void)download_now;
	(void)upload_total;
	(void)upload_now;
	chat_lock();
	int cancelled = worker->cancel_requested;
	chat_unlock();
	return cancelled ? 1 : 0;
}

static const char *http_error_message(long status) {
	if (status == 401 || status == 403) return "API key rechazada.";
	if (status == 404) return "Endpoint o modelo no encontrado.";
	if (status == 429) return "Limite de solicitudes alcanzado.";
	if (status >= 500 && status <= 599) return "El servidor no esta disponible.";
	if (status >= 400) return "La API rechazo la solicitud.";
	return NULL;
}

static const char *curl_error_message(CURLcode code) {
	if (code == CURLE_PEER_FAILED_VERIFICATION || code == CURLE_SSL_CERTPROBLEM ||
		code == CURLE_SSL_CACERT_BADFILE) return "No se pudo verificar el certificado TLS.";
	if (code == CURLE_SSL_CONNECT_ERROR) return "Fallo la conexion TLS.";
	if (code == CURLE_COULDNT_RESOLVE_HOST) return "No se pudo resolver el servidor.";
	if (code == CURLE_COULDNT_CONNECT) return "No se pudo conectar al servidor.";
	if (code == CURLE_OPERATION_TIMEDOUT) return "La solicitud agoto el tiempo.";
	if (code == CURLE_SEND_ERROR || code == CURLE_RECV_ERROR) return "Se interrumpio la conexion.";
	if (code == CURLE_OUT_OF_MEMORY) return "Memoria insuficiente para la solicitud.";
	return "Error de red.";
}

static int chat_worker_entry(SceSize arguments, void *argument) {
	CURL *curl = NULL;
	struct curl_slist *headers = NULL;
	CURLcode result = CURLE_FAILED_INIT;
	long http_status = 0;
	ChatRequestState final_state = CHAT_REQUEST_ERROR;
	const char *final_status = "Solicitud fallida.";
	const char *final_error = "No se pudo iniciar la solicitud.";
	(void)arguments;
	(void)argument;

	chat_lock();
	int request_ready = build_request_locked();
	chat_unlock();
	if (!request_ready) {
		final_error = "El mensaje no cabe en la solicitud.";
		goto finish;
	}
	curl = curl_easy_init();
	if (curl == NULL) goto finish;
	headers = curl_slist_append(headers, "Accept: text/event-stream");
	if (headers == NULL) {
		final_error = "Memoria insuficiente para la solicitud.";
		goto finish;
	}
	struct curl_slist *updated_headers = curl_slist_append(headers, "Content-Type: application/json");
	if (updated_headers == NULL) {
		final_error = "Memoria insuficiente para la solicitud.";
		goto finish;
	}
	headers = updated_headers;
	if (chat_worker.api_key[0] != '\0') {
		struct curl_slist *updated = curl_slist_append(headers, chat_worker.authorization);
		if (updated == NULL) {
			final_error = "Memoria insuficiente para la solicitud.";
			goto finish;
		}
		headers = updated;
	}
	curl_easy_setopt(curl, CURLOPT_URL, chat_worker.url);
	curl_easy_setopt(curl, CURLOPT_POST, 1L);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, chat_worker.request);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)strlen(chat_worker.request));
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "VagaChatVITA/1.1");
	curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
	curl_easy_setopt(curl, CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_2);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
	curl_easy_setopt(curl, CURLOPT_CAINFO, CHAT_CA_FILE);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 180L);
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_sse_data);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &chat_worker.sse);
	curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, transfer_progress);
	curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &chat_worker);
	curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
	chat_lock();
	chat_state = CHAT_REQUEST_STREAMING;
	copy_text(chat_status, sizeof(chat_status), "Esperando respuesta...");
	chat_unlock();
	for (int attempt = 0; attempt < 3; ++attempt) {
		result = curl_easy_perform(curl);
		if ((result != CURLE_COULDNT_RESOLVE_HOST && result != CURLE_COULDNT_CONNECT) ||
			chat_worker.sse.received_content || chat_worker.cancel_requested || attempt == 2) {
			break;
		}
		memset(&chat_worker.sse, 0, sizeof(chat_worker.sse));
		chat_lock();
		chat_log_locked("REQUEST_RETRY attempt=%d curl=%d", attempt + 2, result);
		chat_unlock();
		sceKernelDelayThread(250000);
	}
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_status);
	if (!chat_worker.sse.failed && chat_worker.sse.line_length > 0) {
		if (!finish_sse_line(&chat_worker.sse)) chat_worker.sse.failed = 1;
		chat_worker.sse.line_length = 0;
	}
	if (!chat_worker.sse.failed && chat_worker.sse.event_length > 0) {
		if (!process_sse_event(&chat_worker.sse)) chat_worker.sse.failed = 1;
		chat_worker.sse.event_length = 0;
	}
	chat_lock();
	int cancelled = chat_worker.cancel_requested;
	chat_worker.accept_cancel = 0;
	chat_unlock();
	if (cancelled || result == CURLE_ABORTED_BY_CALLBACK) {
		final_state = CHAT_REQUEST_CANCELLED;
		final_status = "Solicitud cancelada.";
		final_error = "";
	} else if (http_error_message(http_status) != NULL) {
		final_error = http_error_message(http_status);
	} else if (chat_worker.sse.failed) {
		final_error = "La respuesta de la API no es valida.";
	} else if (result != CURLE_OK) {
		final_error = curl_error_message(result);
	} else if (!chat_worker.sse.received_content) {
		final_error = "La API respondio sin contenido.";
	} else {
		final_state = CHAT_REQUEST_COMPLETED;
		final_status = chat_worker.sse.truncated ? "Respuesta completada y recortada." : "Respuesta completada.";
		final_error = "";
	}

finish:
	if (headers != NULL) curl_slist_free_all(headers);
	if (curl != NULL) curl_easy_cleanup(curl);
	chat_lock();
	chat_http_status = http_status;
	chat_worker.accept_cancel = 0;
	ChatConversation *conversation = NULL;
	if (chat_worker.conversation_index >= 0 && chat_worker.conversation_index < chat_conversation_total) {
		conversation = &chat_conversations[chat_worker.conversation_index];
		if (chat_worker.assistant_index >= 0 && chat_worker.assistant_index < conversation->message_count &&
			conversation->messages[chat_worker.assistant_index].length == 0) {
			remove_message(conversation, chat_worker.assistant_index);
		}
	}
	size_t history_length = serialize_history_locked();
	chat_unlock();
	int save_result = write_history_file(history_length);
	chat_lock();
	if (save_result < 0) {
		chat_state = CHAT_REQUEST_ERROR;
		copy_text(chat_status, sizeof(chat_status), "No se pudo guardar el historial.");
		copy_text(chat_error, sizeof(chat_error), "Error al guardar el historial local.");
	} else {
		chat_state = final_state;
		copy_text(chat_status, sizeof(chat_status), final_status);
		copy_text(chat_error, sizeof(chat_error), final_error);
	}
	chat_log_locked("REQUEST_END state=%d http=%ld curl=%d received=%d response_bytes=%u save=%d",
		(int)chat_state, http_status, result, chat_worker.sse.received_content,
		(unsigned int)(conversation != NULL && chat_worker.assistant_index >= 0 &&
			chat_worker.assistant_index < conversation->message_count ?
			conversation->messages[chat_worker.assistant_index].length : 0), save_result);
	chat_worker.finished = 1;
	chat_unlock();
	return 0;
}

int chat_init(void) {
	if (chat_initialized) return CHAT_OK;
	chat_mutex = sceKernelCreateMutex("VagaChatMutex", 0, 1, NULL);
	if (chat_mutex < 0) return CHAT_ERR_THREAD;
	memset(chat_conversations, 0, sizeof(chat_conversations));
	memset(&chat_worker, 0, sizeof(chat_worker));
	chat_conversation_total = 0;
	chat_active_index = -1;
	chat_thread = -1;
	chat_state = CHAT_REQUEST_IDLE;
	chat_http_status = 0;
	chat_status[0] = '\0';
	chat_error[0] = '\0';
	chat_open_log();
	int load_result = load_history_file();
	chat_lock();
	chat_log_locked("SESSION_START history_load=%d conversations=%d active=%d",
		load_result, chat_conversation_total, chat_active_index);
	if (load_result < 0) {
		copy_text(chat_status, sizeof(chat_status), "Historial local ignorado.");
		copy_text(chat_error, sizeof(chat_error), "El historial guardado no es valido.");
	}
	chat_unlock();
	chat_initialized = 1;
	return CHAT_OK;
}

void chat_shutdown(void) {
	if (!chat_initialized) return;
	chat_cancel();
	SceUID thread;
	chat_lock();
	thread = chat_thread;
	chat_unlock();
	if (thread >= 0) {
		sceKernelWaitThreadEnd(thread, NULL, NULL);
		sceKernelDeleteThread(thread);
	}
	chat_lock();
	chat_thread = -1;
	size_t history_length = serialize_history_locked();
	chat_unlock();
	int save_result = write_history_file(history_length);
	chat_lock();
	chat_log_locked("HISTORY_SAVE result=%d bytes=%u", save_result, (unsigned int)history_length);
	chat_log_locked("SESSION_END");
	SceUID log_file = chat_log_file;
	chat_log_file = -1;
	chat_unlock();
	if (log_file >= 0) {
		sceIoClose(log_file);
	}
	sceKernelDeleteMutex(chat_mutex);
	chat_mutex = -1;
	chat_initialized = 0;
}

void chat_update(void) {
	if (!chat_initialized) return;
	SceUID finished_thread = -1;
	chat_lock();
	if (chat_thread >= 0 && chat_worker.finished) {
		finished_thread = chat_thread;
		chat_thread = -1;
	}
	chat_unlock();
	if (finished_thread >= 0) {
		SceUInt timeout = 0;
		if (sceKernelWaitThreadEnd(finished_thread, NULL, &timeout) >= 0) {
			sceKernelDeleteThread(finished_thread);
		} else {
			chat_lock();
			if (chat_thread < 0) chat_thread = finished_thread;
			chat_unlock();
		}
	}
}

int chat_new_conversation(void) {
	if (!chat_initialized) return CHAT_ERR_NOT_INITIALIZED;
	chat_lock();
	if (request_is_active()) {
		chat_unlock();
		return CHAT_ERR_BUSY;
	}
	if (chat_conversation_total == CHAT_MAX_CONVERSATIONS) {
		memmove(chat_conversations, chat_conversations + 1,
			(CHAT_MAX_CONVERSATIONS - 1) * sizeof(chat_conversations[0]));
		--chat_conversation_total;
	}
	chat_active_index = chat_conversation_total++;
	memset(&chat_conversations[chat_active_index], 0, sizeof(chat_conversations[chat_active_index]));
	size_t length = serialize_history_locked();
	int index = chat_active_index;
	chat_unlock();
	int save_result = write_history_file(length);
	chat_lock();
	chat_log_locked("CONVERSATION_NEW index=%d save=%d", index, save_result);
	chat_unlock();
	return save_result < 0 ? CHAT_ERR_IO : index;
}

int chat_select_conversation(int index) {
	if (!chat_initialized) return CHAT_ERR_NOT_INITIALIZED;
	chat_lock();
	if (request_is_active()) {
		chat_unlock();
		return CHAT_ERR_BUSY;
	}
	if (index < 0 || index >= chat_conversation_total) {
		chat_unlock();
		return CHAT_ERR_RANGE;
	}
	chat_active_index = index;
	size_t length = serialize_history_locked();
	chat_unlock();
	int save_result = write_history_file(length);
	chat_lock();
	chat_log_locked("CONVERSATION_SELECT index=%d save=%d", index, save_result);
	chat_unlock();
	return save_result < 0 ? CHAT_ERR_IO : CHAT_OK;
}

int chat_save(void) {
	if (!chat_initialized) return CHAT_ERR_NOT_INITIALIZED;
	chat_lock();
	if (request_is_active()) {
		chat_unlock();
		return CHAT_ERR_BUSY;
	}
	size_t length = serialize_history_locked();
	chat_unlock();
	int save_result = write_history_file(length);
	chat_lock();
	chat_log_locked("HISTORY_SAVE result=%d bytes=%u", save_result, (unsigned int)length);
	chat_unlock();
	return save_result < 0 ? CHAT_ERR_IO : CHAT_OK;
}

int chat_conversation_count(void) {
	if (!chat_initialized) return 0;
	chat_lock();
	int count = chat_conversation_total;
	chat_unlock();
	return count;
}

int chat_active_conversation(void) {
	if (!chat_initialized) return -1;
	chat_lock();
	int index = chat_active_index;
	chat_unlock();
	return index;
}

int chat_copy_conversation_title(int index, char *output, size_t capacity) {
	if (!chat_initialized) return CHAT_ERR_NOT_INITIALIZED;
	char title[CHAT_MAX_TITLE_BYTES + 1] = "Nueva conversacion";
	chat_lock();
	if (index < 0 || index >= chat_conversation_total) {
		chat_unlock();
		return CHAT_ERR_RANGE;
	}
	ChatConversation *conversation = &chat_conversations[index];
	for (int message = 0; message < conversation->message_count; ++message) {
		ChatMessage *entry = &conversation->messages[message];
		if (entry->role == CHAT_ROLE_USER && entry->length > 0) {
			size_t length = utf8_prefix(entry->text, entry->length, CHAT_MAX_TITLE_BYTES);
			memcpy(title, entry->text, length);
			title[length] = '\0';
			break;
		}
	}
	int result = copy_out(title, output, capacity);
	chat_unlock();
	return result;
}

int chat_message_count(void) {
	if (!chat_initialized) return 0;
	chat_lock();
	int count = chat_active_index >= 0 ? chat_conversations[chat_active_index].message_count : 0;
	chat_unlock();
	return count;
}

int chat_copy_message(int index, ChatRole *role, char *output, size_t capacity) {
	if (!chat_initialized) return CHAT_ERR_NOT_INITIALIZED;
	chat_lock();
	if (chat_active_index < 0 || index < 0 ||
		index >= chat_conversations[chat_active_index].message_count) {
		chat_unlock();
		return CHAT_ERR_RANGE;
	}
	ChatMessage *message = &chat_conversations[chat_active_index].messages[index];
	if (role != NULL) *role = (ChatRole)message->role;
	if (output != NULL && capacity > 0) {
		size_t copy_length = utf8_prefix(message->text, message->length, capacity - 1);
		memcpy(output, message->text, copy_length);
		output[copy_length] = '\0';
	}
	int length = message->length;
	chat_unlock();
	return length;
}

static int has_header_control(const char *text, size_t length) {
	for (size_t index = 0; index < length; ++index) {
		unsigned char value = (unsigned char)text[index];
		if (value < 0x20 || value == 0x7F) return 1;
	}
	return 0;
}

static int has_url_unsafe_character(const char *text, size_t length) {
	for (size_t index = 0; index < length; ++index) {
		unsigned char value = (unsigned char)text[index];
		if (value <= 0x20 || value == 0x7F || value == '?' || value == '#') return 1;
	}
	return 0;
}

int chat_send(const char *endpoint, const char *api_key, const char *model, const char *message) {
	if (!chat_initialized) return CHAT_ERR_NOT_INITIALIZED;
	size_t endpoint_length = bounded_length(endpoint, CHAT_MAX_ENDPOINT_BYTES);
	size_t key_length = bounded_length(api_key, CHAT_MAX_API_KEY_BYTES);
	size_t model_length = bounded_length(model, CHAT_MAX_MODEL_BYTES);
	size_t message_length = bounded_length(message, CHAT_MAX_MESSAGE_BYTES);
	if (endpoint == NULL || strncmp(endpoint, "https://", 8) != 0 ||
		endpoint_length <= 8 || endpoint_length > CHAT_MAX_ENDPOINT_BYTES || endpoint[8] == '/' ||
		model == NULL || model_length == 0 || model_length > CHAT_MAX_MODEL_BYTES ||
		message == NULL || message_length == 0 || message_length > CHAT_MAX_MESSAGE_BYTES ||
		(api_key != NULL && key_length > CHAT_MAX_API_KEY_BYTES) ||
		!utf8_validate(model, model_length) || !utf8_validate(message, message_length) ||
		has_url_unsafe_character(endpoint, endpoint_length) ||
		(api_key != NULL && has_header_control(api_key, key_length))) {
		return CHAT_ERR_INVALID;
	}
	while (endpoint_length > 8 && endpoint[endpoint_length - 1] == '/') --endpoint_length;
	static const char suffix[] = "/chat/completions";
	if (endpoint_length + sizeof(suffix) > sizeof(chat_worker.url)) return CHAT_ERR_INVALID;
	chat_update();
	chat_lock();
	if (request_is_active() || chat_thread >= 0) {
		chat_unlock();
		return CHAT_ERR_BUSY;
	}
	if (chat_active_index < 0) {
		chat_active_index = 0;
		chat_conversation_total = 1;
		memset(&chat_conversations[0], 0, sizeof(chat_conversations[0]));
	}
	ChatConversation *conversation = &chat_conversations[chat_active_index];
	if (conversation->message_count > CHAT_MAX_MESSAGES - 2) {
		remove_oldest_messages(conversation, CHAT_MAX_MESSAGES - 2);
	}
	if (conversation->message_count > 0 && conversation->messages[0].role == CHAT_ROLE_ASSISTANT) {
		remove_message(conversation, 0);
	}
	ChatMessage *user = &conversation->messages[conversation->message_count++];
	memset(user, 0, sizeof(*user));
	user->role = CHAT_ROLE_USER;
	user->length = (uint16_t)message_length;
	memcpy(user->text, message, message_length);
	user->text[message_length] = '\0';
	ChatMessage *assistant = &conversation->messages[conversation->message_count++];
	memset(assistant, 0, sizeof(*assistant));
	assistant->role = CHAT_ROLE_ASSISTANT;
	memset(&chat_worker, 0, sizeof(chat_worker));
	memcpy(chat_worker.endpoint, endpoint, endpoint_length);
	chat_worker.endpoint[endpoint_length] = '\0';
	if (api_key != NULL && key_length > 0) memcpy(chat_worker.api_key, api_key, key_length);
	chat_worker.api_key[key_length] = '\0';
	memcpy(chat_worker.model, model, model_length);
	chat_worker.model[model_length] = '\0';
	memcpy(chat_worker.url, endpoint, endpoint_length);
	memcpy(chat_worker.url + endpoint_length, suffix, sizeof(suffix));
	if (key_length > 0) {
		static const char prefix[] = "Authorization: Bearer ";
		memcpy(chat_worker.authorization, prefix, sizeof(prefix) - 1);
		memcpy(chat_worker.authorization + sizeof(prefix) - 1, api_key, key_length);
		chat_worker.authorization[sizeof(prefix) - 1 + key_length] = '\0';
	}
	chat_worker.conversation_index = chat_active_index;
	chat_worker.assistant_index = conversation->message_count - 1;
	chat_worker.accept_cancel = 1;
	chat_state = CHAT_REQUEST_CONNECTING;
	chat_http_status = 0;
	copy_text(chat_status, sizeof(chat_status), "Conectando...");
	chat_error[0] = '\0';
	chat_log_locked("REQUEST_START conversation=%d model=%s message_bytes=%u",
		chat_active_index, chat_worker.model, (unsigned int)message_length);
	chat_thread = sceKernelCreateThread("VagaChatWorker", chat_worker_entry,
		CHAT_THREAD_PRIORITY, CHAT_THREAD_STACK_SIZE, 0,
		SCE_KERNEL_THREAD_CPU_AFFINITY_MASK_DEFAULT, NULL);
	SceUID thread = chat_thread;
	chat_unlock();
	if (thread < 0) {
		chat_lock();
		remove_message(conversation, chat_worker.assistant_index);
		chat_state = CHAT_REQUEST_ERROR;
		copy_text(chat_status, sizeof(chat_status), "No se pudo iniciar la solicitud.");
		copy_text(chat_error, sizeof(chat_error), "No se pudo crear el hilo de red.");
		chat_thread = -1;
		size_t history_length = serialize_history_locked();
		chat_log_locked("REQUEST_END state=%d reason=thread_create", (int)chat_state);
		chat_unlock();
		write_history_file(history_length);
		return CHAT_ERR_THREAD;
	}
	if (sceKernelStartThread(thread, 0, NULL) < 0) {
		sceKernelDeleteThread(thread);
		chat_lock();
		remove_message(conversation, chat_worker.assistant_index);
		chat_state = CHAT_REQUEST_ERROR;
		copy_text(chat_status, sizeof(chat_status), "No se pudo iniciar la solicitud.");
		copy_text(chat_error, sizeof(chat_error), "No se pudo arrancar el hilo de red.");
		chat_thread = -1;
		size_t history_length = serialize_history_locked();
		chat_log_locked("REQUEST_END state=%d reason=thread_start", (int)chat_state);
		chat_unlock();
		write_history_file(history_length);
		return CHAT_ERR_THREAD;
	}
	return CHAT_OK;
}

int chat_cancel(void) {
	if (!chat_initialized) return CHAT_ERR_NOT_INITIALIZED;
	chat_lock();
	if (!request_is_active() || !chat_worker.accept_cancel) {
		chat_unlock();
		return CHAT_ERR_INVALID;
	}
	chat_worker.cancel_requested = 1;
	copy_text(chat_status, sizeof(chat_status), "Cancelando...");
	chat_log_locked("REQUEST_CANCEL");
	chat_unlock();
	return CHAT_OK;
}

ChatRequestState chat_request_state(void) {
	if (!chat_initialized) return CHAT_REQUEST_IDLE;
	chat_lock();
	ChatRequestState state = chat_state;
	chat_unlock();
	return state;
}

long chat_request_status_code(void) {
	if (!chat_initialized) return 0;
	chat_lock();
	long status = chat_http_status;
	chat_unlock();
	return status;
}

int chat_copy_status(char *output, size_t capacity) {
	if (!chat_initialized) return CHAT_ERR_NOT_INITIALIZED;
	chat_lock();
	int result = copy_out(chat_status, output, capacity);
	chat_unlock();
	return result;
}

int chat_copy_error(char *output, size_t capacity) {
	if (!chat_initialized) return CHAT_ERR_NOT_INITIALIZED;
	chat_lock();
	int result = copy_out(chat_error, output, capacity);
	chat_unlock();
	return result;
}
