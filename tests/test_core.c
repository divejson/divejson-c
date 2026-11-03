/* The core, with no reader and no libdivecomputer anywhere in the build. */
#include <math.h>
#include <stdlib.h>

#include "check.h"
#include "dive.h"
#include "divejson.h"
#include "identity.h"
#include "json.h"

static void test_version(void)
{
	CHECK_STR(divejson_version(), DIVEJSON_PROJECT_VERSION);
	CHECK_STR(DIVEJSON_VERSION, DIVEJSON_PROJECT_VERSION);
}

static char *written(json_value *value, int indent)
{
	char *text = json_write(value, indent);
	json_free(value);
	return text;
}

static void test_json_numbers(void)
{
	int failed = 0;
	json_value *array = json_array(&failed);
	json_append(array, json_number(21.0, &failed), &failed);
	json_append(array, json_number(47.7, &failed), &failed);
	json_append(array, json_number(100 - 32.1, &failed), &failed);
	json_append(array, json_number(1.0063, &failed), &failed);
	json_append(array, json_number(-0.5, &failed), &failed);
	json_append(array, json_number(34.5199533, &failed), &failed);
	json_append(array, json_integer(-12, &failed), &failed);
	CHECK(!failed);
	char *text = written(array, 0);
	/* Whole numbers print as integers; the rest as the shortest text that reads back. */
	CHECK_STR(text, "[21,47.7,67.9,1.0063,-0.5,34.5199533,-12]");
	free(text);

	failed = 0;
	CHECK(json_number(INFINITY, &failed) == NULL);
	CHECK(failed);
}

static void test_json_strings_and_layout(void)
{
	int failed = 0;
	json_value *object = json_object(&failed);
	json_set(object, "b", json_string("quote \" backslash \\ tab \t\x01", &failed), &failed);
	json_set(object, "a", json_array(&failed), &failed);
	json_set(object, "c", json_object(&failed), &failed);
	CHECK(!failed);
	char *text = json_write(object, 0);
	/* Members keep the order they were set in. */
	CHECK_STR(text, "{\"b\":\"quote \\\" backslash \\\\ tab \\t\\u0001\",\"a\":[],\"c\":{}}");
	free(text);
	text = written(object, 2);
	CHECK_STR(text, "{\n  \"b\": \"quote \\\" backslash \\\\ tab \\t\\u0001\",\n  \"a\": [],\n  \"c\": {}\n}\n");
	free(text);
}

static void test_digests(void)
{
	char hex[65];
	unsigned char digest[32];
	identity_sha256((const unsigned char *) "", 0, digest);
	identity_hex(digest, 32, hex);
	CHECK_STR(hex, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
	const char *long_ = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
	identity_sha256((const unsigned char *) long_, strlen(long_), digest);
	identity_hex(digest, 32, hex);
	CHECK_STR(hex, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
	identity_sha1((const unsigned char *) "abc", 3, digest);
	identity_hex(digest, 20, hex);
	CHECK_STR(hex, "a9993e364706816aba3e25717850c26c9cd0d89d");
	/* A message that leaves too little of its last block for the length. */
	unsigned char block[56];
	memset(block, 'a', sizeof block);
	identity_sha1(block, sizeof block, digest);
	identity_hex(digest, 20, hex);
	CHECK_STR(hex, "c2db330f6083854c99d4b5bfb6e8f29f201be699");
}

static void test_uuid(void)
{
	/* RFC 9562's NAMESPACE_URL, and the libdivecomputer namespace derived from it. */
	static const unsigned char url[16] = {0x6b, 0xa7, 0xb8, 0x11, 0x9d, 0xad, 0x11, 0xd1,
	                                      0x80, 0xb4, 0x00, 0xc0, 0x4f, 0xd4, 0x30, 0xc8};
	char uuid[37];
	CHECK(identity_uuid5(url, "https://divejson.org/ns/libdivecomputer", uuid));
	CHECK_STR(uuid, "58ef9d16-8f8c-58f9-bd78-f1d242933370");
	CHECK(identity_uuid5(url, "https://divejson.org/ns/uddf", uuid));
	CHECK_STR(uuid, "1b85a949-d5f7-5d67-9d04-dcc78342f907");

	CHECK(identity_uuid_parse("58EF9D16-8F8C-58F9-BD78-F1D242933370", uuid));
	CHECK_STR(uuid, "58ef9d16-8f8c-58f9-bd78-f1d242933370");
	CHECK(!divejson_uuid_valid("58ef9d16-8f8c-58f9-bd78-f1d24293337"));
	CHECK(!divejson_uuid_valid("58ef9d16x8f8c-58f9-bd78-f1d242933370"));
	CHECK(!divejson_uuid_valid("58ef9d16-8f8c-58f9-bd78-f1d24293337g"));
	CHECK(!divejson_uuid_valid(NULL));
}

static void test_date_times(void)
{
	CHECK(divejson_date_time_valid("2026-10-26T15:03:23", 0));
	CHECK(!divejson_date_time_valid("2026-10-26T15:03:23", 1));
	CHECK(divejson_date_time_valid("2026-10-26T12:01:49+00:00", 1));
	CHECK(divejson_date_time_valid("2026-10-26T12:01:49.480-03:30", 1));
	CHECK(divejson_date_time_valid("2024-02-29T00:00:00Z", 1));
	CHECK(!divejson_date_time_valid("2026-02-29T00:00:00Z", 1));
	CHECK(!divejson_date_time_valid("2026-10-26T24:00:00Z", 1));
	CHECK(!divejson_date_time_valid("2026-10-26T15:03", 0));
	CHECK(!divejson_date_time_valid("2026-10-26 15:03:23", 0));
	CHECK(!divejson_date_time_valid("2026-10-26T15:03:23+0300", 1));
	CHECK(!divejson_date_time_valid("2026-10-26T15:03:23.", 0));
	CHECK(!divejson_date_time_valid("2026-10-26T15:03:23+03:00 ", 1));
	CHECK(!divejson_date_time_valid(NULL, 0));
}

/* A dive as a reader would hand it over, with a recording that carries `content`. */
static divejson_dive *make_dive(const char *content)
{
	int failed = 0;
	json_value *recording = json_object(&failed);
	json_set(recording, "mode", json_string("open_circuit", &failed), &failed);
	if (content != NULL)
		json_set(recording, content, json_number(1.0125, &failed), &failed);
	json_value *recordings = json_array(&failed);
	json_append(recordings, recording, &failed);
	json_value *dive = json_object(&failed);
	json_set(dive, "uuid", json_string("58ef9d16-8f8c-58f9-bd78-f1d242933370", &failed), &failed);
	json_set(dive, "started_at", json_string("2026-10-26T15:03:23", &failed), &failed);
	json_set(dive, "recordings", recordings, &failed);
	json_value *provenance = json_object(&failed);
	json_set(provenance, "converted_from", json_string("test", &failed), &failed);
	CHECK(!failed);
	return dive_new(dive, provenance);
}

static void test_document(void)
{
	divejson_dive *dive = make_dive("surface_pressure");
	CHECK(dive != NULL);
	CHECK(dive_infer(dive, "submerged_time"));
	CHECK_STR(divejson_dive_json(dive),
	          "{\"uuid\":\"58ef9d16-8f8c-58f9-bd78-f1d242933370\",\"started_at\":\"2026-10-26T15:03:23\","
	          "\"recordings\":[{\"mode\":\"open_circuit\",\"surface_pressure\":1.0125}]}");
	CHECK(divejson_dive_inferred_count(dive) == 1);
	CHECK_STR(divejson_dive_inferred(dive, 0), "submerged_time");
	CHECK(divejson_dive_inferred(dive, 1) == NULL);

	char error[256] = "";
	divejson_document_options options = {"test", "1", "2026-11-03T00:00:00+00:00"};
	char *document = divejson_document(dive, &options, error, sizeof error);
	CHECK(document != NULL);
	CHECK(strncmp(document, "{\n  \"format\": \"divejson\",\n  \"version\": \"1.0\",\n", 46) == 0);
	CHECK_CONTAINS(document, "\"surface_pressure\": 1.0125");
	CHECK_CONTAINS(document, "\"converted_from\": \"test\",\n      \"inferred\": [\n        \"dives/0/submerged_time\"");
	CHECK_CONTAINS(document, "\"generator\": {\n    \"name\": \"test\",\n    \"version\": \"1\"\n  }");
	divejson_free(document);

	/* The export time needs its offset. */
	options.exported_at = "2026-11-03T00:00:00";
	CHECK(divejson_document(dive, &options, error, sizeof error) == NULL);
	CHECK_CONTAINS(error, "offset");
	options.exported_at = "2026-11-03T00:00:00+00:00";
	options.generator_name = "";
	CHECK(divejson_document(dive, &options, error, sizeof error) == NULL);
	divejson_dive_free(dive);

	/* A recording holding only settings is no recording, and the document leaves it out. */
	dive = make_dive(NULL);
	options.generator_name = "test";
	options.generator_version = NULL;
	document = divejson_document(dive, &options, error, sizeof error);
	CHECK(document != NULL);
	CHECK(document != NULL && strstr(document, "recordings") == NULL);
	CHECK(document != NULL && strstr(document, "\"version\": \"1\"") == NULL);
	CHECK(document != NULL && strstr(document, "inferred") == NULL);
	divejson_free(document);
	/* The dive the host is handed keeps it: the host adds what makes it one. */
	CHECK_CONTAINS(divejson_dive_json(dive), "\"recordings\":[{\"mode\":\"open_circuit\"}]");
	divejson_dive_free(dive);
}

int main(void)
{
	test_version();
	test_json_numbers();
	test_json_strings_and_layout();
	test_digests();
	test_uuid();
	test_date_times();
	test_document();
	return check_report("core");
}
