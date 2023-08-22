/*
	pcmwav.c
	(c) 2008 A Grosz <grosza@hotmail.com>

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
#include "pcmwav.h"

char pcmwav_error[256];

int pcmwav_open(const char *fname, const char *access, pcmwavfile *opwf)
{
	RIFFhdr		rhdr;
	fmt_sub		fmt;
	size_t		nread;
	char		have_fmt = 0;
	unsigned int	subchunk, subchunk_size;

	opwf->winfile = fopen(fname, access);
	if (opwf->winfile == 0) {
		sprintf(pcmwav_error, "Cannot open file \"%s\".\n", fname);
		return 0;
	}
	fseek(opwf->winfile, 0, SEEK_END);
	opwf->filesize = ftell(opwf->winfile);
	fseek(opwf->winfile, 0, SEEK_SET);

	// Read RIFF header
	nread = fread(&rhdr, 1, sizeof(rhdr), opwf->winfile);
	if (!nread) {
		sprintf(pcmwav_error, "Error reading RIFF header (%x).\n", ferror(opwf->winfile));
		fclose(opwf->winfile);
		return 0;
	}

	// Check it
	if ((rhdr.ChunkID != 0x46464952 /* 'RIFF' */) || (rhdr.Format != 0x45564157 /* 'WAVE' */)) {
		sprintf(pcmwav_error, "This is not a PCM WAV file.\n");
		fclose(opwf->winfile);
		return 0;
	}

	/* read subchunks until we encounter 'data' */
	do {
		// Read subchunk ID
		if ( !fread(&subchunk, 1, sizeof(subchunk), opwf->winfile)) {
			sprintf(pcmwav_error, "Read error: this is not a correct PCM WAV file.\n");
			fclose(opwf->winfile);
			return 0;
		}

		if (subchunk == 0x20746D66 /* 'fmt ' */) {
			opwf->formatpos = ftell(opwf->winfile);
			// Read subchunk 1
			nread = fread(&fmt, 1, sizeof(fmt), opwf->winfile);

			// Check it
			if (fmt.AudioFormat != 1 ) {
				sprintf(pcmwav_error, "Error in format subchunk: this is not a PCM WAV file.\n");
				fclose(opwf->winfile);
				return 0;
			}

			opwf->bitspersample = fmt.BitsPerSample;

			if ((opwf->bitspersample != 1) && (opwf->bitspersample != 8) && (opwf->bitspersample != 16)) {
				sprintf(pcmwav_error, "Can only deal with 1-bit, 8-bit or 16-bit samples.\n");
				fclose(opwf->winfile);
				return 0;
			}

			// Skip any extra header bytes
			if (fmt.Subchunk1Size - 16)
				fseek(opwf->winfile, fmt.Subchunk1Size - 16, SEEK_CUR);

			have_fmt = 1;
		} else if (subchunk != 0x61746164 /* 'data' */) {
			// unknown subchunk - read size and skip
			nread = fread(&subchunk_size, 1, sizeof(subchunk_size), opwf->winfile);
			fseek(opwf->winfile, subchunk_size, SEEK_CUR);
		}

	} while (subchunk != 0x61746164 /* 'data' */);

	opwf->datasizepos = ftell(opwf->winfile);
	if (!have_fmt) {
		sprintf(pcmwav_error, "Encountered data subchunk, but no format subchunk found.\n");
		fclose(opwf->winfile);
		return 0;
	}

	/* read data chunk size */
	nread = fread(&opwf->ndatabytes, 1, sizeof(opwf->ndatabytes), opwf->winfile);

	opwf->samplerate = fmt.SampleRate;
	opwf->nchannels = fmt.NumChannels;
	opwf->datapos = ftell(opwf->winfile);

	return 1;
}

int pcmwav_read(pcmwavfile *pwf, void *buf, size_t len)
{
	size_t nread;

	nread = fread(buf, 1, len, pwf->winfile);

	if (nread != len) {
		sprintf(pcmwav_error, "Error in pcmwav_read(); only read %zu instead of %zu bytes.",
			nread, len);
		return 0;
	}

	return 1;
}

int pcmwav_write(pcmwavfile *pwf, void *buf, size_t len)
{
	size_t nwritten;

	nwritten = fread(buf, 1, len, pwf->winfile);

	if (nwritten != len) {
		sprintf(pcmwav_error, "Error in pcmwav_write(); only wrote %zu instead of %zu bytes.",
			nwritten, len);
		return 0;
	}

	return 1;
}

int pcmwav_rewind(pcmwavfile *pwf)
{
	if (fseek(pwf->winfile, pwf->datapos, SEEK_SET)) {
		sprintf(pcmwav_error, "Error in pcmwav_rewind().");
		return 0;
	}

	return 1;
}

int pcmwav_seek(pcmwavfile *pwf, size_t pos)
{
	if (fseek(pwf->winfile, pos, SEEK_CUR)) {
		sprintf(pcmwav_error, "Error in pcmwav_seek() - pos = %zu", pos);
		return 0;
	}

	return 1;
}

int pcmwav_close(pcmwavfile *pwf)
{
	fclose(pwf->winfile);
	return 1;
}
