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

