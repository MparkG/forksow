#include "qcommon/base.h"
#include "qcommon/qcommon.h"
#include "gameshared/gs_public.h"

#define INSTANT 0

#define WEAPONDOWN_TIME 50
#define WEAPONUP_TIME 50

const WeaponDef gs_weaponDefs[] = {
	{
		"Knife", "gb",
		RGB8( 255, 255, 255 ),
		"Knife people in the face",
		0,

		NULL, NULL, NULL,

		0,                              // projectiles fired each shot
		0, // clip size
		0, // reload time

		//timings (in msecs)->
		WEAPONUP_TIME,                  // weapon up time
		WEAPONDOWN_TIME,                // weapon down time
		450,                            // refire time
		62,                             // projectile timeout  / projectile range for instant weapons
		0,                              // recoil
		false,                          // smooth refire

		//damages
		20,                             // damage
		0,                              // selfdamage ratio
		80,                             // knockback
		0,                              // splash radius
		0,                              // splash minimum damage
		0,                              // splash minimum knockback

		//projectile def
		INSTANT,                        // speed
		0,                              // spread
	},

	{
		"SMG", "mg",
		RGB8( 254, 235, 98 ),
		"Shoots fast direct bullets touching enemies at any range",
		100,

		NULL, NULL, NULL,

		1,                              // projectiles fired each shot
		30, // clip size
		3000, // reload time

		//timings (in msecs)
		WEAPONUP_TIME,                  // weapon up time
		WEAPONDOWN_TIME,                // weapon down time
		80,                             // refire time
		6000,                           // projectile timeout
		1.5f,                              // recoil
		false,                          // smooth refire

		//damages
		4,                              // damage
		0,                              // selfdamage ratio
		40,                             // knockback
		0,                              // splash radius
		0,                              // splash minimum damage
		0,                              // splash minimum knockback

		//projectile def
		INSTANT,                        // speed
		0,                              // spread
	},

	{
		"Shotgun", "rg",
		RGB8( 255, 172, 30 ),
		"Basically a shotgun",
		100,

		NULL, NULL, NULL,

		20,                             // projectiles fired each shot
		6, // clip size
		3000, // reload time

		//timings (in msecs)
		WEAPONUP_TIME,                  // weapon up time
		WEAPONDOWN_TIME,                // weapon down time
		1000,                           // refire time
		8192,                           // projectile timeout / projectile range for instant weapons
		20,                              // recoil
		false,                          // smooth refire

		//damages
		2,                              // damage
		0,                              // selfdamage ratio (rg cant selfdamage)
		7,                              // knockback
		0,                              // splash radius
		0,                              // splash minimum damage
		0,                              // splash minimum knockback

		//projectile def
		INSTANT,                        // speed
		80,                             // spread
	},

	{
		"Grenades", "gl",
		RGB8( 62, 141, 255 ),
		"Deprecated gun, enjoy it while it lasts nerds",
		100,

		PATH_GRENADE_MODEL,
		NULL, NULL,

		1,                              // projectiles fired each shot
		3, // clip size
		3000, // reload time

		//timings (in msecs)
		WEAPONUP_TIME,                  // weapon up time
		WEAPONDOWN_TIME,                // weapon down time
		800,                            // refire time
		1250,                           // projectile timeout
		0,                              // recoil
		false,                          // smooth refire

		//damages
		40,                             // damage
		0.75f,                          // selfdamage ratio
		100,                            // knockback
		125,                            // splash radius
		7,                              // splash minimum damage
		35,                             // splash minimum knockback

		//projectile def
		1000,                           // speed
		0,                              // spread
	},

	{
		"Rockets", "rl",
		RGB8( 255, 58, 66 ),
		"Shoots slow moving rockets that deal damage in an area and push bodies away",
		200,

		PATH_ROCKET_MODEL,
		S_WEAPON_ROCKET_FLY,
		NULL,

		1,                              // projectiles fired each shot
		5, // clip size
		3000, // reload time

		//timings (in msecs)
		WEAPONUP_TIME,                  // weapon up time
		WEAPONDOWN_TIME,                // weapon down time
		1000,                           // refire time
		10000,                          // projectile timeout
		0,                              // recoil
		false,                          // smooth refire

		//damages
		40,                             // damage
		0.75f,                          // selfdamage ratio
		100,                            // knockback
		120,                            // splash radius
		7,                              // splash minimum damage
		45,                             // splash minimum knockback

		//projectile def
		1250,                           // speed
		0,                              // spread
	},

	{
		"Plasma", "pg",
		RGB8( 172, 80, 255 ),
		"Shoots fast projectiles that deal damage in an area",
		100,

		PATH_PLASMA_MODEL,
		S_WEAPON_PLASMAGUN_FLY,
		NULL,

		1,                              // projectiles fired each shot
		30, // clip size
		3000, // reload time

		//timings (in msecs)
		WEAPONUP_TIME,                  // weapon up time
		WEAPONDOWN_TIME,                // weapon down time
		100,                            // refire time
		5000,                           // projectile timeout
		0,                              // recoil
		false,                          // smooth refire

		//damages
		8,                              // damage
		0.2f,                           // selfdamage ratio
		22,                             // knockback
		45,                             // splash radius
		4,                              // splash minimum damage
		1,                              // splash minimum knockback

		//projectile def
		2500,                           // speed
		0,                              // spread
	},

	{
		"Laser", "lg",
		RGB8( 82, 252, 95 ),
		"Shoots a continuous trail doing quick but low damage at a certain range",
		200,

		NULL,
		S_WEAPON_LASERGUN_HUM " " S_WEAPON_LASERGUN_STOP " " S_WEAPON_LASERGUN_HIT,
		NULL,

		1,                              // projectiles fired each shot
		50, // clip size
		3000, // reload time

		//timings (in msecs)
		WEAPONUP_TIME,                  // weapon up time
		WEAPONDOWN_TIME,                // weapon down time
		50,                             // refire time
		700,                            // projectile timeout / projectile range for instant weapons
		0,                              // recoil
		true,                           // smooth refire

		//damages
		4,                              // damage
		0,                              // selfdamage ratio
		14,                             // knockback
		0,                              // splash radius
		0,                              // splash minimum damage
		0,                              // splash minimum knockback

		//projectile def
		INSTANT,                        // speed
		0,                              // spread
	},

	{
		"Railgun", "eb",
		RGB8( 80, 243, 255 ),
		"Shoots a direct laser hit doing pretty high damage",
		200,

		NULL,
		S_WEAPON_ELECTROBOLT_HIT,
		NULL,

		1,                              // projectiles fired each shot
		3, // clip size
		3000, // reload time

		//timings (in msecs)
		WEAPONUP_TIME,                  // weapon up time
		WEAPONDOWN_TIME,                // weapon down time
		1250,                           // refire time
		ELECTROBOLT_RANGE,              // range
		0,                              // recoil
		false,                          // smooth refire

		//damages
		35,                             // damage
		0,                              // selfdamage ratio
		80,                             // knockback
		0,                              // splash radius
		0,                              // minimum damage
		0,                              // minimum knockback

		//projectile def
		INSTANT,                        // speed
		0,                              // spread
	},
};

STATIC_ASSERT( ARRAY_COUNT( gs_weaponDefs ) == Weapon_Count );

const WeaponDef * GS_GetWeaponDef( WeaponType weapon ) {
	assert( weapon < Weapon_Count );
	return &gs_weaponDefs[ weapon ];
}
