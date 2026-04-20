/*
	Copyright (c) 2026 fCavEX contributors

	This file is part of fCavEX.

	fCavEX is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.
*/

#ifndef PLATFORM_SOUND_H
#define PLATFORM_SOUND_H

#include <stdbool.h>

/* Platform backend interface for audio playback.
   Implementations live in platform/pc/sound.c and platform/wii/sound.c.
   Sound.c (the agnostic frontend) is the only caller. */

bool plat_sound_init(void);
void plat_sound_destroy(void);

/* Load an OGG Vorbis file from disk. Returns opaque handle or NULL on failure. */
void* plat_sound_load_file(const char* absolute_path);
void plat_sound_free(void* handle);

/* Play a loaded sound. Pitch of 1.0f plays at native rate.
   If positional, (x,y,z) is the world-space source position. */
void plat_sound_play(void* handle, float volume, float pitch,
					 float x, float y, float z, bool positional);

/* Update listener position + forward vector (used for 3D attenuation & panning). */
void plat_sound_listener_set(float x, float y, float z,
							 float fx, float fy, float fz);

#endif
