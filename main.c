#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <dirent.h>
#include <assert.h>
#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>

typedef int8_t i8;
typedef int16_t i16;
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;

static inline u32 read_u32(FILE *fp) {
	u32 x;
	fread(&x, sizeof x, 1, fp);
	return x;
}
static inline u16 read_u16(FILE *fp) {
	u16 x;
	fread(&x, sizeof x, 1, fp);
	return x;
}
static inline u8 read_u8(FILE *fp) {
	return (u8)getc(fp);
}
static inline i8 read_i8(FILE *fp) {
	return (i8)getc(fp);
}

static void die(char const *fmt, ...) {
	va_list args;
	fprintf(stderr, "Error: ");
	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);
	fprintf(stderr, "\n");
	exit(EXIT_FAILURE);
}

typedef struct {
	u16 gen_ndx;
	u16 mod_ndx;
} PresetBag;

typedef struct {
	char name[21]; // 21 because i'm not sure if it's necessarily null-terminated
	u16 preset, bank, bag_ndx;
	u32 library, genre, morphology;
	u32 nbags;
	PresetBag *bags;
} Preset;

typedef struct {
	u8 lo;
	u8 hi;
} Ranges;

typedef union {
	Ranges ranges;
	i16 iamount;
	u16 uamount;
} GenAmount;

int main(void) {
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

	char *device = NULL;
	if (ndevices == 0) {
		die("No midi devices found.");
	} else if (ndevices == 1) {
		device = devices[0];
	} else {
		printf("Please select a MIDI device:\n");
		for (unsigned i = 0; i < ndevices; ++i) 
			printf("[%u] %s\n", i+1, devices[i]);
		printf("Enter a number from %u to %u: ", 1, ndevices);
		fflush(stdout);
		unsigned device_num;
		scanf("%u", &device_num);
		--device_num;
		device = devices[device_num];
	}

	printf("Using midi device %s.\n", device);
	
	bool verbose = false;
	FILE *sndfont = fopen("/usr/share/sounds/sf2/FluidR3_GM.sf2", "rb");
	if (!sndfont) {
		die("Couldn't open soundfont file.");
	}
	// RIFF chunk
	char riff[5] = {0};
	fread(riff, 1, 4, sndfont);
	if (strncmp(riff, "RIFF", 4) != 0) {
		die("invalid soundfont file: no RIFF.");
	}
	u32 riff_size = read_u32(sndfont);
	(void)riff_size;
	// sbfk chunk
	char sbfk[5] = {0};
	fread(sbfk, 1, 4, sndfont);
	if (strncmp(sbfk, "sfbk", 4) != 0) {
		die("invalid soundfont file: no sfbk.");
	}
	// info LIST chunk
	char info_list[5] = {0};
	fread(info_list, 1, 4, sndfont);
	if (strncmp(info_list, "LIST", 4) != 0) {
		die("invalid soundfont file: no LIST.");
	}
	u32 info_size = read_u32(sndfont);
	long info_list_start = ftell(sndfont);
	char info[5] = {0};
	fread(info, 1, 4, sndfont);
	if (strncmp(info, "INFO", 4) != 0) {
		die("invalid soundfont file: no INFO.");
	}
	char ifil[5] = {0};
	fread(ifil, 1, 4, sndfont);
	if (strncmp(ifil, "ifil", 4) != 0) {
		die("invalid soundfont file: no ifil.");
	}
	u32 ifil_size = read_u32(sndfont);
	if (ifil_size != 4) {
		die("invalid soundfont file: wrong ifil size.");
	}
	u16 vmajor = read_u16(sndfont);
	u16 vminor = read_u16(sndfont);

	if (verbose) printf("SoundFont version %u.%u\n", vmajor, vminor);
	if (vmajor != 2) {
		fprintf(stderr, "Warning: SoundFont is not version 2, but version %u.\n", vmajor);
	}
	char isng[5] = {0};
	fread(isng, 1, 4, sndfont);
	if (strncmp(isng, "isng", 4) != 0) {
		die("invalid soundfont file: no isng.");
	}
	u32 isng_size = read_u32(sndfont);
	
	{
		char *sng = calloc(1, isng_size);
		fread(sng, 1, isng_size, sndfont);
		if (verbose) printf("Optimized for %s.\n", sng);
		free(sng);
	}
	char inam[5] = {0};
	fread(inam, 1, 4, sndfont);
	if (strncmp(inam, "INAM", 4) != 0) {
		die("invalid soundfont file: no INAM.");
	}
	u32 inam_size = read_u32(sndfont);
	{
		char *nam = calloc(1, inam_size);
		fread(nam, 1, inam_size, sndfont);
		if (verbose) printf("Sound bank: %s.\n", nam);
		free(nam);
	}

	// skip optional info
	fseek(sndfont, info_list_start + (long)info_size, SEEK_SET);
	char sdta_list[5] = {0};
	fread(sdta_list, 1, 4, sndfont);
	if (strncmp(sdta_list, "LIST", 4) != 0) {
		die("invalid soundfont file: no sdta list.");
	}
	u32 sdta_size = read_u32(sndfont);
	long sdta_list_start = ftell(sndfont);

	char sdta[5] = {0};
	fread(sdta, 1, 4, sndfont);
	if (strncmp(sdta, "sdta", 4) != 0) {
		die("Invalid soundfont file: no sdta.");
	}
	
	// 16-bit samples
	char smpl[5] = {0};
	fread(smpl, 1, 4, sndfont);
	if (strncmp(smpl, "smpl", 4) != 0) {
		die("Invalid soundfont file: no smpl.");
	}
	u32 smpl_size = read_u32(sndfont);
	i16 *samples = malloc(smpl_size);
	u32 bytes_per_sample = (u32)sizeof *samples;
	u32 nsamples = smpl_size / bytes_per_sample;
	if (verbose) printf("Reading 16-bit samples.\n");
	fread(samples, bytes_per_sample, nsamples, sndfont);
	#if 0
	for (u32 i = 0; i < nsamples; ++i) {
		putchar(samples[i] >> 8);
	}
	#endif

	long sdta_list_end = sdta_list_start + (long)sdta_size;
	if (ftell(sndfont) + 8 < sdta_list_end) {
		// read 24-bit samples
	}
	fseek(sndfont, sdta_list_end, SEEK_SET);
	char pdta_list[5] = {0};
	fread(pdta_list, 1, 4, sndfont);
	if (strncmp(pdta_list, "LIST", 4) != 0) {
		die("Invalid soundfont file: no pdta LIST.");
	}
	u32 pdta_size = read_u32(sndfont);
	(void)pdta_size;
	char pdta[5] = {0};
	fread(pdta, 1, 4, sndfont);
	if (strncmp(pdta, "pdta", 4) != 0) {
		die("Invalid soundfont file: no pdta.");
	}
	
	// phdr chunk
	char phdr[5] = {0};
	fread(phdr, 1, 4, sndfont);
	if (strncmp(phdr, "phdr", 4) != 0) {
		die("Invalid soundfont file: no phdr.");
	}
	u32 phdr_size = read_u32(sndfont);
	if (phdr_size % 38 != 0) {
		die("Invalid soundfont file: phdr size is not a multiple of 38.");
	}
	u32 npresets = phdr_size / 38;
	Preset *presets = calloc(npresets, sizeof *presets);
	Preset *preset = presets;
	for (u32 i = 0; i < npresets; ++i, ++preset) {
		fread(preset->name, 1, 20, sndfont);
		preset->preset = read_u16(sndfont);
		preset->bank = read_u16(sndfont);
		preset->bag_ndx = read_u16(sndfont);
		preset->library = read_u32(sndfont);
		preset->genre = read_u32(sndfont);
		preset->morphology = read_u32(sndfont);
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
	fread(pbag, 1, 4, sndfont);
	if (strncmp(pbag, "pbag", 4) != 0) {
		die("Invalid soundfont file: no pbag.");
	}
	u32 pbag_size = read_u32(sndfont);
	fseek(sndfont, (long)pbag_size + ftell(sndfont), SEEK_SET);
#if 0
	(void)pbag_size;
	long pbag_start = ftell(sndfont);
	preset = presets;
	for (u32 i = 0; i < npresets-1 /* -1 for terminating preset */; ++i, ++preset) {
		preset->nbags = (u32)(preset[1].bag_ndx - preset->bag_ndx);
		preset->bags = calloc(preset->nbags, sizeof *preset->bags);
		PresetBag *bag = preset->bags;
		for (u32 j = 0; j < preset->nbags; ++j, ++bag) {
			bag->gen_ndx = read_u16(sndfont);
			bag->mod_ndx = read_u16(sndfont);
		#if 0
			if (verbose) printf("Bag for %s: Gen - %u Mod - %u\n", preset->name, bag->gen_ndx, bag->mod_ndx);
		#endif
		}
	}
	read_u32(sndfont); // terminating pbag

	if (pbag_start + (long)pbag_size != ftell(sndfont)) {
		fprintf(stderr, "Warning: Wrong pbag size.\n");
		fseek(sndfont, pbag_start + (long)pbag_size, SEEK_SET);
	}
#endif

	// pmod chunk
	char pmod[5] = {0};
	fread(pmod, 1, 4, sndfont);
	if (strncmp(pmod, "pmod", 4) != 0) {
		die("Invalid soundfont file: no pmod.");
	}
	u32 pmod_size = read_u32(sndfont);
	u32 npmods = pmod_size / 10;
	if (verbose) printf("There are %u preset modulators\n", (unsigned)npmods);
	// skip PMOD chunk
	fseek(sndfont, (long)pmod_size + ftell(sndfont), SEEK_SET);
	
	// pgen chunk
	char pgen[5] = {0};
	fread(pgen, 1, 4, sndfont);
	if (strncmp(pgen, "pgen", 4) != 0) {
		die("Invalid soundfont file: no pgen.");
	}
	u32 pgen_size = read_u32(sndfont);
	u32 npgens = pgen_size / 4;
	if (verbose) printf("There are %u preset generators\n", (unsigned)npgens - 1);
	for (u32 i = 0; i < npgens; ++i) {
		u16 gen_oper = read_u16(sndfont);
		(void)gen_oper;
		GenAmount amount;
		fread(&amount, sizeof amount, 1, sndfont);
	}

	// inst chunk
	char inst[5] = {0};
	fread(inst, 1, 4, sndfont);
	if (strncmp(inst, "inst", 4) != 0) {
		die("Invalid soundfont file: no inst.");
	}
	u32 inst_size = read_u32(sndfont);
	u32 ninsts = inst_size / 22;
	for (u32 i = 0; i < ninsts; ++i) {
		char name[21] = {0};
		fread(name, 1, 20, sndfont);
		u16 bag_ndx = read_u16(sndfont);
		(void)bag_ndx;
	#if 0
		if (verbose) {
			printf("---Instrument %u/%u: %s---", (unsigned)i+1, (unsigned)ninsts, name);
			printf("Bag %u\n", bag_ndx);
		}
	#endif
	}


	// ibag chunk
	char ibag[5] = {0};
	fread(ibag, 1, 4, sndfont);
	if (strncmp(ibag, "ibag", 4) != 0) {
		die("Invalid soundfont file: no ibag.");
	}
	u32 ibag_size = read_u32(sndfont);
	fseek(sndfont, (long)ibag_size + ftell(sndfont), SEEK_SET);


	// imod chunk
	char imod[5] = {0};
	fread(imod, 1, 4, sndfont);
	if (strncmp(imod, "imod", 4) != 0) {
		die("Invalid soundfont file: no imod.");
	}
	u32 imod_size = read_u32(sndfont);
	u32 nimods = imod_size / 10;
	if (verbose) printf("There are %u instrument modulators\n", (unsigned)nimods - 1);
	// skip IMOD chunk
	fseek(sndfont, (long)imod_size + ftell(sndfont), SEEK_SET);

	// igen chunk
	char igen[5] = {0};
	fread(igen, 1, 4, sndfont);
	if (strncmp(igen, "igen", 4) != 0) {
		die("Invalid soundfont file: no igen.");
	}
	u32 igen_size = read_u32(sndfont);
	u32 nigens = igen_size / 4;
	if (verbose) printf("There are %u instrument generators\n", (unsigned)nigens - 1);
	for (u32 i = 0; i < nigens; ++i) {
		u16 gen_oper = read_u16(sndfont);
		(void)gen_oper;
		GenAmount amount;
		fread(&amount, sizeof amount, 1, sndfont);
	}
	
	char shdr[5] = {0};
	fread(shdr, 1, 4, sndfont);
	if (strncmp(shdr, "shdr", 4) != 0) {
		die("Invalid soundfont file: no shdr.");
	}
	u32 shdr_size = read_u32(sndfont);
	u32 nshdrs = shdr_size / 46;
	FILE *out = fopen("out", "wb");
	for (u32 i = 0; i < nshdrs; ++i) {
		char name[21] = {0};
		fread(name, 1, 20, sndfont);
		u32 start = read_u32(sndfont);
		u32 end = read_u32(sndfont);
		u32 start_loop = read_u32(sndfont);
		u32 end_loop = read_u32(sndfont);
		u32 sample_rate = read_u32(sndfont);
		u8 pitch = read_u8(sndfont);
		i8 pitch_correction = read_i8(sndfont);
		u16 sample_link = read_u16(sndfont);
		u16 sample_type = read_u16(sndfont);
		if (i == nshdrs-1) break;
		if (verbose) {
			printf("---Sample %u/%u: %s---\n", (unsigned)i+1, (unsigned)nshdrs, name);
			printf("Samples: %u-%u\n", (unsigned)start, (unsigned)end-1);
			printf("Loop: %u-%u\n", (unsigned)start_loop, (unsigned)end_loop-1);
			printf("Sample rate: %u\n", (unsigned)sample_rate);
			printf("Pitch: %u Correction: %d\n", pitch, pitch_correction);
			printf("Sample link: %u Type: %u\n", sample_link, sample_type);
		}
		if (pitch_correction != 0) {
			fprintf(stderr, "Warning: Sample has pitch correction, but I'm not gonna deal with it.\n");
		}
		assert(end < nsamples && start < end);
		if (sample_rate == 32000 && strstr(name, "Piano")) {
			puts(name);
			for (u32 i = start; i < end; ++i)
				putc(samples[i] / 256, out);
			for (u32 i = 0; i < 32000; ++i)
				putc(0, out);
		}
	}
	fclose(out);
	fclose(sndfont);

	return 0;

	char *filename = malloc(32 + strlen(device));
	sprintf(filename, "/dev/snd/%s", device);

	FILE *file = fopen(filename, "rb");
	if (!file) {
		die("Couldn't access MIDI device %s.", filename);
	}
	while (1) {
		int c = getc(file);
		if (c == EOF) break;
		int top4 = (c & 0xf0) >> 4;
		switch (top4) {
		case 8:
		case 9: {
			printf("Note o%s\n", top4 == 8 ? "ff" : "n");
			u8 k = (u8)getc(file);
			u8 v = (u8)getc(file);
			if (feof(file)) break;
			printf("Key: %u\n",k);
			printf("Velocity: %u\n",v);
		} break;
		}
	}
	fclose(file);

	return 0;
}
