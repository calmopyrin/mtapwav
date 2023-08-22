#pragma once

#ifndef PATH_MAX
#define PATH_MAX _MAX_PATH
#endif

/* #define's from Markus Brenner's MTAP */
/* The following frequencies in [Hz] determine the length of one TAP unit */
#define C64PALFREQ  123156      /*  985248 / 8 */
#define C64NTSCFREQ 127841      /* 1022727 / 8 */
#define VICPALFREQ  138551      /* 1108405 / 8 */
#define VICNTSCFREQ 127841      /* 1022727 / 8 */
#define C16PALFREQ  110840      /*  886724 / 8 */
#define C16NTSCFREQ 111860      /*  894886 / 8 */

/* Machine entry used in extended TAP header */
enum {
	C64 = 0,
	VIC,
	C264
};

/* Video standards */
#define PAL  0
#define NTSC 1

#pragma pack(push, 1)

/* TAP file structure  */
typedef struct _TAP_HEADER
{
	char header_string[12];
	unsigned char version;
	unsigned char machine;
	unsigned char video_standard;
	unsigned char reserved;
	unsigned int size;
	unsigned char* data;
} tap_image_t;

#pragma pack(pop)

#define MTAP_HEADER_LEN (20) /* 20 - TAP format header length */

extern int mtap_create(const char *filename, int noow);
extern double mtap_write_pulse(double length, int split);
extern void mtap_close();
