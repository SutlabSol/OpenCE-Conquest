/*
GAME_ENGINE_TERRITORIES.H

Territories gametype for OpenCE — Battlefield-style territory capture.

Each map's pre-existing King of the Hill netgame flag groups are treated
as capture points.  Up to TERRITORIES_MAXIMUM_BASES (5) points are active
simultaneously.  Teams earn tickets by holding bases; when a team's tickets
reach zero the other team wins.

port: new gametype added by the OpenCE fork.
*/

#ifndef __GAME_ENGINE_TERRITORIES_H
#define __GAME_ENGINE_TERRITORIES_H
#pragma once

/* ---------- headers */

#include "game/game_engine.h"

/* ---------- constants */

enum
{
	TERRITORIES_MAXIMUM_BASES       = 6,
	TERRITORIES_MAXIMUM_HILL_POINTS = 12,  /* hull-points per base polygon */
	TERRITORIES_CONTESTED           = 2,   /* capturing_team: both teams present */
};

/* Compatibility defines */
#define CONQUEST_MAXIMUM_BASES       TERRITORIES_MAXIMUM_BASES
#define CONQUEST_MAXIMUM_HILL_POINTS TERRITORIES_MAXIMUM_HILL_POINTS
#define CONQUEST_CONTESTED           TERRITORIES_CONTESTED

/* Team indices used throughout (matches the engine's 0 = red, 1 = blue). */
enum territories_team
{
	territories_team_none = -1,
	territories_team_red  =  0,
	territories_team_blue =  1,
};

#define conquest_team territories_team
#define conquest_team_none territories_team_none
#define conquest_team_red  territories_team_red
#define conquest_team_blue territories_team_blue

/* Capture-point state, mirroring Battlefield's flag states. */
enum territories_base_state
{
	territories_base_neutral        = 0,
	territories_base_red            = 1,  /* fully owned by red */
	territories_base_blue           = 2,  /* fully owned by blue */
	territories_base_capturing_red  = 3,  /* red is capturing a neutral/blue base */
	territories_base_capturing_blue = 4,  /* blue is capturing a neutral/red base */
	territories_base_contested      = 5,  /* both teams present — capture paused */
};

#define conquest_base_state territories_base_state
#define conquest_base_neutral territories_base_neutral
#define conquest_base_red territories_base_red
#define conquest_base_blue territories_base_blue
#define conquest_base_capturing_red territories_base_capturing_red
#define conquest_base_capturing_blue territories_base_capturing_blue
#define conquest_base_contested territories_base_contested

/* ---------- structures */

struct model_vertex_uncompressed;
struct render_animation;
struct render_lighting;

/* Geometry of one capture point (mirrors king_globals per-hill data). */
struct territories_base
{
	/* hill group id read from netgame_flag.team_index */
	short hill_id;
	short pad;

	/* convex hull of the boundary polygon in world space */
	long              point_count;
	real_point3d      points[TERRITORIES_MAXIMUM_HILL_POINTS];
	real_point2d      convex_hull[TERRITORIES_MAXIMUM_HILL_POINTS];
	real_point3d      center;
	real              top;
	real              bottom;

	/* capture meter: -TERRITORIES_CAPTURE_TICKS (blue's) .. 0 (neutral) ..
	+TERRITORIES_CAPTURE_TICKS (red's) */
	long              capture_progress;
	/* the owner, enum territories_base_state: neutral, red or blue (kept until
	the meter passes through 0) */
	long              state;
	/* who is on the base this tick: territories_team_none (-1), red (0),
	blue (1), or TERRITORIES_CONTESTED (both) */
	long              capturing_team;
};

#define conquest_base territories_base

/*
 * Full runtime state sent over the distributed netcode.
 * Keep < GAME_ENGINE_MAXIMUM_NETWORK_STATE_SIZE (0xEFC bytes).
 */
struct territories_globals
{
	/* team tickets (counts down to 0; initialised from score_to_win) */
	long tickets[2];

	/* per-base data */
	long                    base_count;
	struct territories_base bases[TERRITORIES_MAXIMUM_BASES];

	/* bleed tick accumulator: ticket is deducted every TERRITORIES_BLEED_INTERVAL */
	long bleed_tick[2];

	/* all-bases lockdown accumulator: ticks team has continuously held all bases */
	long lockdown_ticks[2];
};

#define conquest_globals territories_globals

/* ---------- prototypes/GAME_ENGINE_TERRITORIES.C */

/* netcode helpers called from game_engine.c */
long    game_engine_territories_write_network_state(byte *buffer, long size);
boolean game_engine_territories_read_network_state(byte const *buffer, long size, boolean first);

/* base getters for HUD */
long game_engine_territories_get_base_count(void);
struct territories_base const *game_engine_territories_get_base(long index);
long game_engine_territories_get_tickets(long team_index);
long game_engine_territories_get_starting_tickets(void);
long game_engine_territories_get_lockdown_status(long *locking_team, long *remaining_seconds);

/* Compatibility aliases */
#define game_engine_conquest_write_network_state game_engine_territories_write_network_state
#define game_engine_conquest_read_network_state  game_engine_territories_read_network_state
#define game_engine_conquest_get_base_count      game_engine_territories_get_base_count
#define game_engine_conquest_get_base            game_engine_territories_get_base
#define game_engine_conquest_get_tickets         game_engine_territories_get_tickets
#define game_engine_conquest_get_starting_tickets game_engine_territories_get_starting_tickets
#define game_engine_conquest_get_lockdown_status game_engine_territories_get_lockdown_status

/* ---------- globals */

extern struct game_engine territories_engine;
#define conquest_engine territories_engine

#endif /* __GAME_ENGINE_TERRITORIES_H */
