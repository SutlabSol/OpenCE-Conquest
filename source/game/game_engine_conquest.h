/*
GAME_ENGINE_CONQUEST.H

Conquest gametype for OpenCE — Battlefield-style territory capture.

Each map's pre-existing King of the Hill netgame flag groups are treated
as capture points.  Up to CONQUEST_MAXIMUM_BASES (5) points are active
simultaneously.  Teams earn tickets by holding bases; when a team's tickets
reach zero the other team wins.

port: new gametype added by the OpenCE-Conquest fork.
*/

#ifndef __GAME_ENGINE_CONQUEST_H
#define __GAME_ENGINE_CONQUEST_H
#pragma once

/* ---------- headers */

#include "game/game_engine.h"

/* ---------- constants */

enum
{
	CONQUEST_MAXIMUM_BASES      = 6,
	CONQUEST_MAXIMUM_HILL_POINTS= 12,  /* hull-points per base polygon */
	CONQUEST_CONTESTED          = 2,   /* capturing_team: both teams present */
};

/* Team indices used throughout (matches the engine's 0 = red, 1 = blue). */
enum conquest_team
{
	conquest_team_none = -1,
	conquest_team_red  =  0,
	conquest_team_blue =  1,
};

/* Capture-point state, mirroring Battlefield's flag states. */
enum conquest_base_state
{
	conquest_base_neutral    = 0,
	conquest_base_red        = 1,  /* fully owned by red */
	conquest_base_blue       = 2,  /* fully owned by blue */
	conquest_base_capturing_red  = 3, /* red is capturing a neutral/blue base */
	conquest_base_capturing_blue = 4, /* blue is capturing a neutral/red base */
	conquest_base_contested  = 5,  /* both teams present — capture paused */
};

/* ---------- structures */

struct model_vertex_uncompressed;
struct render_animation;
struct render_lighting;

/* Geometry of one capture point (mirrors king_globals per-hill data). */
struct conquest_base
{
	/* hill group id read from netgame_flag.team_index */
	short hill_id;
	short pad;

	/* convex hull of the boundary polygon in world space */
	long              point_count;
	real_point3d      points[CONQUEST_MAXIMUM_HILL_POINTS];
	real_point2d      convex_hull[CONQUEST_MAXIMUM_HILL_POINTS];
	real_point3d      center;
	real              top;
	real              bottom;

	/* capture meter: -CONQUEST_CAPTURE_TICKS (blue's) .. 0 (neutral) ..
	+CONQUEST_CAPTURE_TICKS (red's) */
	long              capture_progress;
	/* the owner, enum conquest_base_state: neutral, red or blue (kept until
	the meter passes through 0) */
	long              state;
	/* who is on the base this tick: conquest_team_none (-1), red (0),
	blue (1), or CONQUEST_CONTESTED (both) */
	long              capturing_team;
};

/*
 * Full runtime state sent over the distributed netcode.
 * Keep < GAME_ENGINE_MAXIMUM_NETWORK_STATE_SIZE (0xEFC bytes).
 */
struct conquest_globals
{
	/* team tickets (counts down to 0; initialised from score_to_win) */
	long tickets[2];

	/* per-base data */
	long                 base_count;
	struct conquest_base bases[CONQUEST_MAXIMUM_BASES];

	/* bleed tick accumulator: ticket is deducted every CONQUEST_BLEED_INTERVAL */
	long bleed_tick[2];

	/* all-bases lockdown accumulator: ticks team has continuously held all bases */
	long lockdown_ticks[2];
};

/* ---------- prototypes/GAME_ENGINE_CONQUEST.C */

/* netcode helpers called from game_engine.c */
long    game_engine_conquest_write_network_state(byte *buffer, long size);
boolean game_engine_conquest_read_network_state(byte const *buffer, long size, boolean first);

/* ---------- globals */

extern struct game_engine conquest_engine;

#endif /* __GAME_ENGINE_CONQUEST_H */
