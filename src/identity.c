#include "identity.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t rol(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }
static uint32_t ror(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

/* Both digests pad the same way: 0x80, zeros, and the bit length big-endian. */
typedef void (*block_fn)(uint32_t *state, const unsigned char *block);

static void digest(const unsigned char *data, size_t size, uint32_t *state, block_fn block)
{
	size_t whole = size - size % 64;
	for (size_t offset = 0; offset < whole; offset += 64)
		block(state, data + offset);

	unsigned char tail[128] = {0};
	size_t rest = size - whole;
	if (rest)
		memcpy(tail, data + whole, rest);
	tail[rest] = 0x80;
	size_t length = rest + 1 + 8 <= 64 ? 64 : 128;
	uint64_t bits = (uint64_t) size * 8;
	for (int i = 0; i < 8; i++)
		tail[length - 1 - i] = (unsigned char) (bits >> (8 * i));
	for (size_t offset = 0; offset < length; offset += 64)
		block(state, tail + offset);
}

static uint32_t load(const unsigned char *p)
{
	return (uint32_t) p[0] << 24 | (uint32_t) p[1] << 16 | (uint32_t) p[2] << 8 | p[3];
}

static void sha1_block(uint32_t *h, const unsigned char *block)
{
	uint32_t w[80];
	for (int i = 0; i < 16; i++)
		w[i] = load(block + 4 * i);
	for (int i = 16; i < 80; i++)
		w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
	uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
	for (int i = 0; i < 80; i++) {
		uint32_t f, k;
		if (i < 20) {
			f = (b & c) | (~b & d);
			k = 0x5a827999;
		} else if (i < 40) {
			f = b ^ c ^ d;
			k = 0x6ed9eba1;
		} else if (i < 60) {
			f = (b & c) | (b & d) | (c & d);
			k = 0x8f1bbcdc;
		} else {
			f = b ^ c ^ d;
			k = 0xca62c1d6;
		}
		uint32_t t = rol(a, 5) + f + e + k + w[i];
		e = d;
		d = c;
		c = rol(b, 30);
		b = a;
		a = t;
	}
	h[0] += a;
	h[1] += b;
	h[2] += c;
	h[3] += d;
	h[4] += e;
}

static const uint32_t K256[64] = {
	0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
	0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
	0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
	0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
	0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
	0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
	0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
	0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

static void sha256_block(uint32_t *h, const unsigned char *block)
{
	uint32_t w[64];
	for (int i = 0; i < 16; i++)
		w[i] = load(block + 4 * i);
	for (int i = 16; i < 64; i++) {
		uint32_t s0 = ror(w[i - 15], 7) ^ ror(w[i - 15], 18) ^ (w[i - 15] >> 3);
		uint32_t s1 = ror(w[i - 2], 17) ^ ror(w[i - 2], 19) ^ (w[i - 2] >> 10);
		w[i] = w[i - 16] + s0 + w[i - 7] + s1;
	}
	uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], k = h[7];
	for (int i = 0; i < 64; i++) {
		uint32_t t1 = k + (ror(e, 6) ^ ror(e, 11) ^ ror(e, 25)) + ((e & f) ^ (~e & g)) + K256[i] + w[i];
		uint32_t t2 = (ror(a, 2) ^ ror(a, 13) ^ ror(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
		k = g;
		g = f;
		f = e;
		e = d + t1;
		d = c;
		c = b;
		b = a;
		a = t1 + t2;
	}
	h[0] += a;
	h[1] += b;
	h[2] += c;
	h[3] += d;
	h[4] += e;
	h[5] += f;
	h[6] += g;
	h[7] += k;
}

static void store(const uint32_t *state, int words, unsigned char *out)
{
	for (int i = 0; i < words; i++)
		for (int j = 0; j < 4; j++)
			out[4 * i + j] = (unsigned char) (state[i] >> (24 - 8 * j));
}

void identity_sha1(const unsigned char *data, size_t size, unsigned char out[20])
{
	uint32_t state[5] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476, 0xc3d2e1f0};
	digest(data, size, state, sha1_block);
	store(state, 5, out);
}

void identity_sha256(const unsigned char *data, size_t size, unsigned char out[32])
{
	uint32_t state[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
	                     0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
	digest(data, size, state, sha256_block);
	store(state, 8, out);
}

void identity_hex(const unsigned char *digest_, size_t size, char *out)
{
	static const char hex[] = "0123456789abcdef";
	for (size_t i = 0; i < size; i++) {
		out[2 * i] = hex[digest_[i] >> 4];
		out[2 * i + 1] = hex[digest_[i] & 15];
	}
	out[2 * size] = '\0';
}

static void format_uuid(const unsigned char bytes[16], char out[37])
{
	char hex[33];
	identity_hex(bytes, 16, hex);
	snprintf(out, 37, "%.8s-%.4s-%.4s-%.4s-%.12s", hex, hex + 8, hex + 12, hex + 16, hex + 20);
}

int identity_uuid5(const unsigned char namespace_[16], const char *name, char out[37])
{
	size_t length = strlen(name);
	unsigned char *input = malloc(16 + length);
	if (input == NULL)
		return 0;
	memcpy(input, namespace_, 16);
	memcpy(input + 16, name, length);
	unsigned char hash[20];
	identity_sha1(input, 16 + length, hash);
	free(input);
	hash[6] = (unsigned char) ((hash[6] & 0x0f) | 0x50);
	hash[8] = (unsigned char) ((hash[8] & 0x3f) | 0x80);
	format_uuid(hash, out);
	return 1;
}

int identity_uuid_parse(const char *text, char out[37])
{
	if (strlen(text) != 36)
		return 0;
	for (int i = 0; i < 36; i++) {
		char c = text[i];
		if (i == 8 || i == 13 || i == 18 || i == 23) {
			if (c != '-')
				return 0;
		} else if (c >= 'A' && c <= 'F') {
			c = (char) (c - 'A' + 'a');
		} else if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
			return 0;
		}
		out[i] = c;
	}
	out[36] = '\0';
	return 1;
}
