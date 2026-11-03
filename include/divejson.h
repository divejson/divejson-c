/*
 * libdivejson: DiveJSON's C implementation.
 *
 * The core writes DiveJSON. A reader hands its host one dive (a `divejson_dive`), and
 * `divejson_document` wraps a dive in a document. Readers are declared in headers of their
 * own, each behind a build option: `divejson_libdivecomputer.h` is the first.
 *
 * Every function that can fail takes an `error` buffer of `error_size` bytes, writes one
 * sentence into it on failure, and returns NULL. Nothing in the library prints, exits or
 * reads the environment.
 */
#ifndef DIVEJSON_H
#define DIVEJSON_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The library's version, which a release tag repeats as `v<version>`. */
#define DIVEJSON_VERSION "0.1.0"

/* The DiveJSON version the library writes. */
#define DIVEJSON_FORMAT_VERSION "1.0"

/* `DIVEJSON_VERSION`, for a host that links the library rather than compiling the header. */
const char *divejson_version(void);

/* One dive a reader produced: a DiveJSON `dive` object with one recording. */
typedef struct divejson_dive divejson_dive;

/*
 * The dive object as compact UTF-8 JSON, owned by `dive` and valid until it is freed. It
 * carries the members its source records and nothing of its host's: no `device` and no
 * `source_files` on the recording, which a host adds where it has them.
 */
const char *divejson_dive_json(const divejson_dive *dive);

/*
 * The members the reader inferred rather than read, as paths relative to the dive object
 * (`submerged_time`, `recordings/0/profile`), in the shape a document lists them under
 * `extensions.divejson.inferred` once they are prefixed by the dive's place in it.
 */
size_t divejson_dive_inferred_count(const divejson_dive *dive);
const char *divejson_dive_inferred(const divejson_dive *dive, size_t index);

void divejson_dive_free(divejson_dive *dive);

typedef struct divejson_document_options {
	/* `generator.name`: the program writing the document. Required. */
	const char *generator_name;
	/* `generator.version`, or NULL to leave it out. */
	const char *generator_version;
	/* `exported_at`: an RFC 3339 date-time with its offset. Required. */
	const char *exported_at;
} divejson_document_options;

/*
 * A DiveJSON document holding exactly `dive`, as UTF-8 JSON indented by two spaces and
 * ending in a newline. Its `extensions.divejson` carries the reader's provenance and the
 * inferred list, re-rooted under `dives/0/`. A recording that carries none of a profile, a
 * device, stored files or a readout is left out, since a document cannot hold one. Free the
 * result with `divejson_free`.
 */
char *divejson_document(const divejson_dive *dive, const divejson_document_options *options,
                        char *error, size_t error_size);

void divejson_free(void *pointer);

/*
 * Whether `text` is a date-time as the format spells one: `YYYY-MM-DDTHH:MM:SS`, an optional
 * fraction, and an offset (`Z` or `±HH:MM`) that `offset_required` says may not be left out;
 * on a real day, at a real time.
 */
int divejson_date_time_valid(const char *text, int offset_required);

/* Whether `text` is a UUID, in either case. */
int divejson_uuid_valid(const char *text);

#ifdef __cplusplus
}
#endif

#endif /* DIVEJSON_H */
