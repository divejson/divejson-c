/* The tests' one assertion: it reports and counts, so a run shows every failure at once. */
#ifndef DIVEJSON_CHECK_H
#define DIVEJSON_CHECK_H

#include <stdio.h>
#include <string.h>

static int check_failures = 0;

#define CHECK(condition)                                                                    \
	do {                                                                                    \
		if (!(condition)) {                                                                 \
			fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #condition);   \
			check_failures++;                                                               \
		}                                                                                   \
	} while (0)

#define CHECK_STR(actual, expected)                                                          \
	do {                                                                                    \
		const char *check_a = (actual), *check_e = (expected);                              \
		if (check_a == NULL || strcmp(check_a, check_e) != 0) {                             \
			fprintf(stderr, "%s:%d: %s is \"%s\", expected \"%s\"\n", __FILE__, __LINE__,    \
			        #actual, check_a ? check_a : "(null)", check_e);                        \
			check_failures++;                                                               \
		}                                                                                   \
	} while (0)

#define CHECK_CONTAINS(haystack, needle)                                                     \
	do {                                                                                    \
		const char *check_h = (haystack), *check_n = (needle);                              \
		if (check_h == NULL || strstr(check_h, check_n) == NULL) {                          \
			fprintf(stderr, "%s:%d: %s does not contain \"%s\": %s\n", __FILE__, __LINE__,   \
			        #haystack, check_n, check_h ? check_h : "(null)");                      \
			check_failures++;                                                               \
		}                                                                                   \
	} while (0)

static int check_report(const char *suite)
{
	if (check_failures)
		fprintf(stderr, "%s: %d check(s) failed\n", suite, check_failures);
	else
		printf("%s: ok\n", suite);
	return check_failures ? 1 : 0;
}

#endif
