# Reading libdivecomputer into DiveJSON

**Non-normative.** The specification is
[`spec/divejson.md`](https://github.com/divejson/divejson/blob/main/spec/divejson.md); nothing
here changes what a conforming document is.

**The rules that hold for every source format are in
[`converting.md`](https://github.com/divejson/divejson/blob/main/docs/converting.md)** —
leniency, identity, units and arithmetic, what a converter refuses to guess. This document
carries only what is libdivecomputer's: what the reader takes in, its field and sample map,
its identity namespace, what it writes as provenance, and what it deliberately leaves
unmapped. `src/libdivecomputer.c` implements it, and the two change together.

## Why a reader at all

[libdivecomputer](https://www.libdivecomputer.org) is the de facto universal reader of dive
computers: some forty brands, read through one abstract model of a dive's fields
(`dc_field_type_t`) and its samples (`dc_sample_type_t`). A reader of that model makes every
computer libdivecomputer supports a DiveJSON source through one mapping, with nothing lost
relative to what libdivecomputer itself exposes.

The source is not a file format. It is one dive's buffer, exactly as libdivecomputer's
download handed it over (what `dctool download -f raw` writes, one file per dive), and the
buffer alone does not say which parser reads it.

## What the reader takes in

| input | what it is |
| --- | --- |
| the buffer | one dive's bytes, as the download produced them |
| the descriptor | the vendor and product strings and the model number, as `dc_descriptor_get_vendor`, `dc_descriptor_get_product` and `dc_descriptor_get_model` answer them |
| a fallback start | optional: the start to use where the parser reports none |
| a uuid | optional: the dive's identity, where the caller already has one |

**The descriptor is resolved against the descriptor table of the libdivecomputer that is
linked.** Vendor and product are matched exactly. One match is the descriptor; where several
entries share vendor and product, the model number decides between them, and a model that
matches none of them, or more than one, is refused. **A family number is never an input**:
`dc_family_t` is numbered per build, and a fork that inserts a family renumbers every one
after it, so a stored family number means different things to different builds.

The reader reaches libdivecomputer only through its public parser and descriptor calls
(`dc_parser_new2`, `dc_parser_get_datetime`, `dc_parser_get_field`,
`dc_parser_samples_foreach`, the descriptor iterator). It opens no device and no transport,
and it sets no clock on the parser: `dc_parser_new2` takes the family and model from the
descriptor, and the parsers that need a device clock to place a dive — the Reefnet Sensus
family and the Uwatec Memomouse — report no start, which the fallback answers.

## Units

libdivecomputer reports metres, °C, bar, litres, seconds, gas fractions from 0 to 1, CNS as a
fraction, and milliseconds on the sample axis.

| DiveJSON member | DiveJSON unit | libdivecomputer | its unit | factor |
| --- | --- | --- | --- | --- |
| `profile.duration`, every `times` | milliseconds | `DC_SAMPLE_TIME` | milliseconds | — |
| `profile.depth` values | centimetres | `DC_SAMPLE_DEPTH` | metres | × 100 |
| `profile.ceiling` values | centimetres | `DC_SAMPLE_DECO`'s depth, for `DC_DECO_DECOSTOP` | metres | × 100 |
| `profile.temperature` values | hundredths of a °C | `DC_SAMPLE_TEMPERATURE` | °C | × 100 |
| `profile.pressures[]` values | tenths of a bar | `DC_SAMPLE_PRESSURE` | bar | × 10 |
| `profile.heart_rate` values | beats per minute | `DC_SAMPLE_HEARTBEAT` | beats per minute | — |
| `profile.ndl` values | seconds | `DC_SAMPLE_DECO`'s time, for `DC_DECO_NDL` | seconds | — |
| `profile.tts` values | seconds | `DC_SAMPLE_DECO`'s `tts` | seconds | — |
| `profile.ppo2` values | hundredths of a bar | `DC_SAMPLE_PPO2`, sensor `DC_SENSOR_NONE` | bar | × 100 |
| `profile.cns` values | tenths of a percent | `DC_SAMPLE_CNS` | fraction | × 1000 |
| `submerged_time` | seconds | `DC_FIELD_DIVETIME` | seconds | — |
| `max_depth`, `avg_depth` | metres | `DC_FIELD_MAXDEPTH`, `DC_FIELD_AVGDEPTH` | metres | —, to two decimals |
| `cylinders[].oxygen`, `.helium` | percent | `dc_gasmix_t`'s `oxygen`, `helium` | fraction | × 100, to one decimal |
| `cylinders[].volume` | litres | `dc_tank_t`'s `volume` | litres | —, to two decimals |
| `cylinders[].start_pressure`, `.end_pressure` | bar | `dc_tank_t`'s `beginpressure`, `endpressure` | bar | —, to two decimals |
| a recording's `surface_pressure` | bar | `DC_FIELD_ATMOSPHERIC` | bar | —, to four decimals |
| `entry_position` | decimal degrees | `DC_FIELD_LOCATION` | decimal degrees | —, to seven decimals |

A scaled value is rounded to the nearest whole number, halves away from zero. The figures
libdivecomputer hands over are already doubles, so the arithmetic is the double arithmetic:
there is no source text to run decimal arithmetic on. A value that is not finite, or that no
integer below 10¹⁵ holds, is no reading.

## Identity

The rules are `converting.md`'s. libdivecomputer's namespace, fixed forever, is
`58ef9d16-8f8c-58f9-bd78-f1d242933370`, itself
`uuid5(NAMESPACE_URL, "https://divejson.org/ns/libdivecomputer")`.

A buffer carries no id of its own, so **the dive's id is the buffer's SHA-256**, in lowercase
hex: the dive's uuid is `uuid5(namespace, "dive:" + sha256)`. Two different buffers never
share a uuid, and one buffer read twice keeps its own. A caller that already holds the dive's
identity — a host that derives one from the computer's serial and the dive's fingerprint, so
that a dive downloaded twice is recognised — passes it, and the reader writes it lowercased.

## The start

**The start is libdivecomputer's, never a guess.** `dc_parser_get_datetime` gives a wall
clock and, where the parser states one, a zone in seconds east of UTC:

- **A zone stated** writes the wall clock with its offset, in whole minutes. The Suunto
  Nautic parser logs instants: it converts with `dc_datetime_gmtime` and states UTC, so a
  Suunto Ocean's start arrives as `2026-10-26T12:01:49+00:00`. Rendering that in the diver's
  zone is the host's, which knows the zone; the reader never picks one.
- **No zone** (`DC_TIMEZONE_NONE`) writes the wall clock without an offset, which is what a
  Shearwater logs unless its log states a UTC offset.
- **No start**, or a date the calendar does not have, takes the caller's fallback start as
  given. With none, the dive is refused. The Suunto Nautic parser reports no start for a dive
  whose log holds no GPS fix, its only absolute clock, and the fallback a host passes there is
  the start the dive's fingerprint carries.

## The map

Everything the reader writes. A member is written only where libdivecomputer reported it:
`dc_parser_get_field` answering anything but `DC_STATUS_SUCCESS` is absence, never a default.

### Document

The library hands its host **one dive**, and `divejson-raw` wraps it in a document.

| libdivecomputer | DiveJSON |
| --- | --- |
| — | `extensions.divejson.converted_from`: `libdivecomputer` |
| the resolved descriptor | `extensions.divejson.descriptor`: `{vendor, product, model}`, as the linked table spells them |
| — | `extensions.divejson.inferred`, where the reader inferred anything; it infers nothing yet |
| — | `generator`: `divejson-raw` and its `--version`, which names the libdivecomputer commit |
| — | `exported_at`: the moment of conversion, or `--exported-at` |

### Dive

| libdivecomputer | DiveJSON |
| --- | --- |
| the uuid (*Identity*) | `uuid` |
| `dc_parser_get_datetime`, or the fallback (*The start*) | `started_at` |
| `DC_FIELD_DIVETIME`, above zero | `submerged_time` |
| `DC_FIELD_MAXDEPTH`, above zero | `max_depth` |
| `DC_FIELD_AVGDEPTH`, above zero and not deeper than `max_depth` | `avg_depth` |
| `DC_FIELD_LOCATION`, latitude and longitude inside their ranges | `entry_position` |
| `DC_FIELD_TANK`s and `DC_FIELD_GASMIX`es | `cylinders` (below) |
| — | `recordings`: one, the computer's |

### Cylinders

**The dive's cylinders are the computer's tanks in its order, then each gas mix no tank
holds, then each tank that reported pressures and is none of those** (*Profile* below). A cylinder's `gas_number` is its place in that list. A tank or a mix the parser
lists and then cannot read is skipped, and the numbering closes over it; a tank's mix, a gas
switch and a pressure reading still name mixes and tanks by libdivecomputer's own indices,
which are resolved to the cylinders they became.

| libdivecomputer | DiveJSON |
| --- | --- |
| the tank's mix (`dc_tank_t.gasmix`, unless `DC_GASMIX_UNKNOWN`), or the mix itself | `oxygen` when it is 0–100 % to one decimal; then `helium`, capped at what the oxygen leaves |
| `dc_tank_t.usage`, else its mix's: `DC_USAGE_OXYGEN`, `DC_USAGE_DILUENT` | `role`: `oxygen`, `diluent` |
| the same, `DC_USAGE_SIDEMOUNT` | `usage`: `parallel` |
| `dc_tank_t.volume`, where the tank type is not `DC_TANKVOLUME_NONE` and the volume is above zero | `volume` |
| `dc_tank_t.beginpressure`, above zero and at most 350 bar | `start_pressure` |
| `dc_tank_t.endpressure`, above zero, at most 350 bar and not above the start | `end_pressure` |

An imperial tank's volume is the water capacity libdivecomputer has already converted it to.

### Recording

| libdivecomputer | DiveJSON |
| --- | --- |
| `DC_FIELD_DIVEMODE`: freedive, gauge, OC, CCR, SCR | `mode`: `freedive`, `gauge`, `open_circuit`, `closed_circuit`, `semi_closed` |
| `DC_FIELD_DECOMODEL`, `DC_DECOMODEL_BUHLMANN` or `DC_DECOMODEL_RGBM` | `deco_model.algorithm`: `buhlmann`, `rgbm`; with `conservatism` |
| its gradient factors, Bühlmann only, not both zero (libdivecomputer's "not available"), each 0–100 and low not above high | `deco_model.gf_low`, `.gf_high` |
| `DC_FIELD_SALINITY`: `DC_WATER_FRESH` | `salinity`: `fresh` |
| the same, salt at a density within 0.5 kg/m³ of 1020 | `salinity`: `en13319`, EN 13319's density, which the Shearwater parser reports as salt |
| the same, any other salt | `salinity`: `salt` |
| `DC_FIELD_ATMOSPHERIC`, 0.4–1.2 bar to four decimals | `surface_pressure` |
| the samples | `profile` (below) |

**No `device`, and no `source_files`.** How a device is named decides whether two
recordings are one device in a consumer that compares devices by brand, model and serial, and
that rule is the consumer's, not the converter's: the descriptor's product string is
libdivecomputer's vocabulary rather than what the vendor calls the computer, and the serial
and firmware come from the download, not from the buffer. A host that has them adds both;
`divejson-raw`'s document carries the descriptor as provenance instead. A recording that
then carries nothing a recording must — no profile and no readout — is left out of the
document, its settings with it.

### Profile

`dc_parser_samples_foreach` reports a stream of values. **A `DC_SAMPLE_TIME` opens the next
sample, and each value lands on the sample open when it was reported**; a value reported
before the first time belongs to the dive's first moment, time zero. A value reported twice
in one sample keeps the later one, except where a list is kept (pressures, ppO₂ readings,
events). A time is read as a signed count: a parser summing signed deltas reports a sample
before the start as a negative count cast to unsigned, and a negative time is off the axis.

**Each channel keeps the first reading at an instant**: a reading whose time is not after the
channel's last is dropped, which keeps §6.5's times strictly increasing. `duration` is the
largest time of any sample, whatever it carried. A dive whose parser reported no sample at
all has no profile.

| libdivecomputer | DiveJSON |
| --- | --- |
| `DC_SAMPLE_DEPTH` | `depth` |
| `DC_SAMPLE_TEMPERATURE` | `temperature` |
| `DC_SAMPLE_PRESSURE`, per `tank` | `pressures[]`, one series per tank index, in index order, `gas_number` that tank's cylinder. A tank that is no cylinder — listed nowhere, or listed and unreadable — becomes one at the end of `cylinders`, carrying nothing but its pressures, `converting.md`'s *Evidence of a tank* |
| `DC_SAMPLE_DECO`, `DC_DECO_NDL` | `ndl` |
| `DC_SAMPLE_DECO`, `DC_DECO_DECOSTOP` | `ceiling`, from the stop's depth |
| `DC_SAMPLE_DECO`'s `tts`, above zero, whatever the kind | `tts` |
| `DC_SAMPLE_PPO2` with sensor `DC_SENSOR_NONE`, not negative | `ppo2`: the figure the computer calculated |
| `DC_SAMPLE_CNS`, not negative | `cns` |
| `DC_SAMPLE_HEARTBEAT`, at least 1 | `heart_rate` |
| `DC_SAMPLE_GASMIX`, unless `DC_GASMIX_UNKNOWN` | `events[]`: `gas_switch`, with the `gas_number` of the first cylinder holding the mix |
| `DC_SAMPLE_EVENT` (below) | `events[]` |

### Events

A `DC_SAMPLE_EVENT` of type `SAMPLE_EVENT_NONE` is no event. A type the format's vocabulary
has the meaning of becomes that type; every other is labelled with libdivecomputer's own
name for it. An event's own time, flags and value are not carried.

| `parser_sample_event_t` | DiveJSON |
| --- | --- |
| `SAMPLE_EVENT_ASCENT` | `type`: `ascent_rate` |
| `SAMPLE_EVENT_CEILING` | `type`: `ceiling_violation` |
| `SAMPLE_EVENT_BOOKMARK` | `type`: `bookmark` |
| `SAMPLE_EVENT_SAFETYSTOP`, `SAMPLE_EVENT_SAFETYSTOP_VOLUNTARY` | `type`: `safety_stop` |
| `SAMPLE_EVENT_SAFETYSTOP_MANDATORY` | `type`: `safety_stop_mandatory` |
| `SAMPLE_EVENT_DEEPSTOP` | `type`: `deep_stop` |
| `SAMPLE_EVENT_PO2` | `type`: `ppo2_high` |
| `SAMPLE_EVENT_DECOSTOP`, `_RBT`, `_WORKLOAD`, `_TRANSMITTER`, `_VIOLATION`, `_SURFACE`, `_GASCHANGE`, `_CEILING_SAFETYSTOP`, `_FLOOR`, `_DIVETIME`, `_MAXDEPTH`, `_OLF`, `_AIRTIME`, `_RGBM`, `_HEADING`, `_TISSUELEVEL`, `_GASCHANGE2` | `label`: `deco stop`, `remaining bottom time`, `workload`, `transmitter`, `violation`, `surface`, `gas change`, `ceiling (safety stop)`, `floor`, `dive time`, `maximum depth`, `OLF`, `air time`, `RGBM`, `heading`, `tissue level`, `gas change` |
| a type this list does not name | `label`: `event <number>` |

## Sentinels and caps

- **A zero is absence where the member is above zero**: a dive time, a maximum or mean
  depth, a volume or a start pressure of zero is not written. So is a heart rate of zero,
  §6.4's floor of 1, which libdivecomputer passes through from some computers.
- **Bühlmann gradient factors of 0/0** are libdivecomputer's "not available".
- **A tank of type `DC_TANKVOLUME_NONE`** has no volume, whatever its zero says.
- **A no-decompression time at a display cap is a reading.** A Shearwater reports 5940
  seconds, its 99-minute maximum, through most of a shallow dive, and a Suunto Ocean 6000.
  Both are written as they come, `converting.md`'s rule.

## What the reader computes

Nothing yet: every member above is a figure libdivecomputer reported, re-expressed in the
format's units. The `inferred` list exists so that a later mapping that derives a member — a
dive time from the samples, for a parser that reports none — says so in the document.

## Deliberately not mapped

libdivecomputer reports these, and the format has no member for them yet. When the format
takes one over, a release of this library writes it; the original buffer is the lossless
record in the meantime.

| libdivecomputer | why not |
| --- | --- |
| `DC_FIELD_TEMPERATURE_SURFACE`, `_MINIMUM`, `_MAXIMUM` | §6.2's `bottom_temperature` is the logbook's figure for the bottom, and none of the three is that: a minimum is the surface's on a dive that ended in a cold surface layer. The format has no member for a computer's temperature summary; the profile carries its readings. |
| `DC_FIELD_SALINITY`'s density | §6.4a's `salinity` is a named setting; a density beyond EN 13319's has no member. |
| `DC_FIELD_ATMOSPHERIC` outside 0.4–1.2 bar | outside `surface_pressure`'s range, which is the range of a surface a diver dives from. |
| `DC_FIELD_DECOMODEL` of `DC_DECOMODEL_NONE`, `_VPM` or `_DCIEM`, and Bühlmann factors outside 0–100 or with the low above the high | §6.4c's `algorithm` has no value for the three models, and the factors are not a model anything ran. |
| `DC_FIELD_AVGDEPTH` deeper than `DC_FIELD_MAXDEPTH` | a mean deeper than the maximum is not a mean of the same dive. |
| `DC_FIELD_LOCATION`'s altitude | §6's Position is a latitude and a longitude. |
| `dc_tank_t.workpressure` and the tank type | a fact of the vessel, which §6.12 holds on a gear item; `converting.md`'s *Recordings and devices* says why a converter mints none per dive. |
| `DC_SAMPLE_PPO2` with a sensor index | per cell; §6.4's `ppo2` is the one figure the computer calculated. |
| `DC_SAMPLE_SETPOINT` | no rebreather setpoint member. |
| `DC_SAMPLE_DECO`'s stop time, for `DC_DECO_DECOSTOP` | no member for how long a stop lasts. |
| `DC_SAMPLE_DECO` of `DC_DECO_SAFETYSTOP` or `DC_DECO_DEEPSTOP` | a stop the computer showed, with a depth and a duration; an event carries neither. |
| `DC_SAMPLE_RBT` | remaining bottom time, in whatever unit the family reports it in. |
| `DC_SAMPLE_BEARING` | no heading channel. |
| `DC_SAMPLE_LOCATION` | positions along the dive; `entry_position` is the field's. |
| `DC_SAMPLE_VENDOR` | vendor bytes, which are in the original buffer. |
| an event's own time, flags and value | the event's place on the axis is its sample's; the rest is libdivecomputer's encoding of it. |

## Known source artefacts

Things a converted dive shows that are libdivecomputer's or the computer's doing rather than
the reader's. Each was seen on the fixtures, which are one dive recorded on a Shearwater
Perdix 3 and a Suunto Ocean worn together.

- **On a Shearwater, a deco stop's depth is the stop, not the ceiling.** The parser reports
  the depth of the stop the computer shows, in its 3 m steps, where a ceiling is the depth the
  diver may ascend to: the Perdix 3's own export of the same dive starts its ceiling at 1 m
  where this reader starts at 3 m. The Suunto Nautic parser reports a ceiling that moves by
  the centimetre.
- **A Shearwater's samples end their interval.** libdivecomputer stamps each Shearwater sample
  at the end of the 10 seconds it covers, so the first is at 10 s, where the computer's own
  export starts its axis at zero.
- **A Suunto Ocean's end pressure is the transmitter's last reading**, taken after the diver
  surfaced: 120.39 bar on the fixture, where `divejson-py`'s reading of the Suunto app's
  export of the same dive, which sets aside a purged regulator, gives 183.2.
- **A Suunto Ocean also reports the deprecated `SAMPLE_EVENT_GASCHANGE`**, at each switch and
  at other moments around one, so `gas change` labels sit beside the `gas_switch` events that
  its `DC_SAMPLE_GASMIX` gives.
