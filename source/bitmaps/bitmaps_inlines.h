/*
BITMAPS_INLINES.H

file has inline function assertions.
*/

#ifndef __BITMAPS_INLINES_H
#define __BITMAPS_INLINES_H
#pragma once

/* ---------- headers */

#include "cseries/cseries.h"
#include "bitmaps/bitmaps.h"
#include "math/integer_math.h"
#include "math/real_math.h"
#include "effects/particles.h"

/* ---------- constants */

/* ---------- macros */

#define match_assert_valid_real_rgb_color(file, line, rgb) \
	match_vwarn( \
		file, \
		line, \
		valid_real_rgb_color(rgb), \
		csprintf( \
			temporary, \
			"%s: assert_valid_real_rgb_color(%f, %f, %f)", \
			#rgb, \
			(*rgb).red, \
			(*rgb).green, \
			(*rgb).blue))

/* ---------- structures */

/* ---------- prototypes/EXAMPLE.C */

/* ---------- globals */

/* ---------- public code */

__inline pixel32 real_argb_color_to_pixel32(
	real_argb_color const *color)
{
	real scale = 255.0f;
	real alpha = PIN(color->alpha, 0.0f, 1.0f);
	real red = PIN(color->red, 0.0f, 1.0f);
	real green = PIN(color->green, 0.0f, 1.0f);
	real blue = PIN(color->blue, 0.0f, 1.0f);
	pixel32 result;

	match_vwarn(
		"..\\bitmaps\\bitmaps_inlines.h",
		89,
		valid_real_argb_color(color),
		csprintf(
			temporary,
			"%s: assert_valid_real_argb_color(%f, %f, %f, %f)",
			"color",
			color->alpha,
			color->red,
			color->green,
			color->blue));

	result = (pixel32)(
		((long)__builtin_rint((double)blue * scale) & 0xff) |
		(((long)__builtin_rint((double)green * scale) & 0xff) << 8) |
		(((long)__builtin_rint((double)red * scale) & 0xff) << 16) |
		((long)__builtin_rint((double)alpha * scale) << 24));

	return result;
}

__inline pixel32 real_rgb_color_to_pixel32(
	real_rgb_color const *color)
{
	pixel32 result;
	real scale = (real)UNSIGNED_CHAR_MAX;
	real blue = PIN(color->blue, 0.0f, 1.0f);
	real green = PIN(color->green, 0.0f, 1.0f);
	real red = PIN(color->red, 0.0f, 1.0f);

	match_assert_valid_real_rgb_color("..\\bitmaps\\bitmaps_inlines.h", 0xC9, color);

	result = (pixel32)(
		((long)__builtin_rint((double)blue * scale) & 0xff) |
		(((long)__builtin_rint((double)green * scale) & 0xff) << 8) |
		(((long)__builtin_rint((double)red * scale) & 0xff) << 16));

	return result;
}

/* January retains an out-of-line copy of this inline in
 * rasterizer_xbox_active_camouflage.obj. Its assertion records this header
 * and line 291. The stack-local scale, 32-bit FISTP, and in-memory shift are
 * the characteristic packet of the original small x87 helper. */
__inline pixel32 real_alpha_to_pixel32(
	real alpha)
{
	real scale = 255.0f;
	real pinned_alpha = PIN(alpha, 0.0f, 1.0f);
	pixel32 result;

	match_warn(
		"..\\bitmaps\\bitmaps_inlines.h",
		291,
		alpha>=0.0f && alpha<=1.0f);

	result = (pixel32)((long)__builtin_rint((double)pinned_alpha * scale) << 24);

	return result;
}
#endif // __BITMAPS_INLINES_H
