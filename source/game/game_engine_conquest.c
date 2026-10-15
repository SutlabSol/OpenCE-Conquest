/*
GAME_ENGINE_CONQUEST.C

Conquest gametype: a Battlefield-style multi-point territory capture mode.

port: added by the OpenCE-Conquest fork.

How it plays:
  - The map's King of the Hill flag groups are the capture points (at most
    CONQUEST_MAXIMUM_BASES, five). All are live at once.
  - Each team starts with CONQUEST_STARTING_TICKETS tickets.
  - A base has a capture meter from -CONQUEST_CAPTURE_TICKS (blue's) through
    0 (neutral) to +CONQUEST_CAPTURE_TICKS (red's). Players of one team alone
    on a base move it their way; both teams on it hold it still (contested).
    The owner is kept until the meter passes through 0.
  - A team holding fewer bases than the other bleeds a ticket every
    CONQUEST_BLEED_INTERVAL ticks, and a team loses a ticket when one of its
    players dies. The team at 0 tickets loses.
  - Each base's boundary fence is drawn grey while neutral and fades to the
    team's colour as the meter fills (yellow while contested). The base's
    waypoint is the stock arrow of its owner.
*/

/* ---------- headers */

#include "cseries/cseries.h"
#include "cseries/errors.h"
#include "game/game.h"
#include "game/game_engine.h"
#include "game/game_globals.h"
#include "game/game_engine_conquest.h"
#include "game/game_engine_place.h"
#include "game/players.h"
#include <math.h>
#include "main/console.h"
#include "math/geometry.h"
#include "rasterizer/rasterizer.h"
#include "rasterizer/rasterizer_geometry.h"
#include "rasterizer/rasterizer_model_types.h"
#include "rasterizer/rasterizer_models.h"
#include "render/render.h"
#include "scenario/scenario.h"
#include "scenario/scenario_definitions.h"
#include "shaders/shader_definitions.h"
#include "shaders/shaders.h"
#include "tag_files/tag_groups.h"
#include "text/unicode.h"
#include "units/units.h"

/* ---------- constants */

enum
{
	/* a base's meter moves by one per tick: 8 seconds from neutral to owned */
	CONQUEST_CAPTURE_TICKS = 8*TICKS_PER_SECOND,

	/* a team behind on bases loses a ticket this often */
	CONQUEST_BLEED_INTERVAL = 3*TICKS_PER_SECOND,

	/* each team's starting tickets (the variant's score_to_win is minutes in
	the gametype editor, so it is not used) */
	CONQUEST_STARTING_TICKETS = 200,

	/* a team's loss when one of its players dies */
	CONQUEST_KILL_TICKET_LOSS = 1,

	CONQUEST_WARNING_100 = 100,
	CONQUEST_WARNING_50 = 50,
	CONQUEST_WARNING_25 = 25,
};

/* the stock multiplayer sound indices game_engine_king.c uses */
enum
{
	_conquest_sound_red_60_seconds = 0x4,
	_conquest_sound_red_30_seconds = 0x5,
	_conquest_sound_blue_60_seconds = 0x6,
	_conquest_sound_blue_30_seconds = 0x7,
	_conquest_sound_team_king = 0x20,
	_conquest_sound_king = 0x24,
	_conquest_sound_hill_contested = 0x27,
	_conquest_sound_hill_controlled = 0x28,
	_conquest_sound_ting = 0x2B,
};

/* ---------- structures */

typedef char verify_conquest_scenario_netgame_flag_size[
	sizeof(struct scenario_netgame_flag) == 0x94 ? 1 : -1];
typedef char verify_conquest_network_state_size[
	sizeof(struct conquest_globals) <= GAME_ENGINE_MAXIMUM_NETWORK_STATE_SIZE ? 1 : -1];

/* ---------- prototypes */

static void conquest_build_base(
	struct conquest_base *base);
static boolean conquest_player_inside_base(
	long player_index,
	struct conquest_base const *base);
static void conquest_update_hud_goals(
	void);
static boolean conquest_engine_format_message(
	long player_index,
	long message,
	long message_data,
	wchar_t *buffer,
	long buffer_size);
static void conquest_render_base_fence(
	struct conquest_base const *base,
	long shader_index);
static void conquest_check_win_condition(
	void);
static void conquest_ticket_sounds(
	long team_index,
	long tickets,
	long previous_tickets);

/* game_engine_king.c's */
void render_dynamic_quad_initialize(
	void);
void render_dynamic_quad(
	struct model_vertex_uncompressed *vertices,
	long shader_index,
	struct render_lighting const *lighting,
	struct render_animation const *animation,
	real u_scale,
	real v_scale);

/* network_game_globals.c's */
boolean network_game_distributed_client(void);

/* ---------- globals */

static struct conquest_globals cq = { 0 };

/* ---------- public code */

static void conquest_engine_dispose(
	void)
{
	return;
}

static void conquest_engine_dispose_from_old_map(
	void)
{
	return;
}

static void conquest_engine_game_ending(
	void)
{
	return;
}

static void conquest_engine_statistics_append(
	long statistic)
{
	return;
}

static void conquest_engine_handle_client_message(
	void *message)
{
	return;
}

static void conquest_engine_handle_server_message(
	void *message)
{
	return;
}

static void conquest_engine_pregame_post_rasterize(
	void)
{
	return;
}

static void conquest_engine_prespawn_player_update(
	long player_index)
{
	return;
}

static void conquest_engine_player_damaged_player(
	long damaging_player_index,
	long dead_player_index,
	boolean damage_type)
{
	return;
}

static void conquest_engine_player_added(
	long player_index)
{
	return;
}

static void conquest_engine_game_starting(
	void)
{
	game_engine_play_multiplayer_sound(_conquest_sound_team_king);

	return;
}

static void conquest_engine_player_update(
	long player_index)
{
	game_engine_state_message(player_index, NONE, NONE);

	return;
}

static boolean conquest_get_hill_center(
	short hill_id,
	real_point3d *center)
{
	struct scenario *scenario = global_scenario_get();
	long flag_indices[CONQUEST_MAXIMUM_HILL_POINTS];
	long point_count;
	real_point3d min_pt, max_pt;
	long i;

	point_count = find_netgame_flags(
		NULL,
		0.0f,
		0.0f,
		_netgame_flag_hill,
		hill_id,
		CONQUEST_MAXIMUM_HILL_POINTS,
		flag_indices);

	if (point_count <= 0)
		return FALSE;

	{
		struct scenario_netgame_flag *flag0 = TAG_BLOCK_GET_ELEMENT(
			&scenario->netgame_flags,
			flag_indices[0],
			struct scenario_netgame_flag);
		min_pt = max_pt = flag0->position;
	}

	for (i = 1; i < point_count; i++)
	{
		struct scenario_netgame_flag *flag = TAG_BLOCK_GET_ELEMENT(
			&scenario->netgame_flags,
			flag_indices[i],
			struct scenario_netgame_flag);

		min_pt.x = MIN(min_pt.x, flag->position.x);
		min_pt.y = MIN(min_pt.y, flag->position.y);
		min_pt.z = MIN(min_pt.z, flag->position.z);
		max_pt.x = MAX(max_pt.x, flag->position.x);
		max_pt.y = MAX(max_pt.y, flag->position.y);
		max_pt.z = MAX(max_pt.z, flag->position.z);
	}

	center->x = (min_pt.x + max_pt.x) * 0.5f;
	center->y = (min_pt.y + max_pt.y) * 0.5f;
	center->z = (min_pt.z + max_pt.z) * 0.5f;
	return TRUE;
}

struct candidate_hill_info
{
	short hill_id;
	real_point3d center;
	real dist_red;
	real dist_blue;
	real bias;
	boolean too_close;
	boolean selected;
};

#define CONQUEST_MAX_CANDIDATE_HILLS 32

/* Select hills prioritizing equal distance to Red/Blue bases and excluding spawn-adjacent hills */
static void conquest_select_balanced_hills(
	short *chosen_hill_ids,
	short *chosen_count,
	short target_count)
{
	struct scenario *scenario = global_scenario_get();
	struct candidate_hill_info candidates[CONQUEST_MAX_CANDIDATE_HILLS];
	short candidate_count = 0;
	long flag_index;
	boolean has_base_positions = FALSE;
	real_point3d red_base = { 0 };
	real_point3d blue_base = { 0 };
	real base_distance;
	real min_spawn_buffer;
	short i;
	short selected_num = 0;
	real total_dist_red = 0.0f;
	real total_dist_blue = 0.0f;

	*chosen_count = 0;

	/* 1. Gather all unique hill IDs in the scenario */
	for (flag_index = 0;
		flag_index < scenario->netgame_flags.count && candidate_count < CONQUEST_MAX_CANDIDATE_HILLS;
		flag_index++)
	{
		struct scenario_netgame_flag *flag = TAG_BLOCK_GET_ELEMENT(
			&scenario->netgame_flags,
			flag_index,
			struct scenario_netgame_flag);

		if (flag->type == _netgame_flag_hill)
		{
			boolean found = FALSE;
			for (i = 0; i < candidate_count; i++)
			{
				if (candidates[i].hill_id == flag->team_index)
				{
					found = TRUE;
					break;
				}
			}
			if (!found)
			{
				candidates[candidate_count].hill_id = flag->team_index;
				candidates[candidate_count].selected = FALSE;
				candidate_count++;
			}
		}
	}

	if (candidate_count == 0)
		return;

	/* If candidates are fewer than or equal to target_count, take all of them */
	if (candidate_count <= target_count)
	{
		for (i = 0; i < candidate_count; i++)
			chosen_hill_ids[i] = candidates[i].hill_id;
		*chosen_count = candidate_count;
		return;
	}

	/* 2. Locate Red and Blue base positions via CTF flags or player spawn centroids */
	{
		long red_flag_index = find_netgame_flag(NULL, 0.0f, 0.0f, _netgame_flag_ctf_flag, conquest_team_red);
		long blue_flag_index = find_netgame_flag(NULL, 0.0f, 0.0f, _netgame_flag_ctf_flag, conquest_team_blue);

		if (red_flag_index != NONE && blue_flag_index != NONE)
		{
			struct scenario_netgame_flag *rf = TAG_BLOCK_GET_ELEMENT(
				&scenario->netgame_flags, red_flag_index, struct scenario_netgame_flag);
			struct scenario_netgame_flag *bf = TAG_BLOCK_GET_ELEMENT(
				&scenario->netgame_flags, blue_flag_index, struct scenario_netgame_flag);
			if (rf && bf)
			{
				red_base = rf->position;
				blue_base = bf->position;
				has_base_positions = TRUE;
			}
		}
	}

	if (!has_base_positions)
	{
		long red_spawn_count = 0;
		long blue_spawn_count = 0;
		short loc_count = player_get_starting_location_count();
		short loc_idx;

		for (loc_idx = 0; loc_idx < loc_count; loc_idx++)
		{
			struct player_starting_location *loc = player_get_starting_location(loc_idx);
			if (!loc)
				continue;
			if (loc->team_index == conquest_team_red)
			{
				red_base.x += loc->position.x;
				red_base.y += loc->position.y;
				red_base.z += loc->position.z;
				red_spawn_count++;
			}
			else if (loc->team_index == conquest_team_blue)
			{
				blue_base.x += loc->position.x;
				blue_base.y += loc->position.y;
				blue_base.z += loc->position.z;
				blue_spawn_count++;
			}
		}

		if (red_spawn_count > 0 && blue_spawn_count > 0)
		{
			red_base.x /= (real)red_spawn_count;
			red_base.y /= (real)red_spawn_count;
			red_base.z /= (real)red_spawn_count;
			blue_base.x /= (real)blue_spawn_count;
			blue_base.y /= (real)blue_spawn_count;
			blue_base.z /= (real)blue_spawn_count;
			has_base_positions = TRUE;
		}
	}

	if (!has_base_positions)
	{
		/* Fallback if map has neither CTF flags nor team spawns */
		for (i = 0; i < target_count && i < candidate_count; i++)
			chosen_hill_ids[i] = candidates[i].hill_id;
		*chosen_count = MIN(target_count, candidate_count);
		return;
	}

	base_distance = distance3d(&red_base, &blue_base);
	min_spawn_buffer = base_distance * 0.18f;

	/* 3. Compute center, distances, and bias for each candidate hill */
	for (i = 0; i < candidate_count; i++)
	{
		conquest_get_hill_center(candidates[i].hill_id, &candidates[i].center);
		candidates[i].dist_red = distance3d(&candidates[i].center, &red_base);
		candidates[i].dist_blue = distance3d(&candidates[i].center, &blue_base);
		candidates[i].bias = (real)fabs(candidates[i].dist_red - candidates[i].dist_blue);
		candidates[i].too_close = (candidates[i].dist_red < min_spawn_buffer ||
		                           candidates[i].dist_blue < min_spawn_buffer);
	}

	/* 4. Check if enough candidates remain when excluding hills too close to spawn */
	{
		short valid_count = 0;
		for (i = 0; i < candidate_count; i++)
		{
			if (!candidates[i].too_close)
				valid_count++;
		}
		if (valid_count < target_count)
		{
			/* If excluding them leaves too few hills, relax exclusion */
			for (i = 0; i < candidate_count; i++)
				candidates[i].too_close = FALSE;
		}
	}

	/* 5. Select target_count hills greedily to maintain minimum bias and equal distance sum */
	while (selected_num < target_count && selected_num < candidate_count)
	{
		short best_idx = -1;
		real best_score = 1e9f;

		for (i = 0; i < candidate_count; i++)
		{
			real candidate_score;
			real new_diff;

			if (candidates[i].selected || candidates[i].too_close)
				continue;

			new_diff = (real)fabs((total_dist_red + candidates[i].dist_red) -
			                      (total_dist_blue + candidates[i].dist_blue));

			/* Score prioritizes overall balance between teams, then individual neutrality */
			candidate_score = new_diff * 1.5f + candidates[i].bias;

			if (candidate_score < best_score)
			{
				best_score = candidate_score;
				best_idx = i;
			}
		}

		if (best_idx == -1)
		{
			/* If non-too_close candidates exhausted, pick from remaining */
			for (i = 0; i < candidate_count; i++)
			{
				if (!candidates[i].selected)
				{
					best_idx = i;
					break;
				}
			}
		}

		if (best_idx == -1)
			break;

		candidates[best_idx].selected = TRUE;
		chosen_hill_ids[selected_num++] = candidates[best_idx].hill_id;
		total_dist_red += candidates[best_idx].dist_red;
		total_dist_blue += candidates[best_idx].dist_blue;
	}

	*chosen_count = selected_num;
}

static boolean conquest_engine_initialize_for_new_map(
	void)
{
	short hill_ids[CONQUEST_MAXIMUM_BASES];
	short hill_id_count = 0;
	long base_index;
	short configured_bases;
	long starting_tickets;

	csmemset(&cq, 0, sizeof(cq));

	configured_bases = (short)game_engine_get_variant()->game_engine_variant.conquest.base_count;
	if (configured_bases < 3 || configured_bases > CONQUEST_MAXIMUM_BASES)
		configured_bases = 5;

	/* Select fair, balanced hills away from team spawns */
	conquest_select_balanced_hills(hill_ids, &hill_id_count, configured_bases);

	cq.base_count = MIN(hill_id_count, configured_bases);
	cq.base_count = PIN(cq.base_count, 0, CONQUEST_MAXIMUM_BASES);
	for (base_index = 0; base_index < cq.base_count; base_index++)
	{
		struct conquest_base *base = &cq.bases[base_index];

		base->hill_id = hill_ids[base_index];
		base->state = conquest_base_neutral;
		base->capture_progress = 0;
		base->capturing_team = conquest_team_none;
		conquest_build_base(base);
	}

	starting_tickets = game_engine_get_variant()->universal_variant.score_to_win > 0 ?
		game_engine_get_variant()->universal_variant.score_to_win : CONQUEST_STARTING_TICKETS;
	cq.tickets[conquest_team_red] = starting_tickets;
	cq.tickets[conquest_team_blue] = starting_tickets;
	cq.bleed_tick[conquest_team_red] = 0;
	cq.bleed_tick[conquest_team_blue] = 0;
	cq.lockdown_ticks[conquest_team_red] = 0;
	cq.lockdown_ticks[conquest_team_blue] = 0;

	render_dynamic_quad_initialize();
	conquest_update_hud_goals();

	if (cq.base_count == 0)
		error(_error_silent, "conquest: the map has no hill flags");

	/* (a map without hills is still playable: tickets bleed only by kills) */
	return TRUE;
}

static void conquest_engine_update(
	void)
{
	long base_index;

	if (game_engine_can_score() && !network_game_distributed_client())
	{
		long red_bases = 0;
		long blue_bases = 0;
		long team_index;

		for (base_index = 0; base_index < cq.base_count; base_index++)
		{
			struct conquest_base *base = &cq.bases[base_index];
			struct data_iterator player_iterator;
			struct player_datum *player;
			long red_count = 0;
			long blue_count = 0;
			long previous_state = base->state;

			data_iterator_new(&player_iterator, player_data);
			player = (struct player_datum *)data_iterator_next(&player_iterator);
			while (player)
			{
				if (player->unit_index != NONE &&
					conquest_player_inside_base(player_iterator.datum_index, base))
				{
					if (player->team_index == conquest_team_red)
						red_count++;
					else
						blue_count++;
					player->statistics.multiplayer_statistics.king_statistics.time_on_hill++;
				}
				player = (struct player_datum *)data_iterator_next(&player_iterator);
			}

			if (red_count && blue_count)
			{
				base->capturing_team = CONQUEST_CONTESTED;
			}
			else if (red_count)
			{
				base->capturing_team = conquest_team_red;
				if (base->capture_progress < CONQUEST_CAPTURE_TICKS)
					base->capture_progress++;
			}
			else if (blue_count)
			{
				base->capturing_team = conquest_team_blue;
				if (base->capture_progress > -CONQUEST_CAPTURE_TICKS)
					base->capture_progress--;
			}
			else
			{
				base->capturing_team = conquest_team_none;
			}

			/* the owner: set when the meter is full, lost when it passes 0 */
			if (base->capture_progress >= CONQUEST_CAPTURE_TICKS)
				base->state = conquest_base_red;
			else if (base->capture_progress <= -CONQUEST_CAPTURE_TICKS)
				base->state = conquest_base_blue;
			else if (base->state == conquest_base_red && base->capture_progress <= 0)
				base->state = conquest_base_neutral;
			else if (base->state == conquest_base_blue && base->capture_progress >= 0)
				base->state = conquest_base_neutral;

			if (base->state != previous_state)
			{
				game_engine_play_multiplayer_sound(
					base->state == conquest_base_neutral ?
						_conquest_sound_hill_contested :
						_conquest_sound_hill_controlled);
			}

			if (base->state == conquest_base_red)
				red_bases++;
			else if (base->state == conquest_base_blue)
				blue_bases++;
		}

		/* Scaled bleed intervals:
		 * index 0: standard rate (1.0x) = 90 ticks (3.00s)
		 * index 1: slightly increased (1.2x) = 75 ticks (2.50s)
		 * index 2: increased (1.4x) = 64 ticks (2.14s)
		 * index 3: increased (1.6x) = 56 ticks (1.87s)
		 * index 4: increased (1.8x) = 50 ticks (1.67s)
		 * index 5: increased (2.0x) = 45 ticks (1.50s)
		 */
		static short const conquest_bleed_intervals[] = { 90, 75, 64, 56, 50, 45 };

		/* ticket bleed logic:
		 * - if enemy controls 0 bases: you do not bleed
		 * - if enemy controls >= 1 bases:
		 *   * if you control equal or more bases: you bleed at standard rate (1.0x = 90 ticks)
		 *   * if enemy controls more bases: you bleed at increased rate based on deficit
		 */
		for (team_index = 0; team_index < 2; team_index++)
		{
			long own_bases = team_index == conquest_team_red ? red_bases : blue_bases;
			long enemy_bases = team_index == conquest_team_red ? blue_bases : red_bases;
			short interval = 0;

			if (enemy_bases > 0)
			{
				if (own_bases >= enemy_bases)
				{
					/* Standard rate: you hold equal or more bases, but enemy holds at least 1 */
					interval = conquest_bleed_intervals[0];
				}
				else
				{
					/* Deficit: enemy holds more bases than you */
					long effective_diff = own_bases > 0 ? (enemy_bases - own_bases) : (enemy_bases - 1);
					long index = PIN(effective_diff, 0, (long)NUMBEROF(conquest_bleed_intervals) - 1);
					interval = conquest_bleed_intervals[index];
				}
			}

			if (interval > 0)
			{
				if (++cq.bleed_tick[team_index] >= interval)
				{
					long previous_tickets = cq.tickets[team_index];

					cq.bleed_tick[team_index] = 0;
					cq.tickets[team_index] = MAX(cq.tickets[team_index] - 1, 0);
					conquest_ticket_sounds(team_index, cq.tickets[team_index], previous_tickets);
				}
			}
			else
			{
				cq.bleed_tick[team_index] = 0;
			}
		}

		/* full map lockdown: if a team holds all bases for 1 continuous minute (1800 ticks), they automatically win */
		for (team_index = 0; team_index < 2; team_index++)
		{
			long own_bases = team_index == conquest_team_red ? red_bases : blue_bases;

			if (cq.base_count > 0 && own_bases == cq.base_count)
			{
				long previous_lockdown = cq.lockdown_ticks[team_index];
				cq.lockdown_ticks[team_index]++;

				/* Audio warning alerts at 30 seconds and 10 seconds remaining */
				if (previous_lockdown < 30 * TICKS_PER_SECOND && cq.lockdown_ticks[team_index] >= 30 * TICKS_PER_SECOND)
				{
					game_engine_play_multiplayer_sound(_conquest_sound_ting);
				}
				else if (previous_lockdown < 50 * TICKS_PER_SECOND && cq.lockdown_ticks[team_index] >= 50 * TICKS_PER_SECOND)
				{
					game_engine_play_multiplayer_sound(
						team_index == conquest_team_red ? _conquest_sound_red_30_seconds : _conquest_sound_blue_30_seconds);
				}

				if (cq.lockdown_ticks[team_index] >= 60 * TICKS_PER_SECOND)
				{
					long enemy_team = 1 - team_index;
					cq.tickets[enemy_team] = 0;
					game_engine_end_game();
				}
			}
			else
			{
				cq.lockdown_ticks[team_index] = 0;
			}
		}

		conquest_check_win_condition();
	}

	conquest_update_hud_goals();

	return;
}

static void conquest_engine_player_killed_player(
	long killing_player_index,
	long killing_object_index,
	long dead_player_index,
	boolean friendly_fire)
{
	if (!network_game_distributed_client() && dead_player_index != NONE && game_engine_can_score())
	{
		struct player_datum *dead_player = player_get(dead_player_index);
		long team_index = dead_player->team_index;

		if (team_index == conquest_team_red || team_index == conquest_team_blue)
		{
			long previous_tickets = cq.tickets[team_index];

			cq.tickets[team_index] = MAX(cq.tickets[team_index] - CONQUEST_KILL_TICKET_LOSS, 0);
			conquest_ticket_sounds(team_index, cq.tickets[team_index], previous_tickets);
			conquest_check_win_condition();
		}
	}

	return;
}

/* a team's score is its tickets, a player's is his time on the bases */
static long conquest_get_score(
	long player_index,
	enum get_score_type score_type)
{
	struct player_datum *player = player_get(player_index);

	if (score_type == _get_score_team)
	{
		if (player->team_index == conquest_team_red || player->team_index == conquest_team_blue)
			return cq.tickets[player->team_index];
		return 0;
	}

	return player->statistics.multiplayer_statistics.king_statistics.time_on_hill/TICKS_PER_SECOND;
}

static wchar_t *conquest_get_score_string(
	long player_index,
	wchar_t *buffer)
{
	struct player_datum *player = player_get(player_index);

	usprintf(
		buffer,
		L"%d",
		player->statistics.multiplayer_statistics.king_statistics.time_on_hill/TICKS_PER_SECOND);

	return buffer;
}

static wchar_t *conquest_get_score_header_string(
	wchar_t *buffer)
{
	/* port: bounded (into its callers' score_string[256]) */
	ustrncpy_terminated(buffer, L"Seconds", 256);

	return buffer;
}

static wchar_t *conquest_get_team_score_string(
	long team_index,
	wchar_t *buffer)
{
	usprintf(
		buffer,
		L"%d",
		(team_index == conquest_team_red || team_index == conquest_team_blue) ? cq.tickets[team_index] : 0);

	return buffer;
}

/* a spawn near a base the player's team owns is rated higher (up to double) */
static real conquest_engine_starting_location_rating(
	long player_index,
	struct player_starting_location const *starting_location)
{
	struct player_datum *player = player_get(player_index);
	real best_bonus = 0.0f;
	long base_index;

	for (base_index = 0; base_index < cq.base_count; base_index++)
	{
		struct conquest_base const *base = &cq.bases[base_index];
		boolean ours =
			(player->team_index == conquest_team_red && base->state == conquest_base_red) ||
			(player->team_index == conquest_team_blue && base->state == conquest_base_blue);

		if (ours)
		{
			real dx = base->center.x - starting_location->position.x;
			real dy = base->center.y - starting_location->position.y;
			real distance_squared = dx*dx + dy*dy;
			real bonus = 1.0f/(1.0f + distance_squared*0.01f);

			if (bonus > best_bonus)
				best_bonus = bonus;
		}
	}

	return 1.0f + best_bonus;
}

static boolean conquest_engine_format_message(
	long player_index,
	long message,
	long message_data,
	wchar_t *buffer,
	long buffer_size)
{
	return FALSE;
}

static boolean conquest_engine_goal_matches_player(
	long player_index,
	long goal_index)
{
	return TRUE;
}

static void conquest_engine_post_rasterize(
	void)
{
	struct game_globals *game_globals;
	struct game_globals_multiplayer_information *multiplayer_information;
	long shader_index;
	long base_index;

	global_scenario_get();
	game_globals = scenario_get_game_globals();
	multiplayer_information = TAG_BLOCK_GET_ELEMENT(
		&game_globals->multiplayer_information,
		0,
		struct game_globals_multiplayer_information);
	shader_index = multiplayer_information->hill_shader.index;

	for (base_index = 0; base_index < cq.base_count; base_index++)
		conquest_render_base_fence(&cq.bases[base_index], shader_index);

	return;
}

/* ---------- engine table */

static boolean conquest_test_flag(
	long flag)
{
	if (flag == 0)
		return TRUE; /* enforces team starting locations: red at red, blue at blue */

	return FALSE;
}

struct game_engine conquest_engine =
{
	"conquest",
	game_engine_conquest,
	conquest_engine_dispose,
	conquest_engine_initialize_for_new_map,
	conquest_engine_dispose_from_old_map,
	conquest_engine_player_added,
	conquest_engine_game_ending,
	conquest_engine_game_starting,
	conquest_engine_statistics_append,
	conquest_engine_handle_client_message,
	conquest_engine_handle_server_message,
	conquest_engine_pregame_post_rasterize,
	conquest_engine_post_rasterize,
	conquest_engine_player_update,
	NULL,
	NULL,
	NULL,
	conquest_engine_update,
	conquest_get_score,
	conquest_get_score_string,
	conquest_get_score_header_string,
	conquest_get_team_score_string,
	NULL,
	conquest_engine_player_damaged_player,
	conquest_engine_player_killed_player,
	conquest_engine_format_message,
	conquest_engine_starting_location_rating,
	conquest_engine_prespawn_player_update,
	NULL,
	NULL,
	conquest_engine_goal_matches_player,
	conquest_test_flag,
	NULL,
	NULL,
};

/* the distributed netcode (port/linux/game/network_distributed.c): the game
type's state the host sends its clients */
long game_engine_conquest_write_network_state(
	byte *buffer,
	long size)
{
	if (size < (long)sizeof(cq))
		return 0;
	csmemcpy(buffer, &cq, sizeof(cq));
	return sizeof(cq);
}

/* a client takes the host's tickets and bases (the bases' geometry is the
map's, the same on every machine) */
boolean game_engine_conquest_read_network_state(
	byte const *buffer,
	long size,
	boolean first)
{
	struct conquest_globals state;
	long team_index;
	long base_index;

	if (size != (long)sizeof(state))
		return FALSE;
	csmemcpy(&state, buffer, sizeof(state));
	/* (a count or a state from another machine is not trusted) */
	if (state.base_count < 0 || state.base_count > CONQUEST_MAXIMUM_BASES)
		return FALSE;
	for (base_index = 0; base_index < state.base_count; base_index++)
	{
		if (state.bases[base_index].point_count < 0 ||
			state.bases[base_index].point_count > CONQUEST_MAXIMUM_HILL_POINTS)
			return FALSE;
	}

	if (!first)
	{
		for (team_index = 0; team_index < 2; team_index++)
			conquest_ticket_sounds(team_index, state.tickets[team_index], cq.tickets[team_index]);
		for (team_index = 0; team_index < 2; team_index++)
		{
			long prev = cq.lockdown_ticks[team_index];
			long curr = state.lockdown_ticks[team_index];

			if (prev < 30 * TICKS_PER_SECOND && curr >= 30 * TICKS_PER_SECOND)
			{
				game_engine_play_multiplayer_sound(_conquest_sound_ting);
			}
			else if (prev < 50 * TICKS_PER_SECOND && curr >= 50 * TICKS_PER_SECOND)
			{
				game_engine_play_multiplayer_sound(
					team_index == conquest_team_red ? _conquest_sound_red_30_seconds : _conquest_sound_blue_30_seconds);
			}
		}
		for (base_index = 0; base_index < state.base_count && base_index < cq.base_count; base_index++)
		{
			if (state.bases[base_index].state != cq.bases[base_index].state)
			{
				game_engine_play_multiplayer_sound(
					state.bases[base_index].state == conquest_base_neutral ?
						_conquest_sound_hill_contested :
						_conquest_sound_hill_controlled);
			}
		}
	}
	csmemcpy(&cq, &state, sizeof(cq));
	conquest_update_hud_goals();

	return TRUE;
}

/* ---------- private code */

static void conquest_build_base(
	struct conquest_base *base)
{
	struct scenario *scenario = global_scenario_get();
	long flag_indices[CONQUEST_MAXIMUM_HILL_POINTS];
	real_point3d points[CONQUEST_MAXIMUM_HILL_POINTS];
	real_point2d points2d[CONQUEST_MAXIMUM_HILL_POINTS];
	short hull_indices[CONQUEST_MAXIMUM_HILL_POINTS];
	real_point3d minimum, maximum;
	long point_count;
	short hull_point_count;
	long i;

	base->point_count = 0;
	point_count = find_netgame_flags(
		NULL,
		0.0f,
		0.0f,
		_netgame_flag_hill,
		base->hill_id,
		CONQUEST_MAXIMUM_HILL_POINTS,
		flag_indices);
	if (!point_count)
		return;

	for (i = 0; i < point_count; i++)
	{
		struct scenario_netgame_flag *flag = TAG_BLOCK_GET_ELEMENT(
			&scenario->netgame_flags,
			flag_indices[i],
			struct scenario_netgame_flag);

		points[i] = flag->position;
	}

	/* one flag: a two by two box around it, as king of the hill does */
	if (point_count == 1)
	{
		points[1] = points[2] = points[3] = points[0];
		points[0].x -= 1.0f;
		points[0].y -= 1.0f;
		points[1].x += 1.0f;
		points[1].y -= 1.0f;
		points[2].x -= 1.0f;
		points[2].y += 1.0f;
		points[3].x += 1.0f;
		points[3].y += 1.0f;
		point_count = 4;
	}

	for (i = 0; i < point_count; i++)
	{
		points2d[i].x = points[i].x;
		points2d[i].y = points[i].y;
	}
	hull_point_count = convex_hull2d(point_count, points2d, hull_indices);
	for (i = 0; i < hull_point_count; i++)
	{
		base->points[i] = points[hull_indices[i]];
		base->convex_hull[i] = points2d[hull_indices[i]];
	}
	base->point_count = hull_point_count;

	minimum = maximum = base->points[0];
	for (i = 0; i < hull_point_count; i++)
	{
		minimum.x = MIN(minimum.x, base->points[i].x);
		minimum.y = MIN(minimum.y, base->points[i].y);
		minimum.z = MIN(minimum.z, base->points[i].z);
		maximum.x = MAX(maximum.x, base->points[i].x);
		maximum.y = MAX(maximum.y, base->points[i].y);
		maximum.z = MAX(maximum.z, base->points[i].z);
	}
	base->bottom = minimum.z - 0.1f;
	base->top = maximum.z + 0.8f;
	base->center.x = (maximum.x + minimum.x)*0.5f;
	base->center.y = (maximum.y + minimum.y)*0.5f;
	base->center.z = (maximum.z + minimum.z)*0.5f;

	return;
}

static boolean conquest_player_inside_base(
	long player_index,
	struct conquest_base const *base)
{
	boolean inside = FALSE;

	if (player_index != NONE && base->point_count > 0)
	{
		struct player_datum *player = player_get(player_index);

		if (player->unit_index != NONE)
		{
			struct unit_datum *unit = unit_get(player->unit_index);
			real_point3d const *position = &unit->object.bounding_sphere_center;

			if (position->z >= base->bottom && position->z <= base->top)
			{
				real_point2d point;

				point.x = position->x;
				point.y = position->y;
				inside = convex_hull2d_test_point(
					base->point_count,
					base->convex_hull,
					&point,
					0.0f);
			}
		}
	}

	return inside;
}

/* each base's waypoint, the stock arrow the other game types use: the
red-owned "default", the blue-owned "flag_blue", the rest "crown_blue" (the
king of the hill's) */
static void conquest_update_hud_goals(
	void)
{
	long base_index;

	for (base_index = 0; base_index < cq.base_count; base_index++)
	{
		struct conquest_base const *base = &cq.bases[base_index];
		real_point3d position = base->center;
		char const *arrow;
		short team_index;

		if (base->capturing_team == CONQUEST_CONTESTED)
		{
			arrow = "crown_blue";
			team_index = CONQUEST_CONTESTED;
		}
		else if (base->state == conquest_base_red)
		{
			arrow = "default";
			team_index = conquest_team_red;
		}
		else if (base->state == conquest_base_blue)
		{
			arrow = "default";
			team_index = conquest_team_blue;
		}
		else
		{
			arrow = "crown_blue";
			team_index = NONE;
		}

		game_engine_set_goal_position(
			(short)base_index,
			&position,
			0.0f,
			arrow,
			NONE,
			team_index,
			NONE);
	}

	return;
}

/* the king of the hill's fence (game_engine_king.c's post_rasterize) of a base */
static void conquest_render_base_fence(
	struct conquest_base const *base,
	long shader_index)
{
	real perimeter = 0.0f;
	real accumulated_distance = 0.0f;
	real inverse_segment_count;
	real texels_per_unit;
	real previous_u = 0.0f;
	long point_count = base->point_count;
	long point_index;

	if (point_count < 2)
		return;

	for (point_index = 0; point_index < point_count; point_index++)
	{
		perimeter += distance3d(
			&base->points[point_index],
			&base->points[point_index + 1 == point_count ? 0 : point_index + 1]);
	}
	if (perimeter < 0.01f)
		return;

	inverse_segment_count = (real)(1.0/floor(perimeter + 0.5f));
	texels_per_unit = 1.0f/(inverse_segment_count*perimeter);

	for (point_index = 0; point_index < point_count; point_index++)
	{
		struct model_vertex_uncompressed vertices[NUMBER_OF_VERTICES_PER_QUADRILATERAL];
		real_vector3d sides[2];
		real_vector3d normal;
		long next_index = point_index + 1 == point_count ? 0 : point_index + 1;
		real edge_length = distance3d(&base->points[point_index], &base->points[next_index]);
		real edge_end_u = (edge_length + accumulated_distance)*texels_per_unit;

		accumulated_distance += edge_length;
		csmemset(vertices, 0, sizeof(vertices));
		vertices[1].position = base->points[point_index];
		vertices[1].position.z += 0.8f;
		vertices[0].position = base->points[point_index];
		vertices[2].position = vertices[3].position = base->points[next_index];
		vertices[2].position.z += 0.8f;

		vector_from_points3d(&vertices[0].position, &vertices[1].position, &sides[0]);
		vector_from_points3d(&vertices[1].position, &vertices[2].position, &sides[1]);
		cross_product3d(&sides[0], &sides[1], &normal);
		normalize3d(&normal);
		vertices[0].normal = vertices[1].normal = vertices[2].normal = vertices[3].normal = normal;

		vertices[0].texcoord.x = previous_u*inverse_segment_count;
		vertices[0].texcoord.y = 1.0f;
		vertices[1].texcoord.x = previous_u*inverse_segment_count;
		vertices[1].texcoord.y = 0.2f;
		vertices[2].texcoord.x = edge_end_u*inverse_segment_count;
		vertices[2].texcoord.y = 0.2f;
		vertices[3].texcoord.x = edge_end_u*inverse_segment_count;
		vertices[3].texcoord.y = 1.0f;
		previous_u = edge_end_u;

		render_dynamic_quad(
			vertices,
			shader_index,
			NULL,
			NULL,
			1.0f/inverse_segment_count,
			1.0f);
	}

	return;
}

static void conquest_check_win_condition(
	void)
{
	if (cq.tickets[conquest_team_red] <= 0 || cq.tickets[conquest_team_blue] <= 0)
		game_engine_end_game();

	return;
}

static void conquest_ticket_sounds(
	long team_index,
	long tickets,
	long previous_tickets)
{
	if (tickets >= previous_tickets)
		return;

	if (previous_tickets > CONQUEST_WARNING_25 && tickets <= CONQUEST_WARNING_25)
	{
		game_engine_play_multiplayer_sound(
			team_index == conquest_team_red ?
				_conquest_sound_red_30_seconds :
				_conquest_sound_blue_30_seconds);
	}
	else if (previous_tickets > CONQUEST_WARNING_50 && tickets <= CONQUEST_WARNING_50)
	{
		game_engine_play_multiplayer_sound(
			team_index == conquest_team_red ?
				_conquest_sound_red_60_seconds :
				_conquest_sound_blue_60_seconds);
	}
	else if (previous_tickets > CONQUEST_WARNING_100 && tickets <= CONQUEST_WARNING_100)
	{
		game_engine_play_multiplayer_sound(_conquest_sound_ting);
	}

	return;
}

long game_engine_conquest_get_base_count(
	void)
{
	return cq.base_count;
}

struct conquest_base const *game_engine_conquest_get_base(
	long index)
{
	if (index >= 0 && index < cq.base_count)
		return &cq.bases[index];
	return NULL;
}
