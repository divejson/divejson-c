/*
 * The libdivecomputer reader: one dive's bytes, as libdivecomputer's parser reads them,
 * into a DiveJSON dive. docs/libdivecomputer-mapping.md is the field and sample map.
 *
 * Built only with the DIVEJSON_LIBDIVECOMPUTER option on. The library compiles against
 * libdivecomputer's headers and links none of it: the host supplies the dc_* symbols, so a
 * binary that already holds a libdivecomputer for its own downloads holds exactly one. This
 * header names no libdivecomputer type.
 */
#ifndef DIVEJSON_LIBDIVECOMPUTER_H
#define DIVEJSON_LIBDIVECOMPUTER_H

#include <stddef.h>

#include "divejson.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The format id: what a document's `extensions.divejson.converted_from` says. */
#define DIVEJSON_LIBDIVECOMPUTER_FORMAT "libdivecomputer"

typedef struct divejson_libdivecomputer_input {
	/* One dive's buffer, exactly as libdivecomputer's download handed it over. */
	const unsigned char *data;
	size_t size;

	/*
	 * The descriptor: the vendor and product strings and the model number, as
	 * dc_descriptor_get_vendor, dc_descriptor_get_product and dc_descriptor_get_model
	 * answer them. Vendor and product are matched exactly against the descriptor table of
	 * the libdivecomputer that is linked; the model decides between entries that share
	 * both. Never a family number, which is numbered per build.
	 */
	const char *vendor;
	const char *product;
	unsigned int model;

	/*
	 * The start to use when the parser reports none, as an RFC 3339 date-time with or
	 * without an offset; NULL for none. A dive with no start of its own and no fallback is
	 * refused.
	 */
	const char *fallback_start;

	/* The dive's uuid, lowercase or not; NULL to derive one from the buffer. */
	const char *uuid;
} divejson_libdivecomputer_input;

/*
 * Reads one dive. NULL on failure, with the reason in `error`: no descriptor, or more than
 * one, matching the input's; a buffer the parser refuses; no start from the parser or the
 * fallback; an argument that is not what this header says.
 */
divejson_dive *divejson_libdivecomputer_read(const divejson_libdivecomputer_input *input,
                                             char *error, size_t error_size);

#ifdef __cplusplus
}
#endif

#endif /* DIVEJSON_LIBDIVECOMPUTER_H */
