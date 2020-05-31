#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <dirent.h>
#include <assert.h>
#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include <sys/stat.h>
#include <errno.h>

#include <pthread.h>
#include <alsa/asoundlib.h>

typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

#define U8_MAX 255
#define U16_MAX 65535
#define U32_MAX 4294967295

#define arr_count(arr) (sizeof (arr) / sizeof *(arr))

static inline u8 read_u8(FILE *fp) {
	return (u8)getc(fp);
}
static inline i8 read_i8(FILE *fp) {
	return (i8)getc(fp);
}
static inline u16 read_u16(FILE *fp) {
	u16 x;
	fread(&x, sizeof x, 1, fp);
	return x;
}
static inline u32 read_u32(FILE *fp) {
	u32 x;
	fread(&x, sizeof x, 1, fp);
	return x;
}

static inline void write_u16(FILE *fp, u16 x) {
	fwrite(&x, sizeof x, 1, fp);
}
static inline void write_u32(FILE *fp, u32 x) {
	fwrite(&x, sizeof x, 1, fp);
}

static void die(char const *fmt, ...) {
	va_list args;
	fprintf(stderr, "\x1b[91mError\x1b[0m: ");
	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);
	fprintf(stderr, "\n");
	exit(EXIT_FAILURE);
}

static void warn(char const *fmt, ...) {
	va_list args;
	fprintf(stderr, "\x1b[93mWarning\x1b[0m: ");
	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);
	fprintf(stderr, "\n");
}

typedef struct {
	u16 gen_ndx;
	u16 mod_ndx;
} Bag;

typedef struct {
	u16 start;
	u16 end;
} GenZone;

typedef struct {
	char name[21]; // 21 because i'm not sure if it's necessarily null-terminated
	u16 preset, bank, bag_ndx;
	u32 library, genre, morphology;
	u32 nbags;
	Bag *bags;
} Preset;

typedef struct {
	u32 count;
	u32 sample_rate; // original sample rate
	u8 pitch; // original MIDI pitch
	i16 data[1];
} Samples;

typedef struct {
	char name[21];
	bool samples_loaded;
	u16 bag_ndx;
	u32 ngen_zones;
	GenZone *gen_zones;
	Samples *samples[256]; // [2*i] = left channel of note i, [2*i+1] = right channel of note i
} Instrument;

typedef struct {
	u8 lo;
	u8 hi;
} Range;

typedef union {
	Range range;
	i16 sint;
	u16 uint;
} GenAmount;

typedef struct {
	u16 oper;
	GenAmount amount;
} Generator;

typedef struct {
	char name[21];
	u32 start_loop;
	u32 end_loop;
	u32 start;
	u32 count;
	u32 sample_rate;
} SampleHdr;

typedef struct {
	FILE *fp;
	u32 nshdrs;
	SampleHdr *shdrs;
	u32 nigens;
	Generator *igens;
	u32 ninsts;
	Instrument *insts;
	u32 nsamples;
	i64 sdta_offset;
} SoundFont;

typedef enum {
	GEN_startAddrsOffset = 0,
	GEN_endAddrsOffset = 1,
	GEN_startloopAddrsOffset = 2,
	GEN_endloopAddrsOffset = 3,
	GEN_startAddrsCoarseOffset = 4,
	GEN_modLfoToPitch = 5,
	GEN_vibLfoToPitch = 6,
	GEN_modEnvToPitch = 7,
	GEN_initialFilterFc = 8,
	GEN_initialFilterQ = 9,
	GEN_modLfoToFilterFc = 10,
	GEN_modEnvToFilterFc = 11,
	GEN_endAddrsCoarseOffset = 12,
	GEN_modLfoToVolume = 13,
	GEN_unused1 = 14,
	GEN_chorusEffectsSend = 15,
	GEN_reverbEffectsSend = 16,
	GEN_pan = 17,
	GEN_unused2 = 18,
	GEN_unused3 = 19,
	GEN_unused4 = 20,
	GEN_delayModLFO = 21,
	GEN_freqModLFO = 22,
	GEN_delayVibLFO = 23,
	GEN_freqVibLFO = 24,
	GEN_delayModEnv = 25,
	GEN_attackModEnv = 26,
	GEN_holdModEnv = 27,
	GEN_decayModEnv = 28,
	GEN_sustainModEnv = 29,
	GEN_releaseModEnv = 30,
	GEN_keynumToModEnvHold = 31,
	GEN_keynumToModEnvDecay = 32,
	GEN_delayVolEnv = 33,
	GEN_attackVolEnv = 34,
	GEN_holdVolEnv = 35,
	GEN_decayVolEnv = 36,
	GEN_sustainVolEnv = 37,
	GEN_releaseVolEnv = 38,
	GEN_keynumToVolEnvHold = 39,
	GEN_keynumToVolEnvDecay = 40,
	GEN_instrument = 41,
	GEN_reserved1 = 42,
	GEN_keyRange = 43,
	GEN_velRange = 44,
	GEN_startloopAddrsCoarseOffset = 45,
	GEN_keynum = 46,
	GEN_velocity = 47,
	GEN_initialAttenuation = 48,
	GEN_reserved2 = 49,
	GEN_endloopAddrsCoarseOffset = 50,
	GEN_coarseTune = 51,
	GEN_fineTune = 52,
	GEN_sampleID = 53,
	GEN_sampleModes = 54,
	GEN_reserved3 = 55,
	GEN_scaleTuning = 56,
	GEN_exclusiveClass = 57,
	GEN_overridingRootKey = 58,
	GEN_unused5 = 59,
	GEN_endOper = 60
} GenOperStrict;

static char const *gen_oper_to_str(unsigned gen_oper) {
	switch ((GenOperStrict)gen_oper) {
	case GEN_startAddrsOffset: return "startAddrsOffset";
	case GEN_endAddrsOffset: return "endAddrsOffset";
	case GEN_startloopAddrsOffset: return "startloopAddrsOffset";
	case GEN_endloopAddrsOffset: return "endloopAddrsOffset";
	case GEN_startAddrsCoarseOffset: return "startAddrsCoarseOffset";
	case GEN_modLfoToPitch: return "modLfoToPitch";
	case GEN_vibLfoToPitch: return "vibLfoToPitch";
	case GEN_modEnvToPitch: return "modEnvToPitch";
	case GEN_initialFilterFc: return "initialFilterFc";
	case GEN_initialFilterQ: return "initialFilterQ";
	case GEN_modLfoToFilterFc: return "modLfoToFilterFc";
	case GEN_modEnvToFilterFc: return "modEnvToFilterFc";
	case GEN_endAddrsCoarseOffset: return "endAddrsCoarseOffset";
	case GEN_modLfoToVolume: return "modLfoToVolume";
	case GEN_unused1: return "unused1";
	case GEN_chorusEffectsSend: return "chorusEffectsSend";
	case GEN_reverbEffectsSend: return "reverbEffectsSend";
	case GEN_pan: return "pan";
	case GEN_unused2: return "unused2";
	case GEN_unused3: return "unused3";
	case GEN_unused4: return "unused4";
	case GEN_delayModLFO: return "delayModLFO";
	case GEN_freqModLFO: return "freqModLFO";
	case GEN_delayVibLFO: return "delayVibLFO";
	case GEN_freqVibLFO: return "freqVibLFO";
	case GEN_delayModEnv: return "delayModEnv";
	case GEN_attackModEnv: return "attackModEnv";
	case GEN_holdModEnv: return "holdModEnv";
	case GEN_decayModEnv: return "decayModEnv";
	case GEN_sustainModEnv: return "sustainModEnv";
	case GEN_releaseModEnv: return "releaseModEnv";
	case GEN_keynumToModEnvHold: return "keynumToModEnvHold";
	case GEN_keynumToModEnvDecay: return "keynumToModEnvDecay";
	case GEN_delayVolEnv: return "delayVolEnv";
	case GEN_attackVolEnv: return "attackVolEnv";
	case GEN_holdVolEnv: return "holdVolEnv";
	case GEN_decayVolEnv: return "decayVolEnv";
	case GEN_sustainVolEnv: return "sustainVolEnv";
	case GEN_releaseVolEnv: return "releaseVolEnv";
	case GEN_keynumToVolEnvHold: return "keynumToVolEnvHold";
	case GEN_keynumToVolEnvDecay: return "keynumToVolEnvDecay";
	case GEN_instrument: return "instrument";
	case GEN_reserved1: return "reserved1";
	case GEN_keyRange: return "keyRange";
	case GEN_velRange: return "velRange";
	case GEN_startloopAddrsCoarseOffset: return "startloopAddrsCoarseOffset";
	case GEN_keynum: return "keynum";
	case GEN_velocity: return "velocity";
	case GEN_initialAttenuation: return "initialAttenuation";
	case GEN_reserved2: return "reserved2";
	case GEN_endloopAddrsCoarseOffset: return "endloopAddrsCoarseOffset";
	case GEN_coarseTune: return "coarseTune";
	case GEN_fineTune: return "fineTune";
	case GEN_sampleID: return "sampleID";
	case GEN_sampleModes: return "sampleModes";
	case GEN_reserved3: return "reserved3";
	case GEN_scaleTuning: return "scaleTuning";
	case GEN_exclusiveClass: return "exclusiveClass";
	case GEN_overridingRootKey: return "overridingRootKey";
	case GEN_unused5: return "unused5";
	case GEN_endOper: return "endOper";
	}
	return "???";
}


/* 
	timecents are a logarithmic unit of time, much like decibels are a logarithmic unit of sound
	intensity. 0 timecents = 1 second
	n timecents = (twelve hundredth root of two) ^ n
*/
static double timecents_to_seconds(i16 timecents) {
	return pow(1.0005777895065548, timecents);

}

static u32 timecents_to_samples(i16 timecents, u32 sample_rate) {
	/* 
		@OPTIM: Could divide by 1200, multiply/divide by two as necessary, 
		then use floats instead of doubles to do the rest
	*/
	return (u32)(sample_rate * timecents_to_seconds(timecents));
}

static void print_gen(Generator *gen) {
	u16 oper = gen->oper;
	printf("Operation: %s\n", gen_oper_to_str(oper));
	printf("Amount: ");
	GenAmount amount = gen->amount;
	if (oper == GEN_keyRange) {
		printf("%u-%u\n", amount.range.lo, amount.range.hi);
	} else if (oper == GEN_pan) {
		printf("%d\n", amount.sint);
	} else if (oper == GEN_delayVolEnv || oper == GEN_attackVolEnv || oper == GEN_holdVolEnv || oper == GEN_decayVolEnv
		|| oper == GEN_releaseVolEnv) {
		printf("%f seconds\n", timecents_to_seconds(amount.sint));
	} else {
		printf("%u\n", amount.uint);
	}
}

static void load_instrument(SoundFont *sndfont, Instrument *inst) {
	FILE *fp = sndfont->fp;
	Generator *igens = sndfont->igens;
	SampleHdr *shdrs = sndfont->shdrs;
	GenZone *zone = inst->gen_zones;
	u32 ngen_zones = inst->ngen_zones;
//	printf("-----Instrument %s has-----\n", inst->name);
	for (u32 z = 0; z < ngen_zones; ++z, ++zone) {
//		printf("--Zone %u/%u\n", 1+(unsigned)z, (unsigned)ngen_zones);
		u32 start = zone->start, end = zone->end;
		assert(end <= sndfont->nigens);
		Generator *gen = &igens[start];
		i16 pan = 0;
		u8 key_lo = 1;
		u8 key_hi = 0;
		u16 root_key = U16_MAX;
		u16 sample_id = 0;
		for (u32 i = start; i < end; ++i, ++gen) {
			GenAmount amount = gen->amount;
			print_gen(gen);
			switch (gen->oper) {
			case GEN_keyRange:
				key_lo = amount.range.lo;
				key_hi = amount.range.hi;
				break;
			case GEN_pan:
				pan = amount.sint;
				break;
			case GEN_sampleID:
				sample_id = amount.uint;
				break;
			case GEN_overridingRootKey:
				root_key = amount.uint;
			}
		}

		// i dunno what this generator's doing
		if (key_lo > key_hi) 
			continue;

		if (root_key == U16_MAX) {
			// just guess
			warn("Root key not specified for instrument %s. Guessing.", inst->name);
			root_key = (u16)((key_lo + key_hi) / 2);
		}
		
		assert(sample_id < sndfont->nshdrs);
		SampleHdr *hdr = &shdrs[sample_id];
		Samples *samples = NULL;
		size_t const bytes_per_sample = sizeof *samples->data;
		u32 nsamples = hdr->count;
		size_t bytes = bytes_per_sample * nsamples;
		samples = calloc(1, sizeof *samples + bytes);
		samples->pitch = (u8)root_key;
		samples->sample_rate = hdr->sample_rate;
		samples->count = nsamples;
		{
			u32 start_sample = hdr->start;
			fseek(fp, sndfont->sdta_offset, SEEK_SET);
			fseek(fp, (long)start_sample * (long)bytes_per_sample, SEEK_CUR);
			fread(samples->data, bytes_per_sample, nsamples, fp);
		}

		//printf("%u used for %u-%u\n", samples->pitch, key_lo, key_hi);
		if (pan <= 0) {
			for (u8 k = key_lo; k <= key_hi; ++k) {
				inst->samples[2*k] = samples;
			}
		}
		if (pan >= 0) {
			for (u8 k = key_lo; k <= key_hi; ++k) {
				inst->samples[2*k+1] = samples;
			}
		}
	}

	// at this point, there could be gaps in the samples, so we can fill them in nearby notes

	Samples *first_nonnull_L = NULL, *first_nonnull_R = NULL;
	for (int i = 0; i < 256; i += 2) {
		// fix it if there's one channel for a note, but not the other
		if (inst->samples[i] && !inst->samples[i+1]) {
			warn("Missing right channel for note %d. Using left.", i);
			inst->samples[i+1] = inst->samples[i];
		}
		if (!inst->samples[i] && inst->samples[i+1]) {
			warn("Missing left channel for note %d. Using right.", i);
			inst->samples[i] = inst->samples[i+1];
		}

		if (!first_nonnull_L && inst->samples[i]) {
			first_nonnull_L = inst->samples[i];
			first_nonnull_R = inst->samples[i+1];
		}
	}

	if (!first_nonnull_L) {
		warn("No samples for instrument %s.", inst->name);
		return;
	}

	Samples *last_sample_L = first_nonnull_L, *last_sample_R = first_nonnull_R;

	for (int i = 0; i < 256; i += 2) {	
		if (!inst->samples[i]) {
			inst->samples[i] = last_sample_L;
			inst->samples[i+1] = last_sample_R;
		} else {
			last_sample_L = inst->samples[i];
			last_sample_R = inst->samples[i+1];
		}
	}

	for (int i = 0; i < 256; i += 2) {
		assert(inst->samples[i] && inst->samples[i+1]);
		if (inst->samples[i]->sample_rate != inst->samples[i+1]->sample_rate) {
			warn("Sample rate mismatch in soundfont between left and right channels. Using left.");
			inst->samples[i+1] = inst->samples[i];
		}
	}
	inst->samples_loaded = true;
}


// for testing, doesn't do stereo
static void write_samples(FILE *file, u32 target_sample_rate, Samples *samples, u8 pitch, u8 vel) {
	assert(pitch < 128 && vel < 128);
	u32 playback_sample_rate = samples->sample_rate;
	u32 count = samples->count;
	int pitch_diff = pitch - samples->pitch;
	double sample_rate_multiplier = pow(2.0, pitch_diff / 12.0);
	playback_sample_rate = (u32)(playback_sample_rate * sample_rate_multiplier);
	i16 *data = samples->data;
	for (u32 i = 0; ; ++i) {
		u32 src_idx = (u32)(((u64)i * playback_sample_rate) / target_sample_rate);
		if (src_idx >= count) break;
		i16 sample = (i16)(((i32)data[src_idx] * vel) / 127);
		fwrite(&sample, sizeof sample, 1, file);
	}
}

// for testing, doesn't do stereo
static inline void write_note(FILE *file, u32 sample_rate, Instrument *instrument, u8 pitch, u8 vel) {
	write_samples(file, sample_rate, instrument->samples[pitch * 2], pitch, vel);
}

static void read_sound_font(FILE *fp, SoundFont *sound_font, bool verbose) {
	sound_font->fp = fp;
	// RIFF chunk
	char riff[5] = {0};
	fread(riff, 1, 4, fp);
	if (strncmp(riff, "RIFF", 4) != 0) {
		die("invalid soundfont file: no RIFF.");
	}
	u32 riff_size = read_u32(fp);
	(void)riff_size;
	// sbfk chunk
	char sbfk[5] = {0};
	fread(sbfk, 1, 4, fp);
	if (strncmp(sbfk, "sfbk", 4) != 0) {
		die("invalid soundfont file: no sfbk.");
	}
	// info LIST chunk
	char info_list[5] = {0};
	fread(info_list, 1, 4, fp);
	if (strncmp(info_list, "LIST", 4) != 0) {
		die("invalid soundfont file: no LIST.");
	}
	u32 info_size = read_u32(fp);
	long info_list_start = ftell(fp);
	char info[5] = {0};
	fread(info, 1, 4, fp);
	if (strncmp(info, "INFO", 4) != 0) {
		die("invalid soundfont file: no INFO.");
	}
	char ifil[5] = {0};
	fread(ifil, 1, 4, fp);
	if (strncmp(ifil, "ifil", 4) != 0) {
		die("invalid soundfont file: no ifil.");
	}
	u32 ifil_size = read_u32(fp);
	if (ifil_size != 4) {
		die("invalid soundfont file: wrong ifil size.");
	}
	u16 vmajor = read_u16(fp);
	u16 vminor = read_u16(fp);

	if (verbose) printf("SoundFont version %u.%u\n", vmajor, vminor);
	if (vmajor != 2) {
		warn("SoundFont is not version 2, but version %u.", vmajor);
	}
	char isng[5] = {0};
	fread(isng, 1, 4, fp);
	if (strncmp(isng, "isng", 4) != 0) {
		die("invalid soundfont file: no isng.");
	}
	u32 isng_size = read_u32(fp);
	
	{
		char *sng = calloc(1, isng_size);
		fread(sng, 1, isng_size, fp);
		if (verbose) printf("Optimized for %s.\n", sng);
		free(sng);
	}
	char inam[5] = {0};
	fread(inam, 1, 4, fp);
	if (strncmp(inam, "INAM", 4) != 0) {
		die("invalid soundfont file: no INAM.");
	}
	u32 inam_size = read_u32(fp);
	{
		char *nam = calloc(1, inam_size);
		fread(nam, 1, inam_size, fp);
		if (verbose) printf("Sound bank: %s.\n", nam);
		free(nam);
	}

	// skip optional info
	fseek(fp, info_list_start + (long)info_size, SEEK_SET);
	char sdta_list[5] = {0};
	fread(sdta_list, 1, 4, fp);
	if (strncmp(sdta_list, "LIST", 4) != 0) {
		die("invalid soundfont file: no sdta list.");
	}
	u32 sdta_size = read_u32(fp);
	long sdta_list_start = ftell(fp);

	char sdta[5] = {0};
	fread(sdta, 1, 4, fp);
	if (strncmp(sdta, "sdta", 4) != 0) {
		die("Invalid soundfont file: no sdta.");
	}
	
	// 16-bit samples
	char smpl[5] = {0};
	fread(smpl, 1, 4, fp);
	if (strncmp(smpl, "smpl", 4) != 0) {
		die("Invalid soundfont file: no smpl.");
	}

	u32 smpl_size = read_u32(fp);
	u32 bytes_per_sample = 2;
	u32 nsamples = smpl_size / bytes_per_sample;
	sound_font->nsamples = nsamples;
	sound_font->sdta_offset = ftell(fp);
	// these files are big, don't read it into memory until necessary
	fseek(fp, (long)smpl_size + ftell(fp), SEEK_SET);
	#if 0
	i16 *samples = malloc(smpl_size);
	if (verbose) printf("Reading 16-bit samples.\n");
	fread(samples, bytes_per_sample, nsamples, fp);
	#endif

	long sdta_list_end = sdta_list_start + (long)sdta_size;
	// could read 24-bit samples
	fseek(fp, sdta_list_end, SEEK_SET);

	char pdta_list[5] = {0};
	fread(pdta_list, 1, 4, fp);
	if (strncmp(pdta_list, "LIST", 4) != 0) {
		die("Invalid soundfont file: no pdta LIST.");
	}
	u32 pdta_size = read_u32(fp);
	(void)pdta_size;
	char pdta[5] = {0};
	fread(pdta, 1, 4, fp);
	if (strncmp(pdta, "pdta", 4) != 0) {
		die("Invalid soundfont file: no pdta.");
	}
	
	// phdr chunk
	char phdr[5] = {0};
	fread(phdr, 1, 4, fp);
	if (strncmp(phdr, "phdr", 4) != 0) {
		die("Invalid soundfont file: no phdr.");
	}
	u32 phdr_size = read_u32(fp);
	if (phdr_size % 38 != 0) {
		die("Invalid soundfont file: phdr size is not a multiple of 38.");
	}
	u32 npresets = phdr_size / 38;
	Preset *presets = calloc(npresets, sizeof *presets);
	Preset *preset = presets;
	for (u32 i = 0; i < npresets; ++i, ++preset) {
		fread(preset->name, 1, 20, fp);
		preset->preset = read_u16(fp);
		preset->bank = read_u16(fp);
		preset->bag_ndx = read_u16(fp);
		preset->library = read_u32(fp);
		preset->genre = read_u32(fp);
		preset->morphology = read_u32(fp);
	#if 0
		if (verbose) {
			printf("---Preset %u/%u: %s---\n", (unsigned)i + 1, (unsigned)npresets, preset->name);	
			printf("Preset - %u\n", preset->preset);
			printf("Bank - %u\n", preset->bank);
			printf("Preset bag ndx - %u\n", preset->bag_ndx);
			printf("Library - %u\n", (unsigned)preset->library);
			printf("Genre - %u\n", (unsigned)preset->genre);
			printf("Morphology - %u\n", (unsigned)preset->morphology);
		}
	#endif
	}

	// pbag chunk
	char pbag[5] = {0};
	fread(pbag, 1, 4, fp);
	if (strncmp(pbag, "pbag", 4) != 0) {
		die("Invalid soundfont file: no pbag.");
	}
	u32 pbag_size = read_u32(fp);
	fseek(fp, (long)pbag_size + ftell(fp), SEEK_SET);
#if 0
	(void)pbag_size;
	long pbag_start = ftell(fp);
	preset = presets;
	for (u32 i = 0; i < npresets-1 /* -1 for terminating preset */; ++i, ++preset) {
		preset->nbags = (u32)(preset[1].bag_ndx - preset->bag_ndx);
		preset->bags = calloc(preset->nbags, sizeof *preset->bags);
		PresetBag *bag = preset->bags;
		for (u32 j = 0; j < preset->nbags; ++j, ++bag) {
			bag->gen_ndx = read_u16(fp);
			bag->mod_ndx = read_u16(fp);
		#if 0
			if (verbose) printf("Bag for %s: Gen - %u Mod - %u\n", preset->name, bag->gen_ndx, bag->mod_ndx);
		#endif
		}
	}
	read_u32(fp); // terminating pbag

	if (pbag_start + (long)pbag_size != ftell(fp)) {
		warn("Wrong pbag size.");
		fseek(fp, pbag_start + (long)pbag_size, SEEK_SET);
	}
#endif

	// pmod chunk
	char pmod[5] = {0};
	fread(pmod, 1, 4, fp);
	if (strncmp(pmod, "pmod", 4) != 0) {
		die("Invalid soundfont file: no pmod.");
	}
	u32 pmod_size = read_u32(fp);
	u32 npmods = pmod_size / 10;
	if (verbose) printf("There are %u preset modulators\n", (unsigned)npmods);
	// skip PMOD chunk
	fseek(fp, (long)pmod_size + ftell(fp), SEEK_SET);
	
	// pgen chunk
	char pgen[5] = {0};
	fread(pgen, 1, 4, fp);
	if (strncmp(pgen, "pgen", 4) != 0) {
		die("Invalid soundfont file: no pgen.");
	}
	u32 pgen_size = read_u32(fp);
	u32 npgens = pgen_size / 4;
	if (verbose) printf("There are %u preset generators\n", (unsigned)npgens - 1);
	for (u32 i = 0; i < npgens; ++i) {
		u16 gen_oper = read_u16(fp);
		(void)gen_oper;
		GenAmount amount;
		fread(&amount, sizeof amount, 1, fp);
	}

	// inst chunk
	char inst[5] = {0};
	fread(inst, 1, 4, fp);
	if (strncmp(inst, "inst", 4) != 0) {
		die("Invalid soundfont file: no inst.");
	}
	u32 inst_size = read_u32(fp);
	u32 ninsts = inst_size / 22;
	Instrument *insts = calloc(ninsts, sizeof *insts);
	Instrument *instrument = insts;
	for (u32 i = 0; i < ninsts; ++i, ++instrument) {
		fread(instrument->name, 1, 20, fp);
		instrument->bag_ndx = read_u16(fp);
	#if 0
		if (verbose) {
			printf("---Instrument %u/%u: %s---", (unsigned)i+1, (unsigned)ninsts, name);
			printf("Bag %u\n", bag_ndx);
		}
	#endif
	}
	sound_font->insts = insts;
	sound_font->ninsts = ninsts;


	// ibag chunk
	char ibag[5] = {0};
	fread(ibag, 1, 4, fp);
	if (strncmp(ibag, "ibag", 4) != 0) {
		die("Invalid soundfont file: no ibag.");
	}
	u32 ibag_size = read_u32(fp);
	(void)ibag_size;
	long ibag_start = ftell(fp);
	instrument = insts;
	u32 nibags = (u32)(insts[ninsts-1].bag_ndx + 1 /* terminating */);
	Bag *ibags = calloc(nibags, sizeof *ibags);
	{
		Bag *bag = ibags;
		for (u32 i = 0; i < nibags; ++i, ++bag) {
			bag->gen_ndx = read_u16(fp);
			bag->mod_ndx = read_u16(fp);
		}
	}

	instrument = insts;
	for (u32 i = 0; i < ninsts-1; ++i, ++instrument) {
		u32 nbags = (u32)(instrument[1].bag_ndx - instrument->bag_ndx);
		instrument->ngen_zones = nbags;
		GenZone *gen_zones = instrument->gen_zones = calloc(instrument->ngen_zones, sizeof *gen_zones);
		GenZone *gen_zone = gen_zones;
		Bag *bag = &ibags[instrument->bag_ndx];
		for (u32 j = 0; j < nbags; ++j, ++gen_zone, ++bag) {
			gen_zone->start = bag->gen_ndx;
			gen_zone->end = bag[1].gen_ndx;
		}
	}

	if (ibag_start + (long)ibag_size != ftell(fp)) {
		warn("Wrong ibag size, expected %ld but got %ld.", (long)ibag_size, ftell(fp) - ibag_start);
		fseek(fp, ibag_start + (long)ibag_size, SEEK_SET);
	}



	// imod chunk
	char imod[5] = {0};
	fread(imod, 1, 4, fp);
	if (strncmp(imod, "imod", 4) != 0) {
		die("Invalid soundfont file: no imod.");
	}
	u32 imod_size = read_u32(fp);
	u32 nimods = imod_size / 10;
	if (verbose) printf("There are %u instrument modulators\n", (unsigned)nimods - 1);
	// skip IMOD chunk
	fseek(fp, (long)imod_size + ftell(fp), SEEK_SET);

	// igen chunk
	char igen[5] = {0};
	fread(igen, 1, 4, fp);
	if (strncmp(igen, "igen", 4) != 0) {
		die("Invalid soundfont file: no igen.");
	}
	u32 igen_size = read_u32(fp);
	u32 nigens = igen_size / 4;
	Generator *igens = calloc(nigens, sizeof *igens);
	Generator *gen = igens;
	if (verbose) printf("There are %u instrument generators\n", (unsigned)nigens - 1);
	for (u32 i = 0; i < nigens; ++i, ++gen) {
		gen->oper = read_u16(fp);
		fread(&gen->amount, sizeof gen->amount, 1, fp);

#if 0
		if (verbose) {
			printf("---Generator %u/%u---\n", (unsigned)i+1, (unsigned)nigens);
			print_gen(gen);
		}
#endif
	}
	sound_font->igens = igens;
	sound_font->nigens = nigens;

	char shdr[5] = {0};
	fread(shdr, 1, 4, fp);
	if (strncmp(shdr, "shdr", 4) != 0) {
		die("Invalid soundfont file: no shdr.");
	}
	u32 shdr_size = read_u32(fp);
	u32 nshdrs = shdr_size / 46;
	SampleHdr *shdrs = calloc(nshdrs, sizeof *shdrs);
	SampleHdr *sample = shdrs;
	for (u32 i = 0; i < nshdrs; ++i, ++sample) {
		char *name = sample->name;
		fread(name, 1, 20, fp);
		u32 start = read_u32(fp);
		u32 end = read_u32(fp);
		u32 start_loop = read_u32(fp);
		u32 end_loop = read_u32(fp);
		u32 sample_rate = read_u32(fp);
		u8 pitch = read_u8(fp);
		i8 pitch_correction = read_i8(fp);
		u16 sample_link = read_u16(fp);
		u16 sample_type = read_u16(fp);
		if (i == nshdrs-1) break;
		(void)start; (void)end; (void)start_loop;
		(void)end_loop; (void)sample_rate; (void)pitch;
		(void)pitch_correction; (void)sample_link; (void)sample_type;
		
		sample->start = start;
		sample->count = end - start;
		sample->start_loop = start_loop;
		sample->end_loop = end_loop;
		sample->sample_rate = sample_rate;
	#if 0
		if (verbose) {
			printf("---Sample %u/%u: %s---\n", (unsigned)i+1, (unsigned)nshdrs, name);
			printf("Samples: %u-%u\n", (unsigned)start, (unsigned)end-1);
			printf("Loop: %u-%u\n", (unsigned)start_loop, (unsigned)end_loop-1);
			printf("Sample rate: %u\n", (unsigned)sample_rate);
			printf("Pitch: %u Correction: %d\n", pitch, pitch_correction);
			printf("Sample link: %u Type: %u\n", sample_link, sample_type);
		}
	#endif
		if (pitch_correction != 0) {
			warn("Sample has pitch correction, but I'm not gonna deal with it.");
		}
		assert(end < nsamples && start < end);
		//if (sample_rate == 32000 && strstr(name, "Piano")) {
	}
	sound_font->shdrs = shdrs;
	sound_font->nshdrs = nshdrs;
}

static time_t start_second;

static void time_init(void) {
	struct timespec timespec = {0};
	clock_gettime(CLOCK_MONOTONIC, &timespec);
	start_second = timespec.tv_sec;
}

static u64 time_ns(void) {
	struct timespec timespec = {0};
	clock_gettime(CLOCK_MONOTONIC, &timespec);
	return (u64)(timespec.tv_sec - start_second) * 1000000000 + (u64)timespec.tv_nsec;
}

typedef struct {
	bool exists;
	u8 vel;
	bool dampened;
	bool down; // this can be different from dampened if the sustain pedal is down
	float dampening; // how much it's been dampened
	u32 pos; // which sample we are on
} Note;

typedef struct {
	u32 out_wav_samples_written;
	FILE *out_wav;
	pthread_mutex_t mutex;
	snd_pcm_t *pcm;
	Instrument *instrument;
	u32 sample_rate;
	Note notes[128]; // [i] = Note #i
} SoundThreadData;

static inline void sound_lock(SoundThreadData *sound) {
	pthread_mutex_lock(&sound->mutex);
}
static inline void sound_unlock(SoundThreadData *sound) {
	pthread_mutex_unlock(&sound->mutex);
}

static void *sound_thread(void *vdata) {
	SoundThreadData *data = vdata;
	snd_pcm_t *pcm = data->pcm;

	while (1) {
#define nframes 441
		float frames_fL[nframes] = {0.0f}, frames_fR[nframes] = {0.0f};
		i16 frames_L[nframes], frames_R[nframes];
		float t_iter = (float)nframes / (float)data->sample_rate;
		void *datas[] = {frames_L, frames_R};
		sound_lock(data);
		Instrument *instrument = data->instrument;
		u8 n = 0;
		for (Note *note = data->notes; n < 128; ++note, ++n) {
			if (!note->exists) continue;
			Samples *samples_L = instrument->samples[2*n];
			Samples *samples_R = instrument->samples[2*n+1];
			if (!samples_L || !samples_R) {
				die("No samples for %d sorry (%p %p).", n, samples_L, samples_R);
			}
			i16 *in_L = samples_L->data;
			i16 *in_R = samples_R->data;
			if (samples_R->count != samples_L->count) {
				warn("Sample count for left channel doesn't match sample count for right channel.");
				in_R = in_L;
			}
			u32 pos = note->pos;

			u32 sample_frames = samples_L->count;
			int pitch_diff = n - samples_L->pitch;
			float time_multiplier = (float)samples_L->sample_rate / (float)data->sample_rate * powf(2.0f, (float)pitch_diff / 12.0f); // stretch factor of input
			i64 frames_left_in_sample = (i64)sample_frames - pos;
			if (frames_left_in_sample <= 0) {
				note->exists = false;
				continue;
			}
			{
				float *out_L = frames_fL, *out_R = frames_fR;
				float volume = (float)note->vel / 128.0f;
				if (note->dampened) {
					note->dampening *= powf(0.05f, t_iter);
				}
				volume *= note->dampening; // this needs to be here always in case we get note off then sustain pedal off
				float *out_end = frames_fL + nframes;
				float in_idx = (float)pos;
				volume /= 32767.0f; // turn 16-bit signed samples into floating point
				for (; out_L < out_end; ++out_L, ++out_R, in_idx += time_multiplier) {
					u32 ii = (u32)in_idx;
					if (ii >= sample_frames) {
						break;
					}
					i16 iL = in_L[ii];
					i16 iR = in_R[ii];
					*out_L += ((float)iL * volume);
					*out_R += ((float)iR * volume);
				}
				note->pos = (u32)in_idx;
				
			}

			if (note->pos + (u32)time_multiplier >= sample_frames) {
				note->exists = false;
			}
			//printf("NOTE: %d\n", note->note);
		}
		sound_unlock(data);
		// normalization
		float max_sample_L = 1.0f; // default to 1 so we don't make quiet things loud
		float max_sample_R = 1.0f; 
		for (u32 i = 0; i < nframes; ++i) {
			float l = fabsf(frames_fL[i]);
			if (l > max_sample_L)
				max_sample_L = l;
			float r = fabsf(frames_fR[i]);
			if (r > max_sample_R)
				max_sample_R = r;
		}
		float multiplier_L = 32767.0f / max_sample_L;
		float multiplier_R = 32767.0f / max_sample_R;
		for (u32 i = 0; i < nframes; ++i) {
			frames_L[i] = (i16)(multiplier_L * frames_fL[i]);
			frames_R[i] = (i16)(multiplier_R * frames_fR[i]);
		}
		
		if (data->out_wav) {
			sound_lock(data);
			if (data->out_wav_samples_written < U32_MAX) {
				u64 new_samples_written = (u64)data->out_wav_samples_written + nframes;
				if (new_samples_written * 2 /* channels */ * sizeof *frames_L >= U32_MAX - 100 /* to be safe */) {
					warn("So... wav files can only have 4GB of audio data, and you've passed that. Stop recording now and start again.");
					data->out_wav_samples_written = U32_MAX;
				}
				data->out_wav_samples_written = (u32)new_samples_written;
				for (u32 i = 0; i < nframes; ++i) {
					i16 things[] = {frames_L[i], frames_R[i]};
					fwrite(things, sizeof *things, 2, data->out_wav);
				}
			}
			sound_unlock(data);
		}

		snd_pcm_sframes_t frames = snd_pcm_writen(pcm, datas, nframes);
		if (frames < 0)
			frames = snd_pcm_recover(pcm, (int)frames, 0);
		if (frames < 0) {
			printf("snd_pcm_writei failed: %s\n", snd_strerror((int)frames));
			break;
		}
		if (frames > 0 && frames < (snd_pcm_sframes_t)nframes) {
			printf("Short write (expected %ld, wrote %ld)\n", (long)nframes, (long)frames);
		}
	}
	return NULL;
#undef nframes
}

int main(void) {
	time_init();
	

	FILE *sndfont_fp = fopen("/usr/share/sounds/sf2/FluidR3_GM.sf2", "rb");
	if (!sndfont_fp) {
		die("Couldn't open soundfont file.");
	}
	SoundFont sound_font = {0};
	read_sound_font(sndfont_fp, &sound_font, false);
	
	Instrument *instrument = NULL;
	u32 ninsts = sound_font.ninsts;
	if (ninsts < 1) {
		die("No instruments. Your soundfont file is probably corrupted.");
	} else if (ninsts == 2) {
		instrument = &sound_font.insts[0];
	} else {
		Instrument *default_inst = NULL;
		printf("Select an instrument:\n");
		for (u32 i = 0; i < ninsts - 1 /* EOI */; ++i) {
			Instrument *inst = &sound_font.insts[i];
			char *name = inst->name;
			printf("[%u] %s\n", i+1, name);
			if (!default_inst && (strstr(name, "Piano") || strstr(name, "piano"))) {
				default_inst = inst;
			}
		}
		if (!default_inst) default_inst = &sound_font.insts[0];
		printf("Instrument [default: %s]: ", default_inst->name);
		fflush(stdout);
		
		char line[64];
		fgets(line, sizeof line, stdin);
		char *end = NULL;
		long inum = strtol(line, &end, 10);
		if (end == line || inum < 1 || inum > ninsts)
			instrument = default_inst;
		else
			instrument = &sound_font.insts[inum-1];
	}

	printf("Selecting instrument %s.\n", instrument->name);

	load_instrument(&sound_font, instrument);
	fclose(sndfont_fp);
	if (!instrument->samples_loaded) {
		die("That instrument has no samples. Your soundfont file doesn't actually support it, it seems.");
	}

#if 0
	FILE *out = fopen("out", "wb");
	for (u8 pitch = 0; pitch < 128; ++pitch)
		write_note(out, 44100, instrument, pitch, 127);
	fclose(out);
#endif
	
	snd_pcm_t *pcm = NULL;
	SoundThreadData sound = {0};
	{
		int err = 0;
		char const *audio_output_device = "default";
		if ((err = snd_pcm_open(&pcm, audio_output_device, SND_PCM_STREAM_PLAYBACK, 0)) < 0) {
			die("Playback open error: %s\n", snd_strerror(err));
		}
		sound.sample_rate = 44100;
		if ((err = snd_pcm_set_params(pcm, SND_PCM_FORMAT_S16, SND_PCM_ACCESS_RW_NONINTERLEAVED,
			2, sound.sample_rate, 1, 10000)) < 0) {
			die("Audio set params error: %s\n", snd_strerror(err));
		}
		snd_pcm_nonblock(pcm, 0); // always block

		sound.pcm = pcm;
		sound.instrument = instrument;
		pthread_mutex_init(&sound.mutex, NULL);

		pthread_t sound_pthread;
		if ((err = pthread_create(&sound_pthread, NULL, sound_thread, &sound))) {
			die("Couldn't create thread (error %d).", err);
		}
	}

	char const *snd_dir = "/dev/snd";
	DIR *dir = opendir(snd_dir);
	// @TODO: option to use alsa
	if (!dir) {
		die("No %s. Can't find midi devices.", snd_dir);
	}
	struct dirent *ent = NULL;
	
	char *devices[100] = {0};
	unsigned ndevices = 0;
	while ((ent = readdir(dir))) {
		char *name = ent->d_name;
		if (strncmp(name, "midi", 4) == 0) {
			if (ndevices < 100) {
				devices[ndevices++] = strdup(name);
			}
		}
	}
	closedir(dir);

	char *device_name = NULL;
	if (ndevices == 0) {
		die("No midi devices found.");
	} else if (ndevices == 1) {
		device_name = devices[0];
	} else {
		printf("Please select a MIDI device:\n");
		for (unsigned i = 0; i < ndevices; ++i) 
			printf("[%u] %s\n", i+1, devices[i]);
		printf("Enter a number from %u to %u: ", 1, ndevices);
		fflush(stdout);
		unsigned device_num = 0;
		if (scanf("%u", &device_num) < 1 || device_num < 1 || device_num > ndevices)
			device_num = 1;
		--device_num;
		device_name = devices[device_num];
	}

	bool verbose = true;
	if (verbose) printf("Using midi device %s.\n", device_name);


	char *device_filename = malloc(32 + strlen(device_name));
	sprintf(device_filename, "/dev/snd/%s", device_name);

	FILE *device = fopen(device_filename, "rb");
	if (!device) {
		die("Couldn't access MIDI device %s.", device_filename);
	}
	
	bool sustain_pedal = false; // is the sustain pedal down?

	while (1) {
		int c = getc(device);
		if (c == EOF) break;
		if (!(c & 0x80)) continue; // data
		int top4 = (c & 0xf0) >> 4;
		switch (top4) {
		case 8: {
			// Note off
			// @TODO
			u8 n = (u8)getc(device);
			u8 v = (u8)getc(device);
			if (n > 127 || v > 127) break;
			sound_lock(&sound);
			Note *note = &sound.notes[n];
			if (note->exists) {
				note->down = false;
				if (!sustain_pedal) {
					note->dampened = true;
					note->dampening = 1.0f;
				}
			}
			sound_unlock(&sound);
		} break;
		case 9: {
			// Note on
			u8 n = (u8)getc(device);
			u8 v = (u8)getc(device);
			if (n > 127 || v > 127) break;
			if (feof(device)) break;
			sound_lock(&sound);
			Note *note = &sound.notes[n];
			if (note) {
				note->exists = true;
				note->vel = v;
				note->pos = 0;
				note->dampening = 1;
				note->dampened = false;
				note->down = true;
			}
			sound_unlock(&sound);

		} break;
		case 11: { // controller
			u8 controller = (u8)getc(device);
			u8 vel = (u8)getc(device);
			if (controller > 127 || vel > 127) break;
			if (controller == 64) {
				// sustain pedal
				sound_lock(&sound);
				if (vel == 0) { // oddly, 0 velocity is down (at least on my keyboard)
					sustain_pedal = true;
					for (Note *no = sound.notes, *end = no + 128; no < end; ++no) {
						no->dampened = false;
					}
				} else if (vel == 127) {
					sustain_pedal = false;
					for (Note *no = sound.notes, *end = no + 128; no < end; ++no) {
						if (!no->down) {
							no->dampened = true;
						}
					}
				}
				sound_unlock(&sound);
			} else if (controller == 48) {
				// record to wav
				sound_lock(&sound);
				if (vel == 127) {
					char filename[32] = {0};
					for (u32 i = 1;;++i) {
						snprintf(filename, sizeof filename-1, "out-%02d.wav", i);
						struct stat statbuf = {0};
						if (stat(filename, &statbuf) < 0) {
							if (errno == ENOENT) {
								break;
							} else {
								warn("stat failed not recording.");
								i = 0;
								break;
							}
						}
					}
					if (!*filename) break;
					printf("Recording to %s.\n", filename);
					FILE *out_wav = sound.out_wav = fopen(filename, "wb");
					fwrite("RIFF", 1, 4, out_wav);
					write_u32(out_wav, 0); // RIFF chunk size; we don't know this yet
					fwrite("WAVE", 1, 4, out_wav);
					fwrite("fmt ", 1, 4, out_wav);
					write_u32(out_wav, 16); // fmt  chunk size
					write_u16(out_wav, 1); // sample rate
					write_u16(out_wav, 2); // channels
					u32 sample_rate = sound.sample_rate;
					write_u32(out_wav, sample_rate); // sample rate
					write_u32(out_wav, sample_rate * 4); // byte rate (e.g. 44100 samples / sec * 2 bytes / sample * 2 channels)
					write_u16(out_wav, 4); // block align (2 channels * 2 bytes per sample)
					write_u16(out_wav, 16); // bits per sample
					fwrite("data", 1, 4, out_wav);
					write_u32(out_wav, 0); // don't know this yet
					assert(ftell(out_wav) == 44);
				} else {
					if (sound.out_wav) {
						u32 data_chunk_size = sound.out_wav_samples_written * 4; // = # samples * byte rate
						u32 riff_chunk_size = data_chunk_size + 36;
						FILE *out_wav = sound.out_wav;
						// fill in gaps
						fseek(out_wav, 4, SEEK_SET);
						write_u32(out_wav, riff_chunk_size);
						fseek(out_wav, 40, SEEK_SET);
						write_u32(out_wav, data_chunk_size);
						fclose(out_wav);
						printf("Done recording wave file.\n");
						sound.out_wav = NULL;
					}
				}
				sound_unlock(&sound);
			} else {
			#if 0
				printf("%u %u\n",controller,vel);
			#endif
			}
		} break;
		default:
		#if 0
			printf("%d\n", top4);
		#endif
			break;
		}
	}
	fclose(device);
	return 0;
}
