/*
	Copyright (c) 2026 fCavEX contributors

	Wii audio backend -- stub. See platform/pc/sound.c for the functional
	implementation. Ported-to-libogc asndlib backend goes here in future.
*/

bool plat_sound_init(void) {
	return false;
}

void plat_sound_destroy(void) {}

void* plat_sound_load_file(const char* absolute_path) {
	(void)absolute_path;
	return NULL;
}

void plat_sound_free(void* handle) {
	(void)handle;
}

void plat_sound_play(void* handle, float volume, float pitch,
					 float x, float y, float z, bool positional) {
	(void)handle;
	(void)volume;
	(void)pitch;
	(void)x;
	(void)y;
	(void)z;
	(void)positional;
}

void plat_sound_listener_set(float x, float y, float z,
							 float fx, float fy, float fz) {
	(void)x;
	(void)y;
	(void)z;
	(void)fx;
	(void)fy;
	(void)fz;
}
