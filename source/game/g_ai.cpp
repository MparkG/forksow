#include "g_local.h"
#include "../gameshared/gs_public.h"

static struct {
	const char * name;
	const char * model;
} botCharacters[] = {
	{ "vic", "oldvic" },
	{ "crizis", "oldvic" },
	{ "jal", "oldvic" },

	{ "MWAGA", "bigvic" },

	{ "Triangel", "monada" },

	{ "Perrina", "silverclaw" },

	{ "__mute__", "padpork" },
	{ "Slice*>", "padpork" },
};

static void CreateUserInfo( char * buffer, size_t bufferSize ) {
	// Try to avoid bad distribution, otherwise some bots are selected too often. Weights are prime numbers
	int characterIndex = rand() % ARRAY_COUNT( botCharacters );

	memset( buffer, 0, bufferSize );

	Info_SetValueForKey( buffer, "name", botCharacters[characterIndex].name );
	Info_SetValueForKey( buffer, "hand", va( "%i", (int)( random() * 2.5 ) ) );
}

static edict_t * ConnectFakeClient() {
	char userInfo[MAX_INFO_STRING];
	static char fakeSocketType[] = "loopback";
	static char fakeIP[] = "127.0.0.1";
	CreateUserInfo( userInfo, sizeof( userInfo ) );
	int entNum = trap_FakeClientConnect( userInfo, fakeSocketType, fakeIP );
	if( entNum < 1 ) {
		Com_Printf( "AI: Can't spawn the fake client\n" );
		return NULL;
	}
	return game.edicts + entNum;
}

void AI_SpawnBot() {
	if( level.spawnedTimeStamp + 5000 > svs.realtime || !level.canSpawnEntities ) {
		return;
	}

	edict_t * ent = ConnectFakeClient();
	if( ent == NULL )
		return;

	ent->think = NULL;
	ent->nextThink = level.time + 500 + (unsigned)( random() * 2000 );
	ent->classname = "bot";
	ent->die = player_die;

	AI_Respawn( ent );

	game.numBots++;
}

void AI_Respawn( edict_t * ent ) {
	VectorClear( ent->r.client->ps.pmove.delta_angles );
	ent->r.client->level.last_activity = level.time;
}

static void AI_SpecThink( edict_t * self ) {
	self->nextThink = level.time + 100;

	if( !level.canSpawnEntities )
		return;

	if( self->r.client->team == TEAM_SPECTATOR ) {
		// try to join a team
		if( !self->r.client->queueTimeStamp ) {
			G_Teams_JoinAnyTeam( self, false );
		}

		if( self->r.client->team == TEAM_SPECTATOR ) { // couldn't join, delay the next think
			self->nextThink = level.time + 100;
		} else {
			self->nextThink = level.time + 1;
		}
		return;
	}

	usercmd_t ucmd;
	memset( &ucmd, 0, sizeof( usercmd_t ) );

	// set approximate ping and show values
	ucmd.serverTimeStamp = svs.gametime;
	ucmd.msec = (uint8_t)game.frametime;

	ClientThink( self, &ucmd, 0 );
}

static void AI_GameThink( edict_t * self ) {
	if( GS_MatchState( &server_gs ) <= MATCH_STATE_WARMUP ) {
		G_Match_Ready( self );
	}

	usercmd_t ucmd;
	memset( &ucmd, 0, sizeof( usercmd_t ) );

	// set up for pmove
	for( int i = 0; i < 3; i++ )
		ucmd.angles[i] = (short)ANGLE2SHORT( self->s.angles[i] ) - self->r.client->ps.pmove.delta_angles[i];

	VectorSet( self->r.client->ps.pmove.delta_angles, 0, 0, 0 );

	// set approximate ping and show values
	ucmd.msec = (uint8_t)game.frametime;
	ucmd.serverTimeStamp = svs.gametime;

	ClientThink( self, &ucmd, 0 );
	self->nextThink = level.time + 1;
}

void AI_Think( edict_t * self ) {
	if( G_ISGHOSTING( self ) ) {
		AI_SpecThink( self );
	}
	else {
		AI_GameThink( self );
	}
}
