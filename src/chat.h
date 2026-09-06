#ifndef VAGACHAT_CHAT_H
#define VAGACHAT_CHAT_H

#include <stddef.h>

#define CHAT_MAX_CONVERSATIONS 4
#define CHAT_MAX_MESSAGES 20
#define CHAT_MAX_MESSAGE_BYTES 2047
#define CHAT_MAX_TITLE_BYTES 63
#define CHAT_MAX_ENDPOINT_BYTES 511
#define CHAT_MAX_API_KEY_BYTES 511
#define CHAT_MAX_MODEL_BYTES 127
#define CHAT_SUGGESTION_COUNT 4
#define CHAT_SUGGESTION_MAX_BYTES 127

typedef enum ChatRole {
	CHAT_ROLE_USER = 1,
	CHAT_ROLE_ASSISTANT = 2
} ChatRole;

typedef enum ChatRequestState {
	CHAT_REQUEST_IDLE = 0,
	CHAT_REQUEST_CONNECTING,
	CHAT_REQUEST_STREAMING,
	CHAT_REQUEST_COMPLETED,
	CHAT_REQUEST_CANCELLED,
	CHAT_REQUEST_ERROR
} ChatRequestState;

typedef enum ChatSuggestionState {
	CHAT_SUGGESTIONS_IDLE = 0,
	CHAT_SUGGESTIONS_LOADING,
	CHAT_SUGGESTIONS_READY,
	CHAT_SUGGESTIONS_ERROR,
	CHAT_SUGGESTIONS_CANCELLED
} ChatSuggestionState;

enum {
	CHAT_OK = 0,
	CHAT_ERR_INVALID = -1,
	CHAT_ERR_BUSY = -2,
	CHAT_ERR_RANGE = -3,
	CHAT_ERR_IO = -4,
	CHAT_ERR_THREAD = -5,
	CHAT_ERR_NOT_INITIALIZED = -6
};

int chat_init(void);
void chat_shutdown(void);
void chat_update(void);

int chat_new_conversation(void);
int chat_select_conversation(int index);
int chat_save(void);
int chat_conversation_count(void);
int chat_active_conversation(void);
int chat_copy_conversation_title(int index, char *output, size_t capacity);

int chat_message_count(void);
int chat_copy_message(int index, ChatRole *role, char *output, size_t capacity);

int chat_send(const char *endpoint, const char *api_key, const char *model, const char *message,
	const char *system_prompt, const char *context_prompt);
int chat_cancel(void);
ChatRequestState chat_request_state(void);
long chat_request_status_code(void);
int chat_copy_status(char *output, size_t capacity);
int chat_copy_error(char *output, size_t capacity);

int chat_request_suggestions(const char *endpoint, const char *api_key, const char *model,
	const char *title, const char *title_id);
ChatSuggestionState chat_suggestion_state(void);
int chat_copy_suggestion(int index, char *output, size_t capacity);
int chat_copy_suggestion_error(char *output, size_t capacity);
int chat_cancel_suggestions(void);

#endif
