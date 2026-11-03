/*
 * libFuzzer over the reader's one entry point, and over the document it feeds. A consumer
 * may accept any descriptor an archive names, so the descriptor is fuzzed too: the first two
 * bytes pick an entry of the linked descriptor table, the third says whether a fallback start
 * and a uuid are passed, and the rest is the dive's buffer.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <libdivecomputer/descriptor.h>
#include <libdivecomputer/iterator.h>

#include "divejson.h"
#include "divejson_libdivecomputer.h"

typedef struct entry {
	char *vendor, *product;
	unsigned int model;
} entry;

static entry *table;
static size_t table_count;

static char *copy(const char *text)
{
	size_t length = strlen(text) + 1;
	char *out = malloc(length);
	if (out != NULL)
		memcpy(out, text, length);
	return out;
}

static void load_table(void)
{
	dc_iterator_t *iterator = NULL;
	dc_descriptor_t *descriptor = NULL;
	if (dc_descriptor_iterator_new(&iterator, NULL) != DC_STATUS_SUCCESS)
		abort();
	while (dc_iterator_next(iterator, &descriptor) == DC_STATUS_SUCCESS) {
		entry *grown = realloc(table, (table_count + 1) * sizeof *grown);
		if (grown == NULL)
			abort();
		table = grown;
		table[table_count++] = (entry) {copy(dc_descriptor_get_vendor(descriptor)),
		                                copy(dc_descriptor_get_product(descriptor)),
		                                dc_descriptor_get_model(descriptor)};
		dc_descriptor_free(descriptor);
	}
	dc_iterator_free(iterator);
	if (table_count == 0)
		abort();
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	if (table == NULL)
		load_table();
	if (size < 3)
		return 0;
	const entry *chosen = &table[((size_t) data[0] << 8 | data[1]) % table_count];
	uint8_t flags = data[2];

	divejson_libdivecomputer_input input = {
		data + 3, size - 3, chosen->vendor, chosen->product, chosen->model,
		flags & 1 ? "2026-10-02T08:36:00+00:00" : NULL,
		flags & 2 ? "2A7DCB1E-8B2B-4E6F-9C4E-0D6A5B0B8E11" : NULL,
	};
	char error[256];
	divejson_dive *dive = divejson_libdivecomputer_read(&input, error, sizeof error);
	if (dive != NULL) {
		divejson_document_options options = {"fuzz", NULL, "2026-11-03T00:00:00+00:00"};
		divejson_free(divejson_document(dive, &options, error, sizeof error));
		divejson_dive_free(dive);
	}
	return 0;
}
