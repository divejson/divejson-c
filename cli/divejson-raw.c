/*
 * divejson-raw: one dive's libdivecomputer buffer in, one DiveJSON document out. README.md
 * documents the arguments and the exit statuses; they are a contract, so change them there
 * first.
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "divejson.h"
#include "divejson_libdivecomputer.h"

#ifndef DIVEJSON_LIBDIVECOMPUTER_COMMIT
#error "the build names the vendored libdivecomputer commit"
#endif

/* The library's version and the vendored libdivecomputer commit: either moving moves it. */
#define VERSION DIVEJSON_VERSION "+" DIVEJSON_LIBDIVECOMPUTER_COMMIT

enum { EXIT_READ = 1, EXIT_USAGE = 2 };

static const char USAGE[] =
	"usage: divejson-raw --vendor VENDOR --product PRODUCT --model MODEL\n"
	"                    [--fallback-start DATE-TIME] [--uuid UUID]\n"
	"                    [--exported-at DATE-TIME] [--output FILE] FILE\n"
	"       divejson-raw --version\n"
	"\n"
	"Reads one dive's libdivecomputer buffer from FILE, or from standard input for '-',\n"
	"and writes it as a DiveJSON document to standard output or to --output.\n";

static int usage_error(const char *message, const char *detail)
{
	fprintf(stderr, "divejson-raw: %s%s\n\n%s", message, detail ? detail : "", USAGE);
	return EXIT_USAGE;
}

static unsigned char *read_all(FILE *file, size_t *size)
{
	size_t capacity = 1 << 16, length = 0;
	unsigned char *data = malloc(capacity);
	while (data != NULL) {
		length += fread(data + length, 1, capacity - length, file);
		if (length < capacity)
			break;
		unsigned char *grown = capacity > ((size_t) -1) / 2 ? NULL : realloc(data, capacity * 2);
		if (grown == NULL) {
			free(data);
			return NULL;
		}
		data = grown;
		capacity *= 2;
	}
	if (data != NULL && ferror(file)) {
		free(data);
		return NULL;
	}
	*size = length;
	return data;
}

static int parse_model(const char *text, unsigned int *model)
{
	if (text[0] < '0' || text[0] > '9')
		return 0;
	errno = 0;
	char *end = NULL;
	unsigned long value = strtoul(text, &end, 10);
	if (errno != 0 || *end != '\0' || value > 0xFFFFFFFFul)
		return 0;
	*model = (unsigned int) value;
	return 1;
}

int main(int argc, char **argv)
{
	divejson_libdivecomputer_input input = {0};
	const char *model = NULL, *exported_at = NULL, *output = NULL, *path = NULL;

	for (int i = 1; i < argc; i++) {
		const char *argument = argv[i];
		if (strcmp(argument, "--version") == 0) {
			puts(VERSION);
			return 0;
		}
		if (strcmp(argument, "--help") == 0 || strcmp(argument, "-h") == 0) {
			fputs(USAGE, stdout);
			return 0;
		}
		if (strncmp(argument, "--", 2) != 0 || strcmp(argument, "--") == 0) {
			if (strcmp(argument, "--") == 0) {
				if (i + 2 != argc)
					return usage_error("expected one FILE after --", NULL);
				argument = argv[++i];
			}
			if (path != NULL)
				return usage_error("more than one FILE: ", argument);
			path = argument;
			continue;
		}

		static const char *const OPTIONS[] = {"--vendor", "--product", "--model", "--fallback-start",
		                                      "--uuid", "--exported-at", "--output"};
		const char **targets[] = {&input.vendor, &input.product, &model, &input.fallback_start,
		                          &input.uuid, &exported_at, &output};
		const char *value = NULL;
		size_t option = 0;
		for (; option < sizeof OPTIONS / sizeof *OPTIONS; option++) {
			size_t length = strlen(OPTIONS[option]);
			if (strncmp(argument, OPTIONS[option], length) != 0)
				continue;
			if (argument[length] == '=') {
				value = argument + length + 1;
				break;
			}
			if (argument[length] == '\0') {
				if (i + 1 >= argc)
					return usage_error("missing a value for ", argument);
				value = argv[++i];
				break;
			}
		}
		if (option == sizeof OPTIONS / sizeof *OPTIONS)
			return usage_error("unknown option ", argument);
		if (*targets[option] != NULL)
			return usage_error("given twice: ", OPTIONS[option]);
		*targets[option] = value;
	}

	if (input.vendor == NULL || input.product == NULL || model == NULL)
		return usage_error("--vendor, --product and --model are required", NULL);
	if (!parse_model(model, &input.model))
		return usage_error("--model is not a whole number: ", model);
	if (path == NULL)
		return usage_error("expected a FILE, or - for standard input", NULL);
	if (input.fallback_start != NULL && !divejson_date_time_valid(input.fallback_start, 0))
		return usage_error("--fallback-start is not a date-time: ", input.fallback_start);
	if (exported_at != NULL && !divejson_date_time_valid(exported_at, 1))
		return usage_error("--exported-at is not a date-time with its offset: ", exported_at);
	if (input.uuid != NULL && !divejson_uuid_valid(input.uuid))
		return usage_error("--uuid is not a UUID: ", input.uuid);

	char now[32];
	if (exported_at == NULL) {
		time_t seconds = time(NULL);
		struct tm *utc = gmtime(&seconds);
		if (utc == NULL || strftime(now, sizeof now, "%Y-%m-%dT%H:%M:%S+00:00", utc) == 0) {
			fputs("divejson-raw: the system clock has no time to stamp the document with\n", stderr);
			return EXIT_READ;
		}
		exported_at = now;
	}

	FILE *file = strcmp(path, "-") == 0 ? stdin : fopen(path, "rb");
	if (file == NULL) {
		fprintf(stderr, "divejson-raw: cannot open %s: %s\n", path, strerror(errno));
		return EXIT_READ;
	}
	size_t size = 0;
	unsigned char *data = read_all(file, &size);
	if (file != stdin)
		fclose(file);
	if (data == NULL) {
		fprintf(stderr, "divejson-raw: cannot read %s\n", path);
		return EXIT_READ;
	}
	input.data = data;
	input.size = size;

	char error[512];
	divejson_dive *dive = divejson_libdivecomputer_read(&input, error, sizeof error);
	free(data);
	if (dive == NULL) {
		fprintf(stderr, "divejson-raw: %s\n", error);
		return EXIT_READ;
	}
	divejson_document_options options = {"divejson-raw", VERSION, exported_at};
	char *document = divejson_document(dive, &options, error, sizeof error);
	divejson_dive_free(dive);
	if (document == NULL) {
		fprintf(stderr, "divejson-raw: %s\n", error);
		return EXIT_READ;
	}

	FILE *out = output == NULL ? stdout : fopen(output, "wb");
	if (out == NULL) {
		fprintf(stderr, "divejson-raw: cannot write %s: %s\n", output, strerror(errno));
		divejson_free(document);
		return EXIT_READ;
	}
	size_t length = strlen(document);
	int written = fwrite(document, 1, length, out) == length;
	written &= (out == stdout ? fflush(out) : fclose(out)) == 0;
	divejson_free(document);
	if (!written) {
		fprintf(stderr, "divejson-raw: cannot write the document\n");
		return EXIT_READ;
	}
	return 0;
}
