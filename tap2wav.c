/*
	tap2wav.c
	(c) 2003, 2016, 2023 A Grosz

	This is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	It is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the Free Software
	Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#define _USE_MATH_DEFINES
#include <math.h>
#include <limits.h>
#include "mtap.h"

#ifndef PATH_MAX
#define PATH_MAX _MAX_PATH
#endif

#define COPYRIGHT_NOTICE	"tap2wav v1.3 (C) 2003, 2016, 2023 by A Grosz\n" \
							"Commodore MTAP tape image to PCM WAV converter\n"

const char *machine[] = {
	"C64", "VIC-20", "C264"
};

const char *videostd[] = {
	"PAL", "NTSC"
};

#define WAVEFREQ 44100          /* Default wave frequency */

#define MAXSTRLEN 80
#define ZERO (wave.nSamplesPerSec/50)   /* approx. 1/50s, length of a V0 '00'-pause */

#define GAIN 0xC0

/* should be 1-byte aligned */
#pragma pack(1)

/* WAV file header structure */
struct wav_header {
	char riff[4];
	unsigned int file_size;
	char WAVEfmt[8];
	unsigned int fLen /* 0x1020 */;
	unsigned short wFormatTag; /* 0x0001 */
	unsigned short nChannels; /* 0x0001 */
	unsigned int nSamplesPerSec;
	unsigned int nAvgBytesPerSec;
	unsigned short nBlockAlign; /* 0x0001 */
	unsigned short nBitsPerSample; /* 0x0008 */
	char datastr[4];
	unsigned int data_size;
} wave = {
	{'R','I','F','F'},
	0,
	{'W','A','V','E','f','m','t', ' '},
	16,
	0x0001,
	0x0001,
	WAVEFREQ,
	WAVEFREQ,
	0x0001,
	0x0008,
	{'d','a','t','a'},
	0
};
#pragma pack()

struct _options {
	unsigned char invert_signal;
	unsigned char gain;
	double cutoff;
	unsigned int quiet;
	unsigned int nofilter;
} options;

/* Global variables */
static unsigned int data_length;
static unsigned int pause;
static unsigned int half_wave_time;
static unsigned int halfpulse;
static tap_image_t tap;
static unsigned char *buffer, *buffer_end;
static FILE *fpin, *fpout;
static unsigned int pulsestat[256];
static unsigned int mtap_frequency;
static unsigned int edge;

typedef void (*tap_interpreters)(unsigned char byte);
static void _interpret_v0_byte(unsigned char byte);
static void _interpret_v1_byte(unsigned char byte);
static void _interpret_v2_byte(unsigned char byte);
static tap_interpreters interpret_tap_byte[] = {
	_interpret_v0_byte,
	_interpret_v1_byte,
	_interpret_v2_byte,
	NULL
};

static int iirFilter(unsigned char in)
{
	const double hpc = exp(-2.0 * M_PI * options.cutoff / wave.nSamplesPerSec);
	static double hp_accu = 0;

	double accu = (double)in;
	// update hp filter pole
	hp_accu = hpc * hp_accu + (1 - hpc) * accu;
	// apply high pass filtering
	accu = accu - hp_accu;

	return (int)accu;
}

static void wav_write(unsigned char wavbyte)
{
	unsigned char outByte;
	int output = options.nofilter ? (255 - options.gain) / 2 - wavbyte : 0x80 - iirFilter(wavbyte);

	// do clipping
	if (output < 0)
		output = 0;
	else if (output > 255)
		output = 255;
	outByte = output & 0xFF;
	fputc(outByte ^ options.invert_signal, fpout);
}

static void wave_out(unsigned int count, unsigned char *out)
{
	if (wave.nBitsPerSample == 1) {
		static unsigned int bitcount = 1;
		for (unsigned int i = 0; i < count; i++) {
			bitcount = (bitcount << 1) | edge;
			if (bitcount & 0x100) {
				unsigned char out = bitcount & 0xFF;
				fputc(out, fpout);
				bitcount = 1;
			}
		}
		edge ^= 1;
	}
	else {
		while (count--) {
			wav_write(*out);
		}
		// invert wave
		*out ^= options.gain;
	}
}

/* tap byte interpreter functions */
static void _interpret_v0_byte(unsigned char byte)
{
	static unsigned char wavbyte = 0;

	if (byte != 0x00) {
		half_wave_time = (long)((byte * wave.nSamplesPerSec + mtap_frequency / 2) / mtap_frequency);
	} else {
		pause = ZERO;
		for (;(*(buffer+1) == 0) && ((buffer + 1) < buffer_end); buffer++)
			pause += ZERO;
		half_wave_time = (long)(((pause >> 3) * wave.nSamplesPerSec + mtap_frequency / 2) / mtap_frequency);
	}
	halfpulse = half_wave_time / 2;
	wave_out(halfpulse, &wavbyte);
	halfpulse = half_wave_time - halfpulse;
	wave_out(halfpulse, &wavbyte);
	data_length += half_wave_time;
}

static void _interpret_v1_byte(unsigned char byte)
{
	static unsigned char wavbyte = 0x00;
	int i;

	if (byte != 0x00) {
		half_wave_time = (long)((byte * wave.nSamplesPerSec + mtap_frequency / 2) / mtap_frequency);
	} else {
		if ((buffer + 3) >= buffer_end)
			return;
		pause = 0;
		for (i = 0; i < 3; i++) {
			buffer++;
			pause >>= 8;
			pause += (*buffer << 16);
		}
		half_wave_time = (long)(((pause >> 3) * wave.nSamplesPerSec + mtap_frequency / 2) / mtap_frequency);
	}
	halfpulse = half_wave_time / 2;
	wave_out(halfpulse, &wavbyte);
	halfpulse = half_wave_time - halfpulse;
	wave_out(halfpulse, &wavbyte);
	data_length += half_wave_time;
}

static void _interpret_v2_byte(unsigned char byte)
{
	static unsigned char wavbyte = GAIN;//0x00;
	unsigned int i;

	if (byte != 0x00) {
		half_wave_time = (long)((byte * wave.nSamplesPerSec + mtap_frequency / 2) / mtap_frequency);
	} else {
		if ((buffer + 3) >= buffer_end)
			return;
		pause = 0;
		for (i = 0; i < 3; i++) {
			buffer++;
			pause >>= 8;
			pause += (*buffer << 16);
		}
		half_wave_time = (long)(((pause >> 3) * wave.nSamplesPerSec + mtap_frequency / 2) / mtap_frequency);
	}

	halfpulse = half_wave_time;
	wave_out(halfpulse, &wavbyte);
	data_length += half_wave_time;
}

static int freadbyte(FILE *fpin)
{
	/* read one byte from *fpin, abort if none available */
	int c;

	if ((c = fgetc(fpin)) == EOF) {
		fprintf(stderr, "unexpected End of File!\n");
		exit(6);
	}
	return c;
}

static int read_tap_data(FILE *fpin, tap_image_t *tap)
{
	unsigned long filelength;
	char inputstring[MAXSTRLEN];
	int i;

	/* figure out file length */
	fseek(fpin,0,SEEK_END);
	filelength = ftell(fpin);
	rewind(fpin);

	/* check "C16-TAPE-RAW" string */
	fgets(inputstring, 13, fpin);
	if(strncmp(inputstring+4,"TAPE-RAW",8) != 0) {
		fprintf(stderr, "invalid or corrupt TAP file!\n");
		exit(5);
	}

	/* read the TAP-file version */
	tap->version = freadbyte(fpin);
	if ((tap->version < 0) || (tap->version > 2)) {
		fprintf(stderr, "TAP Version not (yet) supported, sorry!\n");
		exit(6);
	}

	/* read additional TAP info fields */
	tap->machine = freadbyte(fpin);
	printf("Machine type : %s\n", machine[tap->machine]);
	tap->video_standard = freadbyte(fpin);
	if (tap->video_standard > 1) {
		fprintf(stderr, "Illegal video standard value (%x) set to PAL.\n", tap->video_standard);
		tap->video_standard = 0;
	} else
		printf("Video standard : %s\n", videostd[tap->video_standard]);
	freadbyte(fpin);

	switch(tap->machine) {
		case VIC:
			mtap_frequency =
				(tap->video_standard == NTSC) ? VICNTSCFREQ : VICPALFREQ;
			break;
		case C264:
			mtap_frequency =
				(tap->video_standard == NTSC) ? C16NTSCFREQ : C16PALFREQ;
			break;
		case C64:
		default:
			mtap_frequency =
				(tap->video_standard == NTSC) ? C64NTSCFREQ : C64PALFREQ;
			break;
	}
	printf("Tape frequency : %d\n", (mtap_frequency)<<3);
	/* read the data length */
	for (tap->size = 0, i = 0; i < 4; i++) {
		tap->size >>= 8;
		tap->size += (freadbyte(fpin) << 24);
	}
	printf("TAP data length : %d\n", tap->size);

	/* check if data length is valid */
	unsigned int real_length = filelength - MTAP_HEADER_LEN;
	if (tap->size != real_length) {
		fprintf(stderr, "WARNING: file size doesn't match header (%ukb vs %ukb)!\n",
			(unsigned int)(real_length / 1024 + 0.5), (unsigned int)(tap->size / 1024 + 0.5) );
		if (tap->size != real_length) {
			tap->size = real_length;
			fprintf(stderr, "TAP size corrected to actual size.\n");
		}
	}
	printf("TAP version : %d\n", tap->version);
	/* allocate buffer for TAP data */
	if ((tap->data = (unsigned char *) calloc(tap->size, sizeof(unsigned char))) == NULL) {
		fprintf(stderr, "Couldn't allocate buffer memory!\n");
		exit(7);
	}

	/* read in TAP data */
	if (fread(tap->data, sizeof(unsigned char), tap->size, fpin) != tap->size) {
		fprintf(stderr, "Couldn't read whole TAP file!\n");
		//exit(8);
	}
	return -1;
}

static void tap_statistics(tap_image_t *t)
{
	unsigned int i;
	unsigned int maxpulslen = 0;
	unsigned int limit = 0xc0 >> (t->version > 1 ? 1 : 0);

	// empty count
	memset(pulsestat, 0, sizeof(pulsestat));
	// count pulse frequencies
	for (i = 0; i < t->size; i++) {
		if (t->data[i] <= limit) {
			pulsestat[t->data[i]] += 1;
		}
	}
	// find highest count
	for (i = 0; i < limit; i++) {
		if (maxpulslen < pulsestat[i])
			maxpulslen = pulsestat[i];
	}
	// write stats
	for (i = 0; i < limit; i++)
		if (pulsestat[i]) {
			printf("  $%02X : %-12u", i, pulsestat[i]);
			unsigned int k = pulsestat[i] * 50 / maxpulslen;
			while (k--) {
				printf(".");
			}
			printf("\n");
		}
}

int main(int argc, char *argv[])
{
	char tap_file_name[PATH_MAX];

	int i;

	if (argc < 3) {
		fprintf(stderr, "\n%s\n", COPYRIGHT_NOTICE);
		fprintf(stderr, "Usage: tap2wav <tapfile> <outputfile> [options]\n"
						"       -b       : generate special 1-bit WAV (more efficient than MTAP)\n"
						"       -c FRQ   : set high pass filter cutoff to 'FRQ' (default: 400 Hz)\n"
						"       -f FRQ   : change sample frequency to 'FRQ' (default: 44100)\n"
						"       -g GAIN  : change amplitude to 'GAIN' (default: 192)\n"
						"       -i       : invert signal\n"
						"       -n       : no DC removal filter\n"
						"       -q       : suppress statistics\n");
		exit(1);
	}

	strcpy( tap_file_name, argv[1]);

	printf("Opening TAP file %s\n", argv[1]);
	if ((fpin = fopen(tap_file_name,"rb")) == NULL) {
		fprintf(stderr, "Couldn't open TAP file %s!\n", tap_file_name);
		exit(2);
	}
	printf("Reading TAP header\n");
	if (!read_tap_data(fpin, &tap)) {
		fprintf(stderr, "Couldn't read TAP file %s!\n", tap_file_name);
		exit(3);
	}

	// set default options
	options.invert_signal = 0;
	options.gain = GAIN;
	options.cutoff = 100.0;
	options.quiet = 0;
	options.nofilter = 0;
	wave.nBitsPerSample = 8;

	if (argc > 3) {
		int i = 3;
		do {
			if (!strcmp(argv[i], "-i")) {
				options.invert_signal = 0xFF;
				printf("Inverting signal...\n");
			} else if (!strcmp(argv[i], "-c")) {
				unsigned int new_freq;
				if (i <= argc) {
					sscanf(argv[++i], "%u", &new_freq);
					if (new_freq < 10 && new_freq > 500) {
						printf("Overriding default high pass filter cutoff frequency with %u.\n", new_freq);
						options.cutoff = new_freq;
					} else {
						printf("Invalid cutoff frequency (must be between 10 and 500 Hz). Resetting to %i Hz.\n", (int) options.cutoff);
					}
				}
			} else if (!strcmp(argv[i], "-f")) {
				unsigned int new_freq;
				if (i <= argc) {
					sscanf(argv[++i], "%u", &new_freq);
					if (new_freq <= 192000 && new_freq >= 8000) {
						printf("Overriding default WAV frequency with %u.\n", new_freq);
						wave.nAvgBytesPerSec = wave.nSamplesPerSec = new_freq;
					} else {
						printf("Invalid frequency (> 192000 Hz). Resetting to %u.\n", WAVEFREQ);
					}
				}
			} else if (!strcmp(argv[i], "-g")) {
				unsigned int new_gain;
				if (i <= argc) {
					sscanf(argv[++i], "%u", &new_gain);
					if (new_gain <= 255 && new_gain >= 16) {
						printf("Overriding default gain with %u.\n", new_gain);
						options.gain = new_gain;
					}
					else {
						printf("Invalid gain value (should be between 16 and 255). Resetting to %u.\n", GAIN);
					}
				}
			} else if (!strcmp(argv[i], "-q")) {
				options.quiet = 1;
			}
			else if (!strcmp(argv[i], "-n")) {
				options.nofilter = 1;
			}
			else if (!strcmp(argv[i], "-b")) {
				wave.nBitsPerSample = 1;
			}
		} while (++i < argc);
	}

	if (!options.quiet)
		tap_statistics(&tap);

	printf( "Creating output file %s\n", argv[2]);
	if ((fpout = fopen(argv[2],"wb")) == NULL) {
		fprintf(stderr, "Couldn't create output file %s!\n", argv[2]);
		exit(4);
	}
	fwrite( &wave, sizeof(wave), 1, fpout);
	data_length = 0;
	edge = options.invert_signal ? 0 : 1;

	buffer_end = tap.data + tap.size;
	// do the conversion
	for (buffer = tap.data; buffer < buffer_end; buffer++) {
		(interpret_tap_byte[tap.version])(*buffer);
		if ( !((buffer_end-buffer)%32768) )
			printf(".");
	}
	printf("\nWave data size : %d bytes\n", data_length);
	i = ftell(fpout);
	printf("Output file size : %d bytes\n", i);
	
	i -= 8;
	fseek(fpout, 4, SEEK_SET);
	fwrite(&i, 4, 1, fpout);

	i = i - sizeof(wave) + 8;
	fseek(fpout, sizeof(wave) - sizeof(unsigned int), SEEK_SET);
	fwrite(&data_length, 4, 1, fpout);
	
	fclose(fpout );
	printf("Finished.\n");

	return 0;
}

