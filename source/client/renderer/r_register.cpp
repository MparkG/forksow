/*
Copyright (C) 2007 Victor Luchits

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

// r_register.c
#include "r_local.h"
#include "qcommon/hash.h"

glconfig_t glConfig;

r_shared_t rsh;

mempool_t *r_mempool;

cvar_t *r_drawentities;
cvar_t *r_drawworld;
cvar_t *r_speeds;
cvar_t *r_sRGB;

cvar_t *r_subdivisions;
cvar_t *r_showtris;
cvar_t *r_showtris2D;
cvar_t *r_leafvis;

cvar_t *r_lighting_specular;
cvar_t *r_lighting_glossintensity;
cvar_t *r_lighting_glossexponent;
cvar_t *r_lighting_ambientscale;
cvar_t *r_lighting_directedscale;

cvar_t *r_outlines_world;
cvar_t *r_outlines_scale;
cvar_t *r_outlines_cutoff;

cvar_t *r_soft_particles;
cvar_t *r_soft_particles_scale;

cvar_t *r_hdr;
cvar_t *r_hdr_gamma;
cvar_t *r_hdr_exposure;

cvar_t *r_samples;

cvar_t *r_gamma;
cvar_t *r_texturefilter;
cvar_t *r_nobind;
cvar_t *r_screenshot_fmtstr;

cvar_t *r_drawflat;
cvar_t *r_wallcolor;
cvar_t *r_floorcolor;

cvar_t *r_usenotexture;

static void R_FinalizeGLExtensions( void );
static void R_GfxInfo_f( void );

static void R_InitVolatileAssets( void );
static void R_DestroyVolatileAssets( void );

static const struct {
	const char * name;
	int * glad;
	bool * enabled;
} exts[] = {
#if !PUBLIC_BUILD
	{ "KHR_debug", &GLAD_GL_KHR_debug, &glConfig.ext.khr_debug },
	{ "AMD_debug_output", &GLAD_GL_AMD_debug_output, &glConfig.ext.amd_debug },
#endif
	{ "EXT_texture_sRGB", &GLAD_GL_EXT_texture_sRGB, &glConfig.ext.texture_sRGB },
	{ "EXT_texture_sRGB_decode", &GLAD_GL_EXT_texture_sRGB_decode, &glConfig.ext.texture_sRGB_decode },
	{ "EXT_texture_compression_s3tc", &GLAD_GL_EXT_texture_compression_s3tc, &glConfig.ext.texture_compression },
	{ "EXT_texture_filter_anisotropic", &GLAD_GL_EXT_texture_filter_anisotropic, &glConfig.ext.texture_filter_anisotropic },

	{ "ARB_half_float_pixel", &GLAD_GL_ARB_half_float_pixel, &glConfig.ext.ARB_half_float_pixel },
	{ "ARB_half_float_vertex", &GLAD_GL_ARB_half_float_vertex, &glConfig.ext.ARB_half_float_vertex },
	{ "ARB_texture_float", &GLAD_GL_ARB_texture_float, &glConfig.ext.ARB_texture_float },
	{ "ARB_draw_instanced", &GLAD_GL_ARB_draw_instanced, &glConfig.ext.ARB_draw_instanced },
	{ "ARB_instanced_arrays", &GLAD_GL_ARB_instanced_arrays, &glConfig.ext.ARB_instanced_arrays },
	{ "ARB_framebuffer_object", &GLAD_GL_ARB_framebuffer_object, &glConfig.ext.ARB_framebuffer_object },
	{ "ARB_texture_swizzle", &GLAD_GL_ARB_texture_swizzle, &glConfig.ext.ARB_texture_swizzle },
	{ "EXT_texture_array", &GLAD_GL_EXT_texture_array, &glConfig.ext.EXT_texture_array },
	{ "ARB_texture_rg", &GLAD_GL_ARB_texture_rg, &glConfig.ext.ARB_texture_rg },
	{ "ARB_vertex_array_object", &GLAD_GL_ARB_vertex_array_object, &glConfig.ext.ARB_vertex_array_object },

	{ "ARB_get_program_binary", &GLAD_GL_ARB_get_program_binary, &glConfig.ext.get_program_binary },
	{ "NVX_gpu_memory_info", &GLAD_GL_NVX_gpu_memory_info, &glConfig.ext.nvidia_meminfo },
	{ "ATI_meminfo", &GLAD_GL_ATI_meminfo, &glConfig.ext.ati_meminfo },
};

/*
* R_RegisterGLExtensions
*/
static bool R_RegisterGLExtensions( void ) {
	for( size_t i = 0; i < ARRAY_COUNT( exts ); i++ ) {
		*exts[ i ].enabled = *exts[ i ].glad != 0;
	}

	R_FinalizeGLExtensions();
	return true;
}

/*
* R_PrintGLExtensionsInfo
*/
static void R_PrintGLExtensionsInfo( void ) {
	for( size_t i = 0; i < ARRAY_COUNT( exts ); i++ ) {
		Com_Printf( "%s: %s\n", exts[ i ].name, *exts[ i ].glad == 0 ? "disabled" : "enabled" );
	}
}

/*
* R_PrintMemoryInfo
*/
static void R_PrintMemoryInfo( void ) {
	int mem[12];

	Com_Printf( "\n" );
	Com_Printf( "Video memory information:\n" );

	if( glConfig.ext.nvidia_meminfo ) {
		glGetIntegerv( GL_GPU_MEMORY_INFO_TOTAL_AVAILABLE_MEMORY_NVX, mem );
		Com_Printf( "total: %i MB\n", mem[0] >> 10 );

		glGetIntegerv( GL_GPU_MEMORY_INFO_DEDICATED_VIDMEM_NVX, mem );
		Com_Printf( "dedicated: %i MB\n", mem[0] >> 10 );

		glGetIntegerv( GL_GPU_MEMORY_INFO_CURRENT_AVAILABLE_VIDMEM_NVX, mem );
		Com_Printf( "available: %i MB\n", mem[0] >> 10 );

		glGetIntegerv( GL_GPU_MEMORY_INFO_EVICTION_COUNT_NVX, mem );
		Com_Printf( "eviction count: %i MB\n", mem[0] >> 10 );

		glGetIntegerv( GL_GPU_MEMORY_INFO_EVICTED_MEMORY_NVX, mem );
		Com_Printf( "totally evicted: %i MB\n", mem[0] >> 10 );
	} else if( glConfig.ext.ati_meminfo ) {
		// ATI
		glGetIntegerv( GL_VBO_FREE_MEMORY_ATI, mem );
		glGetIntegerv( GL_TEXTURE_FREE_MEMORY_ATI, mem + 4 );
		glGetIntegerv( GL_RENDERBUFFER_FREE_MEMORY_ATI, mem + 8 );

		Com_Printf( "total memory free in the pool: (VBO:%i, Tex:%i, RBuf:%i) MB\n", mem[0] >> 10, mem[4] >> 10, mem[8] >> 10 );
		Com_Printf( "largest available free block in the pool: (V:%i, Tex:%i, RBuf:%i) MB\n", mem[5] >> 10, mem[4] >> 10, mem[9] >> 10 );
		Com_Printf( "total auxiliary memory free: (VBO:%i, Tex:%i, RBuf:%i) MB\n", mem[2] >> 10, mem[6] >> 10, mem[10] >> 10 );
		Com_Printf( "largest auxiliary free block: (VBO:%i, Tex:%i, RBuf:%i) MB\n", mem[3] >> 10, mem[7] >> 10, mem[11] >> 10 );
	} else {
		Com_Printf( "not available\n" );
	}
}

/*
* R_FinalizeGLExtensions
*
* Verify correctness of values provided by the driver, init some variables
*/
static void R_FinalizeGLExtensions( void ) {
	char tmp[128];

	const char * glslVersionString = ( const char * ) glGetString( GL_SHADING_LANGUAGE_VERSION );
	int glslMajor, glslMinor;
	sscanf( glslVersionString, "%d.%d", &glslMajor, &glslMinor );
	int glslVersion = glslMajor * 100 + glslMinor;
	glConfig.ext.glsl130 = glslVersion >= 130;

	glConfig.maxTextureSize = 0;
	glGetIntegerv( GL_MAX_TEXTURE_SIZE, &glConfig.maxTextureSize );
	if( glConfig.maxTextureSize <= 0 ) {
		glConfig.maxTextureSize = 256;
	}
	glConfig.maxTextureSize = 1 << Q_log2( glConfig.maxTextureSize );

	ri.Cvar_Get( "gl_max_texture_size", "0", CVAR_READONLY );
	ri.Cvar_ForceSet( "gl_max_texture_size", va_r( tmp, sizeof( tmp ), "%i", glConfig.maxTextureSize ) );

	/* GL_EXT_framebuffer_object */
	glConfig.maxRenderbufferSize = 0;
	glGetIntegerv( GL_MAX_RENDERBUFFER_SIZE, &glConfig.maxRenderbufferSize );
	glConfig.maxRenderbufferSize = 1 << Q_log2( glConfig.maxRenderbufferSize );
	if( glConfig.maxRenderbufferSize > glConfig.maxTextureSize ) {
		glConfig.maxRenderbufferSize = glConfig.maxTextureSize;
	}

	/* GL_EXT_texture_filter_anisotropic */
	glConfig.maxTextureFilterAnisotropic = 0;
	if( glConfig.ext.texture_filter_anisotropic ) {
		glGetIntegerv( GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &glConfig.maxTextureFilterAnisotropic );
	}

	glConfig.maxVertexUniformComponents = glConfig.maxFragmentUniformComponents = 0;

	glGetIntegerv( GL_MAX_VERTEX_ATTRIBS, &glConfig.maxVertexAttribs );
	glGetIntegerv( GL_MAX_VERTEX_UNIFORM_COMPONENTS, &glConfig.maxVertexUniformComponents );
	glGetIntegerv( GL_MAX_FRAGMENT_UNIFORM_COMPONENTS, &glConfig.maxFragmentUniformComponents );

	// require both texture_sRGB and texture_float for sRGB rendering as 8bit framebuffers
	// run out of precision for linear colors in darker areas
	glConfig.sSRGB = r_sRGB->integer && glConfig.ext.texture_sRGB;

	glGetIntegerv( GL_MAX_SAMPLES, &glConfig.maxFramebufferSamples );

	ri.Cvar_Get( "r_texturefilter_max", "0", CVAR_READONLY );
	ri.Cvar_ForceSet( "r_texturefilter_max", va_r( tmp, sizeof( tmp ), "%i", glConfig.maxTextureFilterAnisotropic ) );
}

/*
* R_FillStartupBackgroundColor
*
* Fills the window with a color during the initialization.
*/
static void R_FillStartupBackgroundColor( float r, float g, float b ) {
	glClearColor( r, g, b, 1.0 );
	glClear( GL_COLOR_BUFFER_BIT );
	glFinish();
	VID_Swap();
}

static void R_Register() {
	char tmp[128];

	r_drawentities = ri.Cvar_Get( "r_drawentities", "1", CVAR_CHEAT );
	r_drawworld = ri.Cvar_Get( "r_drawworld", "1", CVAR_CHEAT );
	r_speeds = ri.Cvar_Get( "r_speeds", "0", 0 );
	r_showtris = ri.Cvar_Get( "r_showtris", "0", CVAR_CHEAT );
	r_showtris2D = ri.Cvar_Get( "r_showtris2D", "0", CVAR_CHEAT );
	r_leafvis = ri.Cvar_Get( "r_leafvis", "0", CVAR_CHEAT );

	r_sRGB = ri.Cvar_Get( "r_sRGB", "1", CVAR_ARCHIVE | CVAR_LATCH_VIDEO );

	r_subdivisions = ri.Cvar_Get( "r_subdivisions", STR_TOSTR( SUBDIVISIONS_DEFAULT ), CVAR_ARCHIVE | CVAR_LATCH_VIDEO | CVAR_READONLY );

	r_lighting_specular = ri.Cvar_Get( "r_lighting_specular", "0", CVAR_ARCHIVE | CVAR_LATCH_VIDEO | CVAR_READONLY );
	r_lighting_glossintensity = ri.Cvar_Get( "r_lighting_glossintensity", "1.5", CVAR_ARCHIVE );
	r_lighting_glossexponent = ri.Cvar_Get( "r_lighting_glossexponent", "24", CVAR_ARCHIVE );
	r_lighting_ambientscale = ri.Cvar_Get( "r_lighting_ambientscale", "1", 0 );
	r_lighting_directedscale = ri.Cvar_Get( "r_lighting_directedscale", "1", 0 );

	r_outlines_world = ri.Cvar_Get( "r_outlines_world", "1.8", CVAR_ARCHIVE );
	r_outlines_scale = ri.Cvar_Get( "r_outlines_scale", "1", CVAR_ARCHIVE );
	r_outlines_cutoff = ri.Cvar_Get( "r_outlines_cutoff", "4096", CVAR_ARCHIVE );

	r_soft_particles = ri.Cvar_Get( "r_soft_particles", "1", CVAR_ARCHIVE );
	r_soft_particles_scale = ri.Cvar_Get( "r_soft_particles_scale", "0.02", CVAR_ARCHIVE );

	r_hdr = ri.Cvar_Get( "r_hdr", "1", CVAR_ARCHIVE | CVAR_CHEAT );
	r_hdr_gamma = ri.Cvar_Get( "r_hdr_gamma", "2.2", CVAR_ARCHIVE | CVAR_CHEAT );
	r_hdr_exposure = ri.Cvar_Get( "r_hdr_exposure", "1.0", CVAR_ARCHIVE | CVAR_CHEAT );

	r_samples = ri.Cvar_Get( "r_samples", "0", CVAR_ARCHIVE );

	r_gamma = ri.Cvar_Get( "r_gamma", "1.0", CVAR_ARCHIVE );
	r_texturefilter = ri.Cvar_Get( "r_texturefilter", "4", CVAR_ARCHIVE );

	r_screenshot_fmtstr = ri.Cvar_Get( "r_screenshot_fmtstr", va_r( tmp, sizeof( tmp ), "%s%%y%%m%%d_%%H%%M%%S", APP_SCREENSHOTS_PREFIX ), CVAR_ARCHIVE );

	r_drawflat = ri.Cvar_Get( "r_drawflat", "1", CVAR_READONLY );
	r_wallcolor = ri.Cvar_Get( "r_wallcolor", "8 8 8", CVAR_READONLY );
	r_floorcolor = ri.Cvar_Get( "r_floorcolor", "32 32 32", CVAR_READONLY );

	// make sure we rebuild our 3D texture after vid_restart
	r_wallcolor->modified = r_floorcolor->modified = true;

	ri.Cmd_AddCommand( "imagelist", R_ImageList_f );
	ri.Cmd_AddCommand( "shaderlist", R_ShaderList_f );
	ri.Cmd_AddCommand( "shaderdump", R_ShaderDump_f );
	ri.Cmd_AddCommand( "screenshot", R_ScreenShot_f );
	ri.Cmd_AddCommand( "modellist", Mod_Modellist_f );
	ri.Cmd_AddCommand( "gfxinfo", R_GfxInfo_f );
	ri.Cmd_AddCommand( "glslprogramlist", RP_ProgramList_f );
}

/*
* R_GfxInfo_f
*/
static void R_GfxInfo_f( void ) {
	Com_Printf( "\n" );
	Com_Printf( "GL_VENDOR: %s\n", glConfig.vendorString );
	Com_Printf( "GL_RENDERER: %s\n", glConfig.rendererString );
	Com_Printf( "GL_VERSION: %s\n", glConfig.versionString );

	Com_Printf( "GL_MAX_TEXTURE_SIZE: %i\n", glConfig.maxTextureSize );
	if( glConfig.ext.texture_filter_anisotropic ) {
		Com_Printf( "GL_MAX_TEXTURE_MAX_ANISOTROPY: %i\n", glConfig.maxTextureFilterAnisotropic );
	}
	Com_Printf( "GL_MAX_RENDERBUFFER_SIZE: %i\n", glConfig.maxRenderbufferSize );
	Com_Printf( "GL_MAX_VERTEX_UNIFORM_COMPONENTS: %i\n", glConfig.maxVertexUniformComponents );
	Com_Printf( "GL_MAX_VERTEX_ATTRIBS: %i\n", glConfig.maxVertexAttribs );
	Com_Printf( "GL_MAX_FRAGMENT_UNIFORM_COMPONENTS: %i\n", glConfig.maxFragmentUniformComponents );
	Com_Printf( "GL_MAX_SAMPLES: %i\n", glConfig.maxFramebufferSamples );
	Com_Printf( "\n" );

	Com_Printf( "mode: %ix%i%s\n", glConfig.width, glConfig.height,
				glConfig.fullScreen ? ", fullscreen" : ", windowed" );
	Com_Printf( "anisotropic filtering: %i\n", r_texturefilter->integer );

	R_PrintGLExtensionsInfo();

	R_PrintMemoryInfo();
}

/*
* R_GLVersionHash
*/
static unsigned R_GLVersionHash( const char *vendorString, const char *rendererString, const char *versionString ) {
	uint8_t *tmp;
	size_t csize;
	size_t tmp_size, pos;
	unsigned hash;

	tmp_size = strlen( vendorString ) + strlen( rendererString ) +
			   strlen( versionString ) + strlen( ARCH ) + 1;

	pos = 0;
	tmp = ( uint8_t * ) R_Malloc( tmp_size );

	csize = strlen( vendorString );
	memcpy( tmp + pos, vendorString, csize );
	pos += csize;

	csize = strlen( rendererString );
	memcpy( tmp + pos, rendererString, csize );
	pos += csize;

	csize = strlen( versionString );
	memcpy( tmp + pos, versionString, csize );
	pos += csize;

	// shaders are not compatible between 32-bit and 64-bit at least on Nvidia
	csize = strlen( ARCH );
	memcpy( tmp + pos, ARCH, csize );
	pos += csize;

	hash = Hash32( tmp, tmp_size );

	R_Free( tmp );

	return hash;
}

/*
* R_Init
*/
void RF_Init();
bool R_Init() {
	r_mempool = R_AllocPool( NULL, "Rendering Frontend" );

	R_Register();

	// TODO: sigh
	int w = glConfig.width;
	int h = glConfig.height;
	memset( &glConfig, 0, sizeof( glConfig ) );
	glConfig.width = w;
	glConfig.height = h;

	glConfig.hwGamma = VID_GetGammaRamp( GAMMARAMP_STRIDE, &glConfig.gammaRampSize, glConfig.originalGammaRamp );
	if( glConfig.hwGamma ) {
		r_gamma->modified = true;
	}

	/*
	** get our various GL strings
	*/
	glConfig.vendorString = (const char *)glGetString( GL_VENDOR );
	glConfig.rendererString = (const char *)glGetString( GL_RENDERER );
	glConfig.versionString = (const char *)glGetString( GL_VERSION );

	if( !glConfig.vendorString ) {
		glConfig.vendorString = "";
	}
	if( !glConfig.rendererString ) {
		glConfig.rendererString = "";
	}
	if( !glConfig.versionString ) {
		glConfig.versionString = "";
	}

	glConfig.versionHash = R_GLVersionHash( glConfig.vendorString, glConfig.rendererString,
											glConfig.versionString );

	memset( &rsh, 0, sizeof( rsh ) );
	memset( &rf, 0, sizeof( rf ) );

	rsh.registrationSequence = 1;
	rsh.registrationOpen = false;

	rsh.worldModelSequence = 1;

	for( int i = 0; i < 256; i++ )
		rsh.sinTableByte[i] = sin( (float)i / 255.0 * M_TWOPI );

	rf.frameTime.average = 1;
	rf.speedsMsgLock = ri.Mutex_Create();
	rf.debugSurfaceLock = ri.Mutex_Create();

	R_InitDrawLists();

	if( !R_RegisterGLExtensions() ) {
		return false;
	}

	R_FillStartupBackgroundColor( COLOR_R( APP_STARTUP_COLOR ) / 255.0f,
								  COLOR_G( APP_STARTUP_COLOR ) / 255.0f, COLOR_B( APP_STARTUP_COLOR ) / 255.0f );

	R_AnisotropicFilter( r_texturefilter->integer );

	R_GfxInfo_f();

	// load and compile GLSL programs
	RP_Init();

	R_InitVBO();

	R_InitImages();

	R_InitShaders();

	R_InitModels();

	R_InitText();

	R_ClearScene();

	R_InitVolatileAssets();

	RF_Init();

	GLenum glerr = glGetError();
	if( glerr != GL_NO_ERROR ) {
		Com_Printf( "glGetError() = 0x%x\n", glerr );
		return false;
	}

	return true;
}

/*
* R_InitVolatileAssets
*/
static GLuint vao;
static void R_InitVolatileAssets( void ) {
        glGenVertexArrays( 1, &vao );
        glBindVertexArray( vao );

	rsh.whiteShader = R_LoadShader( "$whiteimage", SHADER_TYPE_2D, true, NULL );

	if( !rsh.nullVBO ) {
		rsh.nullVBO = R_InitNullModelVBO();
	} else {
		R_TouchMeshVBO( rsh.nullVBO );
	}

	if( !rsh.postProcessingVBO ) {
		rsh.postProcessingVBO = R_InitPostProcessingVBO();
	} else {
		R_TouchMeshVBO( rsh.postProcessingVBO );
	}

	R_InitSky();
}

/*
* R_DestroyVolatileAssets
*/
static void R_DestroyVolatileAssets( void ) {
	glBindVertexArray( 0 );
	glDeleteVertexArrays( 1, &vao );
}

void R_BindGlobalVAO() {
	glBindVertexArray( vao );
}

/*
* R_BeginRegistration
*/
void R_BeginRegistration( void ) {
	R_DestroyVolatileAssets();

	rsh.registrationSequence++;
	if( !rsh.registrationSequence ) {
		// make sure assumption that an asset is free it its registrationSequence is 0
		// since rsh.registrationSequence never equals 0
		rsh.registrationSequence = 1;
	}
	rsh.registrationOpen = true;

	R_InitVolatileAssets();

	R_DeferDataSync();

	R_ClearScene();
}

/*
* R_EndRegistration
*/
void R_EndRegistration( void ) {
	if( rsh.registrationOpen == false ) {
		return;
	}

	rsh.registrationOpen = false;

	R_FreeUnusedModels();
	R_FreeUnusedVBOs();
	R_FreeUnusedShaders();
	R_FreeUnusedImages();

	R_DeferDataSync();

	R_ClearScene();
}

/*
* R_Shutdown
*/
void R_Shutdown( bool verbose ) {
	ri.Cmd_RemoveCommand( "modellist" );
	ri.Cmd_RemoveCommand( "screenshot" );
	ri.Cmd_RemoveCommand( "imagelist" );
	ri.Cmd_RemoveCommand( "gfxinfo" );
	ri.Cmd_RemoveCommand( "shaderdump" );
	ri.Cmd_RemoveCommand( "shaderlist" );
	ri.Cmd_RemoveCommand( "glslprogramlist" );

	// free shaders, models, etc.

	R_DestroyVolatileAssets();

	R_ShutdownText();

	R_ShutdownModels();

	R_ShutdownVBO();

	R_ShutdownShaders();

	R_ShutdownImages();

	// destroy compiled GLSL programs
	RP_Shutdown();

	// restore original gamma
	if( glConfig.hwGamma ) {
		VID_SetGammaRamp( GAMMARAMP_STRIDE, glConfig.gammaRampSize, glConfig.originalGammaRamp );
	}

	ri.Mutex_Destroy( &rf.speedsMsgLock );
	ri.Mutex_Destroy( &rf.debugSurfaceLock );

	R_FreePool( &r_mempool );
}
