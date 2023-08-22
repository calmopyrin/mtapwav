/*
	pcmwav.h
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
#include <stdio.h>

#pragma pack(push, 1)

typedef struct {
	unsigned int	ChunkID;		// 'RIFF'
	unsigned int	ChunkSize;
	unsigned int	Format;			// 'WAVE'
} RIFFhdr;

typedef struct {
	unsigned int	Subchunk1Size;
	unsigned short	AudioFormat;	// PCM = 1
	unsigned short	NumChannels;
	unsigned int	SampleRate;
	unsigned int	ByteRate;
	unsigned short	BlockAlign;
	unsigned short	BitsPerSample;
} fmt_sub;

typedef struct {
	unsigned short	nchannels;		// number of channels
	unsigned int	samplerate;		// sampling rate (e.g. 44100)
	unsigned int	bitspersample;	// bits per sample (8, 16)
	unsigned int	ndatabytes;		// number of data bytes in wave file

	// private variables
	FILE			*winfile;		// file handle
	unsigned int	formatpos;
	unsigned int	datapos;
	unsigned int	datasizepos;
	unsigned int	filesize;
} pcmwavfile;

#pragma pack(pop)

extern char pcmwav_error[];	// On error: contains a string that describes the error

// Opens a PCM WAV file and fills opwf with info; returns 1
// if successful or 0 on error
// access = GENERIC_READ or GENERIC_WRITE (or both)
int pcmwav_open(const char *fname, const char *access, pcmwavfile *opwf);

// Reads len data bytes (not samples!) into buf
int pcmwav_read(pcmwavfile *pwf, void *buf, size_t len);

// Writes len data bytes from buf
int pcmwav_write(pcmwavfile *pwf, void *buf, size_t len);

// Rewinds to start of data
int pcmwav_rewind(pcmwavfile *pwf);

// Seeks +/- pos in file
int pcmwav_seek(pcmwavfile *pwf, size_t pos);

// Closes PCM WAV file
int pcmwav_close(pcmwavfile *pwf);
