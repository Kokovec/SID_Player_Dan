/*
       ______/  _____/  _____/     /   _/    /             /
     _/           /     /     /   /  _/     /   ______/   /  _/             ____/     /   ______/   ____/
      ___/       /     /     /   ___/      /   /         __/                    _/   /   /         /     /
         _/    _/    _/    _/   /  _/     /  _/         /  _/             _____/    /  _/        _/    _/
  ______/   _____/  ______/   _/    _/  _/    _____/  _/    _/          _/        _/    _____/    ____/

  SKpicopatch.c

  SIDKick pico - SID-replacement with dual-SID/SID+fm emulation using a RPi pico, reSID and fmopl
  Copyright (c) 2024 Carsten Dachsbacher <frenetic@dachsbacher.de>

*/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>

unsigned char skBinUF2[ 16 * 1024 * 1024 ];
unsigned char skBin[ 16 * 1024 * 1024 ];

unsigned char patDir[ 24 ] = { 'E', 'M', 'P', 'T', 'Y', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
unsigned char patPRG[ 20 ] = { 'S', 'I', 'D', 'K', 'I', 'C', 'K', ' ', 'R', 'E', 'P', 'O', 0, 0, 0, 0, 0, 0, 0, 0 };
unsigned char patBT [  8 ] = { 3, 12, 1, 2, 3, 4, 5, 6 };

bool compare( unsigned char *a, unsigned char *b, int l )
{
	for ( int i = 0; i < l; i++ )
		if ( *( a++ ) != *( b++ ) )
			return false;
	return true;
}

typedef struct {
	unsigned char header[ 32 ];
	unsigned char data[ 476 ];
	unsigned char magic[ 4 ];
} UF2_Block;

void unpackUF2( unsigned char *uf2, uint32_t l, unsigned char *raw )
{
	unsigned char *dst = raw;
	for ( uint32_t i = 0; i < l / 512; i++ )
	{
		uint32_t payloadSize = ( (uint32_t *)uf2 )[ 4 ];
		memcpy( dst, &uf2[ 32 ], payloadSize );
		dst += payloadSize;
		uf2 += 512;
	}
}

void packUF2( unsigned char *uf2, uint32_t l, unsigned char *raw )
{
	unsigned char *dst = raw;
	for ( uint32_t i = 0; i < l / 512; i++ )
	{
		uint32_t payloadSize = ( (uint32_t *)uf2 )[ 4 ];
		memcpy( &uf2[ 32 ], dst, payloadSize );
		dst += payloadSize;
		uf2 += 512;
	}
}

int main( int argc, char *argv[] )
{
	printf( "\nSIDKick pico patch v0.2\n" );
	if ( argc < 2 )
	{
		printf( "Syntax:\n" );
		printf( "SKpicopatch firmware.uf2 [timing-phi2] [timing-bus] [paddle-offset]\n" );
		printf( "timing-phi2 = 5..20 (default 15), timing-bus = 5..25 (default 11)\n" );
		printf( "paddle-offset: < 80 for setting a fixed offset, >= 80 detection + dynamic adjustment (default 0)\n" );
		exit( 1 );
	}

	printf( "\nreading SIDKick pico firmware.\n" );
	FILE *f = fopen( argv[ 1 ], "rb" );
	if ( f == NULL )
	{
		printf( "error reading .uf2!\n" );
		exit( 1 );
	}
	fseek( f, 0, SEEK_END );
	int binSize = ftell( f );
	fseek( f, 0, SEEK_SET );
	fread( skBinUF2, 1, binSize, f );
	fclose( f );

	unpackUF2( skBinUF2, binSize, skBin );

	int ofsDirectory = 0;
	while ( ofsDirectory < binSize - 24 && !compare( &skBin[ ofsDirectory++ ], patDir, 24 ) ) {}
	ofsDirectory --;

	int ofsPRG = 0;
	while ( ofsPRG < binSize - 20 && !compare( &skBin[ ofsPRG++ ], patPRG, 20 ) ) {}
	ofsPRG --;

	int ofsTiming = 0;
	while ( ofsTiming < binSize - 8 && !compare( &skBin[ ofsTiming++ ], patBT, 8 ) ) {}
	ofsTiming --;

	#define max( a, b ) ( (a)>(b)?(a):(b) )
	#define min( a, b ) ( (a)<(b)?(a):(b) )
	long int t;
	if ( argc >= 3 && ( t = strtol( argv[ 2 ], NULL, 10 ) ) ) skBin[ ofsTiming + 1 ] = min( 20, max( 5, t ) );
	if ( argc >= 4 && ( t = strtol( argv[ 3 ], NULL, 10 ) ) ) skBin[ ofsTiming ] = min( 25, max( 5, t ) );
	if ( argc >= 5 && ( t = strtol( argv[ 4 ], NULL, 10 ) ) ) 
	{
		t = min( 255, max( 1, t ) );
		if ( t < 80 ) t ++;
		skBin[ ofsTiming + 2 ] = t;
	}

	int curOffset = 0;
	int nPRGs = 0;

	char *fn, fn2[ 4096 ];
	printf( "\nparsing PRG list:\n" );

	FILE *g = fopen( "prg.lst", "rt" );
	if ( g == NULL )
	{
		printf( "error reading 'prg.lst'! continuing without adding files...\n" );
	} else
	{
		printf( "         menu name : filename\n" );
		while ( !feof( g ) && nPRGs < 16 )
		{
			int printName = -1;
			memset( fn2, 0, 4096 );
			fgets( fn2, 4095, g );
			fn = fn2;
			for ( int i = 0; i < 4095; i++ )
			{
				if ( fn[ i ] == ':' && printName == -1 )
				{
					printName = i + 1;
					fn[ i ] = 0;
				}
				if ( fn[ i ] == 13 || fn[ i ] == 10 ) fn[ i ] = 0;
			}
		#ifdef WIN32
			strupr( &fn[ printName ] );
		#else
			char *s = &fn[ printName ];
			while ( *s ) { *s = toupper( *s ); s++; }
		#endif

			if ( strlen( fn ) > 0 )
			{
				printf( "%18s : %s\n", &fn[ printName ], fn );

				FILE *h = fopen( fn, "rb" );
				if ( h != NULL )
				{
					fseek( h, 0, SEEK_END );
					int s = ftell( h );
					fseek( h, 0, SEEK_SET );

					fread( &skBin[ ofsPRG ], 1, s, h );
					ofsPRG += 65536;

					// create directory entry
					fn[ printName + 17 ] = 0;
					memcpy( &skBin[ ofsDirectory ], &fn[ printName ], 18 );

					skBin[ ofsDirectory + 18 ] = ( curOffset ) & 255;
					skBin[ ofsDirectory + 19 ] = ( curOffset >> 8 ) & 255;
					skBin[ ofsDirectory + 20 ] = ( curOffset >> 16 );
					skBin[ ofsDirectory + 21 ] = 0;

					skBin[ ofsDirectory + 22 ] = s & 255;
					skBin[ ofsDirectory + 23 ] = s >> 8;

					ofsDirectory += 24;
					curOffset += 65536;
					nPRGs++;

					fclose( h );
				} else
				{
					printf( "error reading '%s', skipping...\n", fn );
				}
			}
		}
		fclose( g );
	}

	printf( "\nwriting new SIDKick pico firmware to 'SKpicoPRG.uf2'.\n" );

	packUF2( skBinUF2, binSize, skBin );

	f = fopen( "SKpicoPRG.uf2", "wb" );
	int written = 0;
	if ( f != NULL )
	{
		written = fwrite( skBinUF2, 1, binSize, f );
		fclose( f );
	}
	if ( f == NULL || written != binSize )
	{
		printf( "error writing .uf2\n" );
		exit( 1 );
	}

	return 0;
}
