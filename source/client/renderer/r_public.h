/*
Copyright (C) 2002-2007 Victor Luchits

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
#pragma once

#include "qcommon/qcommon.h"
#include "cgame/ref.h"

//
// these are the functions exported by the refresh module
//
typedef struct {
#ifndef _MSC_VER
	// halts the application or drops to console
	void ( *Com_Error )( com_error_code_t code, const char *format, ... ) __attribute__( ( format( printf, 2, 3 ) ) ) __attribute( ( noreturn ) );
	// console messages
	void ( *Com_Printf )( const char *format, ... ) __attribute__( ( format( printf, 1, 2 ) ) );
	void ( *Com_DPrintf )( const char *format, ... ) __attribute__( ( format( printf, 1, 2 ) ) );
#else
	void ( *Com_Error )( com_error_code_t code, _Printf_format_string_ const char *format, ... );
	void ( *Com_Printf )( _Printf_format_string_ const char *format, ... );
	void ( *Com_DPrintf )( _Printf_format_string_ const char *format, ... );
#endif

	// console variable interaction
	cvar_t *( *Cvar_Get )( const char *name, const char *value, int flags );
	cvar_t *( *Cvar_Set )( const char *name, const char *value );
	cvar_t *( *Cvar_ForceSet )( const char *name, const char *value );      // will return 0 0 if not found
	void ( *Cvar_SetValue )( const char *name, float value );
	float ( *Cvar_Value )( const char *name );
	const char *( *Cvar_String )( const char *name );

	int ( *Cmd_Argc )( void );
	char *( *Cmd_Argv )( int arg );
	char *( *Cmd_Args )( void );        // concatenation of all argv >= 1
	void ( *Cmd_AddCommand )( const char *name, void ( *cmd )( void ) );
	void ( *Cmd_RemoveCommand )( const char *cmd_name );
	void ( *Cmd_ExecuteText )( int exec_when, const char *text );
	void ( *Cmd_Execute )( void );
	void ( *Cmd_SetCompletionFunc )( const char *cmd_name, char **( *completion_func )( const char *partial ) );

	int64_t ( *Sys_Milliseconds )( void );
	uint64_t ( *Sys_Microseconds )( void );
	void ( *Sys_Sleep )( unsigned int milliseconds );

	int ( *FS_FOpenFile )( const char *filename, int *filenum, int mode );
	int ( *FS_FOpenAbsoluteFile )( const char *filename, int *filenum, int mode );
	int ( *FS_Read )( void *buffer, size_t len, int file );
	int ( *FS_Write )( const void *buffer, size_t len, int file );
	int ( *FS_Printf )( int file, const char *format, ... );
	int ( *FS_Tell )( int file );
	int ( *FS_Seek )( int file, int offset, int whence );
	int ( *FS_Eof )( int file );
	int ( *FS_Flush )( int file );
	void ( *FS_FCloseFile )( int file );
	bool ( *FS_RemoveFile )( const char *filename );
	int ( *FS_GetFileList )( const char *dir, const char *extension, char *buf, size_t bufsize, int start, int end );
	const char *( *FS_FirstExtension )( const char *filename, const char * const * extensions, int num_extensions );
	bool ( *FS_MoveFile )( const char *src, const char *dst );
	bool ( *FS_RemoveDirectory )( const char *dirname );
	const char * ( *FS_WriteDirectory )( void );

	// multithreading
	struct qmutex_s *( *Mutex_Create )( void );
	void ( *Mutex_Destroy )( struct qmutex_s **mutex );
	void ( *Mutex_Lock )( struct qmutex_s *mutex );
	void ( *Mutex_Unlock )( struct qmutex_s *mutex );

	struct qbufPipe_s *( *BufPipe_Create )( size_t bufSize, int flags );
	void ( *BufPipe_Destroy )( struct qbufPipe_s **pqueue );
	void ( *BufPipe_Finish )( struct qbufPipe_s *queue );
	void ( *BufPipe_WriteCmd )( struct qbufPipe_s *queue, const void *cmd, unsigned cmd_size );
	int ( *BufPipe_ReadCmds )( struct qbufPipe_s *queue, unsigned( **cmdHandlers )( const void * ) );
	void ( *BufPipe_Wait )( struct qbufPipe_s *queue, int ( *read )( struct qbufPipe_s *, unsigned( ** )( const void * ) ),
		unsigned( **cmdHandlers )( const void * ) );
} ref_import_t;

typedef struct {
	bool ( *Init )( bool verbose );

	void ( *Shutdown )( bool verbose );

	void ( *RegisterWorldModel )( const char *model );
	struct model_s *( *RegisterModel )( const char *name );
	struct shader_s *( *RegisterPic )( const char *name );
	struct shader_s *( *RegisterAlphaMask )( const char *name, int width, int height, const uint8_t * data );
	struct shader_s *( *RegisterSkin )( const char *name );

	void ( *ReplaceRawSubPic )( struct shader_s *shader, int x, int y, int width, int height, uint8_t *data );

	void ( *ClearScene )( void );
	void ( *AddEntityToScene )( const entity_t *ent );
	void ( *AddLightToScene )( const vec3_t org, float intensity, float r, float g, float b );
	void ( *AddPolyToScene )( const poly_t *poly );
	void ( *RenderScene )( const refdef_t *fd );

	/**
	 * BlurScreen performs fullscreen blur of the default framebuffer and blits it to screen
	 */
	void ( *BlurScreen )( void );

	void ( *DrawStretchPic )( int x, int y, int w, int h, float s1, float t1, float s2, float t2,
							  const float *color, StringHash shader );
	void ( *DrawRotatedStretchPic )( int x, int y, int w, int h, float s1, float t1, float s2, float t2,
									 float angle, const vec4_t color, StringHash shader );

	void ( *Scissor )( int x, int y, int w, int h );
	void ( *ResetScissor )( void );

	bool ( *LerpTag )( orientation_t *orient, const struct model_s *mod, int oldframe, int frame, float lerpfrac, const char *name );

	int ( *GetClippedFragments )( const vec3_t origin, float radius, vec3_t axis[3], int maxfverts, vec4_t *fverts,
								  int maxfragments, fragment_t *fragments );

	void ( *TransformVectorToScreen )( const refdef_t *rd, const vec3_t in, vec2_t out );
	bool ( *TransformVectorToScreenClamped )( const refdef_t *rd, const vec3_t target, int border, vec2_t out );

	void ( *BeginFrame )( void );
	void ( *EndFrame )( void );
	const char *( *GetSpeedsMessage )( char *out, size_t size );
	int ( *GetAverageFrametime )( void );

	void ( *AppActivate )( bool active, bool minimize );
} ref_export_t;

typedef ref_export_t *(*GetRefAPI_t)( const ref_import_t *imports );

extern "C" QF_DLL_EXPORT ref_export_t *GetRefAPI( ref_import_t *import );

void R_DrawDynamicPoly( const poly_t * poly );
MinMax3 R_ModelBounds( const model_s *mod );

Span< TRS > R_SampleAnimation( ArenaAllocator * a, const model_s * model, float t );
MatrixPalettes R_ComputeMatrixPalettes( ArenaAllocator * a, const model_s * model, Span< TRS > local_poses );
bool R_FindJointByName( const model_s * model, const char * name, u8 * joint_idx );
void R_MergeLowerUpperPoses( Span< TRS > lower, Span< const TRS > upper, const model_s * model, u8 upper_root_joint );
