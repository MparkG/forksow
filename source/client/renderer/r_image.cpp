/*
Copyright (C) 1997-2001 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*/

#include "r_local.h"
#include "r_imagelib.h"
#include "qcommon/hash.h"

#include "blue_noise.h"
#include "stb/stb_image.h"

#define MAX_GLIMAGES        8192
#define IMAGES_HASH_SIZE    64

static image_t r_images[MAX_GLIMAGES];
static image_t r_images_hash_headnode[IMAGES_HASH_SIZE], *r_free_images;
static qmutex_t *r_imagesLock;

static int r_unpackAlignment;

static unsigned *r_8to24table[2];

static mempool_t *r_imagesPool;
static char *r_imagePathBuf, *r_imagePathBuf2;
static size_t r_sizeof_imagePathBuf, r_sizeof_imagePathBuf2;

#undef ENSUREBUFSIZE
#define ENSUREBUFSIZE( buf,need ) \
	if( r_sizeof_ ## buf < need ) \
	{ \
		if( r_ ## buf ) { \
			R_Free( r_ ## buf );} \
		r_sizeof_ ## buf += ( ( ( need ) & ( MAX_QPATH - 1 ) ) + 1 ) * MAX_QPATH; \
		r_ ## buf = ( char * ) R_MallocExt( r_imagesPool, r_sizeof_ ## buf, 0, 0 ); \
	}

static int gl_anisotropic_filter = 0;

/*
* R_AllocTextureNum
*/
static void R_AllocTextureNum( image_t *tex ) {
	glGenTextures( 1, &tex->texnum );
}

/*
* R_FreeTextureNum
*/
static void R_FreeTextureNum( image_t *tex ) {
	if( !tex->texnum ) {
		return;
	}

	glDeleteTextures( 1, &tex->texnum );
	tex->texnum = 0;

	RB_FlushTextureCache();
}

/*
* R_TextureTarget
*/
int R_TextureTarget( int flags, int *uploadTarget ) {
	int target, target2;

	target = target2 = GL_TEXTURE_2D;

	if( uploadTarget ) {
		*uploadTarget = target2;
	}
	return target;
}

/*
* R_BindImage
*/
static void R_BindImage( const image_t *tex ) {
	glBindTexture( R_TextureTarget( tex->flags, NULL ), tex->texnum );
	RB_FlushTextureCache();
}

/*
* R_UnbindImage
*/
static void R_UnbindImage( const image_t *tex ) {
	glBindTexture( R_TextureTarget( tex->flags, NULL ), 0 );
	RB_FlushTextureCache();
}

/*
* R_UnpackAlignment
*/
static void R_UnpackAlignment( int value ) {
	if( r_unpackAlignment == value ) {
		return;
	}

	r_unpackAlignment = value;
	glPixelStorei( GL_UNPACK_ALIGNMENT, value );
}

/*
* R_AnisotropicFilter
*/
void R_AnisotropicFilter( int value ) {
	int i, old;
	image_t *glt;

	if( !glConfig.ext.texture_filter_anisotropic ) {
		return;
	}

	old = gl_anisotropic_filter;
	gl_anisotropic_filter = bound( 1, value, glConfig.maxTextureFilterAnisotropic );
	if( gl_anisotropic_filter == old ) {
		return;
	}

	// change all the existing mipmap texture objects
	for( i = 1, glt = r_images; i < MAX_GLIMAGES; i++, glt++ ) {
		if( !glt->texnum ) {
			continue;
		}
		if( ( glt->flags & ( IT_NOFILTERING | IT_DEPTH | IT_NOMIPMAP ) ) ) {
			continue;
		}

		R_BindImage( glt );

		glTexParameteri( R_TextureTarget( glt->flags, NULL ), GL_TEXTURE_MAX_ANISOTROPY_EXT, gl_anisotropic_filter );
	}
}

/*
* R_PrintImageList
*/
void R_PrintImageList( const char *mask, bool ( *filter )( const char *mask, const char *value ) ) {
	int i, bpp, bytes;
	int numImages;
	image_t *image;
	double texels = 0, add, total_bytes = 0;

	Com_Printf( "------------------\n" );

	numImages = 0;
	for( i = 0, image = r_images; i < MAX_GLIMAGES; i++, image++ ) {
		if( !image->texnum ) {
			continue;
		}
		if( !image->upload_width || !image->upload_height || !image->layers ) {
			continue;
		}
		if( filter && !filter( mask, image->name ) ) {
			continue;
		}
		if( !image->loaded || image->missing ) {
			continue;
		}

		add = image->upload_width * image->upload_height * image->layers;
		if( !( image->flags & ( IT_DEPTH | IT_NOFILTERING | IT_NOMIPMAP ) ) ) {
			add = (unsigned)floor( add / 0.75 );
		}
		texels += add;

		bpp = image->samples;
		if( image->flags & IT_DEPTH ) {
			bpp = 0; // added later
		}

		if( image->flags & ( IT_DEPTH | IT_DEPTHRB ) ) {
			bpp += 3;
		}

		bytes = add * bpp;
		total_bytes += bytes;

		Com_Printf( " %iW x %iH", image->upload_width, image->upload_height );
		if( image->layers > 1 ) {
			Com_Printf( " x %iL", image->layers );
		}
		Com_Printf( " x %s%iBPP: %s%s%s %.1f KB\n", image->flags & IT_SRGB ? "s" : "", bpp, image->name, image->extension,
					( ( image->flags & ( IT_NOMIPMAP | IT_NOFILTERING ) ) ? "" : " (mip)" ), bytes / 1024.0 );

		numImages++;
	}

	Com_Printf( "Total texels count (counting mipmaps, approx): %.0f\n", texels );
	Com_Printf( "%i RGBA images, totalling %.3f megabytes\n", numImages, total_bytes / 1048576.0 );
}

/*
=================================================================

TEMPORARY IMAGE BUFFERS

=================================================================
*/

enum {
	TEXTURE_LOADING_BUF,
	TEXTURE_RESAMPLING_BUF,
	TEXTURE_LINE_BUF,

	NUM_IMAGE_BUFFERS
};

static uint8_t *r_screenShotBuffer;
static size_t r_screenShotBufferSize;

static uint8_t *r_imageBuffers[NUM_IMAGE_BUFFERS];
static size_t r_imageBufSize[NUM_IMAGE_BUFFERS];

#define R_PrepareImageBuffer( buffer,size ) _R_PrepareImageBuffer( buffer,size,__FILE__,__LINE__ )

/*
* R_PrepareImageBuffer
*/
static uint8_t *_R_PrepareImageBuffer( int buffer, size_t size, const char *filename, int fileline ) {
	if( r_imageBufSize[buffer] < size ) {
		r_imageBufSize[buffer] = size;
		if( r_imageBuffers[buffer] ) {
			R_Free( r_imageBuffers[buffer] );
		}
		r_imageBuffers[buffer] = ( uint8_t * ) R_MallocExt( r_imagesPool, size, 0, 1 );
	}

	memset( r_imageBuffers[buffer], 255, size );

	return r_imageBuffers[buffer];
}

/*
* R_FreeImageBuffers
*/
void R_FreeImageBuffers( void ) {
	for( int i = 0; i < NUM_IMAGE_BUFFERS; i++ ) {
		if( r_imageBuffers[i] ) {
			R_Free( r_imageBuffers[i] );
			r_imageBuffers[i] = NULL;
		}
		r_imageBufSize[i] = 0;
	}
}

/*
* R_ReadImageFromDisk
*/
static int R_ReadImageFromDisk( char *pathname, size_t pathname_size,
								uint8_t **pic, int *width, int *height, int *flags ) {
	const char *extension;
	int samples;

	*pic = NULL;
	*width = *height = 0;
	samples = 0;

	extension = ri.FS_FirstExtension( pathname, IMAGE_EXTENSIONS, NUM_IMAGE_EXTENSIONS );
	if( extension ) {
		COM_ReplaceExtension( pathname, extension, pathname_size );

		r_imginfo_t imginfo = IMG_LoadImage( pathname );

		*pic = imginfo.pixels;
		*width = imginfo.width;
		*height = imginfo.height;
		samples = imginfo.samples;
	}

	return samples;
}

/*
* R_ScaledImageSize
*/
static int R_ScaledImageSize( int width, int height, int *scaledWidth, int *scaledHeight, int flags, int mips, int minmipsize, bool forceNPOT ) {
	int maxSize;
	int mip = 0;
	int clampedWidth, clampedHeight;

	if( flags & ( IT_FRAMEBUFFER | IT_DEPTH ) ) {
		maxSize = glConfig.maxRenderbufferSize;
	} else {
		maxSize = glConfig.maxTextureSize;
	}

	// try to find the smallest supported texture size from mipmaps
	clampedWidth = width;
	clampedHeight = height;
	while( ( clampedWidth > maxSize ) || ( clampedHeight > maxSize ) ) {
		++mip;
		clampedWidth >>= 1;
		clampedHeight >>= 1;
		if( !clampedWidth ) {
			clampedWidth = 1;
		}
		if( !clampedHeight ) {
			clampedHeight = 1;
		}
	}

	if( mip >= mips ) {
		// the smallest size is not in mipmaps, so ignore mipmaps and aspect ratio and simply clamp
		*scaledWidth = min( width, maxSize );
		*scaledHeight = min( height, maxSize );
		return -1;
	}

	*scaledWidth = clampedWidth;
	*scaledHeight = clampedHeight;
	return mip;
}

/*
* R_FlipTexture
*/
static void R_FlipTexture( const uint8_t *in, uint8_t *out, int width, int height,
						   int samples, bool flipx, bool flipy, bool flipdiagonal ) {
	int i, x, y;
	const uint8_t *p, *line;
	int row_inc = ( flipy ? -samples : samples ) * width, col_inc = ( flipx ? -samples : samples );
	int row_ofs = ( flipy ? ( height - 1 ) * width * samples : 0 ), col_ofs = ( flipx ? ( width - 1 ) * samples : 0 );

	if( !in ) {
		return;
	}

	if( flipdiagonal ) {
		for( x = 0, line = in + col_ofs; x < width; x++, line += col_inc )
			for( y = 0, p = line + row_ofs; y < height; y++, p += row_inc, out += samples )
				for( i = 0; i < samples; i++ )
					out[i] = p[i];
	} else {
		for( y = 0, line = in + row_ofs; y < height; y++, line += row_inc )
			for( x = 0, p = line + col_ofs; x < width; x++, p += col_inc, out += samples )
				for( i = 0; i < samples; i++ )
					out[i] = p[i];
	}
}

/*
* R_ResampleTexture
*/
static void R_ResampleTexture( const uint8_t *in, int inwidth, int inheight, uint8_t *out,
							   int outwidth, int outheight, int samples, int alignment ) {
	int i, j, k;
	int inwidthS, outwidthS;
	unsigned int frac, fracstep;
	const uint8_t *inrow, *inrow2, *pix1, *pix2, *pix3, *pix4;
	unsigned *p1, *p2;
	uint8_t *opix;

	if( inwidth == outwidth && inheight == outheight ) {
		memcpy( out, in, inheight * ALIGN( inwidth * samples, alignment ) );
		return;
	}

	p1 = ( unsigned * )R_PrepareImageBuffer( TEXTURE_LINE_BUF, outwidth * sizeof( *p1 ) * 2 );
	p2 = p1 + outwidth;

	fracstep = inwidth * 0x10000 / outwidth;

	frac = fracstep >> 2;
	for( i = 0; i < outwidth; i++ ) {
		p1[i] = samples * ( frac >> 16 );
		frac += fracstep;
	}

	frac = 3 * ( fracstep >> 2 );
	for( i = 0; i < outwidth; i++ ) {
		p2[i] = samples * ( frac >> 16 );
		frac += fracstep;
	}

	inwidthS = ALIGN( inwidth * samples, alignment );
	outwidthS = ALIGN( outwidth * samples, alignment );
	for( i = 0; i < outheight; i++, out += outwidthS ) {
		inrow = in + inwidthS * (int)( ( i + 0.25 ) * inheight / outheight );
		inrow2 = in + inwidthS * (int)( ( i + 0.75 ) * inheight / outheight );
		for( j = 0; j < outwidth; j++ ) {
			pix1 = inrow + p1[j];
			pix2 = inrow + p2[j];
			pix3 = inrow2 + p1[j];
			pix4 = inrow2 + p2[j];
			opix = out + j * samples;

			for( k = 0; k < samples; k++ )
				opix[k] = ( pix1[k] + pix2[k] + pix3[k] + pix4[k] ) >> 2;
		}
	}
}

/*
* R_MipMap
*
* Operates in place, quartering the size of the texture
*/
static void R_MipMap( uint8_t *in, int width, int height, int samples, int alignment ) {
	int i, j, k;
	int instride = ALIGN( width * samples, alignment );
	int outwidth, outheight, outpadding;
	uint8_t *out = in;
	uint8_t *next;
	int inofs;

	outwidth = width >> 1;
	outheight = height >> 1;
	if( !outwidth ) {
		outwidth = 1;
	}
	if( !outheight ) {
		outheight = 1;
	}
	outpadding = ALIGN( outwidth * samples, alignment ) - outwidth * samples;

	for( i = 0; i < outheight; i++, in += instride * 2, out += outpadding ) {
		next = ( ( ( i << 1 ) + 1 ) < height ) ? ( in + instride ) : in;
		for( j = 0, inofs = 0; j < outwidth; j++, inofs += samples ) {
			if( ( ( j << 1 ) + 1 ) < width ) {
				for( k = 0; k < samples; ++k, ++inofs )
					*( out++ ) = ( in[inofs] + in[inofs + samples] + next[inofs] + next[inofs + samples] ) >> 2;
			} else {
				for( k = 0; k < samples; ++k, ++inofs )
					*( out++ ) = ( in[inofs] + next[inofs] ) >> 1;
			}
		}
	}
}

/*
* R_TextureInternalFormat
*/
static int R_TextureInternalFormat( int samples, int flags, int pixelType ) {
	bool sRGB = ( flags & IT_SRGB ) != 0;

	if( samples == 3 ) {
		if( sRGB ) {
			return GL_SRGB;
		}
		return GL_RGB;
	}

	if( samples == 2 ) {
		/* assert( !sRGB ); */ // TODO: WTF
		return GL_RG;
	}

	if( samples == 1 ) {
		/* assert( !sRGB ); */ // TODO: WTF
		return GL_RED;
	}

	if( sRGB ) {
		return GL_SRGB_ALPHA;
	}
	return GL_RGBA;
}

/*
* R_TextureFormat
*/
static void R_TextureFormat( int flags, int samples, int *comp, int *format, int *type ) {
	if( flags & IT_DEPTH ) {
		*comp = *format = GL_DEPTH_COMPONENT;
		*type = GL_UNSIGNED_INT;
	} else if( flags & IT_FRAMEBUFFER ) {
		if( flags & IT_FLOAT ) {
			*type = GL_FLOAT;
			*comp = samples == 4 ? GL_RGBA16F : GL_RGB16F;
		}
		else {
			*type = GL_UNSIGNED_BYTE;
			*comp = samples == 4 ? GL_RGBA : GL_RGB;
		}
		*format = samples == 4 ? GL_RGBA : GL_RGB;
	} else {
		if( samples == 4 ) {
			*format = GL_RGBA;
		} else if( samples == 3 ) {
			*format = GL_RGB;
		} else if( samples == 2 ) {
			*format = GL_RG;
		} else {
			*format = GL_RED;
		}

		if( flags & IT_FLOAT ) {
			*type = GL_FLOAT;
			if( samples == 4 ) {
				*comp = GL_RGBA16F;
			} else if( samples == 3 ) {
				*comp = GL_RGB16F;
			} else if( samples == 2 ) {
				*comp = GL_RG16F;
			} else {
				*comp = GL_R16F;
			}
		} else {
			*type = GL_UNSIGNED_BYTE;
			*comp = *format;

			*comp = R_TextureInternalFormat( samples, flags, GL_UNSIGNED_BYTE );
		}
	}
}

/*
* R_SetupTexParameters
*/
static void R_SetupTexParameters( int flags, int upload_width, int upload_height, int minmipsize, int samples ) {
	int target = R_TextureTarget( flags, NULL );
	int wrap = GL_REPEAT;

	if( flags & IT_NOFILTERING ) {
		glTexParameteri( target, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
		glTexParameteri( target, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
	} else if( flags & IT_DEPTH ) {
		glTexParameteri( target, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
		glTexParameteri( target, GL_TEXTURE_MAG_FILTER, GL_LINEAR );

		if( glConfig.ext.texture_filter_anisotropic ) {
			glTexParameteri( target, GL_TEXTURE_MAX_ANISOTROPY_EXT, 1 );
		}
	} else if( !( flags & IT_NOMIPMAP ) ) {
		glTexParameteri( target, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR );
		glTexParameteri( target, GL_TEXTURE_MAG_FILTER, GL_LINEAR );

		if( glConfig.ext.texture_filter_anisotropic ) {
			glTexParameteri( target, GL_TEXTURE_MAX_ANISOTROPY_EXT, gl_anisotropic_filter );
		}

		if( minmipsize > 1 ) {
			int mipwidth = upload_width, mipheight = upload_height, mip = 0;
			while( ( mipwidth > minmipsize ) || ( mipheight > minmipsize ) ) {
				++mip;
				mipwidth >>= 1;
				mipheight >>= 1;
				if( !mipwidth ) {
					mipwidth = 1;
				}
				if( !mipheight ) {
					mipheight = 1;
				}
			}
			glTexParameteri( target, GL_TEXTURE_MAX_LEVEL, mip );
		}
	} else {
		glTexParameteri( target, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
		glTexParameteri( target, GL_TEXTURE_MAG_FILTER, GL_LINEAR );

		if( glConfig.ext.texture_filter_anisotropic ) {
			glTexParameteri( target, GL_TEXTURE_MAX_ANISOTROPY_EXT, 1 );
		}
	}

	// clamp if required
	if( flags & IT_CLAMP ) {
		wrap = GL_CLAMP_TO_EDGE;
	}
	glTexParameteri( target, GL_TEXTURE_WRAP_S, wrap );
	glTexParameteri( target, GL_TEXTURE_WRAP_T, wrap );

	if( flags & IT_ALPHAMASK ) {
		glTexParameteri( target, GL_TEXTURE_SWIZZLE_R, GL_ONE );
		glTexParameteri( target, GL_TEXTURE_SWIZZLE_G, GL_ONE );
		glTexParameteri( target, GL_TEXTURE_SWIZZLE_B, GL_ONE );
		glTexParameteri( target, GL_TEXTURE_SWIZZLE_A, GL_RED );
	} else if( samples == 1 ) {
		glTexParameteri( target, GL_TEXTURE_SWIZZLE_R, GL_RED );
		glTexParameteri( target, GL_TEXTURE_SWIZZLE_G, GL_RED );
		glTexParameteri( target, GL_TEXTURE_SWIZZLE_B, GL_RED );
		glTexParameteri( target, GL_TEXTURE_SWIZZLE_A, GL_ONE );
	} else if( samples == 2 ) {
		glTexParameteri( target, GL_TEXTURE_SWIZZLE_R, GL_RED );
		glTexParameteri( target, GL_TEXTURE_SWIZZLE_G, GL_RED );
		glTexParameteri( target, GL_TEXTURE_SWIZZLE_B, GL_RED );
		glTexParameteri( target, GL_TEXTURE_SWIZZLE_A, GL_GREEN );
	}
}

/*
* R_Upload32
*/
static void R_Upload32( const uint8_t *data,
						int x, int y, int width, int height,
						int flags, int minmipsize, int *upload_width, int *upload_height, int samples,
						bool subImage, bool noScale ) {
	int comp, format, type;
	int target;
	uint8_t *scaled = NULL;
	int scaledWidth, scaledHeight;

	assert( samples );

	R_ScaledImageSize( width, height, &scaledWidth, &scaledHeight, flags, 1, minmipsize, subImage && noScale );

	R_TextureTarget( flags, &target );

	if( upload_width ) {
		*upload_width = scaledWidth;
	}
	if( upload_height ) {
		*upload_height = scaledHeight;
	}

	R_TextureFormat( flags, samples, &comp, &format, &type );

	R_SetupTexParameters( flags, scaledWidth, scaledHeight, minmipsize, samples );

	R_UnpackAlignment( 1 );

	if( scaledWidth == width && scaledHeight == height && ( flags & IT_NOMIPMAP ) ) {
		if( subImage ) {
			glTexSubImage2D( target, 0, x, y, scaledWidth, scaledHeight, format, type, data );
		} else {
			glTexImage2D( target, 0, comp, scaledWidth, scaledHeight, 0, format, type, data );
		}
	} else {
		uint8_t *mip;

		if( !scaled ) {
			scaled = R_PrepareImageBuffer( TEXTURE_RESAMPLING_BUF, scaledWidth * scaledHeight * samples );
		}

		// resample the texture
		mip = scaled;
		if( data ) {
			R_ResampleTexture( data, width, height, (uint8_t *)mip, scaledWidth, scaledHeight, samples, 1 );
		} else {
			mip = NULL;
		}

		if( subImage ) {
			glTexSubImage2D( target, 0, x, y, scaledWidth, scaledHeight, format, type, mip );
		} else {
			glTexImage2D( target, 0, comp, scaledWidth, scaledHeight, 0, format, type, mip );
		}

		// mipmaps generation
		if( !( flags & IT_NOMIPMAP ) && mip ) {
			int miplevel = 0;
			int w = scaledWidth;
			int h = scaledHeight;
			while( w > minmipsize || h > minmipsize ) {
				R_MipMap( mip, w, h, samples, 1 );

				w >>= 1;
				h >>= 1;
				if( w < 1 ) {
					w = 1;
				}
				if( h < 1 ) {
					h = 1;
				}
				miplevel++;

				if( subImage ) {
					glTexSubImage2D( target, miplevel, x, y, w, h, format, type, mip );
				} else {
					glTexImage2D( target, miplevel, comp, w, h, 0, format, type, mip );
				}
			}
		}
	}
}

/*
* R_LoadImageFromDisk
*/
static bool R_LoadImageFromDisk( image_t *image ) {
	int flags = image->flags;
	size_t len = strlen( image->name );
	char pathname[1024];
	size_t pathsize = sizeof( pathname );
	int width = 1, height = 1, samples = 1;
	bool loaded = false;

	if( len >= pathsize - 7 ) {
		return false;
	}

	memcpy( pathname, image->name, len + 1 );

	uint8_t *pic = NULL;

	Q_strncatz( pathname, ".jpg", pathsize );
	samples = R_ReadImageFromDisk( pathname, pathsize, &pic, &width, &height, &flags );

	if( pic ) {
		image->width = width;
		image->height = height;
		image->samples = samples;

		R_BindImage( image );

		R_Upload32( pic, 0, 0, width, height, flags, image->minmipsize, &image->upload_width,
					&image->upload_height, samples, false, false );

		Q_strncpyz( image->extension, &pathname[len], sizeof( image->extension ) );
		loaded = true;
	} else {
		ri.Com_DPrintf( S_COLOR_YELLOW "Missing image: %s\n", image->name );
	}

	if( loaded ) {
		// Update IT_LOADFLAGS that may be set by R_ReadImageFromDisk.
		image->flags = flags;
		R_DeferDataSync();
	}

	return loaded;
}

/*
* R_LinkPic
*/
static image_t *R_LinkPic( unsigned int hash ) {
	image_t *image;

	if( !r_free_images ) {
		return NULL;
	}

	ri.Mutex_Lock( r_imagesLock );

	hash = hash % IMAGES_HASH_SIZE;
	image = r_free_images;
	r_free_images = image->next;

	// link to the list of active images
	image->prev = &r_images_hash_headnode[hash];
	image->next = r_images_hash_headnode[hash].next;
	image->next->prev = image;
	image->prev->next = image;

	ri.Mutex_Unlock( r_imagesLock );

	return image;
}

/*
* R_UnlinkPic
*/
static void R_UnlinkPic( image_t *image ) {
	ri.Mutex_Lock( r_imagesLock );

	// remove from linked active list
	image->prev->next = image->next;
	image->next->prev = image->prev;

	// insert into linked free list
	image->next = r_free_images;
	r_free_images = image;

	ri.Mutex_Unlock( r_imagesLock );
}

/*
* R_AllocImage
*/
static image_t *R_CreateImage( const char *name, int width, int height, int layers, int flags, int minmipsize, int tags, int samples ) {
	image_t *image;
	int name_len = strlen( name );

	unsigned hash = Hash32( name, name_len );

	image = R_LinkPic( hash );
	if( !image ) {
		ri.Com_Error( ERR_DROP, "R_LoadImage: r_numImages == MAX_GLIMAGES" );
		return NULL;
	}

	image->name = ( char * ) R_MallocExt( r_imagesPool, name_len + 1, 0, 1 );
	strcpy( image->name, name );
	image->width = width;
	image->height = height;
	image->layers = layers;
	image->flags = flags;
	image->minmipsize = minmipsize;
	image->samples = samples;
	image->fbo = 0;
	image->texnum = 0;
	image->registrationSequence = rsh.registrationSequence;
	image->tags = tags;
	image->loaded = true;
	image->missing = false;
	image->extension[0] = '\0';

	R_AllocTextureNum( image );

	return image;
}

/*
* R_LoadImage
*/
image_t *R_LoadImage( const char *name, const uint8_t *pic, int width, int height, int flags, int minmipsize, int tags, int samples ) {
	image_t *image;

	if( !glConfig.sSRGB ) {
		flags &= ~IT_SRGB;
	}

	image = R_CreateImage( name, width, height, 1, flags, minmipsize, tags, samples );

	R_BindImage( image );

	R_Upload32( pic, 0, 0, width, height, flags, minmipsize,
				&image->upload_width, &image->upload_height, image->samples, false, false );

	return image;
}

/*
* R_FreeImage
*/
static void R_FreeImage( image_t *image ) {
	R_UnbindImage( image );

	R_FreeTextureNum( image );

	R_Free( image->name );

	image->name = NULL;
	image->texnum = 0;
	image->registrationSequence = 0;

	R_UnlinkPic( image );
}

/*
* R_ReplaceImage
*
* FIXME: not thread-safe!
*/
void R_ReplaceImage( image_t *image, const uint8_t *pic, int width, int height, int flags, int minmipsize, int samples ) {
	assert( image );
	assert( image->texnum );

	if( !glConfig.sSRGB ) {
		flags &= ~IT_SRGB;
	}

	R_BindImage( image );

	if( image->width != width || image->height != height || image->samples != samples ) {
		R_Upload32( pic, 0, 0, width, height, flags, minmipsize,
					&( image->upload_width ), &( image->upload_height ), samples, false, false );
	} else {
		R_Upload32( pic, 0, 0, width, height, flags, minmipsize,
					&( image->upload_width ), &( image->upload_height ), samples, true, false );
	}

	R_DeferDataSync();

	image->flags = flags;
	image->width = width;
	image->height = height;
	image->layers = 1;
	image->minmipsize = minmipsize;
	image->samples = samples;
	image->registrationSequence = rsh.registrationSequence;
}

/*
* R_ReplaceSubImage
*/
void R_ReplaceSubImage( image_t *image, int x, int y, const uint8_t *pic, int width, int height ) {
	assert( image );
	assert( image->texnum );

	R_BindImage( image );

	R_Upload32( pic, x, y, width, height, image->flags, image->minmipsize,
				NULL, NULL, image->samples, true, true );

	R_DeferDataSync();

	image->registrationSequence = rsh.registrationSequence;
}

/*
* R_FindImage
*
* Finds and loads the given image. For synchronous missing images, NULL is returned.
*/
image_t *R_FindImage( const char *name, const char *suffix, int flags, int minmipsize, int tags ) {
	int i, lastDot, lastSlash, searchFlags;
	unsigned int len, key;
	image_t *hnode;
	char *pathname;

	if( !name || !name[0] ) {
		return NULL; //	ri.Com_Error (ERR_DROP, "R_FindImage: NULL name");

	}
	ENSUREBUFSIZE( imagePathBuf, strlen( name ) + ( suffix ? strlen( suffix ) : 0 ) + 5 );
	pathname = r_imagePathBuf;

	lastDot = -1;
	lastSlash = -1;
	for( i = ( name[0] == '/' || name[0] == '\\' ), len = 0; name[i]; i++ ) {
		if( name[i] == '.' ) {
			lastDot = len;
		}
		if( name[i] == '\\' ) {
			pathname[len] = '/';
		} else {
			pathname[len] = tolower( name[i] );
		}
		if( pathname[len] == '/' ) {
			lastSlash = len;
		}
		len++;
	}

	if( len < 5 ) {
		return NULL;
	}

	// don't confuse paths such as /ui/xyz.cache/123 with file extensions
	if( lastDot < lastSlash ) {
		lastDot = -1;
	}

	if( lastDot != -1 ) {
		len = lastDot;
	}

	if( suffix ) {
		for( i = 0; suffix[i]; i++ )
			pathname[len++] = tolower( suffix[i] );
	}

	pathname[len] = 0;

	if( !glConfig.sSRGB ) {
		flags &= ~IT_SRGB;
	}
	searchFlags = flags & ~IT_LOADFLAGS;

	// look for it
	key = Hash32( pathname, len ) % IMAGES_HASH_SIZE;
	hnode = &r_images_hash_headnode[key];
	for( image_t *image = hnode->prev; image != hnode; image = image->prev ) {
		if( ( ( image->flags & ~IT_LOADFLAGS ) == searchFlags ) &&
			!strcmp( image->name, pathname ) && ( image->minmipsize == minmipsize ) ) {
			R_TouchImage( image, tags );
			return image;
		}
	}

	pathname[len] = 0;

	//
	// load the pic from disk
	//
	image_t *image = R_CreateImage( pathname, 1, 1, 1, flags, minmipsize, tags, 1 );
	bool loaded = R_LoadImageFromDisk( image );
	R_UnbindImage( image );

	if( !loaded )
		return NULL;

	image->loaded = true;

	return image;
}

/*
==============================================================================

SCREEN SHOTS

==============================================================================
*/

/*
* R_ScreenShot
*/
void R_ScreenShot( const char *filename, int x, int y, int width, int height,
				   bool flipx, bool flipy, bool flipdiagonal, bool silent ) {
	size_t size, buf_size;
	uint8_t *buffer, *flipped, *rgb, *rgba;
	r_imginfo_t imginfo;
	const char *extension;

	extension = COM_FileExtension( filename );
	if( !extension ) {
		Com_Printf( "R_ScreenShot: Invalid filename\n" );
		return;
	}

	size = width * height * 3;
	// add extra space incase we need to flip the screenshot
	buf_size = width * height * 4 + size;
	if( buf_size > r_screenShotBufferSize ) {
		if( r_screenShotBuffer ) {
			R_Free( r_screenShotBuffer );
		}
		r_screenShotBuffer = ( uint8_t * ) R_MallocExt( r_imagesPool, buf_size, 0, 1 );
		r_screenShotBufferSize = buf_size;
	}

	buffer = r_screenShotBuffer;
	if( flipx || flipy || flipdiagonal ) {
		flipped = buffer + size;
	} else {
		flipped = NULL;
	}

	imginfo.width = width;
	imginfo.height = height;
	imginfo.samples = 3;
	imginfo.pixels = flipped ? flipped : buffer;

	glReadPixels( 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, buffer );

	rgb = rgba = buffer;
	while( ( size_t )( rgb - buffer ) < size ) {
		*( rgb++ ) = *( rgba++ );
		*( rgb++ ) = *( rgba++ );
		*( rgb++ ) = *( rgba++ );
		rgba++;
	}

	if( flipped ) {
		R_FlipTexture( buffer, flipped, width, height, 3,
					   flipx, flipy, flipdiagonal );
	}

	if( WritePNG( filename, &imginfo ) && !silent ) {
		Com_Printf( "Wrote %s\n", filename );
	}
}

//=======================================================

/*
* R_InitNoTexture
*/
static void R_InitNoTexture( int *w, int *h, int *flags, int *samples ) {
	int x, y;
	uint8_t *data;
	uint8_t dottexture[8][8] =
	{
		{ 0, 0, 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 1, 1, 0, 0, 0, 0 },
		{ 0, 1, 1, 1, 1, 0, 0, 0 },
		{ 0, 1, 1, 1, 1, 0, 0, 0 },
		{ 0, 0, 1, 1, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0, 0, 0, 0 },
	};

	//
	// also use this for bad textures, but without alpha
	//
	*w = *h = 8;
	*flags = IT_SRGB;
	*samples = 3;

	// ch : check samples
	data = R_PrepareImageBuffer( TEXTURE_LOADING_BUF, 8 * 8 * 3 );
	for( x = 0; x < 8; x++ ) {
		for( y = 0; y < 8; y++ ) {
			data[( y * 8 + x ) * 3 + 0] = dottexture[x & 3][y & 3] * 127;
			data[( y * 8 + x ) * 3 + 1] = dottexture[x & 3][y & 3] * 127;
			data[( y * 8 + x ) * 3 + 2] = dottexture[x & 3][y & 3] * 127;
		}
	}
}

/*
* R_InitSolidColorTexture
*/
static uint8_t *R_InitSolidColorTexture( int *w, int *h, int *flags, int *samples, int color, bool srgb ) {
	uint8_t *data;

	//
	// solid color texture
	//
	*w = *h = 1;
	*flags = 0;
	*samples = 3;
	if( srgb ) {
		*flags |= IT_SRGB;
	}

	// ch : check samples
	data = R_PrepareImageBuffer( TEXTURE_LOADING_BUF, 1 * 1 * 3 );
	data[0] = data[1] = data[2] = color;
	return data;
}

/*
* R_InitParticleTexture
*/
static void R_InitParticleTexture( int *w, int *h, int *flags, int *samples ) {
	int x, y;
	int dx2, dy, d;
	float dd2;
	uint8_t *data;

	//
	// particle texture
	//
	*w = *h = 16;
	*flags = IT_NOMIPMAP | IT_SRGB;
	*samples = 4;

	data = R_PrepareImageBuffer( TEXTURE_LOADING_BUF, 16 * 16 * 4 );
	for( x = 0; x < 16; x++ ) {
		dx2 = x - 8;
		dx2 = dx2 * dx2;

		for( y = 0; y < 16; y++ ) {
			dy = y - 8;
			dd2 = dx2 + dy * dy;
			d = 255 - 35 * sqrt( dd2 );
			data[( y * 16 + x ) * 4 + 3] = bound( 0, d, 255 );
		}
	}
}

/*
* R_InitWhiteTexture
*/
static void R_InitWhiteTexture( int *w, int *h, int *flags, int *samples ) {
	R_InitSolidColorTexture( w, h, flags, samples, 255, true );
}

/*
* R_InitBlackTexture
*/
static void R_InitBlackTexture( int *w, int *h, int *flags, int *samples ) {
	R_InitSolidColorTexture( w, h, flags, samples, 0, true );
}

/*
* R_InitGreyTexture
*/
static void R_InitGreyTexture( int *w, int *h, int *flags, int *samples ) {
	R_InitSolidColorTexture( w, h, flags, samples, 127, true );
}

/*
* R_InitBlankBumpTexture
*/
static void R_InitBlankBumpTexture( int *w, int *h, int *flags, int *samples ) {
	uint8_t *data = R_InitSolidColorTexture( w, h, flags, samples, 128, false );

/*
    data[0] = 128;	// normal X
    data[1] = 128;	// normal Y
*/
	data[2] = 255;  // normal Z
	data[3] = 128;  // height
}

/*
 * R_InitBlueNoiseTexture
 */
static void R_InitBlueNoiseTexture( int * w, int * h, int * flags, int * samples ) {
	uint8_t * data = stbi_load_from_memory( blue_noise_png, blue_noise_png_len, w, h, NULL, 1 );
	assert( *w == BLUENOISE_TEXTURE_SIZE && *h == BLUENOISE_TEXTURE_SIZE );
	*flags = IT_NOMIPMAP;
	*samples = 1;

	if( data == NULL ) {
		ri.Com_Error( ERR_FATAL, "stbi_load_from_memory: out of memory (alloc at %s:%i)", __FILE__, __LINE__ );
	}

	uint8_t * ibuf = R_PrepareImageBuffer( TEXTURE_LOADING_BUF, *w * *h );
	memcpy( ibuf, data, *w * *h );

	stbi_image_free( data );
}

/*
* R_GetRenderBufferSize
*/
static void R_GetRenderBufferSize( int inWidth, int inHeight, int inLimit, int *outWidth, int *outHeight ) {
	int limit;

	// limit the texture size to either screen resolution in case we can't use FBO
	// or hardware limits and ensure it's a POW2-texture if we don't support such textures
	limit = glConfig.maxRenderbufferSize;
	if( inLimit ) {
		limit = min( limit, inLimit );
	}
	if( limit < 1 ) {
		limit = 1;
	}

	*outWidth = min( inWidth, limit );
	*outHeight = min( inHeight, limit );
}

/*
* R_InitViewportTexture
*/
void R_InitViewportTexture( image_t **texture, const char *name, int id,
							int viewportWidth, int viewportHeight, int size, int flags, int tags, int samples ) {
	int width, height;
	image_t *t;

	R_GetRenderBufferSize( viewportWidth, viewportHeight, size, &width, &height );

	// create a new texture or update the old one
	if( !( *texture ) || ( *texture )->width != width || ( *texture )->height != height ) {
		if( !*texture ) {
			char uploadName[128];

			Q_snprintfz( uploadName, sizeof( uploadName ), "***%s_%i***", name, id );
			t = *texture = R_LoadImage( uploadName, NULL, width, height, flags, 1, tags, samples );
		} else {
			t = *texture;
			t->width = width;
			t->height = height;

			R_BindImage( t );

			R_Upload32( NULL, 0, 0, width, height, flags, 1, &t->upload_width, &t->upload_height, t->samples, false, false );
		}

		// update FBO, if attached
		if( t->fbo ) {
			RFB_UnregisterObject( t->fbo );
			t->fbo = 0;
		}
		if( t->flags & IT_FRAMEBUFFER ) {
			t->fbo = RFB_RegisterObject( t->upload_width, t->upload_height, 
				( tags & IMAGE_TAG_BUILTIN ) != 0,
				( flags & IT_DEPTHRB ) != 0, 
				false, 0, false, 
				( flags & IT_SRGB ) != 0
			);
			RFB_AttachTextureToObject( t->fbo, ( t->flags & IT_DEPTH ) != 0, 0, t );
		}
	}
}

/*
* R_InitScreenImagePair
*/
static void R_InitScreenImagePair( const char *name, image_t **color, image_t **depth, int orFlags ) {
	char tn[128];
	int flags, colorFlags, depthFlags;

	assert( glConfig.width >= 1 && glConfig.height >= 1 );

	flags = IT_SPECIAL;
	flags |= orFlags;

	colorFlags = flags | IT_FRAMEBUFFER;
	depthFlags = flags | ( IT_DEPTH | IT_NOFILTERING );
	if( !depth ) {
		colorFlags |= IT_DEPTHRB;
	}
	if( flags & IT_FLOAT ) {
		colorFlags |= IT_FLOAT;
	}

	if( color ) {
		R_InitViewportTexture( color, name, 0, glConfig.width, glConfig.height, 0, colorFlags, IMAGE_TAG_BUILTIN, 3 );
	}
	if( depth && color && *color ) {
		R_InitViewportTexture( depth, va_r( tn, sizeof( tn ), "%s_depth", name ),
							   0, glConfig.width, glConfig.height, 0, depthFlags, IMAGE_TAG_BUILTIN, 1 );

		if( colorFlags & IT_FRAMEBUFFER ) {
			RFB_AttachTextureToObject( ( *color )->fbo, true, 0, *depth );
		}
	}
}

/*
* R_InitBuiltinScreenImageSet
*
 * Screen textures may only be used in or referenced from the rendering context/thread.
*/
static void R_InitBuiltinScreenImageSet( refScreenTexSet_t *st, int flags ) {
	char name[128];
	bool useFloat;
	const char *postfix;
	
	flags &= (IT_FLOAT|IT_SRGB);
	useFloat = (flags & IT_FLOAT) != 0;
	postfix = useFloat ? "16f" : "";

	Q_snprintfz( name, sizeof( name ), "r_screenTex%s", postfix );
	R_InitScreenImagePair( name, &st->screenTex, &st->screenDepthTex, flags );

	Q_snprintfz( name, sizeof( name ), "r_screenTexCopy%s", postfix );
	R_InitScreenImagePair( name, &st->screenTexCopy, &st->screenDepthTexCopy, flags );

	for( int j = 0; j < 2; j++ ) {
		Q_snprintfz( name, sizeof( name ), "rsh.screenPP%sCopy%i", postfix, j );
		R_InitScreenImagePair( name, &st->screenPPCopies[j], NULL, flags );
	}
}

/*
* R_ReleaseBuiltinScreenImageSet
*/
static void R_ReleaseBuiltinScreenImageSet( refScreenTexSet_t *st ) {
	assert( st->screenTex != NULL );

	R_FreeImage( st->screenTex );
	R_FreeImage( st->screenDepthTex );

	st->screenTex = NULL;
	st->screenDepthTex = NULL;

	if( st->screenTexCopy == NULL )
		return;

	R_FreeImage( st->screenTexCopy );
	R_FreeImage( st->screenDepthTexCopy );
	R_FreeImage( st->screenPPCopies[0] );
	R_FreeImage( st->screenPPCopies[1] );

	st->screenTexCopy = NULL;
	st->screenDepthTexCopy = NULL;
	st->screenPPCopies[0] = NULL;
	st->screenPPCopies[1] = NULL;
}

/*
* R_RegisterMultisampleTarget
*/
int R_RegisterMultisampleTarget( refScreenTexSet_t *st, int samples, bool useFloat, bool sRGB ) {
	int width, height;

	if( samples <= 0 ) {
		return 0;
	}

	if( !st->multisampleTarget || RFB_GetSamples( st->multisampleTarget ) != samples ) {
		R_GetRenderBufferSize( glConfig.width, glConfig.height, 0, &width, &height );

		if( st->multisampleTarget ) {
			RFB_UnregisterObject( st->multisampleTarget );
		}

		st->multisampleTarget = RFB_RegisterObject( width, height, true, true, true, samples, useFloat, sRGB );
	}

	return st->multisampleTarget;
}

/*
* R_InitBuiltinScreenImages
*/
void R_InitBuiltinScreenImages( void ) {
	R_InitBuiltinScreenImageSet( &rsh.st, 0 );
	R_InitBuiltinScreenImageSet( &rsh.stf, IT_FLOAT );
	R_InitScreenImagePair( "r_2Dtex", &rsh.st2D.screenTex, &rsh.st2D.screenDepthTex, IT_SRGB );
}

/*
* R_ReleaseBuiltinScreenImages
*/
void R_ReleaseBuiltinScreenImages( void ) {
	R_ReleaseBuiltinScreenImageSet( &rsh.st );
	R_ReleaseBuiltinScreenImageSet( &rsh.stf );
	R_ReleaseBuiltinScreenImageSet( &rsh.st2D );
}

/*
* R_InitBuiltinImages
*/
static void R_InitBuiltinImages( void ) {
	int w, h, flags, samples;
	image_t *image;
	const struct {
		const char *name;
		image_t **image;
		void ( *init )( int *w, int *h, int *flags, int *samples );
	}
	textures[] =
	{
		{ "***r_notexture***", &rsh.noTexture, &R_InitNoTexture },
		{ "***r_whitetexture***", &rsh.whiteTexture, &R_InitWhiteTexture },
		{ "***r_blacktexture***", &rsh.blackTexture, &R_InitBlackTexture },
		{ "***r_greytexture***", &rsh.greyTexture, &R_InitGreyTexture },
		{ "***r_blankbumptexture***", &rsh.blankBumpTexture, &R_InitBlankBumpTexture },
		{ "***r_particletexture***", &rsh.particleTexture, &R_InitParticleTexture },
		{ "***r_bluenoisetexture***", &rsh.blueNoiseTexture, &R_InitBlueNoiseTexture },
		{ NULL, NULL, NULL }
	};
	size_t i, num_builtin_textures = ARRAY_COUNT( textures ) - 1;

	for( i = 0; i < num_builtin_textures; i++ ) {
		textures[i].init( &w, &h, &flags, &samples );

		image = R_LoadImage( textures[i].name, r_imageBuffers[ TEXTURE_LOADING_BUF ], w, h, flags, 1, IMAGE_TAG_BUILTIN, samples );

		*textures[i].image = image;
	}
}

/*
* R_ReleaseBuiltinImages
*/
static void R_ReleaseBuiltinImages( void ) {
	rsh.noTexture = NULL;
	rsh.whiteTexture = NULL;
	rsh.blackTexture = NULL;
	rsh.greyTexture = NULL;
	rsh.blankBumpTexture = NULL;
	rsh.particleTexture = NULL;
	rsh.blueNoiseTexture = NULL;
}

//=======================================================

/*
* R_InitImages
*/
void R_InitImages( void ) {
	int i;

	if( r_imagesPool ) {
		return;
	}

	r_imagesPool = R_AllocPool( r_mempool, "Images" );
	r_imagesLock = ri.Mutex_Create();

	r_unpackAlignment = 4;
	glPixelStorei( GL_PACK_ALIGNMENT, 1 );

	r_imagePathBuf = r_imagePathBuf2 = NULL;
	r_sizeof_imagePathBuf = r_sizeof_imagePathBuf2 = 0;

	r_8to24table[0] = r_8to24table[1] = NULL;

	memset( r_images, 0, sizeof( r_images ) );

	// link images
	r_free_images = r_images;
	for( i = 0; i < IMAGES_HASH_SIZE; i++ ) {
		r_images_hash_headnode[i].prev = &r_images_hash_headnode[i];
		r_images_hash_headnode[i].next = &r_images_hash_headnode[i];
	}
	for( i = 0; i < MAX_GLIMAGES - 1; i++ ) {
		r_images[i].next = &r_images[i + 1];
	}

	R_InitBuiltinImages();
}

/*
* R_TouchImage
*/
void R_TouchImage( image_t *image, int tags ) {
	if( !image ) {
		return;
	}

	image->tags |= tags;

	if( image->registrationSequence == rsh.registrationSequence ) {
		return;
	}

	image->registrationSequence = rsh.registrationSequence;
	if( image->fbo ) {
		RFB_TouchObject( image->fbo );
	}
}

/*
* R_FreeUnusedImagesByTags
*/
void R_FreeUnusedImagesByTags( int tags ) {
	int i;
	image_t *image;
	int keeptags = ~tags;

	for( i = 0, image = r_images; i < MAX_GLIMAGES; i++, image++ ) {
		if( !image->name ) {
			// free image
			continue;
		}
		if( image->registrationSequence == rsh.registrationSequence ) {
			// we need this image
			continue;
		}

		image->tags &= keeptags;
		if( image->tags ) {
			// still used for a different purpose
			continue;
		}

		R_FreeImage( image );
	}
}

/*
* R_FreeUnusedImages
*/
void R_FreeUnusedImages( void ) {
	R_FreeUnusedImagesByTags( ~IMAGE_TAG_BUILTIN );
}

/*
* R_ShutdownImages
*/
void R_ShutdownImages( void ) {
	int i;
	image_t *image;

	if( !r_imagesPool ) {
		return;
	}

	R_ReleaseBuiltinImages();

	for( i = 0, image = r_images; i < MAX_GLIMAGES; i++, image++ ) {
		if( !image->name ) {
			// free texture
			continue;
		}
		R_FreeImage( image );
	}

	R_FreeImageBuffers();

	if( r_imagePathBuf ) {
		R_Free( r_imagePathBuf );
	}
	if( r_imagePathBuf2 ) {
		R_Free( r_imagePathBuf2 );
	}

	if( r_8to24table[0] ) {
		R_Free( r_8to24table[0] );
	}
	if( r_8to24table[1] ) {
		R_Free( r_8to24table[1] );
	}
	r_8to24table[0] = r_8to24table[1] = NULL;

	ri.Mutex_Destroy( &r_imagesLock );

	R_FreePool( &r_imagesPool );

	r_screenShotBuffer = NULL;
	r_screenShotBufferSize = 0;

	r_imagePathBuf = r_imagePathBuf2 = NULL;
	r_sizeof_imagePathBuf = r_sizeof_imagePathBuf2 = 0;
}
