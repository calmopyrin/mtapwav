/*
	wav2tap.c
	(c) 2016, 2023 A Grosz

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

#define _USE_MATH_DEFINES
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <time.h>
#include <limits.h>
#include "pcmwav.h"
#include "mtap.h"

#define COPYRIGHT_NOTICE	"wav2tap v1.3 (c) 2016, 2023 A Grosz.\n" \
							"Commodore family PCM WAV to MTAP converter.\n"

#define SIGN(T) ((0 < T) - (T < 0))

static unsigned char* buf;
static size_t			iobufsize = 10000000;
static pcmwavfile		pwf;
static unsigned char	threshold = 0;
static int				quiet = 0, nooverwrite = 0;
static char			    outfname[PATH_MAX];
static int				prompt = 0;
static int              invert_input = 0;
static unsigned int     decode_method = 0;
static int				split_tape = 0;
unsigned char			hdrbuf[16384];

static unsigned int passthrough(void);
static int process_file(const char* fname, const char* outfname);

static int iirFilter(unsigned char in)
{
	const double hpc = exp(-2.0 * M_PI * 100.0 / pwf.samplerate);
	static double hp_accu = 0;

	double accu = (double)in;
	// update hp filter pole
	hp_accu = hpc * hp_accu + (1 - hpc) * accu;
	// apply high pass filtering
	accu = accu - hp_accu;

	return (int)accu;
}

// in: wave sample; out : decoded bit
static void decode_sample(int sample, int threshold, unsigned char* bit)
{
	static int previous_sample = 0;
	static int previous_change = 0;
	int mythreshold;
	int change = sample - previous_sample;

	switch (decode_method) {
	default:
	case 0: // combined
		mythreshold = (128 * threshold) / 100;
		if (sample > 0x80 + mythreshold && (change >= 8)) {
			*bit = 1;
		}
		else if (sample <= 0x7F - mythreshold && (change <= -8)) {
			*bit = 0;
		}
		break;
	case 1: // hysteresis only
		mythreshold = (128 * threshold) / 100;
		if (sample > 0x80 + mythreshold) {
			*bit = 1;
		}
		else if (sample <= 0x7F - mythreshold) {
			*bit = 0;
		}
		break;
	case 2: // edge detect only
		if (abs(change) > threshold)
			*bit ^= 1;
		break;
	case 3: // sign change detect
		if (((previous_sample > 0x80 && sample <= 0x7F) || (sample > 0x80 && previous_sample <= 0x7F))
			&& abs(change) > threshold)
			*bit ^= 1;
		break;
	case 4: // edge detection
	{
		static int lastMax = 0, lastMin = 0;
		unsigned int edgeDetected = 0;

		if (change <= 0 && previous_change > 0) {
			// new local high
			lastMax = sample;
			const int mythreshold = (threshold * 240) / 255;
			if ((lastMax - lastMin) > mythreshold) {
				*bit = 0x10;
			}
		}
		else if (change >= 0 && previous_change < 0) {
			// new local low
			lastMin = sample;
			const int mythreshold = (threshold * 240) / 255;
			if ((lastMax - lastMin) > mythreshold) {
				*bit = 0x00;
			}
		}
	}
	break;
	}
	previous_change = change;
	previous_sample = sample;
}

static unsigned int passthrough(void)
{
	unsigned int	readn, i, bitcount;
	unsigned char bit = 0, prevbit = 0;
	double pulselen = 0.0;
	unsigned int pulsecount = 0;

	readn = (unsigned int)iobufsize;
	if (pwf.ndatabytes < iobufsize)
		readn = pwf.ndatabytes;

	if (!pcmwav_read(&pwf, buf, readn)) {
		if (!quiet)
			fprintf(stderr, "%s\n", pcmwav_error);
		return 0;
	}

	i = 0;
	bitcount = 1;

	while (i < readn) {
		switch (pwf.bitspersample) {
		case 1:
		{
			unsigned char in;

			in = (*((unsigned char*)buf + i)) ^ (invert_input ? 0xFF : 0x00);
			while (bitcount <= 8) {
				bit = (in >> (8 - bitcount)) & 1;
				if (prevbit ^ bit) {
					pulselen = mtap_write_pulse(pulselen, split_tape);
					pulsecount++;
					prevbit = bit;
				}
				pulselen += 1.0f / (double)(pwf.samplerate);
			}
		}
		break;
		case 8:
		{
			unsigned char byte = ((*(buf + i)) ^ (invert_input ? 0xFF : 0x00));

			decode_sample(byte, threshold, &bit);

			if (prevbit ^ bit) {
				pulselen = mtap_write_pulse(pulselen, split_tape);
				pulsecount++;
				prevbit = bit;
			}
			pulselen += 1.0f / (double)(pwf.samplerate);
		}
		break;
		case 16:
		{
			short byte = (*((short*)(buf + i))) ^ (invert_input ? 0xFFFF : 0x00);

			decode_sample(byte, threshold, &bit);

			if (prevbit ^ bit) {
				pulselen = mtap_write_pulse(pulselen, split_tape);
				pulsecount++;
				prevbit = bit;
				//pulselen = 0;
			}
			pulselen += 1.0f / (double)(pwf.samplerate);
		}
		break;
		}
		i++;
	}
	if (!quiet)
		fprintf(stderr, "%u pulses detected.\n", pulsecount);
	return 0;
}

static int process_file(const char* fname, const char* outfname)
{
	unsigned int r;

	// Open PCM WAV file
	if (!pcmwav_open(fname, "rb", &pwf)) {
		if (!quiet)
			fprintf(stderr, "%s\n", pcmwav_error);
		return 1;
	}
	if (!quiet) {
		fprintf(stderr, "Processing file \"%s\"\n", fname);
	}
	if (r = mtap_create(outfname, nooverwrite) != 0) {
		if (!quiet)
			fprintf(stderr, "Couldn't create output file '%s' (%u).\n", outfname, r);
		return 1;
	}
	// Read headers
	fseek(pwf.winfile, 0, SEEK_SET);
	size_t nread = fread(hdrbuf, 1, pwf.datapos, pwf.winfile);

	if (nread != pwf.datapos) {
		if (!quiet)
			fprintf(stderr, "Could not copy headers.\n");
		return 1;
	}
	// Allocate buffer
	iobufsize = *((unsigned int*)(hdrbuf + 4));
	buf = malloc(iobufsize < pwf.filesize ? pwf.filesize : iobufsize);

	if (buf == NULL) {
		if (!quiet)
			fprintf(stderr, "Cannot allocate buffer in memory.\n");
		return 1;
	}
	else {
		if (!quiet) {
			double minutes = (double)(pwf.ndatabytes / pwf.samplerate / (pwf.bitspersample / 8)) / 60.0;
			fprintf(stderr, "Allocated buffer size: %zi.\n", iobufsize);
			fprintf(stderr, "Original tape length %1.1f minutes.\n", minutes);
			fprintf(stderr, "Original sample frequency %u Hz.\n", pwf.samplerate);
		}
	}
	passthrough();

	free(buf);
	pcmwav_close(&pwf);

	mtap_close();

	return 0;
}

static void usage(void)
{
	fprintf(stderr, "\n%s\n", COPYRIGHT_NOTICE);
	fprintf(stderr,
		"    Usage:  wav2tap [flags] input-file\n\n"

		"        -h           display this help\n"
		"        -i           invert input signal\n"
		"        -m <value>   signal detection method (0: combined (default) 1: hysteresis only 2: difference only\n"
		"                                             (3: zero crossing      4: edge detect\n"
		"        -o <file>    write output to <file>\n"
		"        -p           prompt before starting conversion\n"
		"        -q           quiet (no screen output)\n"
		"        -t <value>   set comparison threshold to <value>%% of dynamic range (0..100)\n\n"

		"    error levels: 0 = no error, 1 = I/O error, 2 = parameter error,\n"
		"                  3 = no conversion required, 4 = out of memory,\n"
		"                  5 = user abort\n\n"
		"	- 'input-file' needs to be a PCM WAV file.\n");
}

int main(int argc, char* argv[]) {

	int	i;

	if (2 > argc) {
		usage();
		return 2;
	}

	// empty output file name
	strcat(outfname, "noname.tap");

	/* Parse command line */
	for (i = 1; i < argc; i++) {
		if ((argv[i][0] == '-') && (argv[i][1] != 0x00)) {
			switch (argv[i][1]) {
			case 'h':
				usage();
				return 0;
			case 'q':
				quiet = 1;
				break;
			case 'i':
				invert_input = 1;
				break;
			case 'm':
				decode_method = atoi(argv[++i]);
				if (decode_method > 4) {
					decode_method = 0;
					fprintf(stderr, "Illegal decoding method set to 0.\n");
				}
				break;
			case 'p':
				prompt = 1;
				break;
			case 's':
				//split_tape = 1;
				break;
			case 't':
				threshold = atoi(argv[++i]);
				if (threshold > 100) threshold = 100;
				break;

			case 'o':
				strcpy(outfname, argv[++i]);
				break;

			default:
				fprintf(stderr, "Error: Can't understand flag -%c. Aborting.\n", argv[i][1]);
				return 2;
			}
		}
		else {
			break;
		}
	}

	if (!quiet)
		fprintf(stderr, "\n%s\n", COPYRIGHT_NOTICE);

	return process_file(argv[i], outfname);
}