/*
	mtap.c
	(c) 2016 A Grosz <grosza@hotmail.com>

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
#include <stdlib.h>
#include <stdio.h>
#include <memory.h>
#include <limits.h>
#include "mtap.h"

#pragma pack (1)
static tap_image_t tap_header = {
	{ 'C','1','6','-','T','A','P','E','-','R','A','W' },
	2,
	C264,
	PAL,
	0,
	0
};
#pragma pack()

double tap_frequencies[] = {
	C64PALFREQ, C64NTSCFREQ, VICPALFREQ, VICNTSCFREQ, C16PALFREQ, C16NTSCFREQ
};

static double tap_frequency;
static FILE* tapfile = NULL;
static unsigned int pulsestat[256];
static unsigned int pulsecount;
static char tapname[PATH_MAX];
static unsigned int chunks = 0;

/* create tap file and return 0 on success */
/* 1 : error creating file */
/* 2 : error writing header */
/* 3 : file already exist */
int mtap_create(const char* filename, int noow)
{
	if (tapfile = fopen(filename, "wb")) {
		fclose(tapfile);
		if (noow)
			return 1;
	}
	tapfile = fopen(filename, "wb");
	if (!tapfile)
		return 1;
	if (!fwrite(&tap_header, sizeof(tap_header), 1, tapfile))
		return 1;
	tap_frequency = tap_frequencies[tap_header.machine * 2 + tap_header.video_standard];
	pulsecount = 0;
	// empty pulse statistics
	memset(pulsestat, 0, sizeof(pulsestat));
	return 0;
}

int mtap_new_chunk(unsigned int cnt)
{
	char newname[PATH_MAX];
	char* name = strrchr(tapname, '.');

	*name = '\0';
	sprintf(newname, "%s%03u.tap", name, cnt);

	return mtap_create(newname, 1);
}

void mtap_close()
{
	const unsigned int divisor = tap_header.version > 1 ? 2 : 1;
	const unsigned int pulse_len_limit = 0xD0 / divisor; // longest regular pulse
	unsigned int i, j = 0, maxpulslen = 0;

	// count pulses shorter than ~$CD (longest full wave pulse)
	for (i = 0; i < pulse_len_limit; i++)
		if (pulsestat[i]) {
			j += pulsestat[i];
			// remember highest count pulse for display
			if (maxpulslen < pulsestat[i])
				maxpulslen = pulsestat[i];
		}
	fprintf(stderr, "Converted tape length %1.1f minutes.\n", (double)pulsecount / tap_frequency / 60.0);
	fprintf(stderr, "Number of unique pulse lengths < $%02X : %u\n", pulse_len_limit, j);

	for (i = 0; i < 0x50; i++)
		if (pulsestat[i]) {
			fprintf(stderr, "  $%02X : %-12u", i, pulsestat[i]);
			unsigned int k = pulsestat[i] * 50 / maxpulslen;
			while (k--) {
				fprintf(stderr, ".");
			};
			fprintf(stderr, "\n");
		}
	if (!tapfile)
		return;
	// finish file by adding data length
	tap_header.size = ftell(tapfile) - MTAP_HEADER_LEN;
	fseek(tapfile, 0, SEEK_SET);
	fwrite(&tap_header, MTAP_HEADER_LEN, 1, tapfile);
	// close
	fclose(tapfile);
}

double mtap_write_pulse(double length, int split)
{
	unsigned int i;

	if (!tapfile)
		return 1;

	// 'length' is in seconds
	// convert to TAP units
	unsigned int len8 = (unsigned long)(length * tap_frequency + 0.5);
	double remainder = length - len8 / tap_frequency;

	pulsecount += len8;

	// long pulse?
	if (len8 > 255) {

		unsigned int longpulse = len8;

		do {
			// write pilot byte
			fputc(0, tapfile);
			// write length
			for (i = 0; i < 3; i++) {
				fputc(longpulse & 0xFF, tapfile);
				longpulse >>= 8;
			}
			if (longpulse && split) {
				mtap_close();
				chunks++;
				mtap_new_chunk(chunks);
			}
		} while (longpulse);
		// count as 'zero' for the pulse statistics
		len8 = 0;
	}
	else
		fputc(len8, tapfile);

	pulsestat[len8]++;

	return remainder;
}
