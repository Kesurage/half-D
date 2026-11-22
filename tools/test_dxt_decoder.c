/* Standalone verification harness for the half-D DXT CPU decoder.
 * Compiles the REAL img_dds.c (stubbed engine headers) and decodes
 * hand-computed reference blocks for DXT1 / DXT3 / DXT5.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

typedef uint8_t byte;
typedef uint16_t word;
typedef uint32_t uint;
typedef int qboolean;
typedef size_t fs_offset_t;
#define true 1
#define false 0

/* --- engine stubs the decoder + loader need --- */
static struct { void *imagepool; } host = { NULL };
static void *Stub_Malloc( void *pool, size_t size ) { (void)pool; return malloc( size ); }
#define Mem_Malloc( pool, size ) Stub_Malloc( pool, size )
static void Stub_Printf( const char *fmt, ... ) { (void)fmt; }
#define Con_DPrintf( ... ) Stub_Printf( __VA_ARGS__ )
#define S_ERROR ""
#define S_WARN ""

enum { PF_UNKNOWN = 0, PF_LUMINANCE, PF_BGR_24, PF_RGB_24, PF_BGRA_32, PF_RGBA_32,
	PF_DXT1, PF_DXT3, PF_DXT5 };
enum { IMAGE_HAS_ALPHA = 1, IMAGE_HAS_COLOR = 2, IMAGE_DDS_FORMAT = 4, IMAGE_CUBEMAP = 8 };
enum { IL_DDS_HARDWARE = 1 };
enum { DDS_MIPMAPCOUNT = 0x20000 };

#define Q_max( a, b ) ((a) > (b) ? (a) : (b))
#define Q_min( a, b ) ((a) < (b) ? (a) : (b))
#define bound( a, x, b ) ((x) < (a) ? (a) : (x) > (b) ? (b) : (x))
#define SetBits( a, b ) ((a) |= (b))
#define ClearBits( a, b ) ((a) &= ~(b))
#define FBitSet( a, b ) ((a) & (b))
#define Image_CheckFlag( f ) 0 /* force the CPU-decode path */

static struct image_s
{
	int width, height, depth;
	int type;
	size_t size;
	int flags;
	int num_mips;
	word encode;
	byte *rgba;
} image;

/* the loader body pulls a lot more engine machinery than the decoder needs;
 * extract just the decoder by including the source and stubbing the rest is
 * not feasible, so we include the file but provide the pieces it references.
 * To keep this honest we compile img_dds.c directly and only call the
 * decoder, which is static. The loader is never invoked. */
/* The real decoder is extracted from img_dds.c at build time (see the
 * Make step in the build script) and appended here as dxt_decoder.inc,
 * so the test always exercises the shipped code, not a copy. */
#include "dxt_decoder.inc"

static int failures = 0;

static void expect( const char *what, int got, int want )
{
	if( got != want )
	{
		printf( "FAIL %s: got %d want %d\n", what, got, want );
		failures++;
	}
	else printf( "ok   %s = %d\n", what, got );
}

int main( void )
{
	/* ---- DXT1: 4x4 block, c0 = 0xF800 (red), c1 = 0x07E0 (green), c0 > c1
	 * indices: pixel0=0 (red), pixel1=1 (green), pixel2=2 (2/3 red), pixel3=3 (1/3 red) */
	byte dxt1[8] = { 0x00, 0xF8, 0xE0, 0x07, 0, 0, 0, 0 };
	/* index bits: p0=00, p1=01, p2=10, p3=11 → byte0 = 0b11100100 = 0xE4 */
	dxt1[4] = 0xE4;

	image.width = image.height = 4;
	image.depth = 1;
	image.num_mips = 1;
	image.flags = 0;
	image.type = PF_DXT1;
	image.size = 8;
	image.encode = 0;

	if( !Image_DecodeDXTRGBA( dxt1 ))
	{
		printf( "FAIL: DXT1 decode returned false\n" );
		failures++;
		return 1;
	}

	expect( "dxt1 size", (int)image.size, 4 * 4 * 4 );
	expect( "dxt1 type", image.type, PF_RGBA_32 );
	/* red pixel 0 */
	expect( "p0.r", image.rgba[0], 0xFF );
	expect( "p0.g", image.rgba[1], 0 );
	expect( "p0.b", image.rgba[2], 0 );
	expect( "p0.a", image.rgba[3], 0xFF );
	/* green pixel 1 */
	expect( "p1.r", image.rgba[4], 0 );
	expect( "p1.g", image.rgba[5], 0xFF );
	/* 2/3 red + 1/3 green pixel 2 */
	expect( "p2.r", image.rgba[8], ( 2 * 255 + 0 ) / 3 );
	expect( "p2.g", image.rgba[9], ( 2 * 0 + 255 ) / 3 );
	/* 1/3 red + 2/3 green pixel 3 */
	expect( "p3.g", image.rgba[13], ( 0 + 2 * 255 ) / 3 );

	free( image.rgba );

	/* ---- DXT1 punchthrough: c0 <= c1 → index 3 is black ---- */
	byte dxt1b[8] = { 0xE0, 0x07, 0x00, 0xF8, 0xE4, 0, 0, 0 }; /* c0 green(0x07E0) < c1 red(0xF800) */
	image.rgba = NULL;
	image.type = PF_DXT1;
	image.size = 8;

	if( !Image_DecodeDXTRGBA( dxt1b ))
	{
		printf( "FAIL: DXT1 punchthrough decode returned false\n" );
		failures++;
		return 1;
	}

	/* p3 (index 3) must be black */
	expect( "pt p3.r", image.rgba[12], 0 );
	expect( "pt p3.g", image.rgba[13], 0 );
	/* p2 (index 2) is the average of green and red = (0,255,0)+(255,0,0) /2 */
	expect( "pt p2.r", image.rgba[8], 127 );
	expect( "pt p2.g", image.rgba[9], 127 );

	free( image.rgba );

	/* ---- DXT3: explicit 4-bit alpha + 4-color block ---- */
	byte dxt3[16];
	memset( dxt3, 0, sizeof( dxt3 ));
	/* alpha bytes: pixel0 = 0xF (low nibble of byte0), pixel1 = 0x5 (high nibble of byte0) */
	dxt3[0] = 0x5F;
	/* color: c0 red, c1 green, 4-color mode, p0=0, p1=1 */
	dxt3[8] = 0x00; dxt3[9] = 0xF8; dxt3[10] = 0xE0; dxt3[11] = 0x07;
	dxt3[12] = 0xE4; /* p0=00 p1=01 p2=10 p3=11 */

	image.rgba = NULL;
	image.type = PF_DXT3;
	image.size = 16;

	if( !Image_DecodeDXTRGBA( dxt3 ))
	{
		printf( "FAIL: DXT3 decode returned false\n" );
		failures++;
		return 1;
	}

	expect( "dxt3 p0.a", image.rgba[3], 0xFF );	/* 0xF → 0xFF */
	expect( "dxt3 p1.a", image.rgba[7], 0x55 );	/* 0x5 → 0x55 */
	expect( "dxt3 p0.r", image.rgba[0], 0xFF );
	/* four-color mode even though c0 > c1: p3 = 1/3 red + 2/3 green */
	expect( "dxt3 p3.g", image.rgba[13], ( 2 * 255 ) / 3 );

	free( image.rgba );

	/* ---- DXT5: interpolated alpha ---- */
	byte dxt5[16];
	memset( dxt5, 0, sizeof( dxt5 ));
	dxt5[0] = 255; /* a0 */
	dxt5[1] = 0;   /* a1, a0 > a1 → endpoints + 6 interpolations */
	/* alpha indices: p0=0 (a0=255), p1=2 (218), p2=7 (36), p3=1 (a1=0)
	 * 3 bits per pixel, LSB-first: p0=000 p1=010 p2=111 p3=001
	 * byte0 bits0-7 = 1,1,0,1,0,0,0,0 = 0xD0; byte1 bits8-15 = 1,1,0... = 0x03 */
	dxt5[2] = 0xD0; dxt5[3] = 0x03;
	/* color part same as dxt3 */
	dxt5[8] = 0x00; dxt5[9] = 0xF8; dxt5[10] = 0xE0; dxt5[11] = 0x07;
	dxt5[12] = 0xE4;

	image.rgba = NULL;
	image.type = PF_DXT5;
	image.size = 16;

	if( !Image_DecodeDXTRGBA( dxt5 ))
	{
		printf( "FAIL: DXT5 decode returned false\n" );
		failures++;
		return 1;
	}

	expect( "dxt5 p0.a", image.rgba[3], 255 );
	expect( "dxt5 p1.a", image.rgba[7], ( 6 * 255 ) / 7 );
	expect( "dxt5 p2.a", image.rgba[11], ( 1 * 255 ) / 7 );
	expect( "dxt5 p3.a", image.rgba[15], 0 );
	expect( "dxt5 p0.r", image.rgba[0], 0xFF );

	free( image.rgba );

	/* ---- DXT5 a0 <= a1 branch: endpoints + 4 interpolations + 0 + 255 ---- */
	byte dxt5b[16];
	memset( dxt5b, 0, sizeof( dxt5b ));
	dxt5b[0] = 100; /* a0 */
	dxt5b[1] = 200; /* a1, a0 <= a1 */
	/* p0=0 (100), p1=2 ((4*100+200)/5=120), p2=6 (0), p3=7 (255)
	 * 3 bits per pixel, LSB-first: p0=000 p1=010 p2=110 p3=111
	 * byte0 = b7..b0 = 1,0,0,1,0,0,0,0 = 0b10010000 = 0x90
	 * byte1 = b15..b8 = 0,0,0,0, 1,1,1,1 = 0b00001111 = 0x0F */
	dxt5b[2] = 0x90; dxt5b[3] = 0x0F;
	/* color: flat green */
	dxt5b[8] = 0xE0; dxt5b[9] = 0x07; dxt5b[10] = 0xE0; dxt5b[11] = 0x07;
	dxt5b[12] = 0x00;

	image.rgba = NULL;
	image.type = PF_DXT5;
	image.size = 16;

	if( !Image_DecodeDXTRGBA( dxt5b ))
	{
		printf( "FAIL: DXT5 b-branch decode returned false\n" );
		failures++;
		return 1;
	}

	expect( "dxt5b p0.a", image.rgba[3], 100 );
	expect( "dxt5b p1.a", image.rgba[7], ( 4 * 100 + 1 * 200 ) / 5 );
	expect( "dxt5b p2.a", image.rgba[11], 0 );
	expect( "dxt5b p3.a", image.rgba[15], 255 );
	expect( "dxt5b p0.g", image.rgba[1], 255 );

	free( image.rgba );

	/* ---- mip chain + edge clipping: 6x6 DXT1, 2 mips (6x6 + 3x3) ---- */
	byte chain[8 /* 2x2 blocks at 6x6 */ + 8 /* 1x1 block at 3x3 */];
	memset( chain, 0x80, sizeof( chain ));
	/* level 0: 2x2 blocks of 8 bytes = 32; level 1: 1 block = 8; total 40 */
	image.width = image.height = 6;
	image.num_mips = 2;
	image.type = PF_DXT1;
	image.size = 40;
	image.rgba = NULL;

	if( !Image_DecodeDXTRGBA( chain ))
	{
		printf( "FAIL: chain decode returned false\n" );
		failures++;
		return 1;
	}

	expect( "chain size", (int)image.size, 6 * 6 * 4 + 3 * 3 * 4 );

	free( image.rgba );

	/* ---- rejection: volume texture ---- */
	image.width = image.height = 4;
	image.depth = 2;
	image.num_mips = 1;
	image.size = 16;
	image.rgba = NULL;
	expect( "volume rejected", Image_DecodeDXTRGBA( chain ), 0 );

	if( failures == 0 )
		printf( "\nALL DECODER TESTS PASSED\n" );
	else
		printf( "\n%d FAILURES\n", failures );

	return failures ? 1 : 0;
}
