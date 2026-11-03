/* Digests and identities: SHA-1 for UUIDv5, SHA-256 for naming a buffer. Internal. */
#ifndef DIVEJSON_IDENTITY_H
#define DIVEJSON_IDENTITY_H

#include <stddef.h>

void identity_sha1(const unsigned char *data, size_t size, unsigned char digest[20]);
void identity_sha256(const unsigned char *data, size_t size, unsigned char digest[32]);

/* `digest` as lowercase hex into `out`, which holds 2 * size + 1 bytes. */
void identity_hex(const unsigned char *digest, size_t size, char *out);

/* RFC 9562's version 5: SHA-1 over the namespace's bytes and `name`, as 36 characters. */
int identity_uuid5(const unsigned char namespace_[16], const char *name, char out[37]);

/* Whether `text` is a UUID in either case; writes it lowercased into `out`. */
int identity_uuid_parse(const char *text, char out[37]);

#endif
