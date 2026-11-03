#include "dive.h"
#include "identity.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *divejson_version(void) { return DIVEJSON_VERSION; }

void divejson_free(void *pointer) { free(pointer); }

void dive_error(char *error, size_t error_size, const char *format, ...)
{
	if (error == NULL || error_size == 0)
		return;
	va_list arguments;
	va_start(arguments, format);
	vsnprintf(error, error_size, format, arguments);
	va_end(arguments);
}

divejson_dive *dive_new(json_value *value, json_value *provenance)
{
	divejson_dive *dive = calloc(1, sizeof *dive);
	if (dive != NULL)
		dive->json = json_write(value, 0);
	if (dive == NULL || dive->json == NULL) {
		free(dive);
		json_free(value);
		json_free(provenance);
		return NULL;
	}
	dive->dive = value;
	dive->provenance = provenance;
	return dive;
}

int dive_infer(divejson_dive *dive, const char *path)
{
	char *copy = json_strdup(path);
	char **grown = realloc(dive->inferred, (dive->inferred_count + 1) * sizeof *grown);
	if (copy == NULL || grown == NULL) {
		free(copy);
		if (grown != NULL)
			dive->inferred = grown;
		return 0;
	}
	dive->inferred = grown;
	dive->inferred[dive->inferred_count++] = copy;
	return 1;
}

const char *divejson_dive_json(const divejson_dive *dive) { return dive->json; }

size_t divejson_dive_inferred_count(const divejson_dive *dive) { return dive->inferred_count; }

const char *divejson_dive_inferred(const divejson_dive *dive, size_t index)
{
	return index < dive->inferred_count ? dive->inferred[index] : NULL;
}

void divejson_dive_free(divejson_dive *dive)
{
	if (dive == NULL)
		return;
	json_free(dive->dive);
	json_free(dive->provenance);
	for (size_t i = 0; i < dive->inferred_count; i++)
		free(dive->inferred[i]);
	free(dive->inferred);
	free(dive->json);
	free(dive);
}

/* What a recording must carry at least one of to be one: a device's record of a dive. */
static const char *const RECORDING_CONTENT[] = {
	"device", "profile", "source_files", "surface_pressure",
	"cns_start", "cns_end", "otu_start", "otu_end",
};

static int recording_has_content(const json_value *recording)
{
	for (size_t i = 0; i < sizeof RECORDING_CONTENT / sizeof *RECORDING_CONTENT; i++)
		if (json_get(recording, RECORDING_CONTENT[i]) != NULL)
			return 1;
	return 0;
}

char *divejson_document(const divejson_dive *dive, const divejson_document_options *options,
                        char *error, size_t error_size)
{
	if (dive == NULL || options == NULL || options->generator_name == NULL ||
	    options->generator_name[0] == '\0') {
		dive_error(error, error_size, "a document needs a dive and a generator name");
		return NULL;
	}
	if (options->exported_at == NULL ||
	    !divejson_date_time_valid(options->exported_at, 1)) {
		dive_error(error, error_size,
		           "the export time must be a date-time with its offset, such as 2026-10-26T12:00:00+00:00");
		return NULL;
	}

	int failed = 0;
	json_value *recordings = json_get(dive->dive, "recordings");
	int keep = 0;
	for (size_t i = 0; i < json_count(recordings); i++)
		keep |= recording_has_content(recordings->as.array.items[i]);

	json_value *dives = json_array(&failed);
	json_append(dives, json_clone(dive->dive, keep ? NULL : "recordings", &failed), &failed);

	json_value *divejson = json_clone(dive->provenance, NULL, &failed);
	if (dive->inferred_count > 0) {
		json_value *inferred = json_array(&failed);
		for (size_t i = 0; i < dive->inferred_count; i++) {
			size_t length = strlen(dive->inferred[i]) + sizeof "dives/0/";
			char *path = malloc(length);
			if (path == NULL) {
				failed = 1;
				break;
			}
			snprintf(path, length, "dives/0/%s", dive->inferred[i]);
			json_append(inferred, json_string(path, &failed), &failed);
			free(path);
		}
		json_set(divejson, "inferred", inferred, &failed);
	}
	json_value *extensions = json_object(&failed);
	json_set(extensions, "divejson", divejson, &failed);

	json_value *generator = json_object(&failed);
	json_set(generator, "name", json_string(options->generator_name, &failed), &failed);
	if (options->generator_version != NULL)
		json_set(generator, "version", json_string(options->generator_version, &failed), &failed);

	json_value *document = json_object(&failed);
	json_set(document, "format", json_string("divejson", &failed), &failed);
	json_set(document, "version", json_string(DIVEJSON_FORMAT_VERSION, &failed), &failed);
	json_set(document, "exported_at", json_string(options->exported_at, &failed), &failed);
	json_set(document, "generator", generator, &failed);
	json_set(document, "dives", dives, &failed);
	json_set(document, "extensions", extensions, &failed);

	char *text = failed ? NULL : json_write(document, 2);
	json_free(document);
	if (text == NULL)
		dive_error(error, error_size, "out of memory writing the document");
	return text;
}

static int digits(const char *text, int count, int *out)
{
	int value = 0;
	for (int i = 0; i < count; i++) {
		if (text[i] < '0' || text[i] > '9')
			return 0;
		value = value * 10 + (text[i] - '0');
	}
	*out = value;
	return 1;
}

int divejson_date_time_valid(const char *text, int offset_required)
{
	if (text == NULL)
		return 0;
	int year, month, day, hour, minute, second;
	if (strlen(text) < 19 || !digits(text, 4, &year) || text[4] != '-' ||
	    !digits(text + 5, 2, &month) || text[7] != '-' || !digits(text + 8, 2, &day) ||
	    text[10] != 'T' || !digits(text + 11, 2, &hour) || text[13] != ':' ||
	    !digits(text + 14, 2, &minute) || text[16] != ':' || !digits(text + 17, 2, &second))
		return 0;
	static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
	int leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
	if (year < 1 || month < 1 || month > 12 || day < 1 ||
	    day > days[month - 1] + (month == 2 && leap) || hour > 23 || minute > 59 || second > 59)
		return 0;
	const char *rest = text + 19;
	if (*rest == '.') {
		rest++;
		if (*rest < '0' || *rest > '9')
			return 0;
		while (*rest >= '0' && *rest <= '9')
			rest++;
	}
	if (*rest == '\0')
		return !offset_required;
	if ((*rest == 'Z' || *rest == 'z') && rest[1] == '\0')
		return 1;
	int offset_hours, offset_minutes;
	return (*rest == '+' || *rest == '-') && digits(rest + 1, 2, &offset_hours) && rest[3] == ':' &&
	       digits(rest + 4, 2, &offset_minutes) && rest[6] == '\0' && offset_hours <= 23 &&
	       offset_minutes <= 59;
}

int divejson_uuid_valid(const char *text)
{
	char lowered[37];
	return text != NULL && identity_uuid_parse(text, lowered);
}
