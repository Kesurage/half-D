/*
img_dds.c - dds format load
Copyright (C) 2015 Uncle Mike

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include "imagelib.h"
#include "xash3d_mathlib.h"
#include "img_dds.h"
#include "swaplib.h"

le_struct_begin( dds_pixf_swap )
	le_struct_field( dds_pixf_t, dwSize )
	le_struct_field( dds_pixf_t, dwFlags )
	le_struct_field( dds_pixf_t, dwFourCC )
	le_struct_field( dds_pixf_t, dwRGBBitCount )
	le_struct_field( dds_pixf_t, dwRBitMask )
	le_struct_field( dds_pixf_t, dwGBitMask )
	le_struct_field( dds_pixf_t, dwBBitMask )
	le_struct_field( dds_pixf_t, dwABitMask )
le_struct_end();

le_struct_begin( dds_caps_swap )
	le_struct_field( dds_caps_t, dwCaps1 )
	le_struct_field( dds_caps_t, dwCaps2 )
	le_struct_field( dds_caps_t, dwCaps3 )
	le_struct_field( dds_caps_t, dwCaps4 )
le_struct_end();

le_struct_begin( dds_swap )
	le_struct_field( dds_t, dwIdent )
	le_struct_field( dds_t, dwSize )
	le_struct_field( dds_t, dwFlags )
	le_struct_field( dds_t, dwHeight )
	le_struct_field( dds_t, dwWidth )
	le_struct_field( dds_t, dwLinearSize )
	le_struct_field( dds_t, dwDepth )
	le_struct_field( dds_t, dwMipMapCount )
	le_struct_field( dds_t, dwAlphaBitDepth )
	le_struct_array( dds_t, dwReserved1, 10 )
	le_struct_child( dds_t, dsPixelFormat, dds_pixf_swap )
	le_struct_child( dds_t, dsCaps, dds_caps_swap )
	le_struct_field( dds_t, dwTextureStage )
le_struct_end();

le_struct_begin( dds_dxt10_swap )
	le_struct_field( dds_header_dxt10_t, dxgiFormat )
	le_struct_field( dds_header_dxt10_t, resourceDimension )
	le_struct_field( dds_header_dxt10_t, miscFlag )
	le_struct_field( dds_header_dxt10_t, arraySize )
	le_struct_field( dds_header_dxt10_t, miscFlags2 )
le_struct_end();

static qboolean Image_CheckDXT3Alpha( dds_t *hdr, byte *fin )
{
	for( int y = 0; y < hdr->dwHeight; y += 4 )
	{
		for( int x = 0; x < hdr->dwWidth; x += 4 )
		{
			byte *alpha = fin + 8;
			fin += 16;

			for( int j = 0; j < 4; j++ )
			{
				word sAlpha = alpha[2*j] + 256 * alpha[2*j+1];

				for( int i = 0; i < 4; i++ )
				{
					if((( x + i ) < hdr->dwWidth ) && (( y + j ) < hdr->dwHeight ))
					{
						if( sAlpha == 0 )
							return true;
					}
					sAlpha >>= 4;
				}
			}
		}
	}

	return false;
}

static qboolean Image_CheckDXT5Alpha( dds_t *hdr, byte *fin )
{
	for( int y = 0; y < hdr->dwHeight; y += 4 )
	{
		for( int x = 0; x < hdr->dwWidth; x += 4 )
		{
			if( y >= hdr->dwHeight || x >= hdr->dwWidth )
				break;

			byte *alphamask = fin + 2;
			fin += 8;
			fin += 8;

			// last three bytes
			uint bits = (alphamask[3]) | (alphamask[4] << 8) | (alphamask[5] << 16);

			for( int j = 2; j < 4; j++ )
			{
				for( int i = 0; i < 4; i++ )
				{
					// only put pixels out < width or height
					if((( x + i ) < hdr->dwWidth ) && (( y + j ) < hdr->dwHeight ))
					{
						if( bits & 0x07 )
							return true;
					}
					bits >>= 3;
				}
			}
		}
	}

	return false;
}

static void Image_DXTGetPixelFormat( dds_t *hdr, dds_header_dxt10_t *headerExt )
{
	uint bits = hdr->dsPixelFormat.dwRGBBitCount;

	if( !FBitSet( hdr->dsCaps.dwCaps2, DDS_VOLUME ))
		hdr->dwDepth = 1;

	if( FBitSet( hdr->dsPixelFormat.dwFlags, DDS_FOURCC ))
	{
		if( hdr->dsPixelFormat.dwFourCC == TYPE_DX10 )
		{
			switch( headerExt->dxgiFormat )
			{
			case DXGI_FORMAT_BC4_TYPELESS:
			case DXGI_FORMAT_BC4_UNORM:
				image.type = PF_BC4_UNSIGNED;
				break;
			case DXGI_FORMAT_BC4_SNORM:
				image.type = PF_BC4_SIGNED;
				break;
			case DXGI_FORMAT_BC6H_SF16:
				image.type = PF_BC6H_SIGNED;
				break;
			case DXGI_FORMAT_BC6H_UF16:
			case DXGI_FORMAT_BC6H_TYPELESS:
				image.type = PF_BC6H_UNSIGNED;
				break;
			case DXGI_FORMAT_BC7_UNORM:
			case DXGI_FORMAT_BC7_TYPELESS:
				image.type = PF_BC7_UNORM;
				break;
			case DXGI_FORMAT_BC7_UNORM_SRGB:
				image.type = PF_BC7_SRGB;
				break;
			case DXGI_FORMAT_BC5_TYPELESS:
				image.type = PF_ATI2;
				break;
			case DXGI_FORMAT_BC5_UNORM:
				image.type = PF_BC5_UNSIGNED;
				break;
			case DXGI_FORMAT_BC5_SNORM:
				image.type = PF_BC5_SIGNED;
				break;
			default:
				image.type = PF_UNKNOWN;
				break;
			}
		}
		else
		{
			switch( hdr->dsPixelFormat.dwFourCC )
			{
			case TYPE_DXT1:
				image.type = PF_DXT1;
				break;
			case TYPE_DXT2:
				image.flags &= ~IMAGE_HAS_ALPHA; // alpha is already premultiplied by color
				// intentionally fallthrough
			case TYPE_DXT3:
				image.type = PF_DXT3;
				break;
			case TYPE_DXT4:
				image.flags &= ~IMAGE_HAS_ALPHA; // alpha is already premultiplied by color
				// intentionally fallthrough
			case TYPE_DXT5:
				image.type = PF_DXT5;
				break;
			case TYPE_ATI2:
				image.type = PF_ATI2;
				break;
			case TYPE_BC5S:
				image.type = PF_BC5_SIGNED;
				break;
			case TYPE_BC4S:
				image.type = PF_BC4_SIGNED;
				break;
			case TYPE_BC4U:
				image.type = PF_BC4_UNSIGNED;
				break;
			default:
				image.type = PF_UNKNOWN; // assume error
				break;
			}
		}
	}
	else
	{
		// this dds texture isn't compressed so write out ARGB or luminance format
		if( hdr->dsPixelFormat.dwFlags & DDS_DUDV )
		{
			image.type = PF_UNKNOWN; // assume error
		}
		else if( hdr->dsPixelFormat.dwFlags & DDS_LUMINANCE )
		{
			image.type = PF_UNKNOWN; // assume error
		}
		else
		{
			switch( bits )
			{
			case 32:
				image.type = PF_BGRA_32;
				break;
			case 24:
				image.type = PF_BGR_24;
				break;
			case 8:
				image.type = PF_LUMINANCE;
				break;
			default:
				image.type = PF_UNKNOWN;
				break;
			}
		}
	}

	// setup additional flags
	if( hdr->dsCaps.dwCaps1 & DDS_COMPLEX && hdr->dsCaps.dwCaps2 & DDS_CUBEMAP )
		image.flags |= IMAGE_CUBEMAP;

	if( hdr->dwFlags & DDS_MIPMAPCOUNT )
		image.num_mips = hdr->dwMipMapCount; // get actual mip count
}

// half-D v0.8.1 ── CPU DXT decode for refs without hardware S3TC ──────────
// Under the native GLES ref, GL_TextureImageCompressed is compiled out
// (XASH_GLES) and the loader used to reject every compressed DDS —
// "texture not found" for the whole game, the v0.5.x/v0.7.0 native-path
// blocker. Decode DXT1/DXT3/DXT5 chains to RGBA32 here so the standard
// raw-upload mip walk in GL_UploadTexture handles them. Only fires when
// the ref did not set IL_DDS_HARDWARE (never on gl4es or desktop GL).
// BC4/BC5/BC6H/BC7/ATI2 and volume textures stay rejected as before.
static void Image_DXTExpand565( word c, byte rgb[3] )
{
	const int r = ( c >> 11 ) & 0x1f;
	const int g = ( c >> 5 ) & 0x3f;
	const int b = c & 0x1f;

	rgb[0] = ( r << 3 ) | ( r >> 2 );
	rgb[1] = ( g << 2 ) | ( g >> 4 );
	rgb[2] = ( b << 3 ) | ( b >> 2 );
}

// decode the 4x4 color part of a DXT block; 8 bytes, DXT1 semantics
// (punchthrough) when fourColor is false, DXT3/5 semantics otherwise
static void Image_DXTDecodeColorBlock( const byte *in, byte out[16*4], qboolean fourColor )
{
	byte colors[4][3];

	const word c0 = in[0] | ( in[1] << 8 );
	const word c1 = in[2] | ( in[3] << 8 );

	Image_DXTExpand565( c0, colors[0] );
	Image_DXTExpand565( c1, colors[1] );

	if( fourColor || c0 > c1 )
	{
		for( int i = 0; i < 3; i++ )
		{
			colors[2][i] = ( 2 * colors[0][i] + colors[1][i] ) / 3;
			colors[3][i] = ( colors[0][i] + 2 * colors[1][i] ) / 3;
		}
	}
	else
	{
		for( int i = 0; i < 3; i++ )
		{
			colors[2][i] = ( colors[0][i] + colors[1][i] ) / 2;
			colors[3][i] = 0; // GL_COMPRESSED_RGB_S3TC_DXT1: no alpha, index 3 is black
		}
	}

	const uint indices = in[4] | ( in[5] << 8 ) | ( in[6] << 16 ) | ( in[7] << 24 );

	for( int i = 0; i < 16; i++ )
	{
		const int idx = ( indices >> ( 2 * i )) & 3;

		out[i*4+0] = colors[idx][0];
		out[i*4+1] = colors[idx][1];
		out[i*4+2] = colors[idx][2];
		out[i*4+3] = 255;
	}
}

static qboolean Image_DecodeDXTRGBA( const byte *fin )
{
	if( fin == NULL || image.depth != 1 )
		return false;

	const int numSides = FBitSet( image.flags, IMAGE_CUBEMAP ) ? 6 : 1;
	const int numMips = Q_max( 1, image.num_mips );
	const size_t blockBytes = ( image.type == PF_DXT1 ) ? 8 : 16;

	// the input must hold the whole chain, exactly as Image_DXTCalcSize sized it
	size_t need = 0, total = 0;

	for( int s = 0; s < numSides; s++ )
	{
		for( int m = 0; m < numMips; m++ )
		{
			const int w = Q_max( 1, image.width >> m );
			const int h = Q_max( 1, image.height >> m );

			need += (( w + 3 ) / 4 ) * (( h + 3 ) / 4 ) * blockBytes;
			total += (size_t)w * h * 4;
		}
	}

	if( need > image.size )
		return false;

	byte *out = Mem_Malloc( host.imagepool, total );
	byte *o = out;
	const byte *in = fin;

	for( int s = 0; s < numSides; s++ )
	{
		for( int m = 0; m < numMips; m++ )
		{
			const int w = Q_max( 1, image.width >> m );
			const int h = Q_max( 1, image.height >> m );

			for( int by = 0; by < ( h + 3 ) / 4; by++ )
			{
				for( int bx = 0; bx < ( w + 3 ) / 4; bx++, in += blockBytes )
				{
					byte block[16*4];
					const byte *alpha = NULL;

					if( image.type == PF_DXT3 )
					{
						alpha = in; // 16 x 4-bit alpha first
						Image_DXTDecodeColorBlock( in + 8, block, true );
					}
					else if( image.type == PF_DXT5 )
					{
						alpha = in; // interpolated alpha block first
						Image_DXTDecodeColorBlock( in + 8, block, true );
					}
					else
					{
						Image_DXTDecodeColorBlock( in, block, false );
					}

					if( image.type == PF_DXT3 )
					{
						for( int i = 0; i < 16; i++ )
						{
							// 16 x 4-bit alpha, low nibble = even pixel; expand to full range
							const int a = ( alpha[i >> 1] >> (( i & 1 ) * 4 )) & 0x0f;
							block[i*4+3] = ( a << 4 ) | a;
						}
					}
					else if( image.type == PF_DXT5 )
					{
						const int a0 = alpha[0], a1 = alpha[1];
						int alut[8];

						// BC4/alpha semantics: indices 0/1 are always the
						// endpoints; 0 and 255 codewords exist only in the
						// a0 <= a1 branch (a0 > a1 is 6 interpolations)
						alut[0] = a0;
						alut[1] = a1;

						if( a0 > a1 )
						{
							for( int i = 0; i < 6; i++ )
								alut[2+i] = (( 6 - i ) * a0 + ( 1 + i ) * a1 ) / 7;
						}
						else
						{
							for( int i = 0; i < 4; i++ )
								alut[2+i] = (( 4 - i ) * a0 + ( 1 + i ) * a1 ) / 5;
							alut[6] = 0;
							alut[7] = 255;
						}

						uint bits = alpha[2] | ( alpha[3] << 8 ) | ( alpha[4] << 16 ) | ((uint)alpha[5] << 24 );

						for( int i = 0; i < 16; i++ )
							block[i*4+3] = alut[( bits >> ( 3 * i )) & 7];
					}

					// blit the block, clipping edge blocks to the real w/h
					for( int py = 0; py < 4; py++ )
					{
						const int y = by * 4 + py;

						if( y >= h ) break;

						for( int px = 0; px < 4; px++ )
						{
							const int x = bx * 4 + px;

							if( x >= w ) break;

							byte *dst = &o[( y * w + x ) * 4];
							const byte *src = &block[( py * 4 + px ) * 4];

							dst[0] = src[0];
							dst[1] = src[1];
							dst[2] = src[2];
							dst[3] = src[3];
						}
					}
				}
			}

			o += (size_t)w * h * 4;
		}
	}

	image.rgba = out;
	image.size = total;
	image.type = PF_RGBA_32;
	// keep image.encode: the YCoCg / NORMAL_AG reconstructions work from the
	// decoded RGBA channels in the client shader, so the flag must survive

	return true;
}

static size_t Image_DXTCalcMipmapSize( dds_t *hdr )
{
	size_t	buffsize = 0;

	// now correct buffer size
	for( int i = 0; i < Q_max( 1, ( hdr->dwMipMapCount )); i++ )
	{
		int width = Q_max( 1, ( hdr->dwWidth >> i ));
		int height = Q_max( 1, ( hdr->dwHeight >> i ));
		int depth = Q_max( 1, ( image.depth >> i ));
		buffsize += Image_ComputeSize( image.type, width, height, depth );
	}

	return buffsize;
}

static uint Image_DXTCalcSize( const char *name, dds_t *hdr, size_t filesize )
{
	size_t buffsize = 0;

	if( hdr->dsCaps.dwCaps2 & DDS_CUBEMAP )
	{
		// cubemap w*h always match for all sides
		buffsize = Image_DXTCalcMipmapSize( hdr ) * 6;
	}
	else if( hdr->dwFlags & DDS_MIPMAPCOUNT )
	{
		// if mipcount > 1
		buffsize = Image_DXTCalcMipmapSize( hdr );
	}
	else if( hdr->dwFlags & ( DDS_LINEARSIZE|DDS_PITCH ))
	{
		// just in case (no need, really)
		buffsize = hdr->dwLinearSize;

		// half-D: some tools write a per-row pitch under DDSD_PITCH for
		// uncompressed DDS. Using it as the total size made the loader
		// allocate w*4 bytes for a w*h*4 texture and trash the heap (the
		// v0.4.1 device crash: trashed sentinel at the img_dds.c alloc,
		// then SEGVs inside libft2/hwui/RefBase on the corrupted heap).
		// DDSD_PITCH means per-row; DDSD_LINEARSIZE already is the total.
		if(( hdr->dwFlags & DDS_PITCH ) && !( hdr->dwFlags & DDS_LINEARSIZE )
			&& ( image.type == PF_BGRA_32 || image.type == PF_RGBA_32
			|| image.type == PF_BGR_24 || image.type == PF_RGB_24
			|| image.type == PF_LUMINANCE ))
		{
			buffsize = (size_t)hdr->dwLinearSize * hdr->dwHeight;
		}
	}
	else
	{
		// pretty solution for microsoft bug
		buffsize = Image_DXTCalcMipmapSize( hdr );
	}

	if( filesize != buffsize ) // main check
	{
		Con_DPrintf( S_WARN "%s: (%s) probably corrupted (%zu should be %zu)\n", __func__, name, buffsize, filesize );
		if( buffsize > filesize )
			return false;
	}

	return buffsize;
}

static void Image_DXTAdjustVolume( dds_t *hdr )
{
	if( hdr->dwDepth <= 1 )
		return;

	hdr->dwLinearSize = Image_ComputeSize( image.type, hdr->dwWidth, hdr->dwHeight, hdr->dwDepth );
	hdr->dwFlags |= DDS_LINEARSIZE;
}

/*
=============
Image_LoadDDS
=============
*/
qboolean Image_LoadDDS( const char *name, const byte *buffer, fs_offset_t filesize )
{
	dds_t	header;
	dds_header_dxt10_t header2;

	if( filesize < sizeof( header ))
		return false;

	memcpy( &header, buffer, sizeof( header ));
	le_struct_swap( dds_swap, &header );

	if( header.dwIdent != DDSHEADER )
		return false; // it's not a dds file, just skip it

	if( header.dwSize != sizeof( header ) - sizeof( uint )) // size of the structure (minus MagicNum)
	{
		Con_DPrintf( S_ERROR "%s: (%s) have corrupted header\n", __func__, name );
		return false;
	}

	if( header.dsPixelFormat.dwSize != sizeof( dds_pixf_t )) // size of the structure
	{
		Con_DPrintf( S_ERROR "%s: (%s) have corrupt pixelformat header\n", __func__, name );
		return false;
	}

	int headersOffset = sizeof( header );
	if( header.dsPixelFormat.dwFourCC == TYPE_DX10 )
	{
		memcpy( &header2, buffer + sizeof( header ), sizeof( header2 ));
		le_struct_swap( dds_dxt10_swap, &header2 );
		headersOffset += sizeof( header2 );
	}

	image.width = header.dwWidth;
	image.height = header.dwHeight;

	if( header.dwFlags & DDS_DEPTH )
		image.depth = header.dwDepth;
	else image.depth = 1;

	if( !Image_ValidSize( name )) return false;

	Image_DXTGetPixelFormat( &header, &header2 ); // and image type too :)
	Image_DXTAdjustVolume( &header );

	if( !Image_CheckFlag( IL_DDS_HARDWARE ) && ImageCompressed( image.type )
		&& image.type != PF_DXT1 && image.type != PF_DXT3 && image.type != PF_DXT5 )
		return false; // silently rejected (no CPU fallback for BC4/BC5/BC6H/BC7/ATI2)

	if( image.type == PF_UNKNOWN )
	{
		Con_DPrintf( S_ERROR "%s: (%s) has unrecognized type\n", __func__, name );
		return false;
	}

	image.size = Image_DXTCalcSize( name, &header, filesize - headersOffset );
	if( image.size == 0 ) return false; // just in case
	byte *fin = (byte *)( buffer + headersOffset );

	// copy an encode method
	image.encode = (word)header.dwReserved1[0];

	switch( image.encode )
	{
	case DXT_ENCODE_COLOR_YCoCg:
		SetBits( image.flags, IMAGE_HAS_COLOR );
		break;
	case DXT_ENCODE_NORMAL_AG_ORTHO:
	case DXT_ENCODE_NORMAL_AG_STEREO:
	case DXT_ENCODE_NORMAL_AG_PARABOLOID:
	case DXT_ENCODE_NORMAL_AG_QUARTIC:
	case DXT_ENCODE_NORMAL_AG_AZIMUTHAL:
		SetBits( image.flags, IMAGE_HAS_COLOR );
		break;
	default:	// check for real alpha-pixels
		if( image.type == PF_DXT3 && Image_CheckDXT3Alpha( &header, fin ))
			SetBits( image.flags, IMAGE_HAS_ALPHA );
		else if( image.type == PF_DXT5 && Image_CheckDXT5Alpha( &header, fin ))
			SetBits( image.flags, IMAGE_HAS_ALPHA );
		else if( image.type == PF_BC5_SIGNED || image.type == PF_BC5_UNSIGNED )
			SetBits( image.flags, IMAGE_HAS_ALPHA );
		else if( image.type == PF_BC7_UNORM || image.type == PF_BC7_SRGB )
			SetBits( image.flags, IMAGE_HAS_ALPHA );
		if( !FBitSet( header.dsPixelFormat.dwFlags, DDS_LUMINANCE ))
			SetBits( image.flags, IMAGE_HAS_COLOR );
		if (image.type == PF_BGRA_32 || image.type == PF_RGBA_32)
			SetBits( image.flags, IMAGE_HAS_ALPHA );
		
		break;
	}

	if( image.type == PF_LUMINANCE )
		ClearBits( image.flags, IMAGE_HAS_COLOR|IMAGE_HAS_ALPHA );

	if( header.dwReserved1[1] != 0 )
	{
		// store texture reflectivity
		image.fogParams[0] = ((header.dwReserved1[1] & 0x000000FF) >> 0 );
		image.fogParams[1] = ((header.dwReserved1[1] & 0x0000FF00) >> 8 );
		image.fogParams[2] = ((header.dwReserved1[1] & 0x00FF0000) >> 16);
		image.fogParams[3] = ((header.dwReserved1[1] & 0xFF000000) >> 24);
	}

	// half-D v0.8.1: native GLES ref (no IL_DDS_HARDWARE, compressed upload
	// compiled out under XASH_GLES) — decode DXT1/3/5 to an RGBA32 chain so
	// the raw-upload mip walk handles it, instead of failing the load (the
	// v0.5.x/v0.7.0 "every .dds not found" native-path blocker).
	if( !Image_CheckFlag( IL_DDS_HARDWARE ) && ImageCompressed( image.type ))
	{
		if( !Image_DecodeDXTRGBA( fin ))
		{
			Con_DPrintf( S_ERROR "%s: (%s) DXT CPU decode failed\n", __func__, name );
			return false;
		}

		SetBits( image.flags, IMAGE_DDS_FORMAT );
		return true;
	}

	// dds files will be uncompressed on a render. requires minimal of info for set this
	image.rgba = Mem_Malloc( host.imagepool, image.size );
	memcpy( image.rgba, fin, image.size );
	SetBits( image.flags, IMAGE_DDS_FORMAT );

	return true;
}
