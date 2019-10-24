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

// gs_weapons.c	-	game shared weapons definitions

#include "q_arch.h"
#include "q_math.h"
#include "q_shared.h"
#include "q_comref.h"
#include "q_collision.h"
#include "gs_public.h"

/*
* GS_TraceBullet
*/
trace_t *GS_TraceBullet( trace_t *trace, vec3_t start, vec3_t dir, vec3_t right, vec3_t up, float r, float u, int range, int ignore, int timeDelta ) {
	vec3_t end;
	bool water = false;
	vec3_t water_start;
	int content_mask = MASK_SHOT | MASK_WATER;
	static trace_t water_trace;

	assert( trace );

	VectorMA( start, range, dir, end );
	if( r ) {
		VectorMA( end, r, right, end );
	}
	if( u ) {
		VectorMA( end, u, up, end );
	}

	if( gs.api.PointContents( start, timeDelta ) & MASK_WATER ) {
		water = true;
		VectorCopy( start, water_start );
		content_mask &= ~MASK_WATER;
	}

	gs.api.Trace( trace, start, vec3_origin, vec3_origin, end, ignore, content_mask, timeDelta );

	// see if we hit water
	if( trace->contents & MASK_WATER ) {
		water_trace = *trace;

		VectorCopy( trace->endpos, water_start );

		// re-trace ignoring water this time
		gs.api.Trace( trace, water_start, vec3_origin, vec3_origin, end, ignore, MASK_SHOT, timeDelta );

		return &water_trace;
	}

	if( water ) {
		water_trace = *trace;
		VectorCopy( water_start, water_trace.endpos );
		return &water_trace;
	}

	return NULL;
}

void GS_TraceLaserBeam( trace_t *trace, vec3_t origin, vec3_t angles, float range, int ignore, int timeDelta, void ( *impact )( trace_t *tr, vec3_t dir ) ) {
	vec3_t dir, end;
	vec3_t mins = { -0.5, -0.5, -0.5 };
	vec3_t maxs = { 0.5, 0.5, 0.5 };

	AngleVectors( angles, dir, NULL, NULL );
	VectorMA( origin, range, dir, end );

	trace->ent = 0;

	gs.api.Trace( trace, origin, mins, maxs, end, ignore, MASK_SHOT, timeDelta );
	if( trace->ent != -1 && impact != NULL ) {
		impact( trace, dir );
	}
}


//============================================================
//
//		PLAYER WEAPON THINKING
//
//============================================================

#define NOAMMOCLICK_PENALTY 100

/*
* GS_SelectBestWeapon
*/
int GS_SelectBestWeapon( player_state_t *playerState ) {
	int weap, weap_chosen = WEAP_NONE;
	gs_weapon_definition_t *weapondef;

	//find with strong ammos
	for( weap = WEAP_TOTAL - 1; weap >= WEAP_GUNBLADE; weap-- ) {
		if( !playerState->inventory[weap] ) {
			continue;
		}

		weapondef = GS_GetWeaponDef( weap );

		if( !weapondef->firedef.usage_count ||
			playerState->inventory[weapondef->firedef.ammo_id] >= weapondef->firedef.usage_count ) {
			weap_chosen = weap;
			goto found;
		}
	}
found:
	return weap_chosen;
}

/*
* GS_CheckAmmoInWeapon
*/
bool GS_CheckAmmoInWeapon( player_state_t *playerState, int checkweapon ) {
	if( checkweapon != WEAP_NONE && !playerState->inventory[checkweapon] ) {
		return false;
	}

	const firedef_t * firedef = GS_GetWeaponDef( checkweapon )->firedef;
	if( !firedef->usage_count || firedef->ammo_id == AMMO_NONE ) {
		return true;
	}

	return playerState->inventory[firedef->ammo_id] >= firedef->usage_count;
}

/*
* GS_ThinkPlayerWeapon
*/
int GS_ThinkPlayerWeapon( player_state_t *playerState, int buttons, int msecs, int timeDelta ) {
	firedef_t *firedef;
	bool refire = false;

	assert( playerState->stats[STAT_PENDING_WEAPON] >= 0 && playerState->stats[STAT_PENDING_WEAPON] < WEAP_TOTAL );

	if( GS_MatchPaused() ) {
		return playerState->stats[STAT_WEAPON];
	}

	if( playerState->pmove.pm_type != PM_NORMAL ) {
		playerState->weaponState = WEAPON_STATE_READY;
		playerState->stats[STAT_PENDING_WEAPON] = playerState->stats[STAT_WEAPON] = WEAP_NONE;
		playerState->stats[STAT_WEAPON_TIME] = 0;
		return playerState->stats[STAT_WEAPON];
	}

	if( playerState->pmove.stats[PM_STAT_NOUSERCONTROL] > 0 ) {
		buttons = 0;
	}

	if( !msecs ) {
		goto done;
	}

	playerState->stats[STAT_WEAPON_TIME] = Max2( 0, playerState->stats[ STAT_WEAPON_TIME ] - msecs );

	firedef = GS_FiredefForPlayerState( playerState, playerState->stats[STAT_WEAPON] );

	// during cool-down time it can shoot again or go into reload time
	if( playerState->weaponState == WEAPON_STATE_REFIRE ) {
		if( playerState->stats[STAT_WEAPON_TIME] > 0 ) {
			goto done;
		}

		refire = true;

		playerState->weaponState = WEAPON_STATE_READY;
	}

	if( playerState->weaponState == WEAPON_STATE_NOAMMOCLICK ) {
		if( playerState->stats[STAT_WEAPON_TIME] > 0 ) {
			goto done;
		}

		if( playerState->stats[STAT_WEAPON] != playerState->stats[STAT_PENDING_WEAPON] ) {
			playerState->weaponState = WEAPON_STATE_READY;
		}
	}

	// there is a weapon to be changed
	if( playerState->stats[STAT_WEAPON] != playerState->stats[STAT_PENDING_WEAPON] ) {
		if( playerState->weaponState == WEAPON_STATE_READY || playerState->weaponState == WEAPON_STATE_ACTIVATING ) {
			playerState->weaponState = WEAPON_STATE_DROPPING;
			playerState->stats[STAT_WEAPON_TIME] += firedef->weapondown_time;

			if( firedef->weapondown_time ) {
				gs.api.PredictedEvent( playerState->POVnum, EV_WEAPONDROP, 0 );
			}
		}
	}

	// do the change
	if( playerState->weaponState == WEAPON_STATE_DROPPING ) {
		if( playerState->stats[STAT_WEAPON_TIME] > 0 ) {
			goto done;
		}

		bool had_weapon_before = playerState->stats[STAT_WEAPON] != WEAP_NONE;
		playerState->stats[STAT_WEAPON] = playerState->stats[STAT_PENDING_WEAPON];

		// update the firedef
		firedef = GS_FiredefForPlayerState( playerState, playerState->stats[STAT_WEAPON] );
		playerState->weaponState = WEAPON_STATE_ACTIVATING;
		playerState->stats[STAT_WEAPON_TIME] += firedef->weaponup_time;

		int parm = playerState->stats[STAT_WEAPON] << 1;
		if( !had_weapon_before )
			parm |= 1;

		gs.api.PredictedEvent( playerState->POVnum, EV_WEAPONACTIVATE, parm );
	}

	if( playerState->weaponState == WEAPON_STATE_ACTIVATING ) {
		if( playerState->stats[STAT_WEAPON_TIME] > 0 ) {
			goto done;
		}

		playerState->weaponState = WEAPON_STATE_READY;
	}

	if( playerState->weaponState == WEAPON_STATE_READY || playerState->weaponState == WEAPON_STATE_NOAMMOCLICK ) {
		if( playerState->stats[STAT_WEAPON_TIME] > 0 ) {
			goto done;
		}

		if( !GS_ShootingDisabled() ) {
			if( buttons & BUTTON_ATTACK ) {
				if( GS_CheckAmmoInWeapon( playerState, playerState->stats[STAT_WEAPON] ) ) {
					playerState->weaponState = WEAPON_STATE_FIRING;
				}
				else if( playerState->weaponState != WEAPON_STATE_NOAMMOCLICK ) {
					// player has no ammo nor clips
					playerState->weaponState = WEAPON_STATE_NOAMMOCLICK;
					playerState->stats[STAT_WEAPON_TIME] += NOAMMOCLICK_PENALTY;
					gs.api.PredictedEvent( playerState->POVnum, EV_NOAMMOCLICK, 0 );
					goto done;
				}
			}
		}
	}

	if( playerState->weaponState == WEAPON_STATE_FIRING ) {
		int parm = playerState->stats[STAT_WEAPON] << 1;

		playerState->stats[STAT_WEAPON_TIME] += firedef->reload_time;
		playerState->weaponState = WEAPON_STATE_REFIRE;

		if( refire && firedef->smooth_refire ) {
			gs.api.PredictedEvent( playerState->POVnum, EV_SMOOTHREFIREWEAPON, parm );
		} else {
			gs.api.PredictedEvent( playerState->POVnum, EV_FIREWEAPON, parm );
		}

		if( !GS_InfiniteAmmo() && playerState->stats[STAT_WEAPON] != WEAP_GUNBLADE ) {
			if( firedef->ammo_id != AMMO_NONE && firedef->usage_count ) {
				playerState->inventory[firedef->ammo_id] -= firedef->usage_count;
				if( playerState->inventory[firedef->ammo_id] == 0 ) {
					gs.api.PredictedEvent( playerState->POVnum, EV_NOAMMOCLICK, 0 );
				}
			}
		}
	}
done:
	return playerState->stats[STAT_WEAPON];
}
