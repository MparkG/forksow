/*
Copyright (C) 2016 Victor Luchits

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
#include "r_cmdque.h"
#include "r_frontend.h"

static ref_frontend_t rrf;

/*
* RF_AdapterShutdown
*/
static void RF_AdapterShutdown( ref_frontendAdapter_t *adapter ) {
	if( !adapter->cmdPipe ) {
		return;
	}

	adapter->cmdPipe->Shutdown( adapter->cmdPipe );
	RF_DestroyCmdPipe( &adapter->cmdPipe );

	memset( adapter, 0, sizeof( *adapter ) );
}

void RF_Init() {
	rrf.frameNum = rrf.lastFrameNum = 0;
	rrf.frame = RF_CreateCmdBuf();
	rrf.frame->Clear( rrf.frame );

	rrf.adapter.owner = (void *)&rrf;
	rrf.adapter.cmdPipe = RF_CreateCmdPipe();
	rrf.adapter.cmdPipe->Init( rrf.adapter.cmdPipe );
}

void RF_AppActivate( bool active, bool minimize ) {
	R_Flush();
}

void RF_Shutdown( bool verbose ) {
	RF_AdapterShutdown( &rrf.adapter );

	RF_DestroyCmdBuf( &rrf.frame );
	memset( &rrf, 0, sizeof( rrf ) );

	R_Shutdown( verbose );
}

static void RF_CheckCvars( void ) {
	// update gamma
	if( r_gamma->modified ) {
		r_gamma->modified = false;
		rrf.adapter.cmdPipe->SetGamma( rrf.adapter.cmdPipe, r_gamma->value );
	}

	if( r_texturefilter->modified ) {
		r_texturefilter->modified = false;
		rrf.adapter.cmdPipe->SetTextureFilter( rrf.adapter.cmdPipe, r_texturefilter->integer );
	}

	if( r_wallcolor->modified || r_floorcolor->modified ) {
		vec3_t wallColor, floorColor;

		sscanf( r_wallcolor->string,  "%3f %3f %3f", &wallColor[0], &wallColor[1], &wallColor[2] );
		sscanf( r_floorcolor->string, "%3f %3f %3f", &floorColor[0], &floorColor[1], &floorColor[2] );

		r_wallcolor->modified = r_floorcolor->modified = false;

		rrf.adapter.cmdPipe->SetWallFloorColors( rrf.adapter.cmdPipe, wallColor, floorColor );
	}

	// keep r_outlines_cutoff value in sane bounds to prevent wallhacking
	if( r_outlines_scale->modified ) {
		if( r_outlines_scale->value < 0 ) {
			ri.Cvar_ForceSet( r_outlines_scale->name, "0" );
		} else if( r_outlines_scale->value > 3 ) {
			ri.Cvar_ForceSet( r_outlines_scale->name, "3" );
		}
		r_outlines_scale->modified = false;
	}
}

void RF_BeginFrame() {
	RF_CheckCvars();

	rrf.frame->Clear( rrf.frame );
	rrf.frame->BeginFrame( rrf.frame );
}

void RF_EndFrame( void ) {
	rrf.frame->EndFrame( rrf.frame );
}

void RF_BeginRegistration( void ) {
	R_BeginRegistration();
	rrf.adapter.cmdPipe->BeginRegistration( rrf.adapter.cmdPipe );
}

void RF_EndRegistration( void ) {
	R_EndRegistration();
	rrf.adapter.cmdPipe->EndRegistration( rrf.adapter.cmdPipe );
}

void RF_RegisterWorldModel( const char *model ) {
	R_RegisterWorldModel( model );
}

void RF_ClearScene( void ) {
	rrf.frame->ClearScene( rrf.frame );
}

void RF_AddEntityToScene( const entity_t *ent ) {
	rrf.frame->AddEntityToScene( rrf.frame, ent );
}

void RF_AddLightToScene( const vec3_t org, float intensity, float r, float g, float b ) {
	rrf.frame->AddLightToScene( rrf.frame, org, intensity, r, g, b );
}

void RF_AddPolyToScene( const poly_t *poly ) {
	rrf.frame->AddPolyToScene( rrf.frame, poly );
}

void RF_RenderScene( const refdef_t *fd ) {
	rrf.frame->RenderScene( rrf.frame, fd );
}

void RF_BlurScreen( void ) {
	rrf.frame->BlurScreen( rrf.frame );
}

void RF_DrawStretchPic( int x, int y, int w, int h, float s1, float t1, float s2, float t2,
						const vec4_t color, const shader_t *shader ) {
	rrf.frame->DrawRotatedStretchPic( rrf.frame, x, y, w, h, s1, t1, s2, t2, 0, color, shader );
}

void RF_DrawRotatedStretchPic( int x, int y, int w, int h, float s1, float t1, float s2, float t2, float angle,
							   const vec4_t color, const shader_t *shader ) {
	rrf.frame->DrawRotatedStretchPic( rrf.frame, x, y, w, h, s1, t1, s2, t2, angle, color, shader );
}

void RF_SetScissor( int x, int y, int w, int h ) {
	rrf.frame->SetScissor( rrf.frame, x, y, w, h );
	Vector4Set( rrf.scissor, x, y, w, h );
}

void RF_ResetScissor( void ) {
	rrf.frame->ResetScissor( rrf.frame );
	Vector4Set( rrf.scissor, 0, 0, glConfig.width, glConfig.height );
}

void RF_ResizeFramebuffers() {
	rrf.adapter.cmdPipe->ResizeFramebuffers( rrf.adapter.cmdPipe );
}

void RF_ScreenShot( const char *path, const char *name, const char *fmtstring, bool silent ) {
	rrf.adapter.cmdPipe->ScreenShot( rrf.adapter.cmdPipe, path, name, fmtstring, silent );
}

const char *RF_GetSpeedsMessage( char *out, size_t size ) {
	ri.Mutex_Lock( rf.speedsMsgLock );
	Q_strncpyz( out, rf.speedsMsg, size );
	ri.Mutex_Unlock( rf.speedsMsgLock );
	return out;
}

int RF_GetAverageFrametime( void ) {
	return rf.frameTime.average;
}

void RF_ReplaceRawSubPic( shader_t *shader, int x, int y, int width, int height, uint8_t *data ) {
	R_ReplaceRawSubPic( shader, x, y, width, height, data );
}

void RF_TransformVectorToScreen( const refdef_t *rd, const vec3_t in, vec2_t out ) {
	mat4_t p, m;
	vec4_t temp, temp2;

	if( !rd || !in || !out ) {
		return;
	}

	temp[0] = in[0];
	temp[1] = in[1];
	temp[2] = in[2];
	temp[3] = 1.0f;

	if( rd->rdflags & RDF_USEORTHO ) {
		Matrix4_OrthoProjection( rd->ortho_x, rd->ortho_x, rd->ortho_y, rd->ortho_y,
									  -4096.0f, 4096.0f, p );
	} else {
		Matrix4_PerspectiveProjection( rd->fov_x, rd->fov_y, Z_NEAR, p );
	}

	Matrix4_QuakeModelview( rd->vieworg, rd->viewaxis, m );

	Matrix4_Multiply_Vector( m, temp, temp2 );
	Matrix4_Multiply_Vector( p, temp2, temp );

	if( !temp[3] ) {
		return;
	}

	out[0] = rd->x + rd->width * ( temp[0] / temp[3] + 1.0f ) * 0.5f;
	out[1] = rd->y + rd->height * ( 1.0f - ( temp[1] / temp[3] + 1.0f ) * 0.5f );
}

bool RF_TransformVectorToScreenClamped( const refdef_t *rd, const vec3_t target, int border, vec2_t out ) {
	float near_plane = 0.0001f;
	mat4_t p, v;
	Matrix4_PerspectiveProjection( rd->fov_x, rd->fov_y, near_plane, p );
	Matrix4_QuakeModelview( rd->vieworg, rd->viewaxis, v );

	vec4_t homo, view, clip;
	Vector4Set( homo, target[0], target[1], target[2], 1.0f );
	Matrix4_Multiply_Vector( v, homo, view );
	Matrix4_Multiply_Vector( p, view, clip );

	if( clip[2] == 0 ) {
		Vector2Set( out, rd->width * 0.5f, rd->height * 0.5f );
		return false;
	}

	vec2_t res;
	Vector2Set( res, clip[0] / clip[2], clip[1] / clip[2] );

	vec3_t to_target;
	VectorSubtract( target, rn.viewOrigin, to_target );
	float d = DotProduct( &rn.viewAxis[AXIS_FORWARD], to_target );
	if( d < 0 ) {
		res[0] = -res[0];
		res[1] = -res[1];
	}

	bool clamped = false;
	vec2_t clamp;
	Vector2Set( clamp, 1.0f - ( float ) border / rd->width, 1.0f - ( float ) border / rd->height );

	if( d < 0 || fabsf( res[0] ) > clamp[0] || fabsf( res[1] ) > clamp[1] ) {
		float rx = clamp[0] / fabsf( res[0] );
		float ry = clamp[1] / fabsf( res[1] );

		res[0] *= min( rx, ry );
		res[1] *= min( rx, ry );

		clamped = true;
	}

	out[0] = rd->x + rd->width * ( res[0] + 1.0f ) * 0.5f;
	out[1] = rd->y + rd->height * ( 1.0f - ( res[1] + 1.0f ) * 0.5f );

	return clamped;
}

bool RF_LerpTag( orientation_t *orient, const model_t *mod, int oldframe, int frame, float lerpfrac, const char *name ) {
	if( !orient ) {
		return false;
	}

	VectorClear( orient->origin );
	Matrix3_Identity( orient->axis );

	if( !name ) {
		return false;
	}

	if( mod->type == mod_alias ) {
		return R_AliasModelLerpTag( orient, (const maliasmodel_t *)mod->extradata, oldframe, frame, lerpfrac, name );
	}

	return false;
}
