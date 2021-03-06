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
// cl_parse.c  -- parse a message received from the server
//
#include <time.h>

#include "client/client.h"
#include "qcommon/version.h"

static void CL_InitServerDownload( const char *filename, int size, unsigned checksum, bool allow_localhttpdownload,
								   const char *url, bool initial );
void CL_StopServerDownload( void );

/*
* CL_DownloadRequest
*
* Request file download
* return false if couldn't request it for some reason
*/
bool CL_DownloadRequest( const char *filename ) {
	if( cls.download.requestname ) {
		Com_Printf( "Can't download: %s. Download already in progress.\n", filename );
		return false;
	}

	if( !COM_ValidateRelativeFilename( filename ) ) {
		Com_Printf( "Can't download: %s. Invalid filename.\n", filename );
		return false;
	}

	if( FS_FOpenFile( filename, NULL, FS_READ ) != -1 ) {
		Com_Printf( "Can't download: %s. File already exists.\n", filename );
		return false;
	}

	// only allow demo downloads
	if( FileExtension( filename ) != APP_DEMO_EXTENSION_STR ) {
		Com_Printf( "Can't download, got arbitrary file type: %s\n", filename );
		return false;
	}

	if( cls.socket->type == SOCKET_LOOPBACK ) {
		Com_DPrintf( "Can't download: %s. Loopback server.\n", filename );
		return false;
	}

	Com_Printf( "Asking to download: %s\n", filename );

	cls.download.requestname = ( char * ) Mem_ZoneMalloc( sizeof( char ) * ( strlen( filename ) + 1 ) );
	Q_strncpyz( cls.download.requestname, filename, sizeof( char ) * ( strlen( filename ) + 1 ) );
	cls.download.timeout = Sys_Milliseconds() + 5000;
	CL_AddReliableCommand( va( "download \"%s\"", filename ) );

	return true;
}

/*
* CL_CheckOrDownloadFile
*
* Returns true if the file exists or couldn't send download request
*/
bool CL_CheckOrDownloadFile( const char *filename ) {
	if( !cl_downloads->integer ) {
		return true;
	}

	if( !COM_ValidateRelativeFilename( filename ) ) {
		return true;
	}

	if( FileExtension( filename ).n == 0 ) {
		return true;
	}

	if( FS_FOpenFile( filename, NULL, FS_READ ) != -1 ) {
		return true;
	}

	if( !CL_DownloadRequest( filename ) ) {
		return true;
	}

	cls.download.requestnext = true; // call CL_RequestNextDownload when done

	return false;
}

/*
* CL_DownloadComplete
*
* Checks downloaded file's checksum, renames it and adds to the filesystem.
*/
static void CL_DownloadComplete( void ) {
	unsigned checksum = 0;
	int length;

	FS_FCloseFile( cls.download.filenum );
	cls.download.filenum = 0;

	// verify checksum
	length = FS_LoadBaseFile( cls.download.tempname, NULL, NULL, 0 );
	if( length < 0 ) {
		Com_Printf( "Error: Couldn't load downloaded file\n" );
		return;
	}
	checksum = FS_ChecksumBaseFile( cls.download.tempname );

	if( cls.download.checksum != checksum ) {
		Com_Printf( "Downloaded file has wrong checksum. Removing: %u %u %s\n", cls.download.checksum, checksum, cls.download.tempname );
		FS_RemoveBaseFile( cls.download.tempname );
		return;
	}

	if( !FS_MoveBaseFile( cls.download.tempname, cls.download.name ) ) {
		Com_Printf( "Failed to rename the downloaded file\n" );
		return;
	}

	// Maplist hook so we also know when a new map is added
	RefreshMapList();

	cls.download.successCount++;
	cls.download.timeout = 0;
}

/*
* CL_FreeDownloadList
*/
void CL_FreeDownloadList( void ) {
	download_list_t *next;

	while( cls.download.list ) {
		next = cls.download.list->next;
		Mem_ZoneFree( cls.download.list->filename );
		Mem_ZoneFree( cls.download.list );
		cls.download.list = next;
	}
}

/*
* CL_DownloadDone
*/
void CL_DownloadDone( void ) {
	bool requestnext;

	if( cls.download.name ) {
		CL_StopServerDownload();
	}

	Mem_ZoneFree( cls.download.requestname );
	cls.download.requestname = NULL;

	requestnext = cls.download.requestnext;
	cls.download.requestnext = false;
	cls.download.timeout = 0;
	cls.download.timestart = 0;
	cls.download.offset = cls.download.baseoffset = 0;
	cls.download.web = false;
	cls.download.filenum = 0;
	cls.download.cancelled = false;

	// the server has changed map during the download
	if( cls.download.pending_reconnect ) {
		cls.download.pending_reconnect = false;
		CL_FreeDownloadList();
		CL_ServerReconnect_f();
		return;
	}

	if( requestnext && cls.state > CA_DISCONNECTED ) {
		CL_RequestNextDownload();
	}
}

/*
* CL_WebDownloadDoneCb
*/
static void CL_WebDownloadDoneCb( int status, const char *contentType, void *privatep ) {
	download_t download = cls.download;
	bool disconnect = download.disconnect;
	bool cancelled = download.cancelled;
	bool success = ( download.offset == download.size ) && ( status > -1 );

	Com_Printf( "Web download %s: %s (%i)\n", success ? "successful" : "failed", download.tempname, status );

	if( success ) {
		CL_DownloadComplete();
	}
	if( cancelled ) {
		cls.download.requestnext = false;
	}

	// check if user pressed escape to stop the download
	if( disconnect ) {
		CL_Disconnect( NULL ); // this also calls CL_DownloadDone()
		return;
	}

	// try a non-official mirror (the builtin HTTP server or a remote mirror)
	if( !success && !cancelled ) {
		int size = download.size;
		char *filename = ZoneCopyString( download.origname );
		unsigned checksum = download.checksum;
		char *url = ZoneCopyString( download.web_url );
		bool allow_localhttp = download.web_local_http;

		cls.download.cancelled = true; // remove the temp file
		CL_StopServerDownload();
		CL_InitServerDownload( filename, size, checksum, allow_localhttp, url, false );

		Mem_Free( filename );
		Mem_Free( url );
		return;
	}

	CL_DownloadDone();
}

/*
* CL_WebDownloadReadCb
*/
static size_t CL_WebDownloadReadCb( const void *buf, size_t numb, float percentage, int status,
									const char *contentType, void *privatep ) {
	bool stop = cls.download.disconnect || cls.download.cancelled || status < 0 || status >= 300;
	size_t write = 0;

	if( !stop ) {
		write = FS_Write( buf, numb, cls.download.filenum );
	}

	// ignore percentage passed by the downloader as it doesn't account for total file size
	// of resumed downloads
	cls.download.offset += write;
	cls.download.percent = Clamp01( (double)cls.download.offset / (double)cls.download.size );

	Cvar_ForceSet( "cl_download_percent", va( "%.1f", cls.download.percent * 100 ) );

	cls.download.timeout = 0;

	// abort if disconnected, canclled or writing failed
	return stop ? !numb : write;
}

/*
* CL_InitDownload
*
* Hanldles server's initdownload message, starts web or server download if possible
*/
static void CL_InitServerDownload( const char *filename, int size, unsigned checksum, bool allow_localhttpdownload,
								   const char *url, bool initial ) {
	int alloc_size;
	const char *baseurl;
	download_list_t *dl;

	// ignore download commands coming from demo files
	if( cls.demo.playing ) {
		return;
	}

	if( !cls.download.requestname ) {
		Com_Printf( "Got init download message without request\n" );
		return;
	}

	if( cls.download.filenum || cls.download.web ) {
		Com_Printf( "Got init download message while already downloading\n" );
		return;
	}

	if( size == -1 ) {
		// means that download was refused
		Com_Printf( "Server refused download request: %s\n", url ); // if it's refused, url field holds the reason
		CL_DownloadDone();
		return;
	}

	if( size <= 0 ) {
		Com_Printf( "Server gave invalid size, not downloading\n" );
		CL_DownloadDone();
		return;
	}

	if( checksum == 0 ) {
		Com_Printf( "Server didn't provide checksum, not downloading\n" );
		CL_DownloadDone();
		return;
	}

	if( !COM_ValidateRelativeFilename( filename ) ) {
		Com_Printf( "Not downloading, invalid filename: %s\n", filename );
		CL_DownloadDone();
		return;
	}

	if( !strchr( filename, '/' ) ) {
		Com_Printf( "Refusing to download file with no gamedir: %s\n", filename );
		CL_DownloadDone();
		return;
	}

	// check that it is in game or basegame dir
	if( strlen( filename ) < strlen( FS_GameDirectory() ) + 1 ||
		strncmp( filename, FS_GameDirectory(), strlen( FS_GameDirectory() ) ) ||
		filename[strlen( FS_GameDirectory() )] != '/' ) {
		if( strlen( filename ) < strlen( FS_BaseGameDirectory() ) + 1 ||
			strncmp( filename, FS_BaseGameDirectory(), strlen( FS_BaseGameDirectory() ) ) ||
			filename[strlen( FS_BaseGameDirectory() )] != '/' ) {
			Com_Printf( "Can't download, invalid game directory: %s\n", filename );
			CL_DownloadDone();
			return;
		}
	}

	if( strcmp( cls.download.requestname, strchr( filename, '/' ) + 1 ) ) {
		Com_Printf( "Can't download, got different file than requested: %s\n", filename );
		CL_DownloadDone();
		return;
	}

	if( initial ) {
		if( cls.download.requestnext ) {
			dl = cls.download.list;
			while( dl != NULL ) {
				if( !Q_stricmp( dl->filename, filename ) ) {
					Com_Printf( "Skipping, already tried downloading: %s\n", filename );
					CL_DownloadDone();
					return;
				}
				dl = dl->next;
			}
		}
	}

	alloc_size = strlen( "downloads" ) + 1 /* '/' */ + strlen( filename ) + 1;
	cls.download.name = ( char * ) Mem_ZoneMalloc( alloc_size );
	// it's an official pak, otherwise
	// if we're not downloading a pak, this must be a demo so drop it into the gamedir
	snprintf( cls.download.name, alloc_size, "%s", filename );

	alloc_size = strlen( cls.download.name ) + strlen( ".tmp" ) + 1;
	cls.download.tempname = ( char * ) Mem_ZoneMalloc( alloc_size );
	snprintf( cls.download.tempname, alloc_size, "%s.tmp", cls.download.name );

	cls.download.origname = ZoneCopyString( filename );
	cls.download.web = false;
	cls.download.web_url = ZoneCopyString( url );
	cls.download.web_local_http = allow_localhttpdownload;
	cls.download.cancelled = false;
	cls.download.disconnect = false;
	cls.download.size = size;
	cls.download.checksum = checksum;
	cls.download.percent = 0;
	cls.download.timeout = 0;
	cls.download.retries = 0;
	cls.download.timestart = Sys_Milliseconds();
	cls.download.offset = 0;
	cls.download.baseoffset = 0;
	cls.download.pending_reconnect = false;

	Cvar_ForceSet( "cl_download_name", COM_FileBase( filename ) );
	Cvar_ForceSet( "cl_download_percent", "0" );

	if( initial ) {
		if( cls.download.requestnext ) {
			dl = ( download_list_t * ) Mem_ZoneMalloc( sizeof( download_list_t ) );
			dl->filename = ZoneCopyString( filename );
			dl->next = cls.download.list;
			cls.download.list = dl;
		}
	}

	baseurl = cls.httpbaseurl;

	if( cl_downloads_from_web->integer && allow_localhttpdownload && url && url[0] != 0 ) {
		cls.download.web = true;
		Com_Printf( "Web download: %s from %s/%s\n", cls.download.tempname, baseurl, url );
	} else if( cl_downloads_from_web->integer && url && url[0] != 0 ) {
		cls.download.web = true;
		Com_Printf( "Web download: %s from %s\n", cls.download.tempname, url );
	} else {
		Com_Printf( "Server download: %s\n", cls.download.tempname );
	}

	cls.download.baseoffset = cls.download.offset = FS_FOpenBaseFile( cls.download.tempname, &cls.download.filenum, FS_APPEND );
	if( !cls.download.filenum ) {
		Com_Printf( "Can't download, couldn't open %s for writing\n", cls.download.tempname );
		CL_DownloadDone();
		return;
	}

	if( cls.download.web ) {
		char *referer, *fullurl;
		const char *headers[] = { NULL, NULL, NULL, NULL, NULL, NULL, NULL };

		if( cls.download.offset == cls.download.size ) {
			// special case for completed downloads to avoid passing empty HTTP range
			CL_WebDownloadDoneCb( 200, "", NULL );
			return;
		}

		alloc_size = strlen( APP_URI_SCHEME ) + strlen( NET_AddressToString( &cls.serveraddress ) ) + 1;
		referer = ( char * ) alloca( alloc_size );
		snprintf( referer, alloc_size, APP_URI_SCHEME "%s", NET_AddressToString( &cls.serveraddress ) );
		Q_strlwr( referer );

		if( allow_localhttpdownload ) {
			alloc_size = strlen( baseurl ) + 1 + strlen( url ) + 1;
			fullurl = ( char * ) alloca( alloc_size );
			snprintf( fullurl, alloc_size, "%s/%s", baseurl, url );
		} else {
			size_t url_len = strlen( url );
			alloc_size = url_len + 1 + strlen( filename ) * 3 + 1;
			fullurl = ( char * ) alloca( alloc_size );
			snprintf( fullurl, alloc_size, "%s/", url );
			Q_urlencode_unsafechars( filename, fullurl + url_len + 1, alloc_size - url_len - 1 );
		}

		headers[0] = "Referer";
		headers[1] = referer;

		CL_AddSessionHttpRequestHeaders( fullurl, &headers[2] );

		CL_AsyncStreamRequest( fullurl, headers, cl_downloads_from_web_timeout->integer / 100, cls.download.offset,
							   CL_WebDownloadReadCb, CL_WebDownloadDoneCb, NULL, NULL, false );

		return;
	}

	cls.download.timeout = Sys_Milliseconds() + 3000;
	cls.download.retries = 0;

	CL_AddReliableCommand( va( "nextdl \"%s\" %li", cls.download.name, cls.download.offset ) );
}

/*
* CL_InitDownload_f
*/
static void CL_InitDownload_f( void ) {
	const char *filename;
	const char *url;
	int size;
	unsigned checksum;
	bool allow_localhttpdownload;

	// ignore download commands coming from demo files
	if( cls.demo.playing ) {
		return;
	}

	// read the data
	filename = Cmd_Argv( 1 );
	size = atoi( Cmd_Argv( 2 ) );
	checksum = strtoul( Cmd_Argv( 3 ), NULL, 10 );
	allow_localhttpdownload = ( atoi( Cmd_Argv( 4 ) ) != 0 ) && cls.httpbaseurl != NULL;
	url = Cmd_Argv( 5 );

	CL_InitServerDownload( filename, size, checksum, allow_localhttpdownload, url, true );
}

/*
* CL_StopServerDownload
*/
void CL_StopServerDownload( void ) {
	if( cls.download.filenum > 0 ) {
		FS_FCloseFile( cls.download.filenum );
		cls.download.filenum = 0;
	}

	if( cls.download.cancelled ) {
		FS_RemoveBaseFile( cls.download.tempname );
	}

	Mem_ZoneFree( cls.download.name );
	cls.download.name = NULL;

	Mem_ZoneFree( cls.download.tempname );
	cls.download.tempname = NULL;

	Mem_ZoneFree( cls.download.origname );
	cls.download.origname = NULL;

	Mem_ZoneFree( cls.download.web_url );
	cls.download.web_url = NULL;

	cls.download.offset = 0;
	cls.download.size = 0;
	cls.download.percent = 0;
	cls.download.timeout = 0;
	cls.download.retries = 0;
	cls.download.web = false;

	Cvar_ForceSet( "cl_download_name", "" );
	Cvar_ForceSet( "cl_download_percent", "0" );
}

/*
* CL_RetryDownload
* Resends download request
* Also aborts download if we have retried too many times
*/
static void CL_RetryDownload( void ) {
	if( ++cls.download.retries > 5 ) {
		Com_Printf( "Download timed out: %s\n", cls.download.name );

		// let the server know we're done
		CL_AddReliableCommand( va( "nextdl \"%s\" %i", cls.download.name, -2 ) );
		CL_DownloadDone();
	} else {
		cls.download.timeout = Sys_Milliseconds() + 3000;
		CL_AddReliableCommand( va( "nextdl \"%s\" %li", cls.download.name, cls.download.offset ) );
	}
}

/*
* CL_CheckDownloadTimeout
* Retry downloading if too much time has passed since last download packet was received
*/
void CL_CheckDownloadTimeout( void ) {
	if( !cls.download.timeout || cls.download.timeout > Sys_Milliseconds() ) {
		return;
	}

	if( cls.download.filenum ) {
		CL_RetryDownload();
	} else {
		Com_Printf( "Download request timed out.\n" );
		CL_DownloadDone();
	}
}

/*
* CL_DownloadStatus_f
*/
void CL_DownloadStatus_f( void ) {
	if( !cls.download.requestname ) {
		Com_Printf( "No download active\n" );
		return;
	}

	if( !cls.download.name ) {
		Com_Printf( "%s: Requesting\n", COM_FileBase( cls.download.requestname ) );
		return;
	}

	Com_Printf( "%s: %s download %3.2f%c done\n", COM_FileBase( cls.download.name ),
				( cls.download.web ? "Web" : "Server" ), cls.download.percent * 100.0f, '%' );
}

/*
* CL_DownloadCancel_f
*/
void CL_DownloadCancel_f( void ) {
	if( !cls.download.requestname ) {
		Com_Printf( "No download active\n" );
		return;
	}

	if( !cls.download.name ) {
		CL_DownloadDone();
		Com_Printf( "Canceled download request\n" );
		return;
	}

	Com_Printf( "Canceled download of %s\n", cls.download.name );

	cls.download.cancelled = true;

	if( !cls.download.web ) {
		CL_AddReliableCommand( va( "nextdl \"%s\" %i", cls.download.name, -2 ) ); // let the server know we're done
		CL_DownloadDone();
	}
}

/*
* CL_ParseDownload
* Handles download message from the server.
* Writes data to the file and requests next download block.
*/
static void CL_ParseDownload( msg_t *msg ) {
	size_t size, offset;
	char *svFilename;

	// read the data
	svFilename = MSG_ReadString( msg );
	offset = MSG_ReadInt32( msg );
	size = MSG_ReadInt32( msg );

	if( cls.demo.playing ) {
		// ignore download commands coming from demo files
		return;
	}

	if( msg->readcount + size > msg->cursize ) {
		Com_Printf( "Error: Download message didn't have as much data as it promised\n" );
		CL_RetryDownload();
		return;
	}

	if( !cls.download.filenum ) {
		Com_Printf( "Error: Download message while not dowloading\n" );
		msg->readcount += size;
		return;
	}

	if( Q_stricmp( cls.download.name, svFilename ) ) {
		Com_Printf( "Error: Download message for wrong file\n" );
		msg->readcount += size;
		return;
	}

	if( offset + size > cls.download.size ) {
		Com_Printf( "Error: Invalid download message\n" );
		msg->readcount += size;
		CL_RetryDownload();
		return;
	}

	if( cls.download.offset != offset ) {
		Com_Printf( "Error: Download message for wrong position\n" );
		msg->readcount += size;
		CL_RetryDownload();
		return;
	}

	FS_Write( msg->data + msg->readcount, size, cls.download.filenum );
	msg->readcount += size;
	cls.download.offset += size;
	cls.download.percent = Clamp01( (double)cls.download.offset / (double)cls.download.size );

	Cvar_ForceSet( "cl_download_percent", va( "%.1f", cls.download.percent * 100 ) );

	if( cls.download.offset < cls.download.size ) {
		cls.download.timeout = Sys_Milliseconds() + 3000;
		cls.download.retries = 0;

		CL_AddReliableCommand( va( "nextdl \"%s\" %li", cls.download.name, cls.download.offset ) );
	} else {
		Com_Printf( "Download complete: %s\n", cls.download.name );

		CL_DownloadComplete();

		// let the server know we're done
		CL_AddReliableCommand( va( "nextdl \"%s\" %i", cls.download.name, -1 ) );

		CL_DownloadDone();
	}
}

/*
=====================================================================

SERVER CONNECTING MESSAGES

=====================================================================
*/

/*
* CL_ParseServerData
*/
static void CL_ParseServerData( msg_t *msg ) {
	const char *str;
	int i, sv_bitflags;
	int http_portnum;

	Com_DPrintf( "Serverdata packet received.\n" );

	// wipe the client_state_t struct

	CL_ClearState();
	CL_SetClientState( CA_CONNECTED );

	// parse protocol version number
	i = MSG_ReadInt32( msg );

	if( i != APP_PROTOCOL_VERSION ) {
		Com_GGError( ERR_DROP, "Server returned version {}, not {}", i, APP_PROTOCOL_VERSION );
	}

	cl.servercount = MSG_ReadInt32( msg );
	cl.snapFrameTime = (unsigned int)MSG_ReadInt16( msg );

	// set extrapolation time to half snapshot time
	Cvar_ForceSet( "cl_extrapolationTime", va( "%i", (unsigned int)( cl.snapFrameTime * 0.5 ) ) );
	cl_extrapolationTime->modified = false;

	// base game directory
	str = MSG_ReadString( msg );
	if( !str || !str[0] ) {
		Com_Error( ERR_DROP, "Server sent an empty base game directory" );
	}
	if( !COM_ValidateRelativeFilename( str ) || strchr( str, '/' ) ) {
		Com_Error( ERR_DROP, "Server sent an invalid base game directory: %s", str );
	}
	if( strcmp( FS_BaseGameDirectory(), str ) ) {
		Com_Error( ERR_DROP, "Server has different base game directory (%s) than the client (%s)", str,
				   FS_BaseGameDirectory() );
	}

	// parse player entity number
	cl.playernum = MSG_ReadInt16( msg );

	sv_bitflags = MSG_ReadUint8( msg );

	if( cls.demo.playing ) {
		cls.reliable = ( sv_bitflags & SV_BITFLAGS_RELIABLE );
	} else {
		if( cls.reliable != ( ( sv_bitflags & SV_BITFLAGS_RELIABLE ) != 0 ) ) {
			Com_Error( ERR_DROP, "Server and client disagree about connection reliability" );
		}
	}

	// builting HTTP server port
	if( cls.httpbaseurl ) {
		Mem_Free( cls.httpbaseurl );
		cls.httpbaseurl = NULL;
	}

	if( ( sv_bitflags & SV_BITFLAGS_HTTP ) != 0 ) {
		if( ( sv_bitflags & SV_BITFLAGS_HTTP_BASEURL ) != 0 ) {
			// read base upstream url
			cls.httpbaseurl = ZoneCopyString( MSG_ReadString( msg ) );
		} else {
			http_portnum = MSG_ReadInt16( msg ) & 0xffff;
			cls.httpaddress = cls.serveraddress;
			if( cls.httpaddress.type == NA_IP6 ) {
				cls.httpaddress.address.ipv6.port = BigShort( http_portnum );
			} else {
				cls.httpaddress.address.ipv4.port = BigShort( http_portnum );
			}
			if( http_portnum ) {
				if( cls.httpaddress.type == NA_LOOPBACK ) {
					cls.httpbaseurl = ZoneCopyString( va( "http://localhost:%hu/", http_portnum ) );
				} else {
					cls.httpbaseurl = ZoneCopyString( va( "http://%s/", NET_AddressToString( &cls.httpaddress ) ) );
				}
			}
		}
	}

	// get the configstrings request
	CL_AddReliableCommand( va( "configstrings %i 0", cl.servercount ) );
}

/*
* CL_ParseBaseline
*/
static void CL_ParseBaseline( msg_t *msg ) {
	SNAP_ParseBaseline( msg, cl_baselines );
}

/*
* CL_ParseFrame
*/
static void CL_ParseFrame( msg_t *msg ) {
	snapshot_t *snap, *oldSnap;
	int delta;

	oldSnap = ( cl.receivedSnapNum > 0 ) ? &cl.snapShots[cl.receivedSnapNum & UPDATE_MASK] : NULL;

	snap = SNAP_ParseFrame( msg, oldSnap, cl.snapShots, cl_baselines, cl_shownet->integer );
	if( snap->valid ) {
		cl.receivedSnapNum = snap->serverFrame;

		if( cls.demo.recording ) {
			if( cls.demo.waiting && !snap->delta ) {
				cls.demo.waiting = false; // we can start recording now
				cls.demo.basetime = snap->serverTime;
				cls.demo.localtime = time( NULL );

				// clear demo meta data, we'll write some keys later
				cls.demo.meta_data_realsize = SNAP_ClearDemoMeta( cls.demo.meta_data, sizeof( cls.demo.meta_data ) );

				// write out messages to hold the startup information
				SNAP_BeginDemoRecording( cls.demo.file, 0x10000 + cl.servercount, cl.snapFrameTime,
										 cls.reliable ? SV_BITFLAGS_RELIABLE : 0,
										 cl.configstrings[0], cl_baselines );

				// the rest of the demo file will be individual frames
			}

			if( !cls.demo.waiting ) {
				cls.demo.duration = snap->serverTime - cls.demo.basetime;
			}
			cls.demo.time = cls.demo.duration;
		}

		if( cl_debug_timeDelta->integer ) {
			if( oldSnap != NULL && ( oldSnap->serverFrame + 1 != snap->serverFrame ) ) {
				Com_Printf( S_COLOR_RED "***** SnapShot lost\n" );
			}
		}

		// the first snap, fill all the timeDeltas with the same value
		// don't let delta add big jumps to the smoothing ( a stable connection produces jumps inside +-3 range)
		delta = ( snap->serverTime - cl.snapFrameTime ) - cls.gametime;
		if( cl.currentSnapNum <= 0 || delta < cl.newServerTimeDelta - 175 || delta > cl.newServerTimeDelta + 175 ) {
			CL_RestartTimeDeltas( delta );
		} else {
			if( cl_debug_timeDelta->integer ) {
				if( delta < cl.newServerTimeDelta - (int)cl.snapFrameTime ) {
					Com_Printf( S_COLOR_CYAN "***** timeDelta low clamp\n" );
				} else if( delta > cl.newServerTimeDelta + (int)cl.snapFrameTime ) {
					Com_Printf( S_COLOR_CYAN "***** timeDelta high clamp\n" );
				}
			}

			delta = Clamp( cl.newServerTimeDelta - (int)cl.snapFrameTime, delta, cl.newServerTimeDelta + (int)cl.snapFrameTime );

			cl.serverTimeDeltas[cl.receivedSnapNum & MASK_TIMEDELTAS_BACKUP] = delta;
		}
	}
}

//========= StringCommands================

/*
* CL_UpdateConfigString
*/
static void CL_UpdateConfigString( int idx, const char *s ) {
	if( !s ) {
		return;
	}

	if( cl_debug_serverCmd->integer && ( cls.state >= CA_ACTIVE || cls.demo.playing ) ) {
		Com_Printf( "CL_ParseConfigstringCommand(%i): \"%s\"\n", idx, s );
	}

	if( idx < 0 || idx >= MAX_CONFIGSTRINGS ) {
		Com_Error( ERR_DROP, "configstring > MAX_CONFIGSTRINGS" );
	}

	// wsw : jal : warn if configstring overflow
	if( strlen( s ) >= MAX_CONFIGSTRING_CHARS ) {
		Com_Printf( "%sWARNING:%s Configstring %i overflowed\n", S_COLOR_YELLOW, S_COLOR_WHITE, idx );
		Com_Printf( "%s%s\n", S_COLOR_WHITE, s );
	}

	if( !COM_ValidateConfigstring( s ) ) {
		Com_Printf( "%sWARNING:%s Invalid Configstring (%i): %s\n", S_COLOR_YELLOW, S_COLOR_WHITE, idx, s );
		return;
	}

	Q_strncpyz( cl.configstrings[idx], s, sizeof( cl.configstrings[idx] ) );

	// allow cgame to update it too
	CL_GameModule_ConfigString( idx, s );
}

/*
* CL_ParseConfigstringCommand
*/
static void CL_ParseConfigstringCommand( void ) {
	int i, argc, idx;
	const char *s;

	if( Cmd_Argc() < 3 ) {
		return;
	}

	// ch : configstrings may come batched now, so lets loop through them
	argc = Cmd_Argc();
	for( i = 1; i < argc - 1; i += 2 ) {
		idx = atoi( Cmd_Argv( i ) );
		s = Cmd_Argv( i + 1 );

		CL_UpdateConfigString( idx, s );
	}
}

typedef struct {
	const char *name;
	void ( *func )( void );
} svcmd_t;

svcmd_t svcmds[] =
{
	{ "forcereconnect", CL_Reconnect_f },
	{ "reconnect", CL_ServerReconnect_f },
	{ "changing", CL_Changing_f },
	{ "precache", CL_Precache_f },
	{ "cmd", CL_ForwardToServer_f },
	{ "cs", CL_ParseConfigstringCommand },
	{ "disconnect", CL_ServerDisconnect_f },
	{ "initdownload", CL_InitDownload_f },

	{ NULL, NULL }
};


/*
* CL_ParseServerCommand
*/
static void CL_ParseServerCommand( msg_t *msg ) {
	const char *s;
	char *text;
	svcmd_t *cmd;

	text = MSG_ReadString( msg );

	Cmd_TokenizeString( text );
	s = Cmd_Argv( 0 );

	if( cl_debug_serverCmd->integer && ( cls.state < CA_ACTIVE || cls.demo.playing ) ) {
		Com_Printf( "CL_ParseServerCommand: \"%s\"\n", text );
	}

	// filter out these server commands to be called from the client
	for( cmd = svcmds; cmd->name; cmd++ ) {
		if( !strcmp( s, cmd->name ) ) {
			cmd->func();
			return;
		}
	}

	Com_Printf( "Unknown server command: %s\n", s );
}

/*
=====================================================================

ACTION MESSAGES

=====================================================================
*/

/*
* CL_ParseServerMessage
*/
void CL_ParseServerMessage( msg_t *msg ) {
	if( cl_shownet->integer == 1 ) {
		Com_Printf( "%" PRIuPTR " ", (uintptr_t)msg->cursize );
	} else if( cl_shownet->integer >= 2 ) {
		Com_Printf( "------------------\n" );
	}

	// parse the message
	while( msg->readcount < msg->cursize ) {
		int cmd;
		size_t meta_data_maxsize;

		cmd = MSG_ReadUint8( msg );
		if( cl_debug_serverCmd->integer & 4 ) {
			Com_Printf( "%3" PRIi64 ":CMD %i %s\n", (int64_t)(msg->readcount - 1), cmd, !svc_strings[cmd] ? "bad" : svc_strings[cmd] );
		}

		if( cl_shownet->integer >= 2 ) {
			if( !svc_strings[cmd] ) {
				Com_Printf( "%3" PRIi64 ":BAD CMD %i\n", (int64_t)(msg->readcount - 1), cmd );
			} else {
				SHOWNET( msg, svc_strings[cmd] );
			}
		}

		// other commands
		switch( cmd ) {
			default:
				Com_Error( ERR_DROP, "CL_ParseServerMessage: Illegible server message" );
				break;

			case svc_servercmd:
				if( !cls.reliable ) {
					int cmdNum = MSG_ReadInt32( msg );
					if( cmdNum < 0 ) {
						Com_Error( ERR_DROP, "CL_ParseServerMessage: Invalid cmdNum value received: %i\n",
								   cmdNum );
						return;
					}
					if( cmdNum <= cls.lastExecutedServerCommand ) {
						MSG_ReadString( msg ); // read but ignore
						break;
					}
					cls.lastExecutedServerCommand = cmdNum;
				}
			// fall through
			case svc_servercs: // configstrings from demo files. they don't have acknowledge
				CL_ParseServerCommand( msg );
				break;

			case svc_serverdata:
				if( cls.state == CA_HANDSHAKE ) {
					Cbuf_Execute(); // make sure any stuffed commands are done
					CL_ParseServerData( msg );
				} else {
					return; // ignore rest of the packet (serverdata is always sent alone)
				}
				break;

			case svc_spawnbaseline:
				CL_ParseBaseline( msg );
				break;

			case svc_download:
				CL_ParseDownload( msg );
				break;

			case svc_clcack:
				if( cls.reliable ) {
					Com_Error( ERR_DROP, "CL_ParseServerMessage: clack message for reliable client\n" );
					return;
				}
				cls.reliableAcknowledge = MSG_ReadUintBase128( msg );
				cls.ucmdAcknowledged = MSG_ReadUintBase128( msg );
				if( cl_debug_serverCmd->integer & 4 ) {
					Com_Printf( "svc_clcack:reliable cmd ack:%" PRIi64 " ucmdack:%" PRIi64 "\n", cls.reliableAcknowledge, cls.ucmdAcknowledged );
				}
				break;

			case svc_frame:
				CL_ParseFrame( msg );
				break;

			case svc_demoinfo:
				assert( cls.demo.playing );

				MSG_ReadInt32( msg );
				MSG_ReadInt32( msg );
				cls.demo.meta_data_realsize = (size_t)MSG_ReadInt32( msg );
				meta_data_maxsize = (size_t)MSG_ReadInt32( msg );

				// sanity check
				if( cls.demo.meta_data_realsize > meta_data_maxsize ) {
					cls.demo.meta_data_realsize = meta_data_maxsize;
				}
				if( cls.demo.meta_data_realsize > sizeof( cls.demo.meta_data ) ) {
					cls.demo.meta_data_realsize = sizeof( cls.demo.meta_data );
				}

				MSG_ReadData( msg, cls.demo.meta_data, cls.demo.meta_data_realsize );
				MSG_SkipData( msg, meta_data_maxsize - cls.demo.meta_data_realsize );
				break;

			case svc_playerinfo:
			case svc_packetentities:
			case svc_match:
				Com_Error( ERR_DROP, "Out of place frame data" );
				break;
		}
	}

	if( msg->readcount > msg->cursize ) {
		Com_Error( ERR_DROP, "CL_ParseServerMessage: Bad server message" );
		return;
	}

	if( cl_debug_serverCmd->integer & 4 ) {
		Com_Printf( "%3li:CMD %i %s\n", msg->readcount, -1, "EOF" );
	}
	SHOWNET( msg, "END OF MESSAGE" );

	CL_AddNetgraph();

	//
	// if recording demos, copy the message out
	//
	//
	// we don't know if it is ok to save a demo message until
	// after we have parsed the frame
	//
	if( cls.demo.recording && !cls.demo.waiting ) {
		CL_WriteDemoMessage( msg );
	}
}
