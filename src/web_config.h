#ifndef VAGACHAT_WEB_CONFIG_H
#define VAGACHAT_WEB_CONFIG_H

#include <stddef.h>

#define WEB_CONFIG_PORT 8080
#define WEB_CONFIG_IP_CAPACITY 16
#define WEB_CONFIG_ENDPOINT_CAPACITY 255
#define WEB_CONFIG_API_KEY_CAPACITY 255
#define WEB_CONFIG_PASSWORD_HASH_BYTES 32

int web_config_hash_password(const char *password, unsigned char output[WEB_CONFIG_PASSWORD_HASH_BYTES]);
int web_config_start(const char *ip, const char *endpoint, const char *api_key,
	const unsigned char password_hash[WEB_CONFIG_PASSWORD_HASH_BYTES], int password_configured);
void web_config_stop(void);
int web_config_is_running(void);
int web_config_copy_ip(char *output, size_t capacity);
int web_config_take_pending(char *endpoint, size_t endpoint_capacity, char *api_key,
	size_t api_key_capacity, unsigned char password_hash[WEB_CONFIG_PASSWORD_HASH_BYTES],
	int *password_configured);

#endif
