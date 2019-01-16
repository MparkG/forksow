/*
Copyright (C) 2002-2003 Victor Luchits

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

#include "client.h"
#include "qcommon/version.h"
#include "gameshared/angelwrap/qas_public.h"

static cgame_export_t *cge;

extern "C" QF_DLL_EXPORT cgame_export_t *GetCGameAPI( void * );

static mempool_t *cl_gamemodulepool;

//======================================================================

// CL_GameModule versions of the CM functions passed to the game module
// they only add sv.cms as the first parameter

//======================================================================

static inline int CL_GameModule_CM_NumInlineModels( void ) {
	return CM_NumInlineModels( cl.cms );
}

static inline int CL_GameModule_CM_TransformedPointContents( const vec3_t p, struct cmodel_s *cmodel, const vec3_t origin, const vec3_t angles ) {
	return CM_TransformedPointContents( cl.cms, p, cmodel, origin, angles );
}

static inline void CL_GameModule_CM_TransformedBoxTrace( trace_t *tr, const vec3_t start, const vec3_t end, const vec3_t mins, const vec3_t maxs,
														 struct cmodel_s *cmodel, int brushmask, const vec3_t origin, const vec3_t angles ) {
	CM_TransformedBoxTrace( cl.cms, tr, start, end, mins, maxs, cmodel, brushmask, origin, angles );
}

static inline struct cmodel_s *CL_GameModule_CM_InlineModel( int num ) {
	return CM_InlineModel( cl.cms, num );
}

static inline void CL_GameModule_CM_InlineModelBounds( struct cmodel_s *cmodel, vec3_t mins, vec3_t maxs ) {
	CM_InlineModelBounds( cl.cms, cmodel, mins, maxs );
}

static inline struct cmodel_s *CL_GameModule_CM_ModelForBBox( vec3_t mins, vec3_t maxs ) {
	return CM_ModelForBBox( cl.cms, mins, maxs );
}

static inline struct cmodel_s *CL_GameModule_CM_OctagonModelForBBox( vec3_t mins, vec3_t maxs ) {
	return CM_OctagonModelForBBox( cl.cms, mins, maxs );
}

static inline bool CL_GameModule_CM_InPVS( const vec3_t p1, const vec3_t p2 ) {
	return CM_InPVS( cl.cms, p1, p2 );
}

//======================================================================

#ifndef _MSC_VER
static void CL_GameModule_Error( const char *msg ) __attribute__( ( noreturn ) );
#else
__declspec( noreturn ) static void CL_GameModule_Error( const char *msg );
#endif

/*
* CL_GameModule_Error
*/
static void CL_GameModule_Error( const char *msg ) {
	Com_Error( ERR_DROP, "%s", msg );
}

/*
* CL_GameModule_Print
*/
static void CL_GameModule_Print( const char *msg ) {
	Com_Printf( "%s", msg );
}

/*
* CL_GameModule_PrintToLog
*/
static void CL_GameModule_PrintToLog( const char *msg ) {
	Con_PrintSilent( msg );
}

/*
* CL_GameModule_GetConfigString
*/
static void CL_GameModule_GetConfigString( int i, char *str, int size ) {
	if( i < 0 || i >= MAX_CONFIGSTRINGS ) {
		Com_DPrintf( S_COLOR_RED "CL_GameModule_GetConfigString: i > MAX_CONFIGSTRINGS" );
		return;
	}
	if( !str || size <= 0 ) {
		Com_DPrintf( S_COLOR_RED "CL_GameModule_GetConfigString: NULL string" );
		return;
	}

	Q_strncpyz( str, cl.configstrings[i], size );
}

/*
* CL_GameModule_NET_GetUserCmd
*/
static void CL_GameModule_NET_GetUserCmd( int frame, usercmd_t *cmd ) {
	if( cmd ) {
		if( frame < 0 ) {
			frame = 0;
		}

		*cmd = cl.cmds[frame & CMD_MASK];
	}
}

/*
* CL_GameModule_NET_GetCurrentUserCmdNum
*/
static int CL_GameModule_NET_GetCurrentUserCmdNum( void ) {
	return cls.ucmdHead;
}

/*
* CL_GameModule_NET_GetCurrentState
*/
static void CL_GameModule_NET_GetCurrentState( int64_t *incomingAcknowledged, int64_t *outgoingSequence, int64_t *outgoingSent ) {
	if( incomingAcknowledged ) {
		*incomingAcknowledged = cls.ucmdAcknowledged;
	}
	if( outgoingSequence ) {
		*outgoingSequence = cls.ucmdHead;
	}
	if( outgoingSent ) {
		*outgoingSent = cls.ucmdSent;
	}
}

/*
* CL_GameModule_MemAlloc
*/
static void *CL_GameModule_MemAlloc( size_t size, const char *filename, int fileline ) {
	return _Mem_Alloc( cl_gamemodulepool, size, MEMPOOL_CLIENTGAME, 0, filename, fileline );
}

/*
* CL_GameModule_MemFree
*/
static void CL_GameModule_MemFree( void *data, const char *filename, int fileline ) {
	_Mem_Free( data, MEMPOOL_CLIENTGAME, 0, filename, fileline );
}

//==============================================

float VID_GetPixelRatio() { return 1; }

/*
* CL_GameModule_Init
*/
void CL_GameModule_Init( void ) {
	int64_t start;
	cgame_import_t import;

	// stop all playing sounds
	CL_SoundModule_StopAllSounds( true );

	CL_GameModule_Shutdown();

	cl_gamemodulepool = _Mem_AllocPool( NULL, "Client Game Progs", MEMPOOL_CLIENTGAME, __FILE__, __LINE__ );

	import.Error = CL_GameModule_Error;
	import.Print = CL_GameModule_Print;
	import.PrintToLog = CL_GameModule_PrintToLog;

	import.Cvar_Get = Cvar_Get;
	import.Cvar_Set = Cvar_Set;
	import.Cvar_SetValue = Cvar_SetValue;
	import.Cvar_ForceSet = Cvar_ForceSet;
	import.Cvar_String = Cvar_String;
	import.Cvar_Value = Cvar_Value;

	import.Cmd_TokenizeString = Cmd_TokenizeString;
	import.Cmd_Argc = Cmd_Argc;
	import.Cmd_Argv = Cmd_Argv;
	import.Cmd_Args = Cmd_Args;

	import.Cmd_AddCommand = Cmd_AddCommand;
	import.Cmd_RemoveCommand = Cmd_RemoveCommand;
	import.Cmd_ExecuteText = Cbuf_ExecuteText;
	import.Cmd_Execute = Cbuf_Execute;
	import.Cmd_SetCompletionFunc = Cmd_SetCompletionFunc;

	import.FS_FOpenFile = FS_FOpenFile;
	import.FS_Read = FS_Read;
	import.FS_Write = FS_Write;
	import.FS_Print = FS_Print;
	import.FS_Tell = FS_Tell;
	import.FS_Seek = FS_Seek;
	import.FS_Eof = FS_Eof;
	import.FS_Flush = FS_Flush;
	import.FS_FCloseFile = FS_FCloseFile;
	import.FS_RemoveFile = FS_RemoveFile;
	import.FS_GetFileList = FS_GetFileList;
	import.FS_FirstExtension = FS_FirstExtension;
	import.FS_IsPureFile = FS_IsPureFile;
	import.FS_MoveFile = FS_MoveFile;
	import.FS_RemoveDirectory = FS_RemoveDirectory;

	import.Key_GetBindingBuf = Key_GetBindingBuf;
	import.Key_KeynumToString = Key_KeynumToString;

	import.GetConfigString = CL_GameModule_GetConfigString;
	import.Milliseconds = Sys_Milliseconds;
	import.DownloadRequest = CL_DownloadRequest;

	import.NET_GetUserCmd = CL_GameModule_NET_GetUserCmd;
	import.NET_GetCurrentUserCmdNum = CL_GameModule_NET_GetCurrentUserCmdNum;
	import.NET_GetCurrentState = CL_GameModule_NET_GetCurrentState;

	import.R_UpdateScreen = SCR_UpdateScreen;
	import.R_GetClippedFragments = re.GetClippedFragments;
	import.R_ClearScene = re.ClearScene;
	import.R_AddEntityToScene = re.AddEntityToScene;
	import.R_AddLightToScene = re.AddLightToScene;
	import.R_AddPolyToScene = re.AddPolyToScene;
	import.R_RenderScene = re.RenderScene;
	import.R_GetSpeedsMessage = re.GetSpeedsMessage;
	import.R_GetAverageFrametime = re.GetAverageFrametime;
	import.R_RegisterWorldModel = re.RegisterWorldModel;
	import.R_ModelBounds = re.ModelBounds;
	import.R_ModelFrameBounds = re.ModelFrameBounds;
	import.R_RegisterModel = re.RegisterModel;
	import.R_RegisterPic = re.RegisterPic;
	import.R_RegisterRawPic = re.RegisterRawPic;
	import.R_RegisterLevelshot = re.RegisterLevelshot;
	import.R_RegisterSkin = re.RegisterSkin;
	import.R_RegisterSkinFile = re.RegisterSkinFile;
	import.R_RegisterLinearPic = re.RegisterLinearPic;
	import.R_LerpTag = re.LerpTag;
	import.R_SetCustomColor = re.SetCustomColor;
	import.R_DrawStretchPic = re.DrawStretchPic;
	import.R_DrawRotatedStretchPic = re.DrawRotatedStretchPic;
	import.R_TransformVectorToScreen = re.TransformVectorToScreen;
	import.R_TransformVectorToScreenClamped = re.TransformVectorToScreenClamped;
	import.R_SkeletalGetNumBones = re.SkeletalGetNumBones;
	import.R_SkeletalGetBoneInfo = re.SkeletalGetBoneInfo;
	import.R_SkeletalGetBonePose = re.SkeletalGetBonePose;

	import.R_GetShaderForOrigin = re.GetShaderForOrigin;

	import.VID_FlashWindow = VID_FlashWindow;

	import.CM_NumInlineModels = CL_GameModule_CM_NumInlineModels;
	import.CM_InlineModel = CL_GameModule_CM_InlineModel;
	import.CM_TransformedBoxTrace = CL_GameModule_CM_TransformedBoxTrace;
	import.CM_TransformedPointContents = CL_GameModule_CM_TransformedPointContents;
	import.CM_ModelForBBox = CL_GameModule_CM_ModelForBBox;
	import.CM_OctagonModelForBBox = CL_GameModule_CM_OctagonModelForBBox;
	import.CM_InlineModelBounds = CL_GameModule_CM_InlineModelBounds;
	import.CM_InPVS = CL_GameModule_CM_InPVS;

	import.S_StartFixedSound = CL_SoundModule_StartFixedSound;
	import.S_StartEntitySound = CL_SoundModule_StartEntitySound;
	import.S_StartGlobalSound = CL_SoundModule_StartGlobalSound;
	import.S_StartLocalSound = CL_SoundModule_StartLocalSound;
	import.S_Update = CL_SoundModule_Update;
	import.S_ImmediateSound = CL_SoundModule_ImmediateSound;
	import.S_UpdateEntity = CL_SoundModule_UpdateEntity;

	import.SCR_RegisterFont = SCR_RegisterFont;
	import.SCR_RegisterSpecialFont = SCR_RegisterSpecialFont;
	import.SCR_DrawString = SCR_DrawString;
	import.SCR_DrawStringWidth = SCR_DrawStringWidth;
	import.SCR_DrawClampString = SCR_DrawClampString;
	import.SCR_DrawMultilineString = SCR_DrawMultilineString;
	import.SCR_DrawRawChar = SCR_DrawRawChar;
	import.SCR_DrawClampChar = SCR_DrawClampChar;
	import.SCR_FontSize = SCR_FontSize;
	import.SCR_FontHeight = SCR_FontHeight;
	import.SCR_FontUnderline = SCR_FontUnderline;
	import.SCR_FontAdvance = SCR_FontAdvance;
	import.SCR_FontXHeight = SCR_FontXHeight;
	import.SCR_SetDrawCharIntercept = SCR_SetDrawCharIntercept;
	import.SCR_strWidth = SCR_strWidth;
	import.SCR_StrlenForWidth = SCR_StrlenForWidth;
	import.SCR_DrawChat = Con_DrawChat;

	import.Mem_Alloc = CL_GameModule_MemAlloc;
	import.Mem_Free = CL_GameModule_MemFree;

	import.asGetAngelExport = QAS_GetAngelExport;

	cge = GetCGameAPI( &import );

	start = Sys_Milliseconds();
	cge->Init( cls.servername, cl.playernum,
			   viddef.width, viddef.height, VID_GetPixelRatio(),
			   cls.demo.playing, cls.demo.playing ? cls.demo.filename : "",
			   cls.sv_pure, cl.snapFrameTime, APP_PROTOCOL_VERSION, APP_DEMO_EXTENSION_STR,
			   cls.mediaRandomSeed, cl.gamestart );

	Com_DPrintf( "CL_GameModule_Init: %.2f seconds\n", (float)( Sys_Milliseconds() - start ) * 0.001f );

	cl.gamestart = false;
	cls.cgameActive = true;
}

void CL_GameModule_ResizeWindow( int width, int height ) {
	if( cge ) {
		cge->ResizeWindow( width, height );
	}
}

/*
* CL_GameModule_Reset
*/
void CL_GameModule_Reset( void ) {
	if( cge ) {
		cge->Reset();
	}
}

/*
* CL_GameModule_Shutdown
*/
void CL_GameModule_Shutdown( void ) {
	if( !cge ) {
		return;
	}

	cls.cgameActive = false;

	cge->Shutdown();
	Mem_FreePool( &cl_gamemodulepool );
	cge = NULL;
}

/*
* CL_GameModule_EscapeKey
*/
void CL_GameModule_EscapeKey( void ) {
	if( cge ) {
		cge->EscapeKey();
	}
}

/*
* CL_GameModule_GetEntitySoundOrigin
*/
void CL_GameModule_GetEntitySpatilization( int entNum, vec3_t origin, vec3_t velocity ) {
	if( cge ) {
		cge->GetEntitySpatilization( entNum, origin, velocity );
	}
}

/*
* CL_GameModule_ConfigString
*/
void CL_GameModule_ConfigString( int number, const char *value ) {
	if( cge ) {
		cge->ConfigString( number, value );
	}
}

/*
* CL_GameModule_NewSnapshot
*/
bool CL_GameModule_NewSnapshot( int pendingSnapshot ) {
	snapshot_t *currentSnap, *newSnap;

	if( cge ) {
		currentSnap = ( cl.currentSnapNum <= 0 ) ? NULL : &cl.snapShots[cl.currentSnapNum & UPDATE_MASK];
		newSnap = &cl.snapShots[pendingSnapshot & UPDATE_MASK];
		return cge->NewFrameSnapshot( newSnap, currentSnap );
	}

	return false;
}

/*
* CL_GameModule_RenderView
*/
void CL_GameModule_RenderView() {
	if( cge && cls.cgameActive ) {
		cge->RenderView( cls.frametime, cls.realFrameTime, cls.monotonicTime, cls.realtime, cl.serverTime,
						 cl_extrapolate->integer && !cls.demo.playing ? cl_extrapolationTime->integer : 0 );
	}
}

/*
* CL_GameModule_InputFrame
*/
void CL_GameModule_InputFrame( int frameTime ) {
	if( cge ) {
		cge->InputFrame( frameTime );
	}
}

/*
* CL_GameModule_ClearInputState
*/
void CL_GameModule_ClearInputState( void ) {
	if( cge ) {
		cge->ClearInputState();
	}
}

/*
* CL_GameModule_GetButtonBits
*/
unsigned CL_GameModule_GetButtonBits( void ) {
	if( cge ) {
		return cge->GetButtonBits();
	}
	return 0;
}

/*
* CL_GameModule_AddViewAngles
*/
void CL_GameModule_AddViewAngles( vec3_t viewAngles ) {
	if( cge ) {
		cge->AddViewAngles( viewAngles );
	}
}

/*
* CL_GameModule_AddMovement
*/
void CL_GameModule_AddMovement( vec3_t movement ) {
	if( cge ) {
		cge->AddMovement( movement );
	}
}

/*
* CL_GameModule_MouseMove
*/
void CL_GameModule_MouseMove( int dx, int dy ) {
	if( cge ) {
		cge->MouseMove( dx, dy );
	}
}

/*
* CL_GameModule_KeyEvent
*/
bool CL_GameModule_KeyEvent( int key, bool down ) {
	if( cge ) {
		return cge->KeyEvent( key, down );
	}
	return false;
}
