/*
Copyright (C) 2006 Pekka Lampila ("Medar"), Damien Deville ("Pb")
and German Garcia Fernandez ("Jal") for Chasseur de bots association.


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

#include "cg_local.h"

/*
* CG_ForceTeam
*/
static int CG_IsAlly( int team ) {
	if( team == TEAM_ALLY || team == TEAM_ENEMY )
		return team == TEAM_ALLY;

	int myteam = cg.predictedPlayerState.stats[STAT_TEAM];
	if( myteam == TEAM_SPECTATOR )
		return team == TEAM_ALPHA;
	return team == myteam;
}

/*
* CG_SetSceneTeamColors
* Updates the team colors in the renderer with the ones assigned to each team.
*/
void CG_SetSceneTeamColors( void ) {
	int team;
	vec4_t color;

	// send always white for the team spectators
	trap_R_SetCustomColor( TEAM_SPECTATOR, 255, 255, 255 );

	for( team = TEAM_PLAYERS; team < GS_MAX_TEAMS; team++ ) {
		CG_TeamColor( team, color );
		trap_R_SetCustomColor( team, (uint8_t)( color[0] * 255 ), (uint8_t)( color[1] * 255 ), (uint8_t)( color[2] * 255 ) ); // update the renderer
	}
}

/*
* CG_RegisterForceModel
*/
static void CG_RegisterForceModel( cvar_t *modelCvar, cvar_t *modelForceCvar, pmodelinfo_t **model, StringHash *skin ) {
	if( !modelCvar->modified && !modelForceCvar->modified )
		return;
	modelCvar->modified = false;
	modelForceCvar->modified = false;

	*model = NULL;
	*skin = EMPTY_HASH;

	if( modelForceCvar->integer ) {
		const char * name = modelCvar->string;
		pmodelinfo_t * new_model = CG_RegisterPlayerModel( va( "models/players/%s", name ) );
		if( new_model == NULL ) {
			name = modelCvar->dvalue;
			new_model = CG_RegisterPlayerModel( va( "models/players/%s", name ) );
		}

		StringHash new_skin = StringHash( va( "models/players/%s/default", name ) );

		if( new_model != NULL ) {
			*model = new_model;
			*skin = new_skin;
		}
	}
}

static void CG_CheckUpdateTeamModelRegistration( bool ally ) {
	cvar_t * modelCvar = ally ? cg_allyModel : cg_enemyModel;
	cvar_t * modelForceCvar = ally ? cg_allyForceModel : cg_enemyForceModel;
	CG_RegisterForceModel( modelCvar, modelForceCvar, &cgs.teamModelInfo[ int( ally ) ], &cgs.teamCustomSkin[ int( ally ) ] );
}

/*
* CG_PModelForCentity
*/
void CG_PModelForCentity( centity_t *cent, pmodelinfo_t **pmodelinfo, StringHash *skin ) {
	centity_t * owner = cent;
	if( cent->current.type == ET_CORPSE && cent->current.bodyOwner )
		owner = &cg_entities[cent->current.bodyOwner];
	unsigned int ownerNum = owner->current.number;

	bool ally = CG_IsAlly( owner->current.team );

	CG_CheckUpdateTeamModelRegistration( ally );

	// use the player defined one if not forcing
	*pmodelinfo = cgs.pModelsIndex[cent->current.modelindex];
	*skin = cent->current.skin;

	if( GS_CanForceModels() && ownerNum < unsigned( gs.maxclients + 1 ) ) {
		if( cgs.teamModelInfo[ int( ally ) ] != NULL ) {
			*pmodelinfo = cgs.teamModelInfo[ int( ally ) ];
			*skin = cgs.teamCustomSkin[ int( ally ) ];
		}
	}
}

void CG_RegisterTeamColor( bool ally ) {
	cvar_t * cvar = ally ? cg_allyColor : cg_enemyColor;
	if( !cvar->modified )
		return;
	cvar->modified = false;

	int rgb = COM_ReadColorRGBString( cvar->string );
	if( rgb == -1 )
		rgb = COM_ReadColorRGBString( cvar->dvalue );
	cgs.teamColor[ int( ally ) ] = rgb;
}

void CG_TeamColor( int team, vec4_t color ) {
	bool ally = CG_IsAlly( team );

	CG_RegisterTeamColor( ally );

	int rgb = cgs.teamColor[ int( ally ) ];
	color[0] = COLOR_R( rgb ) * ( 1.0f / 255.0f );
	color[1] = COLOR_G( rgb ) * ( 1.0f / 255.0f );
	color[2] = COLOR_B( rgb ) * ( 1.0f / 255.0f );
	color[3] = 1.0f;
}

void CG_TeamColorForEntity( int entNum, byte_vec4_t color ) {
	if( entNum < 1 || entNum >= MAX_EDICTS ) {
		Vector4Set( color, 255, 255, 255, 255 );
		return;
	}

	centity_t *cent = &cg_entities[entNum];
	if( cent->current.type == ET_CORPSE ) {
		Vector4Set( color, 60, 60, 60, 255 );
		return;
	}

	bool ally = CG_IsAlly( cent->current.team );

	CG_RegisterTeamColor( ally );

	int rgb = cgs.teamColor[ int( ally ) ];
	Vector4Set( color, COLOR_R( rgb ), COLOR_G( rgb ), COLOR_B( rgb ), 255 );
}

void CG_RegisterForceModels( void ) {
	CG_CheckUpdateTeamModelRegistration( true );
	CG_CheckUpdateTeamModelRegistration( false );

	CG_RegisterTeamColor( true );
	CG_RegisterTeamColor( false );
}
