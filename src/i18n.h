#ifndef VAGACHAT_I18N_H
#define VAGACHAT_I18N_H

#include <stddef.h>

typedef enum AppLanguage {
	APP_LANGUAGE_SPANISH = 0,
	APP_LANGUAGE_ENGLISH,
	APP_LANGUAGE_ITALIAN,
	APP_LANGUAGE_COUNT
} AppLanguage;

void i18n_set_language(AppLanguage language);
AppLanguage i18n_get_language(void);
const char *i18n_translate(const char *spanish);
const char *i18n_language_code(AppLanguage language);
const char *i18n_language_name(AppLanguage language);
AppLanguage i18n_next_language(AppLanguage language);
AppLanguage i18n_previous_language(AppLanguage language);

const char *i18n_normal_system_prompt(AppLanguage language);
int i18n_game_system_prompt(AppLanguage language, const char *title, const char *title_id,
	char *output, size_t capacity);
const char *i18n_suggestions_system_prompt(AppLanguage language);
int i18n_suggestions_user_prompt(AppLanguage language, const char *title, const char *title_id,
	char *output, size_t capacity);

#endif
