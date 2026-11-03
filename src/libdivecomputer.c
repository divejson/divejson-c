/*
 * The libdivecomputer reader. docs/libdivecomputer-mapping.md is its specification: each
 * rule here is stated there with its reason, and the two are changed together.
 */
#include "divejson_libdivecomputer.h"

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libdivecomputer/context.h>
#include <libdivecomputer/descriptor.h>
#include <libdivecomputer/iterator.h>
#include <libdivecomputer/parser.h>

#include "dive.h"
#include "identity.h"

/* uuid5(NAMESPACE_URL, "https://divejson.org/ns/libdivecomputer"), fixed forever. */
static const unsigned char NAMESPACE[16] = {
	0x58, 0xef, 0x9d, 0x16, 0x8f, 0x8c, 0x58, 0xf9,
	0xbd, 0x78, 0xf1, 0xd2, 0x42, 0x93, 0x33, 0x70,
};

/* ---- Numbers ---------------------------------------------------------------------- */

/* `value` times `factor` to the nearest whole number, halves away from zero. */
static int scaled(double value, double factor, long long *out)
{
	double result = round(value * factor);
	if (!isfinite(result) || fabs(result) >= 1e15)
		return 0;
	*out = (long long) result;
	return 1;
}

static int centi(double value, long long *out) { return scaled(value, 100, out); }

/* `value` to `places` decimals. */
static int rounded(double value, int places, double *out)
{
	double scale = pow(10, places);
	double result = round(value * scale) / scale;
	if (!isfinite(result))
		return 0;
	*out = result;
	return 1;
}

/* ---- A series: strictly increasing times, the first reading at an instant kept ----- */

typedef struct series {
	long long *times;
	long long *values;
	size_t count, capacity;
	long long key; /* a pressure series' tank */
} series;

static int series_add(series *s, long long time, long long value)
{
	if (time < 0 || (s->count > 0 && time <= s->times[s->count - 1]))
		return 1;
	if (s->count == s->capacity) {
		size_t next = s->capacity ? s->capacity * 2 : 256;
		long long *times = realloc(s->times, next * sizeof *times);
		if (times == NULL)
			return 0;
		s->times = times;
		long long *values = realloc(s->values, next * sizeof *values);
		if (values == NULL)
			return 0;
		s->values = values;
		s->capacity = next;
	}
	s->times[s->count] = time;
	s->values[s->count] = value;
	s->count++;
	return 1;
}

static void series_free(series *s)
{
	free(s->times);
	free(s->values);
}

static json_value *series_json(const series *s, int *failed)
{
	json_value *object = json_object(failed);
	json_value *times = json_array(failed);
	json_value *values = json_array(failed);
	for (size_t i = 0; i < s->count; i++) {
		json_append(times, json_integer(s->times[i], failed), failed);
		json_append(values, json_integer(s->values[i], failed), failed);
	}
	json_set(object, "times", times, failed);
	json_set(object, "values", values, failed);
	return object;
}

/* ---- Samples ---------------------------------------------------------------------- */

typedef struct sample_pressure {
	unsigned int tank;
	double bar;
} sample_pressure;

typedef struct sample_ppo2 {
	unsigned int sensor;
	double bar;
} sample_ppo2;

/* One sample as the parser reported it: a scalar reported twice keeps the later value. */
typedef struct sample {
	long long time;
	int has_depth, has_temperature, has_heartbeat, has_cns, has_deco, has_gasmix;
	double depth, temperature, cns;
	unsigned int heartbeat, gasmix;
	dc_deco_type_t deco_type;
	unsigned int deco_time, deco_tts;
	double deco_depth;
	sample_pressure *pressures;
	size_t pressure_count, pressure_capacity;
	sample_ppo2 *ppo2;
	size_t ppo2_count, ppo2_capacity;
	unsigned int *events;
	size_t event_count, event_capacity;
} sample;

typedef struct profile {
	/* Gas numbers, by index into the parser's gas mixes; -1 for none. */
	const long long *mix_numbers;
	unsigned int mix_count;

	int open;
	sample current;
	size_t samples;
	long long duration;

	series depth, ceiling, temperature, heart_rate, ndl, tts, ppo2, cns;
	series *pressures;
	size_t pressure_count;
	json_value *events;
	int failed;
} profile;

static int grow(void **items, size_t *capacity, size_t count, size_t size)
{
	if (count < *capacity)
		return 1;
	size_t next = *capacity ? *capacity * 2 : 4;
	void *grown = realloc(*items, next * size);
	if (grown == NULL)
		return 0;
	*items = grown;
	*capacity = next;
	return 1;
}

static void sample_reset(sample *s, long long time)
{
	sample_pressure *pressures = s->pressures;
	sample_ppo2 *ppo2 = s->ppo2;
	unsigned int *events = s->events;
	size_t pressure_capacity = s->pressure_capacity, ppo2_capacity = s->ppo2_capacity,
	       event_capacity = s->event_capacity;
	memset(s, 0, sizeof *s);
	s->time = time;
	s->pressures = pressures;
	s->pressure_capacity = pressure_capacity;
	s->ppo2 = ppo2;
	s->ppo2_capacity = ppo2_capacity;
	s->events = events;
	s->event_capacity = event_capacity;
}

static series *pressure_series(profile *p, unsigned int tank)
{
	for (size_t i = 0; i < p->pressure_count; i++)
		if (p->pressures[i].key == tank)
			return &p->pressures[i];
	series *grown = realloc(p->pressures, (p->pressure_count + 1) * sizeof *grown);
	if (grown == NULL)
		return NULL;
	p->pressures = grown;
	series *s = &p->pressures[p->pressure_count++];
	memset(s, 0, sizeof *s);
	s->key = tank;
	return s;
}

/* The format's event types, by libdivecomputer's parser_sample_event_t. */
static const char *event_type(unsigned int type)
{
	switch (type) {
	case SAMPLE_EVENT_ASCENT: return "ascent_rate";
	case SAMPLE_EVENT_CEILING: return "ceiling_violation";
	case SAMPLE_EVENT_BOOKMARK: return "bookmark";
	case SAMPLE_EVENT_SAFETYSTOP: return "safety_stop";
	case SAMPLE_EVENT_SAFETYSTOP_VOLUNTARY: return "safety_stop";
	case SAMPLE_EVENT_SAFETYSTOP_MANDATORY: return "safety_stop_mandatory";
	case SAMPLE_EVENT_DEEPSTOP: return "deep_stop";
	case SAMPLE_EVENT_PO2: return "ppo2_high";
	default: return NULL;
	}
}

/* libdivecomputer's own name for an event the format has no type for. */
static const char *event_label(unsigned int type)
{
	switch (type) {
	case SAMPLE_EVENT_DECOSTOP: return "deco stop";
	case SAMPLE_EVENT_RBT: return "remaining bottom time";
	case SAMPLE_EVENT_WORKLOAD: return "workload";
	case SAMPLE_EVENT_TRANSMITTER: return "transmitter";
	case SAMPLE_EVENT_VIOLATION: return "violation";
	case SAMPLE_EVENT_SURFACE: return "surface";
	case SAMPLE_EVENT_GASCHANGE: return "gas change";
	case SAMPLE_EVENT_CEILING_SAFETYSTOP: return "ceiling (safety stop)";
	case SAMPLE_EVENT_FLOOR: return "floor";
	case SAMPLE_EVENT_DIVETIME: return "dive time";
	case SAMPLE_EVENT_MAXDEPTH: return "maximum depth";
	case SAMPLE_EVENT_OLF: return "OLF";
	case SAMPLE_EVENT_AIRTIME: return "air time";
	case SAMPLE_EVENT_RGBM: return "RGBM";
	case SAMPLE_EVENT_HEADING: return "heading";
	case SAMPLE_EVENT_TISSUELEVEL: return "tissue level";
	case SAMPLE_EVENT_GASCHANGE2: return "gas change";
	default: return NULL;
	}
}

/* Writes a closed sample's readings onto the profile's channels. */
static void sample_close(profile *p)
{
	const sample *s = &p->current;
	long long t = s->time, value;
	p->samples++;
	if (t < 0)
		return;
	if (t > p->duration)
		p->duration = t;
	int ok = 1;
	if (s->has_depth && centi(s->depth, &value))
		ok &= series_add(&p->depth, t, value);
	if (s->has_temperature && centi(s->temperature, &value))
		ok &= series_add(&p->temperature, t, value);
	for (size_t i = 0; i < s->pressure_count; i++) {
		series *pressures = pressure_series(p, s->pressures[i].tank);
		if (pressures == NULL)
			ok = 0;
		else if (scaled(s->pressures[i].bar, 10, &value))
			ok &= series_add(pressures, t, value);
	}
	if (s->has_deco) {
		if (s->deco_type == DC_DECO_NDL)
			ok &= series_add(&p->ndl, t, s->deco_time);
		else if (s->deco_type == DC_DECO_DECOSTOP && centi(s->deco_depth, &value))
			ok &= series_add(&p->ceiling, t, value);
		if (s->deco_tts > 0)
			ok &= series_add(&p->tts, t, s->deco_tts);
	}
	for (size_t i = 0; i < s->ppo2_count; i++)
		if (s->ppo2[i].bar >= 0 && centi(s->ppo2[i].bar, &value) &&
		    s->ppo2[i].sensor == DC_SENSOR_NONE)
			ok &= series_add(&p->ppo2, t, value);
	if (s->has_cns && s->cns >= 0 && scaled(s->cns, 1000, &value))
		ok &= series_add(&p->cns, t, value);
	if (s->has_heartbeat && s->heartbeat >= 1)
		ok &= series_add(&p->heart_rate, t, s->heartbeat);

	if (s->has_gasmix) {
		json_value *event = json_object(&p->failed);
		json_set(event, "time", json_integer(t, &p->failed), &p->failed);
		json_set(event, "type", json_string("gas_switch", &p->failed), &p->failed);
		if (s->gasmix < p->mix_count && p->mix_numbers[s->gasmix] >= 0)
			json_set(event, "gas_number", json_integer(p->mix_numbers[s->gasmix], &p->failed), &p->failed);
		json_append(p->events, event, &p->failed);
	}
	for (size_t i = 0; i < s->event_count; i++) {
		unsigned int type = s->events[i];
		if (type == SAMPLE_EVENT_NONE)
			continue;
		json_value *event = json_object(&p->failed);
		json_set(event, "time", json_integer(t, &p->failed), &p->failed);
		if (event_type(type) != NULL) {
			json_set(event, "type", json_string(event_type(type), &p->failed), &p->failed);
		} else {
			char label[32];
			if (event_label(type) != NULL)
				snprintf(label, sizeof label, "%s", event_label(type));
			else
				snprintf(label, sizeof label, "event %u", type);
			json_set(event, "label", json_string(label, &p->failed), &p->failed);
		}
		json_append(p->events, event, &p->failed);
	}
	if (!ok)
		p->failed = 1;
}

static void sample_callback(dc_sample_type_t type, const dc_sample_value_t *value, void *userdata)
{
	profile *p = userdata;
	if (value == NULL || p->failed)
		return;
	if (type == DC_SAMPLE_TIME) {
		if (p->open)
			sample_close(p);
		/* A parser summing signed deltas reports a sample before the start as a negative
		 * count cast to unsigned; read as signed, it falls off the axis. */
		sample_reset(&p->current, (long long) (int) value->time);
		p->open = 1;
		return;
	}
	/* A reading before the first time belongs to the dive's first moment. */
	if (!p->open) {
		sample_reset(&p->current, 0);
		p->open = 1;
	}
	sample *s = &p->current;
	switch (type) {
	case DC_SAMPLE_DEPTH:
		s->has_depth = 1;
		s->depth = value->depth;
		break;
	case DC_SAMPLE_PRESSURE:
		if (!grow((void **) &s->pressures, &s->pressure_capacity, s->pressure_count, sizeof *s->pressures)) {
			p->failed = 1;
			break;
		}
		s->pressures[s->pressure_count++] = (sample_pressure) {value->pressure.tank, value->pressure.value};
		break;
	case DC_SAMPLE_TEMPERATURE:
		s->has_temperature = 1;
		s->temperature = value->temperature;
		break;
	case DC_SAMPLE_EVENT:
		if (!grow((void **) &s->events, &s->event_capacity, s->event_count, sizeof *s->events)) {
			p->failed = 1;
			break;
		}
		s->events[s->event_count++] = value->event.type;
		break;
	case DC_SAMPLE_HEARTBEAT:
		s->has_heartbeat = 1;
		s->heartbeat = value->heartbeat;
		break;
	case DC_SAMPLE_PPO2:
		if (!grow((void **) &s->ppo2, &s->ppo2_capacity, s->ppo2_count, sizeof *s->ppo2)) {
			p->failed = 1;
			break;
		}
		s->ppo2[s->ppo2_count++] = (sample_ppo2) {value->ppo2.sensor, value->ppo2.value};
		break;
	case DC_SAMPLE_CNS:
		s->has_cns = 1;
		s->cns = value->cns;
		break;
	case DC_SAMPLE_DECO:
		/* A deco type outside the four libdivecomputer names is no reading. */
		if (value->deco.type <= DC_DECO_DEEPSTOP) {
			s->has_deco = 1;
			s->deco_type = (dc_deco_type_t) value->deco.type;
			s->deco_time = value->deco.time;
			s->deco_depth = value->deco.depth;
			s->deco_tts = value->deco.tts;
		}
		break;
	case DC_SAMPLE_GASMIX:
		if (value->gasmix != DC_GASMIX_UNKNOWN) {
			s->has_gasmix = 1;
			s->gasmix = value->gasmix;
		}
		break;
	default:
		/* Remaining bottom time, bearing, setpoint, vendor records and positions: none has
		 * a member yet (docs/libdivecomputer-mapping.md, *Not mapped*). */
		break;
	}
}

static void profile_free(profile *p)
{
	series *all[] = {&p->depth, &p->ceiling, &p->temperature, &p->heart_rate,
	                 &p->ndl, &p->tts, &p->ppo2, &p->cns};
	for (size_t i = 0; i < sizeof all / sizeof *all; i++)
		series_free(all[i]);
	for (size_t i = 0; i < p->pressure_count; i++)
		series_free(&p->pressures[i]);
	free(p->pressures);
	free(p->current.pressures);
	free(p->current.ppo2);
	free(p->current.events);
	json_free(p->events);
}

static int by_tank(const void *a, const void *b)
{
	long long left = ((const series *) a)->key, right = ((const series *) b)->key;
	return (left > right) - (left < right);
}

/* The profile, or NULL with `*failed` untouched where the parser reported no sample. */
static json_value *profile_json(profile *p, int *failed)
{
	if (p->open)
		sample_close(p);
	p->open = 0;
	if (p->failed)
		*failed = 1;
	if (p->samples == 0)
		return NULL;
	json_value *out = json_object(failed);
	json_set(out, "duration", json_integer(p->duration, failed), failed);
	struct {
		const char *key;
		series *s;
	} channels[] = {
		{"depth", &p->depth}, {"ceiling", &p->ceiling}, {"temperature", &p->temperature},
		{"heart_rate", &p->heart_rate}, {"ndl", &p->ndl}, {"tts", &p->tts},
		{"ppo2", &p->ppo2}, {"cns", &p->cns},
	};
	for (size_t i = 0; i < sizeof channels / sizeof *channels; i++)
		if (channels[i].s->count > 0)
			json_set(out, channels[i].key, series_json(channels[i].s, failed), failed);
	if (p->pressure_count > 1)
		qsort(p->pressures, p->pressure_count, sizeof *p->pressures, by_tank);
	json_value *pressures = NULL;
	for (size_t i = 0; i < p->pressure_count; i++) {
		if (p->pressures[i].count == 0)
			continue;
		if (pressures == NULL)
			pressures = json_array(failed);
		json_value *one = series_json(&p->pressures[i], failed);
		json_set(one, "gas_number", json_integer(p->pressures[i].key, failed), failed);
		json_append(pressures, one, failed);
	}
	if (pressures != NULL)
		json_set(out, "pressures", pressures, failed);
	if (json_count(p->events) > 0) {
		json_set(out, "events", p->events, failed);
		p->events = NULL;
	}
	return out;
}

/* ---- Fields ----------------------------------------------------------------------- */

static int field(dc_parser_t *parser, dc_field_type_t type, unsigned int flags, void *value, size_t size)
{
	memset(value, 0, size);
	return dc_parser_get_field(parser, type, flags, value) == DC_STATUS_SUCCESS;
}

/* A gas mix's fractions as a cylinder's percentages. */
static void put_gas(json_value *cylinder, const dc_gasmix_t *mix, int *failed)
{
	double oxygen, helium;
	if (!rounded(mix->oxygen * 100, 1, &oxygen) || !(oxygen >= 0 && oxygen <= 100))
		return;
	json_set(cylinder, "oxygen", json_number(oxygen, failed), failed);
	if (rounded(mix->helium * 100, 1, &helium)) {
		if (helium > 100 - oxygen)
			helium = 100 - oxygen;
		if (helium >= 0)
			json_set(cylinder, "helium", json_number(helium, failed), failed);
	}
}

static int known_usage(dc_usage_t usage)
{
	return usage == DC_USAGE_OXYGEN || usage == DC_USAGE_DILUENT || usage == DC_USAGE_SIDEMOUNT;
}

static void put_usage(json_value *cylinder, dc_usage_t usage, int *failed)
{
	if (usage == DC_USAGE_OXYGEN)
		json_set(cylinder, "role", json_string("oxygen", failed), failed);
	else if (usage == DC_USAGE_DILUENT)
		json_set(cylinder, "role", json_string("diluent", failed), failed);
	else if (usage == DC_USAGE_SIDEMOUNT)
		json_set(cylinder, "usage", json_string("parallel", failed), failed);
}

/*
 * The dive's cylinders: the parser's tanks in its order, then each gas mix no tank holds.
 * `mix_numbers` receives each mix's gas number, the first cylinder holding it.
 */
static json_value *cylinders_json(dc_parser_t *parser, long long **mix_numbers,
                                  unsigned int *mix_count, int *failed)
{
	unsigned int mixes = 0, tanks = 0;
	if (!field(parser, DC_FIELD_GASMIX_COUNT, 0, &mixes, sizeof mixes))
		mixes = 0;
	if (!field(parser, DC_FIELD_TANK_COUNT, 0, &tanks, sizeof tanks))
		tanks = 0;

	/* A mix the parser lists and cannot read is no mix, as a tank it cannot read is none. */
	dc_gasmix_t *gas = calloc(mixes ? mixes : 1, sizeof *gas);
	long long *numbers = malloc((mixes ? mixes : 1) * sizeof *numbers);
	json_value *out = json_array(failed);
	if (gas == NULL || numbers == NULL) {
		free(gas);
		free(numbers);
		json_free(out);
		*failed = 1;
		return NULL;
	}
	unsigned int read = 0;
	for (unsigned int i = 0; i < mixes; i++) {
		if (field(parser, DC_FIELD_GASMIX, i, &gas[read], sizeof gas[read]))
			read++;
	}
	mixes = read;
	for (unsigned int i = 0; i < mixes; i++)
		numbers[i] = -1;

	long long number = 0;
	for (unsigned int i = 0; i < tanks; i++) {
		dc_tank_t tank;
		if (!field(parser, DC_FIELD_TANK, i, &tank, sizeof tank))
			continue;
		json_value *cylinder = json_object(failed);
		json_set(cylinder, "gas_number", json_integer(number, failed), failed);
		dc_usage_t usage = tank.usage;
		if (tank.gasmix != DC_GASMIX_UNKNOWN && tank.gasmix < mixes) {
			put_gas(cylinder, &gas[tank.gasmix], failed);
			if (!known_usage(usage))
				usage = gas[tank.gasmix].usage;
			if (numbers[tank.gasmix] < 0)
				numbers[tank.gasmix] = number;
		}
		put_usage(cylinder, usage, failed);
		double volume, start = 0, end;
		int has_start = 0;
		if (tank.type != DC_TANKVOLUME_NONE && tank.volume > 0 && rounded(tank.volume, 2, &volume) && volume > 0)
			json_set(cylinder, "volume", json_number(volume, failed), failed);
		if (tank.beginpressure > 0 && tank.beginpressure <= 350 &&
		    rounded(tank.beginpressure, 2, &start) && start > 0) {
			has_start = 1;
			json_set(cylinder, "start_pressure", json_number(start, failed), failed);
		}
		if (tank.endpressure > 0 && tank.endpressure <= 350 && rounded(tank.endpressure, 2, &end) &&
		    (!has_start || end <= start))
			json_set(cylinder, "end_pressure", json_number(end, failed), failed);
		json_append(out, cylinder, failed);
		number++;
	}
	for (unsigned int i = 0; i < mixes; i++) {
		if (numbers[i] >= 0)
			continue;
		numbers[i] = number;
		json_value *cylinder = json_object(failed);
		json_set(cylinder, "gas_number", json_integer(number, failed), failed);
		put_gas(cylinder, &gas[i], failed);
		put_usage(cylinder, gas[i].usage, failed);
		json_append(out, cylinder, failed);
		number++;
	}
	free(gas);
	*mix_numbers = numbers;
	*mix_count = mixes;
	return out;
}

/* The settings the computer dived with, onto the recording. */
static void put_settings(json_value *recording, dc_parser_t *parser, int *failed)
{
	dc_divemode_t mode;
	if (field(parser, DC_FIELD_DIVEMODE, 0, &mode, sizeof mode)) {
		const char *name = NULL;
		switch (mode) {
		case DC_DIVEMODE_FREEDIVE: name = "freedive"; break;
		case DC_DIVEMODE_GAUGE: name = "gauge"; break;
		case DC_DIVEMODE_OC: name = "open_circuit"; break;
		case DC_DIVEMODE_CCR: name = "closed_circuit"; break;
		case DC_DIVEMODE_SCR: name = "semi_closed"; break;
		}
		if (name != NULL)
			json_set(recording, "mode", json_string(name, failed), failed);
	}

	dc_decomodel_t model;
	if (field(parser, DC_FIELD_DECOMODEL, 0, &model, sizeof model) &&
	    (model.type == DC_DECOMODEL_BUHLMANN || model.type == DC_DECOMODEL_RGBM)) {
		json_value *deco = json_object(failed);
		json_set(deco, "algorithm",
		         json_string(model.type == DC_DECOMODEL_BUHLMANN ? "buhlmann" : "rgbm", failed), failed);
		json_set(deco, "conservatism", json_integer(model.conservatism, failed), failed);
		unsigned int low = model.params.gf.low, high = model.params.gf.high;
		/* 0/0 is libdivecomputer's "not available". */
		if (model.type == DC_DECOMODEL_BUHLMANN && (low != 0 || high != 0) && low <= 100 &&
		    high <= 100 && low <= high) {
			json_set(deco, "gf_low", json_integer(low, failed), failed);
			json_set(deco, "gf_high", json_integer(high, failed), failed);
		}
		json_set(recording, "deco_model", deco, failed);
	}

	dc_salinity_t salinity;
	if (field(parser, DC_FIELD_SALINITY, 0, &salinity, sizeof salinity)) {
		const char *water = "fresh";
		if (salinity.type != DC_WATER_FRESH)
			/* EN 13319's 1020 kg/m³, which the Shearwater parser reports as salt. */
			water = salinity.density > 0 && fabs(salinity.density - 1020) < 0.5 ? "en13319" : "salt";
		json_set(recording, "salinity", json_string(water, failed), failed);
	}

	double atmospheric, pressure;
	if (field(parser, DC_FIELD_ATMOSPHERIC, 0, &atmospheric, sizeof atmospheric) &&
	    rounded(atmospheric, 4, &pressure) && pressure >= 0.4 && pressure <= 1.2)
		json_set(recording, "surface_pressure", json_number(pressure, failed), failed);
}

/* ---- The start -------------------------------------------------------------------- */

static int valid_datetime(const dc_datetime_t *dt)
{
	static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
	if (dt->year < 1 || dt->year > 9999 || dt->month < 1 || dt->month > 12 || dt->day < 1)
		return 0;
	int leap = (dt->year % 4 == 0 && dt->year % 100 != 0) || dt->year % 400 == 0;
	return dt->day <= days[dt->month - 1] + (dt->month == 2 && leap) && dt->hour >= 0 &&
	       dt->hour <= 23 && dt->minute >= 0 && dt->minute <= 59 && dt->second >= 0 &&
	       dt->second <= 59;
}

/*
 * The start as the parser reported it: the wall clock, with its offset where the parser
 * stated a zone. A parser that logs instants states UTC, so its start reads `+00:00`.
 */
static int parser_start(dc_parser_t *parser, char *out, size_t size)
{
	dc_datetime_t dt;
	memset(&dt, 0, sizeof dt);
	if (dc_parser_get_datetime(parser, &dt) != DC_STATUS_SUCCESS || !valid_datetime(&dt))
		return 0;
	int length = snprintf(out, size, "%04d-%02d-%02dT%02d:%02d:%02d", dt.year, dt.month, dt.day,
	                      dt.hour, dt.minute, dt.second);
	if (dt.timezone != DC_TIMEZONE_NONE) {
		/* Whole minutes, as an offset is written; a zone past a day is no zone. */
		long long minutes = dt.timezone / 60;
		if (minutes <= -24 * 60 || minutes >= 24 * 60)
			return 1;
		long long magnitude = minutes < 0 ? -minutes : minutes;
		snprintf(out + length, size - (size_t) length, "%c%02lld:%02lld", minutes < 0 ? '-' : '+',
		         magnitude / 60, magnitude % 60);
	}
	return 1;
}

/* ---- The descriptor --------------------------------------------------------------- */

static dc_descriptor_t *resolve(dc_context_t *context, const divejson_libdivecomputer_input *input,
                                char *error, size_t error_size)
{
	dc_iterator_t *iterator = NULL;
	if (dc_descriptor_iterator_new(&iterator, context) != DC_STATUS_SUCCESS) {
		dive_error(error, error_size, "libdivecomputer could not list its dive computers");
		return NULL;
	}
	dc_descriptor_t *named = NULL, *modelled = NULL, *descriptor = NULL;
	size_t named_count = 0, modelled_count = 0;
	while (dc_iterator_next(iterator, &descriptor) == DC_STATUS_SUCCESS) {
		const char *vendor = dc_descriptor_get_vendor(descriptor);
		const char *product = dc_descriptor_get_product(descriptor);
		int keep = 0;
		if (vendor != NULL && product != NULL && strcmp(vendor, input->vendor) == 0 &&
		    strcmp(product, input->product) == 0) {
			if (named_count++ == 0) {
				named = descriptor;
				keep = 1;
			}
			if (dc_descriptor_get_model(descriptor) == input->model && modelled_count++ == 0) {
				modelled = descriptor;
				keep = 1;
			}
		}
		if (!keep)
			dc_descriptor_free(descriptor);
	}
	dc_iterator_free(iterator);

	dc_descriptor_t *chosen = NULL;
	if (named_count == 1)
		chosen = named;
	else if (named_count > 1 && modelled_count == 1)
		chosen = modelled;
	if (named != chosen)
		dc_descriptor_free(named);
	if (modelled != chosen && modelled != named)
		dc_descriptor_free(modelled);

	if (chosen == NULL) {
		if (named_count == 0)
			dive_error(error, error_size, "libdivecomputer knows no dive computer \"%s\" \"%s\"",
			           input->vendor, input->product);
		else
			dive_error(error, error_size,
			           "libdivecomputer knows %zu dive computers \"%s\" \"%s\", and model %u settles %s",
			           named_count, input->vendor, input->product, input->model,
			           modelled_count == 0 ? "none of them" : "none of them alone");
	}
	return chosen;
}

/* ---- The dive --------------------------------------------------------------------- */

typedef struct log_capture {
	char message[160];
} log_capture;

/* Keeps libdivecomputer's first error, to say why it refused a buffer. */
static void capture_log(dc_context_t *context, dc_loglevel_t level, const char *file, unsigned int line,
                        const char *function, const char *message, void *userdata)
{
	(void) context, (void) level, (void) file, (void) line, (void) function;
	log_capture *capture = userdata;
	if (capture->message[0] == '\0' && message != NULL)
		snprintf(capture->message, sizeof capture->message, "%s", message);
}

static const char *status_name(dc_status_t status)
{
	switch (status) {
	case DC_STATUS_UNSUPPORTED: return "unsupported";
	case DC_STATUS_INVALIDARGS: return "invalid arguments";
	case DC_STATUS_NOMEMORY: return "out of memory";
	case DC_STATUS_DATAFORMAT: return "not in the format it reads";
	default: return "an error";
	}
}

static json_value *build(dc_parser_t *parser, const char *uuid, const char *start, int *failed)
{
	json_value *dive = json_object(failed);
	json_set(dive, "uuid", json_string(uuid, failed), failed);
	json_set(dive, "started_at", json_string(start, failed), failed);

	unsigned int divetime;
	if (field(parser, DC_FIELD_DIVETIME, 0, &divetime, sizeof divetime) && divetime > 0)
		json_set(dive, "submerged_time", json_integer(divetime, failed), failed);
	double value, max_depth = 0, average;
	int has_max = 0;
	if (field(parser, DC_FIELD_MAXDEPTH, 0, &value, sizeof value) && value > 0 &&
	    rounded(value, 2, &max_depth) && max_depth > 0) {
		has_max = 1;
		json_set(dive, "max_depth", json_number(max_depth, failed), failed);
	}
	if (field(parser, DC_FIELD_AVGDEPTH, 0, &value, sizeof value) && value > 0 &&
	    rounded(value, 2, &average) && average > 0 && (!has_max || average <= max_depth))
		json_set(dive, "avg_depth", json_number(average, failed), failed);
	dc_location_t location;
	double latitude, longitude;
	if (field(parser, DC_FIELD_LOCATION, 0, &location, sizeof location) &&
	    rounded(location.latitude, 7, &latitude) && latitude >= -90 && latitude <= 90 &&
	    rounded(location.longitude, 7, &longitude) && longitude >= -180 && longitude <= 180) {
		json_value *position = json_object(failed);
		json_set(position, "latitude", json_number(latitude, failed), failed);
		json_set(position, "longitude", json_number(longitude, failed), failed);
		json_set(dive, "entry_position", position, failed);
	}

	profile p;
	memset(&p, 0, sizeof p);
	long long *mix_numbers = NULL;
	json_value *cylinders = cylinders_json(parser, &mix_numbers, &p.mix_count, failed);
	if (json_count(cylinders) > 0)
		json_set(dive, "cylinders", cylinders, failed);
	else
		json_free(cylinders);

	json_value *recording = json_object(failed);
	put_settings(recording, parser, failed);

	p.mix_numbers = mix_numbers;
	p.events = json_array(&p.failed);
	/* A walk that fails part-way keeps the samples delivered before it. */
	dc_parser_samples_foreach(parser, sample_callback, &p);
	json_value *profile = profile_json(&p, failed);
	if (profile != NULL)
		json_set(recording, "profile", profile, failed);
	profile_free(&p);
	free(mix_numbers);

	json_value *recordings = json_array(failed);
	json_append(recordings, recording, failed);
	json_set(dive, "recordings", recordings, failed);
	return dive;
}

static json_value *provenance_json(dc_descriptor_t *descriptor, int *failed)
{
	json_value *provenance = json_object(failed);
	json_set(provenance, "converted_from", json_string(DIVEJSON_LIBDIVECOMPUTER_FORMAT, failed), failed);
	json_value *named = json_object(failed);
	json_set(named, "vendor", json_string(dc_descriptor_get_vendor(descriptor), failed), failed);
	json_set(named, "product", json_string(dc_descriptor_get_product(descriptor), failed), failed);
	json_set(named, "model", json_integer(dc_descriptor_get_model(descriptor), failed), failed);
	json_set(provenance, "descriptor", named, failed);
	return provenance;
}

divejson_dive *divejson_libdivecomputer_read(const divejson_libdivecomputer_input *input,
                                             char *error, size_t error_size)
{
	if (input == NULL || input->vendor == NULL || input->product == NULL) {
		dive_error(error, error_size, "a dive needs a descriptor's vendor and product");
		return NULL;
	}
	if (input->data == NULL || input->size == 0) {
		dive_error(error, error_size, "the dive's buffer is empty");
		return NULL;
	}
	if (input->fallback_start != NULL &&
	    !divejson_date_time_valid(input->fallback_start, 0)) {
		dive_error(error, error_size,
		           "the fallback start \"%s\" is not a date-time such as 2026-10-26T12:00:00+00:00",
		           input->fallback_start);
		return NULL;
	}
	char uuid[37];
	if (input->uuid != NULL) {
		if (!identity_uuid_parse(input->uuid, uuid)) {
			dive_error(error, error_size, "the dive uuid \"%s\" is not a UUID", input->uuid);
			return NULL;
		}
	} else {
		unsigned char digest[32];
		char name[sizeof "dive:" + 64];
		identity_sha256(input->data, input->size, digest);
		memcpy(name, "dive:", 5);
		identity_hex(digest, 32, name + 5);
		if (!identity_uuid5(NAMESPACE, name, uuid)) {
			dive_error(error, error_size, "out of memory");
			return NULL;
		}
	}

	dc_context_t *context = NULL;
	if (dc_context_new(&context) != DC_STATUS_SUCCESS) {
		dive_error(error, error_size, "libdivecomputer could not make a context");
		return NULL;
	}
	log_capture capture = {{0}};
	dc_context_set_loglevel(context, DC_LOGLEVEL_ERROR);
	dc_context_set_logfunc(context, capture_log, &capture);

	divejson_dive *dive = NULL;
	dc_parser_t *parser = NULL;
	dc_descriptor_t *descriptor = resolve(context, input, error, error_size);
	if (descriptor == NULL)
		goto done;

	dc_status_t status = dc_parser_new2(&parser, context, descriptor, input->data, input->size);
	if (status != DC_STATUS_SUCCESS || parser == NULL) {
		dive_error(error, error_size, "libdivecomputer's %s %s parser refused the buffer: %s%s%s",
		           input->vendor, input->product, status_name(status), capture.message[0] ? ": " : "",
		           capture.message);
		parser = NULL;
		goto done;
	}

	char start[40];
	if (!parser_start(parser, start, sizeof start)) {
		if (input->fallback_start == NULL) {
			dive_error(error, error_size,
			           "libdivecomputer reports no start for this dive, and no fallback start was given");
			goto done;
		}
		snprintf(start, sizeof start, "%s", input->fallback_start);
	}

	int failed = 0;
	json_value *value = build(parser, uuid, start, &failed);
	json_value *provenance = provenance_json(descriptor, &failed);
	if (failed) {
		json_free(value);
		json_free(provenance);
		dive_error(error, error_size, "out of memory reading the dive");
		goto done;
	}
	dive = dive_new(value, provenance);
	if (dive == NULL)
		dive_error(error, error_size, "out of memory reading the dive");

done:
	if (parser != NULL)
		dc_parser_destroy(parser);
	if (descriptor != NULL)
		dc_descriptor_free(descriptor);
	dc_context_free(context);
	return dive;
}
