#pragma once

#include <stddef.h>
#include <stdint.h>

// fnv1a
uint32_t Hash32( const void * data, size_t n, uint32_t basis = UINT32_C( 2166136261 ) );
uint64_t Hash64( const void * data, size_t n, uint64_t basis = UINT64_C( 14695981039346656037 ) );

uint32_t Hash32( const char * str );
uint64_t Hash64( const char * str );

// compile time hashing
constexpr uint32_t Hash32_CT( const char * str, size_t n, uint32_t basis = UINT32_C( 2166136261 ) ) {
	return n == 0 ? basis : Hash32_CT( str + 1, n - 1, ( basis ^ str[ 0 ] ) * UINT32_C( 16777619 ) );
}

constexpr uint64_t Hash64_CT( const char * str, size_t n, uint64_t basis = UINT64_C( 14695981039346656037 ) ) {
	return n == 0 ? basis : Hash64_CT( str + 1, n - 1, ( basis ^ str[ 0 ] ) * UINT64_C( 1099511628211 ) );
}

struct StringHash {
	uint64_t hash;

	StringHash() = default;
	explicit StringHash( const char * s );

#ifdef PUBLIC_BUILD
	template< size_t N >
	constexpr StringHash( const char ( &s )[ N ] ) : hash( Hash64_CT( s, N - 1 ) ) { }

	constexpr explicit StringHash( uint64_t h ) : hash( h ) { }
#else
	const char * str;

	template< size_t N >
	constexpr StringHash( const char ( &s )[ N ] ) : hash( Hash64_CT( s, N - 1 ) ), str( s ) { }

	constexpr explicit StringHash( uint64_t h ) : hash( h ), str( NULL ) { }
#endif

};

inline bool operator==( StringHash a, StringHash b ) { return a.hash == b.hash; }
inline bool operator!=( StringHash a, StringHash b ) { return !( a == b ); }

// define this as hash = 0 so memset( 0 ) can be used to reset StringHashes
constexpr StringHash EMPTY_HASH = StringHash( UINT64_C( 0 ) );
