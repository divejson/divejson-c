/* The libdivecomputer reader, through its one call. The documents are test_cli.py's. */
#include <stdlib.h>

#include "check.h"
#include "divejson.h"
#include "divejson_libdivecomputer.h"

/*
 * A Suunto Solution log: three header bytes, depth deltas in feet one sample per three
 * minutes, the 0x80 end marker and the minutes after the last sample. Its parser reports no
 * start, so this is the dive the fallback exists for.
 */
static const unsigned char SOLUTION[] = {0x00, 0x00, 0x00, 0x10, 0x10, 0xF0, 0xF0, 0x80, 0x02};

static divejson_dive *read_solution(const unsigned char *data, size_t size, const char *fallback,
                                    const char *uuid, char *error)
{
	divejson_libdivecomputer_input input = {data, size, "Suunto", "Solution", 0, fallback, uuid};
	return divejson_libdivecomputer_read(&input, error, 256);
}

static void test_fallback_start(void)
{
	char error[256] = "";
	CHECK(read_solution(SOLUTION, sizeof SOLUTION, NULL, NULL, error) == NULL);
	CHECK_CONTAINS(error, "no start");

	divejson_dive *dive = read_solution(SOLUTION, sizeof SOLUTION, "2026-10-02T11:36:00+03:00", NULL, error);
	CHECK(dive != NULL);
	if (dive == NULL)
		return;
	const char *json = divejson_dive_json(dive);
	CHECK_CONTAINS(json, "\"started_at\":\"2026-10-02T11:36:00+03:00\"");
	/* 4 samples * 3 minutes + 2, and 32 ft at the deepest. */
	CHECK_CONTAINS(json, "\"submerged_time\":840");
	CHECK_CONTAINS(json, "\"max_depth\":9.75");
	CHECK_CONTAINS(json, "\"depth\":{\"times\":[180000,360000,540000,720000],\"values\":[488,975,488,0]}");
	CHECK_CONTAINS(json, "\"events\":[{\"time\":180000,\"type\":\"gas_switch\",\"gas_number\":0}]");
	CHECK(strstr(json, "\"device\"") == NULL);
	CHECK(strstr(json, "source_files") == NULL);
	divejson_dive_free(dive);

	/* A wall clock with no offset is a fallback too. */
	dive = read_solution(SOLUTION, sizeof SOLUTION, "2026-10-02T11:36:00", NULL, error);
	CHECK(dive != NULL);
	if (dive != NULL)
		CHECK_CONTAINS(divejson_dive_json(dive), "\"started_at\":\"2026-10-02T11:36:00\"");
	divejson_dive_free(dive);

	/* A fallback is written as given, however long its fraction. */
	const char *precise = "2026-10-02T08:36:00.12345678901234567890+03:00";
	dive = read_solution(SOLUTION, sizeof SOLUTION, precise, NULL, error);
	CHECK(dive != NULL);
	if (dive != NULL)
		CHECK_CONTAINS(divejson_dive_json(dive), "\"started_at\":\"2026-10-02T08:36:00.12345678901234567890+03:00\"");
	divejson_dive_free(dive);

	CHECK(read_solution(SOLUTION, sizeof SOLUTION, "2026-10-02 11:36", NULL, error) == NULL);
	CHECK_CONTAINS(error, "fallback start");
}

static void test_uuid(void)
{
	char error[256] = "";
	divejson_dive *first = read_solution(SOLUTION, sizeof SOLUTION, "2026-10-02T11:36:00Z", NULL, error);
	divejson_dive *again = read_solution(SOLUTION, sizeof SOLUTION, "2026-10-02T11:36:00Z", NULL, error);
	unsigned char other[sizeof SOLUTION];
	memcpy(other, SOLUTION, sizeof other);
	other[4] = 0x11;
	divejson_dive *changed = read_solution(other, sizeof other, "2026-10-02T11:36:00Z", NULL, error);
	divejson_dive *given = read_solution(SOLUTION, sizeof SOLUTION, "2026-10-02T11:36:00Z",
	                                     "2A7DCB1E-8B2B-4E6F-9C4E-0D6A5B0B8E11", error);
	CHECK(first && again && changed && given);
	if (first && again && changed && given) {
		/* uuid5 over the namespace and "dive:" + the buffer's SHA-256. */
		CHECK_CONTAINS(divejson_dive_json(first), "\"uuid\":\"9bf8ee6a-69a0-5209-a602-7fc629396ee8\"");
		CHECK_STR(divejson_dive_json(first), divejson_dive_json(again));
		CHECK(strncmp(divejson_dive_json(first), divejson_dive_json(changed), 50) != 0);
		CHECK_CONTAINS(divejson_dive_json(given), "\"uuid\":\"2a7dcb1e-8b2b-4e6f-9c4e-0d6a5b0b8e11\"");
	}
	divejson_dive_free(first);
	divejson_dive_free(again);
	divejson_dive_free(changed);
	divejson_dive_free(given);

	CHECK(read_solution(SOLUTION, sizeof SOLUTION, "2026-10-02T11:36:00Z", "not-a-uuid", error) == NULL);
	CHECK_CONTAINS(error, "not a UUID");
}

static void test_descriptor(void)
{
	char error[256] = "";
	divejson_libdivecomputer_input input = {SOLUTION, sizeof SOLUTION, "Suunto", "Solutions", 0, NULL, NULL};
	CHECK(divejson_libdivecomputer_read(&input, error, sizeof error) == NULL);
	CHECK_CONTAINS(error, "\"Suunto\" \"Solutions\"");

	/* Matching is exact: a family's vendor spelled another way is no match. */
	input.vendor = "suunto";
	input.product = "Solution";
	CHECK(divejson_libdivecomputer_read(&input, error, sizeof error) == NULL);
	CHECK_CONTAINS(error, "knows no dive computer");

	/* Three OC1s share vendor and product; the model settles which, and nothing else does. */
	input.vendor = "Oceanic";
	input.product = "OC1";
	input.model = 0;
	CHECK(divejson_libdivecomputer_read(&input, error, sizeof error) == NULL);
	CHECK_CONTAINS(error, "knows 3 dive computers \"Oceanic\" \"OC1\", and model 0 settles none of them");
	input.model = 0x4449;
	/* The model chose the parser, which finds no start in a Solution's bytes. */
	CHECK(divejson_libdivecomputer_read(&input, error, sizeof error) == NULL);
	CHECK_CONTAINS(error, "no start");

	input.vendor = NULL;
	CHECK(divejson_libdivecomputer_read(&input, error, sizeof error) == NULL);
	CHECK(divejson_libdivecomputer_read(NULL, error, sizeof error) == NULL);
	input.vendor = "Suunto";
	input.product = "Solution";
	input.size = 0;
	CHECK(divejson_libdivecomputer_read(&input, error, sizeof error) == NULL);
	CHECK_CONTAINS(error, "empty");
}

static unsigned char *load(const char *name, size_t *size)
{
	char path[1024];
	snprintf(path, sizeof path, "%s/%s", DIVEJSON_FIXTURES, name);
	FILE *file = fopen(path, "rb");
	if (file == NULL)
		return NULL;
	fseek(file, 0, SEEK_END);
	long length = ftell(file);
	fseek(file, 0, SEEK_SET);
	unsigned char *data = length > 0 ? malloc((size_t) length) : NULL;
	if (data != NULL && fread(data, 1, (size_t) length, file) != (size_t) length) {
		free(data);
		data = NULL;
	}
	fclose(file);
	*size = (size_t) length;
	return data;
}

static void test_fixture(const char *name, const char *vendor, const char *product, unsigned int model,
                         const char *started_at)
{
	size_t size = 0;
	unsigned char *data = load(name, &size);
	CHECK(data != NULL);
	if (data == NULL)
		return;
	char error[256] = "";
	divejson_libdivecomputer_input input = {data, size, vendor, product, model, NULL, NULL};
	divejson_dive *dive = divejson_libdivecomputer_read(&input, error, sizeof error);
	CHECK(dive != NULL);
	if (dive != NULL) {
		CHECK_CONTAINS(divejson_dive_json(dive), started_at);
		/* The fallback is used only where the parser reports no start. */
		input.fallback_start = "2000-01-01T00:00:00Z";
		divejson_dive *fallback = divejson_libdivecomputer_read(&input, error, sizeof error);
		CHECK(fallback != NULL);
		if (fallback != NULL)
			CHECK_STR(divejson_dive_json(fallback), divejson_dive_json(dive));
		divejson_dive_free(fallback);
		CHECK(divejson_dive_inferred_count(dive) == 0);
	} else {
		fprintf(stderr, "%s: %s\n", name, error);
	}
	divejson_dive_free(dive);

	/* Every prefix of a real dive is malformed input, and none of them crashes the read. */
	for (size_t length = 1; length < size; length += length < 512 ? 1 : 65521) {
		input.size = length;
		divejson_dive_free(divejson_libdivecomputer_read(&input, error, sizeof error));
	}
	free(data);
}

int main(void)
{
	test_fallback_start();
	test_uuid();
	test_descriptor();
	/* A Shearwater logs a wall clock with no zone; a Suunto Ocean, an instant in UTC. */
	test_fixture("shearwater-perdix-3_D9772626_2026-10-26T1503.bin", "Shearwater", "Perdix 3", 14,
	             "\"started_at\":\"2026-10-26T15:03:23\"");
	test_fixture("suunto-ocean_253810000400_2026-10-26T1501.bin", "Suunto", "Ocean", 1,
	             "\"started_at\":\"2026-10-26T12:01:49+00:00\"");
	return check_report("libdivecomputer");
}
