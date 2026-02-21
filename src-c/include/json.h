/*
 * json.h — cJSON wrapper helpers
 */
#ifndef JSON_H
#define JSON_H

#include "cJSON.h"

/*
 * Get a string from a JSON object by key. Returns NULL if missing or wrong type.
 */
const char *json_get_str(const cJSON *obj, const char *key);

/*
 * Get an int from a JSON object by key. Returns default_val if missing.
 */
int json_get_int(const cJSON *obj, const char *key, int default_val);

/*
 * Get a double from a JSON object by key. Returns default_val if missing.
 */
double json_get_double(const cJSON *obj, const char *key, double default_val);

/*
 * Get a bool from a JSON object by key. Returns default_val if missing.
 */
bool json_get_bool(const cJSON *obj, const char *key, bool default_val);

/*
 * Read an entire file into a malloc'd string. Returns NULL on error.
 * Caller must free().
 */
char *json_read_file(const char *path);

/*
 * Parse a JSON file. Returns NULL on error.
 * Caller must cJSON_Delete().
 */
cJSON *json_parse_file(const char *path);

/*
 * Build an Anthropic messages API request body.
 */
cJSON *json_build_anthropic_request(const char *model, const char *system_prompt,
                                     const char *user_message, int max_tokens,
                                     double temperature);

/*
 * Build an OpenAI chat completions API request body.
 */
cJSON *json_build_openai_request(const char *model, const char *system_prompt,
                                  const char *user_message, int max_tokens,
                                  double temperature);

#endif /* JSON_H */
