/*
	Copyright (c) 2026 fCavEX contributors

	This file is part of fCavEX.

	fCavEX is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.
*/

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sound.h"

#include "../block/blocks_data.h"
#include "../config.h"
#include "../game/game_state.h"
#include "../parson/parson.h"
#include "../platform/sound.h"

#define MAX_EVENTS 256
#define MAX_VARIANTS 16
#define MAX_EVENT_NAME 48
#define MAX_PATH 160

struct sound_event {
	char name[MAX_EVENT_NAME];
	void* variants[MAX_VARIANTS]; /* platform handles, some may be NULL */
	int variant_count;
};

static struct sound_event g_events[MAX_EVENTS];
static int g_event_count = 0;
static bool g_enabled = false;
static char g_resource_root[128];

static struct sound_event* find_event(const char* name) {
	for(int i = 0; i < g_event_count; i++) {
		if(strcmp(g_events[i].name, name) == 0)
			return &g_events[i];
	}
	return NULL;
}

/* Lazy-add an event with a list of sound file ids (paths w/o .ogg extension,
   relative to <resource_root>/minecraft/sounds/). */
static void register_event(const char* name, const char* const* ids, int count) {
	if(g_event_count >= MAX_EVENTS)
		return;
	if(find_event(name))
		return;

	struct sound_event* ev = &g_events[g_event_count];
	strncpy(ev->name, name, MAX_EVENT_NAME - 1);
	ev->name[MAX_EVENT_NAME - 1] = '\0';
	ev->variant_count = 0;

	int n = count < MAX_VARIANTS ? count : MAX_VARIANTS;
	for(int i = 0; i < n; i++) {
		char path[MAX_PATH];
		snprintf(path, sizeof(path), "%s/minecraft/sounds/%s.ogg",
				 g_resource_root, ids[i]);
		void* handle = plat_sound_load_file(path);
		if(handle) {
			ev->variants[ev->variant_count++] = handle;
		}
	}

	if(ev->variant_count > 0)
		g_event_count++;
}

static bool parse_sounds_json(const char* path) {
	JSON_Value* root = json_parse_file(path);
	if(!root)
		return false;
	if(json_value_get_type(root) != JSONObject) {
		json_value_free(root);
		return false;
	}

	JSON_Object* obj = json_value_get_object(root);
	size_t count = json_object_get_count(obj);
	for(size_t i = 0; i < count; i++) {
		const char* event_name = json_object_get_name(obj, i);
		JSON_Value* entry = json_object_get_value_at(obj, i);
		if(!event_name || !entry
		   || json_value_get_type(entry) != JSONObject)
			continue;
		JSON_Array* sounds
			= json_object_get_array(json_value_get_object(entry), "sounds");
		if(!sounds)
			continue;

		size_t n = json_array_get_count(sounds);
		if(n > MAX_VARIANTS)
			n = MAX_VARIANTS;

		const char* ids[MAX_VARIANTS];
		int id_count = 0;
		for(size_t j = 0; j < n; j++) {
			JSON_Value* v = json_array_get_value(sounds, j);
			const char* s = NULL;
			if(json_value_get_type(v) == JSONString) {
				s = json_value_get_string(v);
			} else if(json_value_get_type(v) == JSONObject) {
				/* vanilla format allows {name: "...", ...} */
				s = json_object_get_string(json_value_get_object(v), "name");
			}
			if(s)
				ids[id_count++] = s;
		}
		if(id_count > 0)
			register_event(event_name, ids, id_count);
	}

	json_value_free(root);
	return true;
}

bool sound_init(void) {
	if(!plat_sound_init())
		return false;

	const char* root = config_read_string(&gstate.config_user,
										  "paths.texturepack", "assets");
	strncpy(g_resource_root, root, sizeof(g_resource_root) - 1);
	g_resource_root[sizeof(g_resource_root) - 1] = '\0';

	char json_path[MAX_PATH];
	snprintf(json_path, sizeof(json_path), "%s/minecraft/sounds.json",
			 g_resource_root);

	if(!parse_sounds_json(json_path)) {
		/* Non-fatal: keep the subsystem up so missing events just go silent. */
		fprintf(stderr, "[sound] sounds.json not found at %s, audio disabled\n",
				json_path);
	}

	g_enabled = true;
	return true;
}

void sound_destroy(void) {
	if(!g_enabled)
		return;
	for(int i = 0; i < g_event_count; i++) {
		for(int j = 0; j < g_events[i].variant_count; j++) {
			plat_sound_free(g_events[i].variants[j]);
		}
	}
	g_event_count = 0;
	plat_sound_destroy();
	g_enabled = false;
}

static void* pick_variant(const char* event) {
	struct sound_event* ev = find_event(event);
	if(!ev || ev->variant_count == 0)
		return NULL;
	int idx = rand() % ev->variant_count;
	return ev->variants[idx];
}

void sound_play_ex(const char* event, float x, float y, float z,
				   float volume, float pitch) {
	if(!g_enabled || !event)
		return;
	void* handle = pick_variant(event);
	if(!handle)
		return;
	plat_sound_play(handle, volume, pitch, x, y, z, true);
}

void sound_play(const char* event, float x, float y, float z) {
	sound_play_ex(event, x, y, z, 1.0f, 1.0f);
}

void sound_play_ui(const char* event) {
	if(!g_enabled || !event)
		return;
	void* handle = pick_variant(event);
	if(!handle)
		return;
	plat_sound_play(handle, 1.0f, 1.0f, 0, 0, 0, false);
}

void sound_set_listener(float x, float y, float z,
						float fx, float fy, float fz) {
	if(!g_enabled)
		return;
	plat_sound_listener_set(x, y, z, fx, fy, fz);
}

/* --- block type -> dig event mapping ---
   Roughly mirrors the Beta 1.7.3 Block.stepSound assignment. */
static const char* dig_event_for_block(uint8_t type) {
	switch(type) {
		case BLOCK_AIR:
			return NULL;

		/* "grass"/foliage step -> soft crunch */
		case BLOCK_GRASS:
		case BLOCK_DIRT:
		case BLOCK_LEAVES:
		case BLOCK_SAPLING:
		case BLOCK_TALL_GRASS:
		case BLOCK_FLOWER:
		case BLOCK_ROSE:
		case BLOCK_BROWM_MUSHROOM:
		case BLOCK_RED_MUSHROOM:
		case BLOCK_FARMLAND:
		case BLOCK_CROPS:
		case BLOCK_REED:
		case BLOCK_CACTUS:
			return "dig.grass";

		/* wood */
		case BLOCK_PLANKS:
		case BLOCK_LOG:
		case BLOCK_CHEST:
		case BLOCK_WORKBENCH:
		case BLOCK_BOOKSHELF:
		case BLOCK_NOTEBLOCK:
		case BLOCK_JUKEBOX:
		case BLOCK_FENCE:
		case BLOCK_SIGN:
		case BLOCK_LADDER:
		case BLOCK_DOOR_WOOD:
		case BLOCK_TRAP_DOOR:
		case BLOCK_WOODEN_STAIRS:
		case BLOCK_SLAB:
		case BLOCK_DOUBLE_SLAB:
		case BLOCK_PUMPKIN:
		case BLOCK_PUMPKIN_LIT:
		case BLOCK_IRON_CHEST:
			return "dig.wood";

		/* sand */
		case BLOCK_SAND:
			return "dig.sand";

		/* gravel uses the "gravel" sound */
		case BLOCK_GRAVEL:
		case BLOCK_CLAY:
			return "dig.gravel";

		/* wool / cloth */
		case BLOCK_WOOL:
		case BLOCK_BED:
			return "dig.cloth";

		/* snow */
		case BLOCK_SNOW:
		case BLOCK_SNOW_BLOCK:
			return "dig.snow";

		/* glass breaks with a different event */
		case BLOCK_GLASS:
		case BLOCK_ICE:
			return "random.glass";

		/* everything else: stone-like */
		default:
			return "dig.stone";
	}
}

void sound_play_block_break(uint8_t block_type, float x, float y, float z) {
	const char* ev = dig_event_for_block(block_type);
	if(ev)
		sound_play(ev, x, y, z);
}

void sound_play_block_place(uint8_t block_type, float x, float y, float z) {
	const char* ev = dig_event_for_block(block_type);
	if(ev)
		sound_play(ev, x, y, z);
}

void sound_play_step(uint8_t block_type, float x, float y, float z) {
	const char* dig = dig_event_for_block(block_type);
	if(!dig || strncmp(dig, "dig.", 4) != 0)
		return;
	char buf[32];
	snprintf(buf, sizeof(buf), "step.%s", dig + 4);
	sound_play_ex(buf, x, y, z, 0.15f, 1.0f);
}

void sound_play_block_dig(uint8_t block_type, float x, float y, float z) {
	const char* ev = dig_event_for_block(block_type);
	if(ev)
		sound_play_ex(ev, x, y, z, 0.25f, 0.5f);
}
