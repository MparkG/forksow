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

#include <time.h>

#include "cg_local.h"
#include "client/ui.h"

/*
==========================================================================

SERVER COMMANDS

==========================================================================
*/

/*
* CG_SC_Print
*/
static void CG_SC_Print( void ) {
	CG_LocalPrint( "%s", Cmd_Argv( 1 ) );
}

/*
* CG_SC_ChatPrint
*/
static void CG_SC_ChatPrint() {
	bool teamonly = Q_stricmp( Cmd_Argv( 0 ), "tch" ) == 0;
	int who = atoi( Cmd_Argv( 1 ) );

	if( who < 0 || who > MAX_CLIENTS ) {
		return;
	}

	if( cg_chatFilter->integer & ( teamonly ? 2 : 1 ) ) {
		return;
	}

	const char * text = Cmd_Argv( 2 );

	if( who == 0 ) {
		CG_LocalPrint( "Console: %s\n", text );
		return;
	}

	const char * name = cgs.clientInfo[ who - 1 ].name;
	int team = cg_entities[ who ].current.team;
	RGB8 team_color = team == TEAM_SPECTATOR ? RGB8( 128, 128, 128 ) : CG_TeamColor( team );

	const char * prefix = "";
	if( teamonly ) {
		prefix = team == TEAM_SPECTATOR ? "[SPEC] " : "[TEAM] ";
	}

	ImGuiColorToken color( team_color );
	CG_LocalPrint( "%s%s%s%s: %s\n",
		prefix,
		( const char * ) ImGuiColorToken( team_color ).token, name,
		( const char * ) ImGuiColorToken( rgba8_white ).token, text );

	if( !cgs.demoPlaying ) {
		CG_FlashChatHighlight( who - 1, text );
	}
}

/*
* CG_SC_CenterPrint
*/
static void CG_SC_CenterPrint( void ) {
	CG_CenterPrint( Cmd_Argv( 1 ) );
}

/*
* CG_ConfigString
*/
void CG_ConfigString( int i, const char *s ) {
	size_t len;

	// wsw : jal : warn if configstring overflow
	len = strlen( s );
	if( len >= MAX_CONFIGSTRING_CHARS ) {
		CG_Printf( "%sWARNING:%s Configstring %i overflowed\n", S_COLOR_YELLOW, S_COLOR_WHITE, i );
	}

	if( i < 0 || i >= MAX_CONFIGSTRINGS ) {
		CG_Error( "configstring > MAX_CONFIGSTRINGS" );
		return;
	}

	Q_strncpyz( cgs.configStrings[i], s, sizeof( cgs.configStrings[i] ) );

	// do something apropriate
	if( i == CS_AUTORECORDSTATE ) {
		CG_SC_AutoRecordAction( cgs.configStrings[i] );
	} else if( i >= CS_MODELS && i < CS_MODELS + MAX_MODELS ) {
		cgs.modelDraw[i - CS_MODELS] = FindModel( cgs.configStrings[i] );
	} else if( i >= CS_SOUNDS && i < CS_SOUNDS + MAX_SOUNDS ) {
		cgs.soundPrecache[i - CS_SOUNDS] = FindSoundEffect( cgs.configStrings[i] );
	} else if( i >= CS_IMAGES && i < CS_IMAGES + MAX_IMAGES ) {
		cgs.imagePrecache[i - CS_IMAGES] = FindMaterial( cgs.configStrings[i] );
	} else if( i >= CS_PLAYERINFOS && i < CS_PLAYERINFOS + MAX_CLIENTS ) {
		CG_LoadClientInfo( i - CS_PLAYERINFOS );
	} else if( i >= CS_GAMECOMMANDS && i < CS_GAMECOMMANDS + MAX_GAMECOMMANDS ) {
		if( !cgs.demoPlaying ) {
			Cmd_AddCommand( cgs.configStrings[i], NULL );
		}
	} else if( i >= CS_WEAPONDEFS && i < CS_WEAPONDEFS + MAX_WEAPONDEFS ) {
		CG_OverrideWeapondef( i - CS_WEAPONDEFS, cgs.configStrings[i] );
	}
}

/*
* CG_SC_Scoreboard
*/
static void CG_SC_Scoreboard( void ) {
	SCR_UpdateScoreboardMessage( Cmd_Argv( 1 ) );
}

static int ParseIntOr0( const char ** cursor ) {
	Span< const char > token = ParseToken( cursor, Parse_DontStopOnNewLine );
	int x;
	return SpanToInt( token, &x ) ? x : 0;
}

static void CG_SC_PlayerStats() {
	const char * s = Cmd_Argv( 1 );

	int playerNum = ParseIntOr0( &s );
	if( playerNum < 0 || playerNum >= client_gs.maxclients ) {
		return;
	}

	CG_LocalPrint( "Stats for %s" S_COLOR_WHITE ":\n", cgs.clientInfo[playerNum].name );
	CG_Printf( "\nWeapon\n" );
	CG_Printf( "    hit/shot percent\n" );

	for( int i = WEAP_GUNBLADE; i < WEAP_TOTAL; i++ ) {
		const gsitem_t * item = GS_FindItemByTag( i );
		assert( item );

		int shots = ParseIntOr0( &s );
		if( shots < 1 ) { // only continue with registered shots
			continue;
		}
		int hits = ParseIntOr0( &s );

		// name
		CG_Printf( "%s%2s" S_COLOR_WHITE ": ", ImGuiColorToken( item->color ).token, item->shortname );

#define STATS_PERCENT( hit, total ) ( ( total ) == 0 ? 0 : ( ( hit ) == ( total ) ? 100 : (float)( hit ) * 100.0f / (float)( total ) ) )

		// total
		CG_Printf( S_COLOR_GREEN "%3i" S_COLOR_WHITE "/" S_COLOR_CYAN "%3i      " S_COLOR_YELLOW "%2.1f\n",
			   hits, shots, STATS_PERCENT( hits, shots ) );
	}

	CG_Printf( "\n" );

	int total_damage_given = ParseIntOr0( &s );
	int total_damage_received = ParseIntOr0( &s );

	CG_LocalPrint( S_COLOR_YELLOW "Damage given/received: " S_COLOR_WHITE "%i/%i " S_COLOR_YELLOW "ratio: %s%3.2f\n",
		total_damage_given, total_damage_received,
		total_damage_given > total_damage_received ? S_COLOR_GREEN : S_COLOR_RED,
		STATS_PERCENT( total_damage_given, total_damage_given + total_damage_received ) );

#undef STATS_PERCENT
}

/*
* CG_SC_AutoRecordName
*/
static const char *CG_SC_AutoRecordName( void ) {
	static char name[MAX_STRING_CHARS];

	char date[ 128 ];
	Sys_FormatTime( date, sizeof( date ), "%Y-%m-%d_%H-%M" );

	snprintf( name, sizeof( name ), "%s_%s_%04i", date, cgs.configStrings[CS_MAPNAME], random_uniform( &cls.rng, 0, 10000 ) );

	return name;
}

/*
* CG_SC_AutoRecordAction
*/
void CG_SC_AutoRecordAction( const char *action ) {
	static bool autorecording = false;
	const char *name;
	bool spectator;

	if( !action[0] ) {
		return;
	}

	// filter out autorecord commands when playing a demo
	if( cgs.demoPlaying ) {
		return;
	}

	// let configstrings and other stuff arrive before taking any action
	if( !cgs.precacheDone ) {
		return;
	}

	if( cg.frame.playerState.pmove.pm_type == PM_SPECTATOR || cg.frame.playerState.pmove.pm_type == PM_CHASECAM ) {
		spectator = true;
	} else {
		spectator = false;
	}

	name = CG_SC_AutoRecordName();

	if( !Q_stricmp( action, "start" ) ) {
		if( cg_autoaction_demo->integer && ( !spectator || cg_autoaction_spectator->integer ) ) {
			Cbuf_ExecuteText( EXEC_NOW, "stop silent" );
			Cbuf_ExecuteText( EXEC_NOW, va( "record autorecord/%s silent", name ) );
			autorecording = true;
		}
	} else if( !Q_stricmp( action, "altstart" ) ) {
		if( cg_autoaction_demo->integer && ( !spectator || cg_autoaction_spectator->integer ) ) {
			Cbuf_ExecuteText( EXEC_NOW, va( "record autorecord/%s silent", name ) );
			autorecording = true;
		}
	} else if( !Q_stricmp( action, "stop" ) ) {
		if( autorecording ) {
			Cbuf_ExecuteText( EXEC_NOW, "stop silent" );
			autorecording = false;
		}

		if( cg_autoaction_screenshot->integer && ( !spectator || cg_autoaction_spectator->integer ) ) {
			Cbuf_ExecuteText( EXEC_NOW, va( "screenshot autorecord/%s silent", name ) );
		}
	} else if( !Q_stricmp( action, "cancel" ) ) {
		if( autorecording ) {
			Cbuf_ExecuteText( EXEC_NOW, "stop cancel silent" );
			autorecording = false;
		}
	} else if( developer->integer ) {
		CG_Printf( "CG_SC_AutoRecordAction: Unknown action: %s\n", action );
	}
}

/*
* CG_Cmd_DemoGet_f
*/
static bool demo_requested = false;
static void CG_Cmd_DemoGet_f( void ) {
	if( demo_requested ) {
		CG_Printf( "Already requesting a demo\n" );
		return;
	}

	if( Cmd_Argc() != 2 || ( atoi( Cmd_Argv( 1 ) ) <= 0 && Cmd_Argv( 1 )[0] != '.' ) ) {
		CG_Printf( "Usage: demoget <number>\n" );
		CG_Printf( "Downloads a demo from the server\n" );
		CG_Printf( "Use the demolist command to see list of demos on the server\n" );
		return;
	}

	Cbuf_ExecuteText( EXEC_NOW, va( "cmd demoget %s", Cmd_Argv( 1 ) ) );

	demo_requested = true;
}

/*
* CG_SC_DemoGet
*/
static void CG_SC_DemoGet( void ) {
	const char *filename, *extension;

	if( cgs.demoPlaying ) {
		// ignore download commands coming from demo files
		return;
	}

	if( !demo_requested ) {
		CG_Printf( "Warning: demoget when not requested, ignored\n" );
		return;
	}

	demo_requested = false;

	if( Cmd_Argc() < 2 ) {
		CG_Printf( "No such demo found\n" );
		return;
	}

	filename = Cmd_Argv( 1 );
	extension = COM_FileExtension( filename );
	if( !COM_ValidateRelativeFilename( filename ) ||
		!extension || Q_stricmp( extension, APP_DEMO_EXTENSION_STR ) ) {
		CG_Printf( "Warning: demoget: Invalid filename, ignored\n" );
		return;
	}

	trap_DownloadRequest( filename );
}

static void CG_SC_ChangeLoadout() {
	int weapons[ WEAP_TOTAL ] = { };
	size_t n = 0;

	if( Cmd_Argc() - 1 >= ARRAY_COUNT( weapons ) )
		return;

	for( int i = 0; i < Cmd_Argc() - 1; i++ ) {
		int weapon = atoi( Cmd_Argv( i + 1 ) );
		if( weapon <= WEAP_NONE || weapon >= WEAP_TOTAL )
			return;
		weapons[ n ] = weapon;
		n++;
	}

	UI_ShowLoadoutMenu( Span< int >( weapons, n ) );
}

static void CG_SC_SaveLoadout() {
	Cvar_Set( "cg_loadout", Cmd_Args() );
}

void CG_AddAward( const char * str ) {
	if( !str || !str[0] ) {
		return;
	}

	Q_strncpyz( cg.award_lines[cg.award_head % MAX_AWARD_LINES], str, MAX_CONFIGSTRING_CHARS );
	cg.award_times[cg.award_head % MAX_AWARD_LINES] = cl.serverTime;
	cg.award_head++;
}

static void CG_SC_AddAward() {
	CG_AddAward( Cmd_Argv( 1 ) );
}

struct ServerCommand {
	const char * name;
	void ( *func )();
};

static const ServerCommand server_commands[] = {
	{ "pr", CG_SC_Print },
	{ "ch", CG_SC_ChatPrint },
	{ "tch", CG_SC_ChatPrint },
	{ "cp", CG_SC_CenterPrint },
	{ "obry", CG_SC_Obituary },
	{ "scb", CG_SC_Scoreboard },
	{ "plstats", CG_SC_PlayerStats },
	{ "demoget", CG_SC_DemoGet },
	{ "aw", CG_SC_AddAward },
	{ "changeloadout", CG_SC_ChangeLoadout },
	{ "saveloadout", CG_SC_SaveLoadout },
};

void CG_GameCommand( const char * command ) {
	Cmd_TokenizeString( command );
	const char * name = Cmd_Argv( 0 );

	for( ServerCommand cmd : server_commands ) {
		if( strcmp( name, cmd.name ) == 0 ) {
			cmd.func();
			return;
		}
	}

	CG_Printf( "Unknown game command: %s\n", name );
}

/*
==========================================================================

CGAME COMMANDS

==========================================================================
*/

/*
* CG_UseItem
*/
void CG_UseItem( const char *name ) {
	const gsitem_t *item;

	if( !cg.frame.valid || cgs.demoPlaying ) {
		return;
	}

	if( !name ) {
		return;
	}

	item = GS_Cmd_UseItem( &client_gs, &cg.frame.playerState, name, 0 );
	if( item ) {
		if( item->type & IT_WEAPON ) {
			CG_Predict_ChangeWeapon( item->tag );
			cg.lastWeapon = cg.predictedPlayerState.stats[STAT_PENDING_WEAPON];
		}

		Cbuf_ExecuteText( EXEC_NOW, va( "cmd use %i", item->tag ) );
	}
}

/*
* CG_Cmd_UseItem_f
*/
static void CG_Cmd_UseItem_f( void ) {
	if( !Cmd_Argc() ) {
		CG_Printf( "Usage: 'use <item name>' or 'use <item index>'\n" );
		return;
	}

	CG_UseItem( Cmd_Args() );
}

static const Item * CG_UseWeaponStep( const player_state_t * playerState, bool next, ItemType predicted_equipped_item ) {
	if( predicted_equipped_item < Item_FirstWeapon || predicted_equipped_item > Item_LastWeapon ) {
		return GS_FindItemByType( cg.lastWeapon );
	}

	ItemType weapon = predicted_equipped_item;
	while( true ) {
		weapon += next ? 1 : -1;
		if( weapon < Item_FirstWeapon ) {
			weapon += Item_WeaponCount;
		}
		if( weapon > Item_LastWeapon ) {
			weapon -= Item_WeaponCount;
		}

		if( weapon == predicted_equipped_item )
			break;

		if( GS_CanEquip( playerState, weapon ) ) {
			return GS_FindItemByType( weapon );
		}
	}

	return NULL;
}

/*
* CG_Cmd_NextWeapon_f
*/
static void CG_Cmd_NextWeapon_f( void ) {
	const gsitem_t *item;

	if( !cg.frame.valid ) {
		return;
	}

	if( cgs.demoPlaying || cg.predictedPlayerState.pmove.pm_type == PM_CHASECAM ) {
		CG_ChaseStep( 1 );
		return;
	}

	item = CG_UseWeaponStep( &cg.frame.playerState, 1, cg.predictedWeaponSwitch );
	if( item ) {
		CG_Predict_ChangeWeapon( item->tag );
		Cbuf_ExecuteText( EXEC_NOW, va( "cmd use %i", item->tag ) );
		cg.lastWeapon = cg.predictedPlayerState.stats[STAT_PENDING_WEAPON];
	}
}

/*
* CG_Cmd_PrevWeapon_f
*/
static void CG_Cmd_PrevWeapon_f( void ) {
	const gsitem_t *item;

	if( !cg.frame.valid ) {
		return;
	}

	if( cgs.demoPlaying || cg.predictedPlayerState.pmove.pm_type == PM_CHASECAM ) {
		CG_ChaseStep( -1 );
		return;
	}

	item = CG_UseWeaponStep( &cg.frame.playerState, -1, cg.predictedWeaponSwitch );
	if( item ) {
		CG_Predict_ChangeWeapon( item->tag );
		Cbuf_ExecuteText( EXEC_NOW, va( "cmd use %i", item->tag ) );
		cg.lastWeapon = cg.predictedPlayerState.stats[STAT_PENDING_WEAPON];
	}
}

/*
* CG_Cmd_PrevWeapon_f
*/
static void CG_Cmd_LastWeapon_f( void ) {
	const gsitem_t *item;

	if( !cg.frame.valid || cgs.demoPlaying ) {
		return;
	}

	if( cg.lastWeapon != WEAP_NONE && cg.lastWeapon != cg.predictedPlayerState.stats[STAT_PENDING_WEAPON] ) {
		item = GS_Cmd_UseItem( &client_gs, &cg.frame.playerState, va( "%i", cg.lastWeapon ), IT_WEAPON );
		if( item ) {
			if( item->type & IT_WEAPON ) {
				CG_Predict_ChangeWeapon( item->tag );
			}

			Cbuf_ExecuteText( EXEC_NOW, va( "cmd use %i", item->tag ) );
			cg.lastWeapon = cg.predictedPlayerState.stats[STAT_PENDING_WEAPON];
		}
	}
}

static void CG_Cmd_Weapon_f() {
	int w = atoi( Cmd_Argv( 1 ) );
	int seen = 0;
	for( int i = WEAP_GUNBLADE; i < WEAP_TOTAL; i++ ) {
		if( cg.predictedPlayerState.inventory[ i ] == 0 )
			continue;
		seen++;

		if( seen == w ) {
			const gsitem_t * item = GS_FindItemByTag( i );
			CG_UseItem( item->name );
		}
	}
}

/*
* CG_Viewpos_f
*/
static void CG_Viewpos_f( void ) {
	CG_Printf( "\"origin\" \"%i %i %i\"\n", (int)cg.view.origin[0], (int)cg.view.origin[1], (int)cg.view.origin[2] );
	CG_Printf( "\"angles\" \"%i %i %i\"\n", (int)cg.view.angles[0], (int)cg.view.angles[1], (int)cg.view.angles[2] );
}

// ======================================================================

/*
* CG_PlayerNamesCompletionExt_f
*
* Helper function
*/
static char **CG_PlayerNamesCompletionExt_f( const char *partial, bool teamOnly ) {
	int i;
	int team = cg_entities[cgs.playerNum + 1].current.team;
	char **matches = NULL;
	int num_matches = 0;

	if( partial ) {
		size_t partial_len = strlen( partial );

		matches = (char **) CG_Malloc( sizeof( char * ) * ( client_gs.maxclients + 1 ) );
		for( i = 0; i < client_gs.maxclients; i++ ) {
			cg_clientInfo_t *info = cgs.clientInfo + i;
			if( !info->name[0] ) {
				continue;
			}
			if( teamOnly && ( cg_entities[i + 1].current.team != team ) ) {
				continue;
			}
			if( !Q_strnicmp( info->name, partial, partial_len ) ) {
				matches[num_matches++] = info->name;
			}
		}
		matches[num_matches] = NULL;
	}

	return matches;
}

/*
* CG_PlayerNamesCompletion_f
*/
static char **CG_PlayerNamesCompletion_f( const char *partial ) {
	return CG_PlayerNamesCompletionExt_f( partial, false );
}

/*
* CG_TeamPlayerNamesCompletion_f
*/
static char **CG_TeamPlayerNamesCompletion_f( const char *partial ) {
	return CG_PlayerNamesCompletionExt_f( partial, true );
}

/*
* CG_SayCmdAdd_f
*/
static void CG_SayCmdAdd_f( void ) {
	Cmd_SetCompletionFunc( "say", &CG_PlayerNamesCompletion_f );
}

/*
* CG_SayTeamCmdAdd_f
*/
static void CG_SayTeamCmdAdd_f( void ) {
	Cmd_SetCompletionFunc( "say_team", &CG_TeamPlayerNamesCompletion_f );
}

/*
* CG_StatsCmdAdd_f
*/
static void CG_StatsCmdAdd_f( void ) {
	Cmd_SetCompletionFunc( "stats", &CG_PlayerNamesCompletion_f );
}

// server commands
static const ServerCommand cg_consvcmds[] = {
	{ "say", CG_SayCmdAdd_f },
	{ "say_team", CG_SayTeamCmdAdd_f },
	{ "stats", CG_StatsCmdAdd_f },

	{ NULL, NULL }
};

// local cgame commands
typedef struct
{
	const char *name;
	void ( *func )( void );
	bool allowdemo;
} cgcmd_t;

static const cgcmd_t cgcmds[] =
{
	{ "+scores", CG_ScoresOn_f, true },
	{ "-scores", CG_ScoresOff_f, true },
	{ "demoget", CG_Cmd_DemoGet_f, false },
	{ "demolist", NULL, false },
	{ "use", CG_Cmd_UseItem_f, false },
	{ "weapnext", CG_Cmd_NextWeapon_f, false },
	{ "weapprev", CG_Cmd_PrevWeapon_f, false },
	{ "weaplast", CG_Cmd_LastWeapon_f, false },
	{ "weapon", CG_Cmd_Weapon_f, false },
	{ "viewpos", CG_Viewpos_f, true },
	{ "players", NULL, false },
	{ "spectators", NULL, false },

	{ NULL, NULL, false }
};

/*
* CG_RegisterCGameCommands
*/
void CG_RegisterCGameCommands( void ) {
	int i;
	char *name;
	const cgcmd_t *cmd;

	if( !cgs.demoPlaying ) {
		const ServerCommand *svcmd;

		// add game side commands
		for( i = 0; i < MAX_GAMECOMMANDS; i++ ) {
			name = cgs.configStrings[CS_GAMECOMMANDS + i];
			if( !name[0] ) {
				continue;
			}

			// check for local command overrides
			for( cmd = cgcmds; cmd->name; cmd++ ) {
				if( !Q_stricmp( cmd->name, name ) ) {
					break;
				}
			}
			if( cmd->name ) {
				continue;
			}

			Cmd_AddCommand( name, NULL );

			// check for server commands we might want to do some special things for..
			for( svcmd = cg_consvcmds; svcmd->name; svcmd++ ) {
				if( !Q_stricmp( svcmd->name, name ) ) {
					if( svcmd->func ) {
						svcmd->func();
					}
					break;
				}
			}
		}
	}

	// add local commands
	for( cmd = cgcmds; cmd->name; cmd++ ) {
		if( cgs.demoPlaying && !cmd->allowdemo ) {
			continue;
		}
		Cmd_AddCommand( cmd->name, cmd->func );
	}
}

/*
* CG_UnregisterCGameCommands
*/
void CG_UnregisterCGameCommands( void ) {
	int i;
	char *name;
	const cgcmd_t *cmd;

	if( !cgs.demoPlaying ) {
		// remove game commands
		for( i = 0; i < MAX_GAMECOMMANDS; i++ ) {
			name = cgs.configStrings[CS_GAMECOMMANDS + i];
			if( !name[0] ) {
				continue;
			}

			// check for local command overrides so we don't try
			// to unregister them twice
			for( cmd = cgcmds; cmd->name; cmd++ ) {
				if( !Q_stricmp( cmd->name, name ) ) {
					break;
				}
			}
			if( cmd->name ) {
				continue;
			}

			Cmd_RemoveCommand( name );
		}
	}

	// remove local commands
	for( cmd = cgcmds; cmd->name; cmd++ ) {
		if( cgs.demoPlaying && !cmd->allowdemo ) {
			continue;
		}
		Cmd_RemoveCommand( cmd->name );
	}
}
