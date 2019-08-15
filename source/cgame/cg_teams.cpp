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

static bool CG_IsAlly( int team ) {
	if( team == TEAM_ALLY || team == TEAM_ENEMY )
		return team == TEAM_ALLY;

	int myteam = cg.predictedPlayerState.stats[STAT_TEAM];
	if( myteam == TEAM_SPECTATOR )
		return team == TEAM_ALPHA;
	if( myteam == TEAM_PLAYERS )
		return false;
	return team == myteam;
}

static void CG_RegisterForceModel( cvar_t *modelCvar, cvar_t *modelForceCvar, PlayerModelMetadata **model ) {
	if( !modelCvar->modified && !modelForceCvar->modified )
		return;
	modelCvar->modified = false;
	modelForceCvar->modified = false;

	*model = NULL;

	if( modelForceCvar->integer ) {
		const char * name = modelCvar->string;
		PlayerModelMetadata * new_model = CG_RegisterPlayerModel( va( "models/players/%s", name ) );
		if( new_model == NULL ) {
			name = modelCvar->dvalue;
			new_model = CG_RegisterPlayerModel( va( "models/players/%s", name ) );
		}

		if( new_model != NULL ) {
			*model = new_model;
		}
	}
}

static void CG_CheckUpdateTeamModelRegistration( bool ally ) {
	cvar_t * modelCvar = ally ? cg_allyModel : cg_enemyModel;
	cvar_t * modelForceCvar = ally ? cg_allyForceModel : cg_enemyForceModel;
	CG_RegisterForceModel( modelCvar, modelForceCvar, &cgs.teamModelInfo[ int( ally ) ] );
}

void CG_PModelForCentity( centity_t *cent, PlayerModelMetadata **pmodelinfo ) {
	centity_t * owner = cent;
	if( cent->current.type == ET_CORPSE && cent->current.bodyOwner )
		owner = &cg_entities[cent->current.bodyOwner];
	unsigned int ownerNum = owner->current.number;

	bool ally = CG_IsAlly( owner->current.team );

	CG_CheckUpdateTeamModelRegistration( ally );

	// use the player defined one if not forcing
	*pmodelinfo = cgs.pModelsIndex[cent->current.modelindex];

	if( GS_CanForceModels() && ownerNum < unsigned( gs.maxclients + 1 ) ) {
		if( cgs.teamModelInfo[ int( ally ) ] != NULL ) {
			*pmodelinfo = cgs.teamModelInfo[ int( ally ) ];
		}
	}
}

RGB8 CG_TeamColor( int team ) {
	if( team == TEAM_PLAYERS )
		return RGB8( 255, 255, 255 );

	cvar_t * cvar = CG_IsAlly( team ) ? cg_allyColor : cg_enemyColor;

	if( cvar->integer >= int( ARRAY_COUNT( TEAM_COLORS ) ) )
		trap_Cvar_Set( cvar->name, cvar->dvalue );

	return TEAM_COLORS[ cvar->integer ].rgb;
}

void CG_TeamColor( int team, vec4_t color ) {
	RGB8 rgb = CG_TeamColor( team );
	color[0] = rgb.r * ( 1.0f / 255.0f );
	color[1] = rgb.g * ( 1.0f / 255.0f );
	color[2] = rgb.b * ( 1.0f / 255.0f );
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

	RGB8 rgb = CG_TeamColor( cent->current.team );
	color[0] = rgb.r;
	color[1] = rgb.g;
	color[2] = rgb.b;
	color[3] = 255;
}

void CG_RegisterForceModels() {
	CG_CheckUpdateTeamModelRegistration( true );
	CG_CheckUpdateTeamModelRegistration( false );
}
