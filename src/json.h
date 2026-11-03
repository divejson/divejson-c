/* A JSON value tree, built in member order and written in it. Internal to the library. */
#ifndef DIVEJSON_JSON_H
#define DIVEJSON_JSON_H

#include <stddef.h>

typedef enum json_type {
	JSON_OBJECT,
	JSON_ARRAY,
	JSON_STRING,
	JSON_INTEGER,
	JSON_NUMBER,
} json_type;

typedef struct json_value json_value;

typedef struct json_member {
	char *key;
	json_value *value;
} json_member;

struct json_value {
	json_type type;
	union {
		long long integer;
		double number;
		char *string;
		struct {
			json_member *items;
			size_t count, capacity;
		} object;
		struct {
			json_value **items;
			size_t count, capacity;
		} array;
	} as;
};

/*
 * Constructors return NULL when memory runs out, and every function taking a child accepts
 * NULL and records the failure in `*failed` instead, so a builder checks once at the end.
 */
json_value *json_object(int *failed);
json_value *json_array(int *failed);
json_value *json_string(const char *text, int *failed);
json_value *json_integer(long long value, int *failed);
/* A finite number. Integral values within 1e15 are written with no fraction. */
json_value *json_number(double value, int *failed);

/* Appends `value` under `key`, taking ownership; frees `value` on failure. */
void json_set(json_value *object, const char *key, json_value *value, int *failed);
void json_append(json_value *array, json_value *value, int *failed);

json_value *json_get(const json_value *object, const char *key);
size_t json_count(const json_value *value);

void json_free(json_value *value);

/* strdup, which C11 does not declare. */
char *json_strdup(const char *text);

/* A deep copy, leaving out the object member `leave_out` where it is not NULL. */
json_value *json_clone(const json_value *value, const char *leave_out, int *failed);

/* The value as UTF-8 text: compact for indent 0, else one member per line. */
char *json_write(const json_value *value, int indent);

#endif
