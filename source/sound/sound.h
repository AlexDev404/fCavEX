/*
	Copyright (c) 2026 fCavEX contributors

	This file is part of fCavEX.

	fCavEX is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.
*/

#ifndef SOUND_H
#define SOUND_H

#include <stdbool.h>
#include <stdint.h>

/* High-level sound event system.

   Events are named the same as in the vanilla Minecraft sounds.json
   (e.g. "dig.stone", "random.door_open", "damage.hit1"). Each event maps
   to a list of OGG files; sound_play picks one at random.

   Resource-pack layout (matches vanilla):
	   <resource_pack>/minecraft/sounds.json
	   <resource_pack>/minecraft/sounds/<sub>/<file>.ogg
 */

bool sound_init(void);
void sound_destroy(void);

/* Positional playback in world space. */
void sound_play(const char* event, float x, float y, float z);

/* Non-positional playback (UI, ambient, menu). */
void sound_play_ui(const char* event);

/* Explicit pitch/volume overrides (positional). */
void sound_play_ex(const char* event, float x, float y, float z,
				   float volume, float pitch);

/* Convenience: emit the appropriate dig.<material> sound for a block-type id. */
void sound_play_block_break(uint8_t block_type, float x, float y, float z);
void sound_play_block_place(uint8_t block_type, float x, float y, float z);
void sound_play_step(uint8_t block_type, float x, float y, float z);

/* Update listener position + forward vector. Call once per frame. */
void sound_set_listener(float x, float y, float z,
						float fx, float fy, float fz);

#endif
