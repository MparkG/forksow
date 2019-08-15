#pragma once

#include "qcommon/qcommon.h"

typedef void ( *fdrawchar_t )( int x, int y, int w, int h, float s1, float t1, float s2, float t2, const vec4_t color, StringHash shader );

bool FTLIB_Init();
void FTLIB_Shutdown();

void FTLIB_LoadLibrary( bool verbose );
struct qfontface_s *FTLIB_RegisterFont( const char *family, const char *fallback, int style, unsigned int size );
void FTLIB_TouchFont( struct qfontface_s *qfont );
void FTLIB_TouchAllFonts( void );
void FTLIB_PrecacheFonts( bool verbose );
void FTLIB_FreeFonts( bool verbose );

// drawing functions

size_t FTLIB_FontSize( struct qfontface_s *font );
size_t FTLIB_FontHeight( struct qfontface_s *font );
size_t FTLIB_StringWidth( const char *str, struct qfontface_s *font, size_t maxlen );
size_t FTLIB_StrlenForWidth( const char *str, struct qfontface_s *font, size_t maxwidth );
int FTLIB_FontUnderline( struct qfontface_s *font, int *thickness );
void FTLIB_DrawRawChar( int x, int y, wchar_t num, struct qfontface_s *font, const vec4_t color );
void FTLIB_DrawClampChar( int x, int y, wchar_t num, int xmin, int ymin, int xmax, int ymax, struct qfontface_s *font, const vec4_t color );
void FTLIB_DrawClampString( int x, int y, const char *str, int xmin, int ymin, int xmax, int ymax, struct qfontface_s *font, const vec4_t color );
size_t FTLIB_DrawRawString( int x, int y, const char *str, size_t maxwidth, int *width, struct qfontface_s *font, const vec4_t color );
int FTLIB_DrawMultilineString( int x, int y, const char *str, int halign, int maxwidth, int maxlines, struct qfontface_s *font, const vec4_t color );
fdrawchar_t FTLIB_SetDrawCharIntercept( fdrawchar_t intercept );
