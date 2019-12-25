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

#pragma once

#define MAX_ENT_CLUSTERS    16

typedef struct edict_s edict_t;
typedef struct gclient_s gclient_t;
typedef struct gclient_quit_s gclient_quit_t;

typedef struct {
	int ping;
	int health;
	int frags;
} client_shared_t;

typedef struct {
	gclient_t *client;
	bool inuse;

	int num_clusters;           // if -1, use headnode instead
	int clusternums[MAX_ENT_CLUSTERS];
	int leafnums[MAX_ENT_CLUSTERS];
	int headnode;               // unused if num_clusters != -1
	int areanum, areanum2;

	//================================

	unsigned int svflags;                // SVF_NOCLIENT, SVF_MONSTER, etc
	vec3_t mins, maxs;
	vec3_t absmin, absmax, size;
	solid_t solid;
	int clipmask;
	edict_t *owner;
} entity_shared_t;

//===============================================================

//
// functions provided by the main engine
//
typedef struct {
	// special messages
	void ( *Print )( const char *msg );

	// aborts server with a game error
#ifndef _MSC_VER
	void ( *Error )( const char *msg ) __attribute__( ( noreturn ) );
#else
	void ( *Error )( const char *msg );
#endif

	// server commands sent to clients
	void ( *GameCmd )( edict_t *ent, const char *cmd );

	// config strings hold all the index strings,
	// and misc data like audio track and gridsize.
	// All of the current configstrings are sent to clients when
	// they connect, and changes are sent to all connected clients.
	void ( *ConfigString )( int num, const char *string );
	const char *( *GetConfigString )( int num );

	// the *index functions create configstrings and some internal server state
	int ( *ModelIndex )( const char *name );
	int ( *SoundIndex )( const char *name );
	int ( *ImageIndex )( const char *name );

	int64_t ( *Milliseconds )( void );

	bool ( *inPVS )( const vec3_t p1, const vec3_t p2 );

	int ( *CM_NumInlineModels )( void );
	struct cmodel_s *( *CM_InlineModel )( int num );
	int ( *CM_TransformedPointContents )( const vec3_t p, struct cmodel_s *cmodel, const vec3_t origin, const vec3_t angles );
	void ( *CM_TransformedBoxTrace )( trace_t *tr, const vec3_t start, const vec3_t end, const vec3_t mins, const vec3_t maxs, struct cmodel_s *cmodel, int brushmask, const vec3_t origin, const vec3_t angles );
	void ( *CM_InlineModelBounds )( const struct cmodel_s *cmodel, vec3_t mins, vec3_t maxs );
	struct cmodel_s *( *CM_ModelForBBox )( vec3_t mins, vec3_t maxs );
	struct cmodel_s *( *CM_OctagonModelForBBox )( vec3_t mins, vec3_t maxs );
	void ( *CM_SetAreaPortalState )( int area, int otherarea, bool open );
	bool ( *CM_AreasConnected )( int area1, int area2 );
	int ( *CM_BoxLeafnums )( vec3_t mins, vec3_t maxs, int *list, int listsize, int *topnode );
	int ( *CM_LeafCluster )( int leafnum );
	int ( *CM_LeafArea )( int leafnum );
	int ( *CM_LeafsInPVS )( int leafnum1, int leafnum2 );

	bool ( *ML_Update )( void );
	size_t ( *ML_GetMapByNum )( int num, char *out, size_t size );
	bool ( *ML_FilenameExists )( const char *filename );

	// add commands to the server console as if they were typed in for map changing, etc
	void ( *Cbuf_ExecuteText )( int exec_when, const char *text );
	void ( *Cbuf_Execute )( void );

	// a fake client connection, ClientConnect is called afterwords
	// with fakeClient set to true
	int ( *FakeClientConnect )( const char *fakeUserinfo, const char *fakeSocketType, const char *fakeIP );
	void ( *DropClient )( struct edict_s *ent, int type, const char *message );
	int ( *GetClientState )( int numClient );
	void ( *ExecuteClientThinks )( int clientNum );

	// The edict array is allocated in the game dll so it
	// can vary in size from one game to another.
	void ( *LocateEntities )( struct edict_s *edicts, size_t edict_size, int num_edicts, int max_edicts );

	// angelscript api
	struct angelwrap_api_s *( *asGetAngelExport )( void );

	bool is_dedicated_server;
} game_import_t;

//
// functions exported by the game subsystem
//
typedef struct {
	// the init function will only be called when a game starts,
	// not each time a level is loaded.  Persistant data for clients
	// and the server can be allocated in init
	void ( *Init )( unsigned int framemsec );
	void ( *Shutdown )( void );

	// each new level entered will cause a call to SpawnEntities
	void ( *InitLevel )( char *mapname, char *entities, int entstrlen, int64_t levelTime );

	bool ( *ClientConnect )( edict_t *ent, char *userinfo, bool fakeClient );
	void ( *ClientBegin )( edict_t *ent );
	void ( *ClientUserinfoChanged )( edict_t *ent, char *userinfo );
	bool ( *ClientMultiviewChanged )( edict_t *ent, bool multiview );
	void ( *ClientDisconnect )( edict_t *ent, const char *reason );
	void ( *ClientCommand )( edict_t *ent );
	void ( *ClientThink )( edict_t *ent, usercmd_t *cmd, int timeDelta );

	void ( *RunFrame )( unsigned int msec );
	void ( *SnapFrame )( void );
	void ( *ClearSnap )( void );

	SyncGameState *( *GetGameState )( void );
} game_export_t;

game_export_t * GetGameAPI( game_import_t * import );
