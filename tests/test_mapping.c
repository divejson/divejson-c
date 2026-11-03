/*
 * The mapping's numbering rules, over a libdivecomputer this test supplies itself: the library
 * leaves every dc_* symbol to its host, so a host may be a scripted parser. Real parsers
 * rarely list a gas mix or a tank they then cannot read, and none of the fixtures does.
 *
 * With an output directory as its argument, it also writes the documents it reads there, for
 * test_cli.py to validate.
 */
#include <stdlib.h>

#include <libdivecomputer/context.h>
#include <libdivecomputer/descriptor.h>
#include <libdivecomputer/iterator.h>
#include <libdivecomputer/parser.h>

#include "check.h"
#include "divejson.h"
#include "divejson_libdivecomputer.h"

/* ---- A scripted libdivecomputer ----------------------------------------------------- */

struct dc_context_t {
	int unused;
};
struct dc_descriptor_t {
	int unused;
};
struct dc_iterator_t {
	int done;
};
struct dc_parser_t {
	int unused;
};

/* What the scripted parser reports. */
typedef struct script_t {
	unsigned int mixes, tanks;
	unsigned int unreadable_mix, unreadable_tank; /* an index, or 99 for none */
	const double *oxygen;                          /* by mix */
	const unsigned int *tank_mix;                  /* by tank */
	const unsigned int *switches;                  /* gas switches, one per sample */
	size_t switch_count;
	const unsigned int *pressure_tanks;            /* a pressure for each, on every sample */
	size_t pressure_tank_count;
} script_t;

static script_t script;

static struct dc_context_t context;
static struct dc_descriptor_t descriptor;
static struct dc_iterator_t iterator;
static struct dc_parser_t parser;

dc_status_t dc_context_new(dc_context_t **out)
{
	*out = &context;
	return DC_STATUS_SUCCESS;
}
dc_status_t dc_context_free(dc_context_t *c) { (void) c; return DC_STATUS_SUCCESS; }
dc_status_t dc_context_set_loglevel(dc_context_t *c, dc_loglevel_t l) { (void) c, (void) l; return DC_STATUS_SUCCESS; }
dc_status_t dc_context_set_logfunc(dc_context_t *c, dc_logfunc_t f, void *u)
{
	(void) c, (void) f, (void) u;
	return DC_STATUS_SUCCESS;
}

dc_status_t dc_descriptor_iterator_new(dc_iterator_t **out, dc_context_t *c)
{
	(void) c;
	iterator.done = 0;
	*out = &iterator;
	return DC_STATUS_SUCCESS;
}
dc_status_t dc_iterator_next(dc_iterator_t *it, void *item)
{
	if (it->done)
		return DC_STATUS_DONE;
	it->done = 1;
	*(dc_descriptor_t **) item = &descriptor;
	return DC_STATUS_SUCCESS;
}
dc_status_t dc_iterator_free(dc_iterator_t *it) { (void) it; return DC_STATUS_SUCCESS; }
void dc_descriptor_free(dc_descriptor_t *d) { (void) d; }
const char *dc_descriptor_get_vendor(const dc_descriptor_t *d) { (void) d; return "Test"; }
const char *dc_descriptor_get_product(const dc_descriptor_t *d) { (void) d; return "Scripted"; }
unsigned int dc_descriptor_get_model(const dc_descriptor_t *d) { (void) d; return 7; }

dc_status_t dc_parser_new2(dc_parser_t **out, dc_context_t *c, dc_descriptor_t *d,
                           const unsigned char data[], size_t size)
{
	(void) c, (void) d, (void) data, (void) size;
	*out = &parser;
	return DC_STATUS_SUCCESS;
}
dc_status_t dc_parser_destroy(dc_parser_t *p) { (void) p; return DC_STATUS_SUCCESS; }

dc_status_t dc_parser_get_datetime(dc_parser_t *p, dc_datetime_t *dt)
{
	(void) p;
	*dt = (dc_datetime_t) {2026, 10, 26, 15, 3, 23, DC_TIMEZONE_NONE};
	return DC_STATUS_SUCCESS;
}

dc_status_t dc_parser_get_field(dc_parser_t *p, dc_field_type_t type, unsigned int flags, void *value)
{
	(void) p;
	switch (type) {
	case DC_FIELD_GASMIX_COUNT:
		*(unsigned int *) value = script.mixes;
		return DC_STATUS_SUCCESS;
	case DC_FIELD_GASMIX:
		if (flags >= script.mixes || flags == script.unreadable_mix)
			return DC_STATUS_DATAFORMAT;
		*(dc_gasmix_t *) value = (dc_gasmix_t) {0, script.oxygen[flags], 1 - script.oxygen[flags], DC_USAGE_NONE};
		return DC_STATUS_SUCCESS;
	case DC_FIELD_TANK_COUNT:
		*(unsigned int *) value = script.tanks;
		return DC_STATUS_SUCCESS;
	case DC_FIELD_TANK:
		if (flags >= script.tanks || flags == script.unreadable_tank)
			return DC_STATUS_DATAFORMAT;
		*(dc_tank_t *) value = (dc_tank_t) {script.tank_mix[flags], DC_TANKVOLUME_NONE, 0, 0, 200, 50, DC_USAGE_NONE};
		return DC_STATUS_SUCCESS;
	default:
		return DC_STATUS_UNSUPPORTED;
	}
}

dc_status_t dc_parser_samples_foreach(dc_parser_t *p, dc_sample_callback_t callback, void *userdata)
{
	(void) p;
	size_t samples = script.switch_count > 0 ? script.switch_count : 1;
	for (size_t i = 0; i < samples; i++) {
		dc_sample_value_t value = {0};
		value.time = (unsigned int) (i * 10000);
		callback(DC_SAMPLE_TIME, &value, userdata);
		value.depth = 10;
		callback(DC_SAMPLE_DEPTH, &value, userdata);
		if (i < script.switch_count) {
			value.gasmix = script.switches[i];
			callback(DC_SAMPLE_GASMIX, &value, userdata);
		}
		for (size_t t = 0; t < script.pressure_tank_count; t++) {
			value.pressure.tank = script.pressure_tanks[t];
			value.pressure.value = 200.0 - (double) i;
			callback(DC_SAMPLE_PRESSURE, &value, userdata);
		}
	}
	return DC_STATUS_SUCCESS;
}

/* ---- The tests ------------------------------------------------------------------------ */

static const char *output_dir;

static divejson_dive *read_script(const char *name)
{
	static const unsigned char data[] = {1};
	divejson_libdivecomputer_input input = {data, sizeof data, "Test", "Scripted", 7, NULL, NULL};
	char error[256] = "";
	divejson_dive *dive = divejson_libdivecomputer_read(&input, error, sizeof error);
	CHECK(dive != NULL);
	if (dive != NULL && output_dir != NULL) {
		divejson_document_options options = {"test_mapping", NULL, "2026-11-03T00:00:00+00:00"};
		char *document = divejson_document(dive, &options, error, sizeof error);
		char path[1024];
		snprintf(path, sizeof path, "%s/%s.divejson", output_dir, name);
		FILE *file = fopen(path, "w");
		CHECK(document != NULL && file != NULL);
		if (document != NULL && file != NULL)
			fputs(document, file);
		if (file != NULL)
			fclose(file);
		divejson_free(document);
	}
	return dive;
}

static void test_unreadable_entries(void)
{
	/* Mix 0 and tank 0 cannot be read. Tank 1 holds mix 1, mix 2 is in no tank, and tank 5 is
	 * listed nowhere but reports a pressure. */
	static const double oxygen[] = {0.21, 0.32, 0.5};
	static const unsigned int tank_mix[] = {0, 1};
	static const unsigned int switches[] = {1, 2, 0};
	static const unsigned int pressure_tanks[] = {0, 1, 5};
	script = (script_t) {3, 2, 0, 0, oxygen, tank_mix, switches, 3, pressure_tanks, 3};

	divejson_dive *dive = read_script("unreadable-entries");
	if (dive == NULL)
		return;
	const char *json = divejson_dive_json(dive);
	/* Tank 1, then mix 2, then a cylinder each for the unreadable tank 0 and the unlisted 5. */
	CHECK_CONTAINS(json, "\"cylinders\":[{\"oxygen\":32,\"helium\":0,\"start_pressure\":200,"
	                     "\"end_pressure\":50,\"gas_number\":0},{\"oxygen\":50,\"helium\":0,\"gas_number\":1},"
	                     "{\"gas_number\":2},{\"gas_number\":3}]");
	/* Switches to mixes 1 and 2 name their cylinders; one to the unreadable mix 0 names none. */
	CHECK_CONTAINS(json, "\"events\":[{\"time\":0,\"type\":\"gas_switch\",\"gas_number\":0},"
	                     "{\"time\":10000,\"type\":\"gas_switch\",\"gas_number\":1},"
	                     "{\"time\":20000,\"type\":\"gas_switch\"}]");
	CHECK_CONTAINS(json, "\"pressures\":[{\"times\":[0,10000,20000],\"values\":[2000,1990,1980],\"gas_number\":2},"
	                     "{\"times\":[0,10000,20000],\"values\":[2000,1990,1980],\"gas_number\":0},"
	                     "{\"times\":[0,10000,20000],\"values\":[2000,1990,1980],\"gas_number\":3}]");
	divejson_dive_free(dive);
}

static void test_numbered_only_where_referred_to(void)
{
	static const double oxygen[] = {0.21, 0.5};
	static const unsigned int tank_mix[] = {0};
	script = (script_t) {2, 1, 99, 99, oxygen, tank_mix, NULL, 0, NULL, 0};

	divejson_dive *dive = read_script("unnumbered");
	if (dive == NULL)
		return;
	const char *json = divejson_dive_json(dive);
	CHECK_CONTAINS(json, "\"cylinders\":[{\"oxygen\":21,\"helium\":0,\"start_pressure\":200,"
	                     "\"end_pressure\":50},{\"oxygen\":50,\"helium\":0}]");
	CHECK(strstr(json, "gas_number") == NULL);
	divejson_dive_free(dive);
}

int main(int argc, char **argv)
{
	output_dir = argc > 1 ? argv[1] : NULL;
	test_unreadable_entries();
	test_numbered_only_where_referred_to();
	return check_report("mapping");
}
