#include "json.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *json_strdup(const char *text)
{
	size_t length = strlen(text) + 1;
	char *copy = malloc(length);
	if (copy != NULL)
		memcpy(copy, text, length);
	return copy;
}

static json_value *json_new(json_type type, int *failed)
{
	json_value *value = calloc(1, sizeof *value);
	if (value == NULL)
		*failed = 1;
	else
		value->type = type;
	return value;
}

json_value *json_object(int *failed) { return json_new(JSON_OBJECT, failed); }

json_value *json_array(int *failed) { return json_new(JSON_ARRAY, failed); }

json_value *json_string(const char *text, int *failed)
{
	json_value *value = json_new(JSON_STRING, failed);
	if (value == NULL)
		return NULL;
	value->as.string = json_strdup(text);
	if (value->as.string == NULL) {
		free(value);
		*failed = 1;
		return NULL;
	}
	return value;
}

json_value *json_integer(long long integer, int *failed)
{
	json_value *value = json_new(JSON_INTEGER, failed);
	if (value != NULL)
		value->as.integer = integer;
	return value;
}

json_value *json_number(double number, int *failed)
{
	if (!isfinite(number)) {
		*failed = 1;
		return NULL;
	}
	if (number == round(number) && fabs(number) < 1e15)
		return json_integer((long long) number, failed);
	json_value *value = json_new(JSON_NUMBER, failed);
	if (value != NULL)
		value->as.number = number;
	return value;
}

static int grow(void **items, size_t *capacity, size_t count, size_t size)
{
	if (count < *capacity)
		return 1;
	size_t next = *capacity ? *capacity * 2 : 8;
	void *grown = realloc(*items, next * size);
	if (grown == NULL)
		return 0;
	*items = grown;
	*capacity = next;
	return 1;
}

void json_set(json_value *object, const char *key, json_value *value, int *failed)
{
	if (object == NULL || value == NULL) {
		*failed = 1;
		json_free(value);
		return;
	}
	char *copy = json_strdup(key);
	if (copy == NULL || !grow((void **) &object->as.object.items, &object->as.object.capacity,
	                          object->as.object.count, sizeof(json_member))) {
		free(copy);
		json_free(value);
		*failed = 1;
		return;
	}
	object->as.object.items[object->as.object.count++] = (json_member) {copy, value};
}

void json_append(json_value *array, json_value *value, int *failed)
{
	if (array == NULL || value == NULL ||
	    !grow((void **) &array->as.array.items, &array->as.array.capacity,
	          array->as.array.count, sizeof(json_value *))) {
		*failed = 1;
		json_free(value);
		return;
	}
	array->as.array.items[array->as.array.count++] = value;
}

json_value *json_get(const json_value *object, const char *key)
{
	if (object == NULL || object->type != JSON_OBJECT)
		return NULL;
	for (size_t i = 0; i < object->as.object.count; i++)
		if (strcmp(object->as.object.items[i].key, key) == 0)
			return object->as.object.items[i].value;
	return NULL;
}

size_t json_count(const json_value *value)
{
	if (value == NULL)
		return 0;
	if (value->type == JSON_OBJECT)
		return value->as.object.count;
	if (value->type == JSON_ARRAY)
		return value->as.array.count;
	return 0;
}

json_value *json_clone(const json_value *value, const char *leave_out, int *failed)
{
	if (value == NULL) {
		*failed = 1;
		return NULL;
	}
	json_value *copy;
	switch (value->type) {
	case JSON_OBJECT:
		copy = json_object(failed);
		for (size_t i = 0; copy != NULL && i < value->as.object.count; i++) {
			const json_member *member = &value->as.object.items[i];
			if (leave_out == NULL || strcmp(member->key, leave_out) != 0)
				json_set(copy, member->key, json_clone(member->value, NULL, failed), failed);
		}
		return copy;
	case JSON_ARRAY:
		copy = json_array(failed);
		for (size_t i = 0; copy != NULL && i < value->as.array.count; i++)
			json_append(copy, json_clone(value->as.array.items[i], NULL, failed), failed);
		return copy;
	case JSON_STRING:
		return json_string(value->as.string, failed);
	case JSON_INTEGER:
		return json_integer(value->as.integer, failed);
	case JSON_NUMBER:
		copy = json_new(JSON_NUMBER, failed);
		if (copy != NULL)
			copy->as.number = value->as.number;
		return copy;
	}
	return NULL;
}

static void json_free_shell(json_value *object)
{
	if (object == NULL)
		return;
	for (size_t i = 0; i < object->as.object.count; i++)
		free(object->as.object.items[i].key);
	free(object->as.object.items);
	free(object);
}

void json_free(json_value *value)
{
	if (value == NULL)
		return;
	switch (value->type) {
	case JSON_OBJECT:
		for (size_t i = 0; i < value->as.object.count; i++)
			json_free(value->as.object.items[i].value);
		json_free_shell(value);
		return;
	case JSON_ARRAY:
		for (size_t i = 0; i < value->as.array.count; i++)
			json_free(value->as.array.items[i]);
		free(value->as.array.items);
		break;
	case JSON_STRING:
		free(value->as.string);
		break;
	default:
		break;
	}
	free(value);
}

typedef struct buffer {
	char *data;
	size_t length, capacity;
	int failed;
} buffer;

static void put(buffer *out, const char *text, size_t length)
{
	if (out->failed)
		return;
	if (out->length + length + 1 > out->capacity) {
		size_t next = out->capacity ? out->capacity : 4096;
		while (out->length + length + 1 > next)
			next *= 2;
		char *grown = realloc(out->data, next);
		if (grown == NULL) {
			out->failed = 1;
			return;
		}
		out->data = grown;
		out->capacity = next;
	}
	memcpy(out->data + out->length, text, length);
	out->length += length;
	out->data[out->length] = '\0';
}

static void puts_(buffer *out, const char *text) { put(out, text, strlen(text)); }

static void put_string(buffer *out, const char *text)
{
	puts_(out, "\"");
	for (const unsigned char *p = (const unsigned char *) text; *p; p++) {
		char escape[8];
		switch (*p) {
		case '"': puts_(out, "\\\""); break;
		case '\\': puts_(out, "\\\\"); break;
		case '\n': puts_(out, "\\n"); break;
		case '\r': puts_(out, "\\r"); break;
		case '\t': puts_(out, "\\t"); break;
		default:
			if (*p < 0x20) {
				snprintf(escape, sizeof escape, "\\u%04x", *p);
				puts_(out, escape);
			} else {
				put(out, (const char *) p, 1);
			}
		}
	}
	puts_(out, "\"");
}

/* The shortest spelling that reads back as the same double. */
static void put_number(buffer *out, double number)
{
	char text[32];
	for (int precision = 1; precision <= 17; precision++) {
		snprintf(text, sizeof text, "%.*g", precision, number);
		if (strtod(text, NULL) == number)
			break;
	}
	puts_(out, text);
}

static void newline(buffer *out, int indent, int depth)
{
	if (indent == 0)
		return;
	puts_(out, "\n");
	for (int i = 0; i < indent * depth; i++)
		puts_(out, " ");
}

static void write_value(buffer *out, const json_value *value, int indent, int depth)
{
	char text[32];
	switch (value->type) {
	case JSON_OBJECT:
		if (value->as.object.count == 0) {
			puts_(out, "{}");
			return;
		}
		puts_(out, "{");
		for (size_t i = 0; i < value->as.object.count; i++) {
			if (i > 0)
				puts_(out, ",");
			newline(out, indent, depth + 1);
			put_string(out, value->as.object.items[i].key);
			puts_(out, indent ? ": " : ":");
			write_value(out, value->as.object.items[i].value, indent, depth + 1);
		}
		newline(out, indent, depth);
		puts_(out, "}");
		return;
	case JSON_ARRAY:
		if (value->as.array.count == 0) {
			puts_(out, "[]");
			return;
		}
		puts_(out, "[");
		for (size_t i = 0; i < value->as.array.count; i++) {
			if (i > 0)
				puts_(out, ",");
			newline(out, indent, depth + 1);
			write_value(out, value->as.array.items[i], indent, depth + 1);
		}
		newline(out, indent, depth);
		puts_(out, "]");
		return;
	case JSON_STRING:
		put_string(out, value->as.string);
		return;
	case JSON_INTEGER:
		snprintf(text, sizeof text, "%lld", value->as.integer);
		puts_(out, text);
		return;
	case JSON_NUMBER:
		put_number(out, value->as.number);
		return;
	}
}

char *json_write(const json_value *value, int indent)
{
	buffer out = {0};
	write_value(&out, value, indent, 0);
	if (indent)
		puts_(&out, "\n");
	if (out.failed) {
		free(out.data);
		return NULL;
	}
	return out.data;
}
