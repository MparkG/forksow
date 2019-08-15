/*
Copyright (C) 2013 Victor Luchits

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

// r_public.c
#include "r_local.h"
#include "r_frontend.h"

ref_import_t ri;

/*
* GetRefAPI
*
* Returns a pointer to the structure with all entry points
*/

extern "C" QF_DLL_EXPORT ref_export_t *GetRefAPI( ref_import_t *import ) {
	static ref_export_t globals;

	ri = *import;

	globals.Shutdown = RF_Shutdown;
	globals.AppActivate = RF_AppActivate;

	globals.BeginFrame = RF_BeginFrame;
	globals.EndFrame = RF_EndFrame;
	globals.ClearScene = RF_ClearScene;
	globals.AddEntityToScene = RF_AddEntityToScene;
	globals.AddLightToScene = RF_AddLightToScene;
	globals.AddPolyToScene = RF_AddPolyToScene;
	globals.RenderScene = RF_RenderScene;
	globals.DrawStretchPic = RF_DrawStretchPic;
	globals.DrawRotatedStretchPic = RF_DrawRotatedStretchPic;
	globals.Scissor = RF_SetScissor;
	globals.ResetScissor = RF_ResetScissor;
	globals.ReplaceRawSubPic = RF_ReplaceRawSubPic;
	globals.BlurScreen = RF_BlurScreen;

	globals.LerpTag = RF_LerpTag;
	globals.TransformVectorToScreen = RF_TransformVectorToScreen;
	globals.TransformVectorToScreenClamped = RF_TransformVectorToScreenClamped;

	globals.GetSpeedsMessage = RF_GetSpeedsMessage;
	globals.GetAverageFrametime = RF_GetAverageFrametime;

	globals.RegisterWorldModel = RF_RegisterWorldModel;
	globals.RegisterModel = R_RegisterModel;
	globals.RegisterPic = R_RegisterPic;
	globals.RegisterAlphaMask = R_RegisterAlphaMask;
	globals.RegisterSkin = R_RegisterSkin;

	globals.GetClippedFragments = R_GetClippedFragments;

	return &globals;
}
