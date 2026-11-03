/* What a reader builds a `divejson_dive` from. Internal to the library. */
#ifndef DIVEJSON_DIVE_H
#define DIVEJSON_DIVE_H

#include <stdarg.h>
#include <stddef.h>

#include "divejson.h"
#include "json.h"

struct divejson_dive {
	json_value *dive;
	/* The members of `extensions.divejson` that are the reader's: its format id first. */
	json_value *provenance;
	char **inferred;
	size_t inferred_count;
	char *json;
};

/* Takes ownership of both values, and frees them if it fails. */
divejson_dive *dive_new(json_value *dive, json_value *provenance);

/* Records `path`, relative to the dive, as inferred. Zero when memory runs out. */
int dive_infer(divejson_dive *dive, const char *path);

/* Writes the formatted sentence into `error`; for every function taking one. */
void dive_error(char *error, size_t error_size, const char *format, ...);


#endif
