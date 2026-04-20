/*
	Copyright (c) 2026 fCavEX contributors

	Wii audio backend: libogc ASND + stb_vorbis OGG decode. Up to 16
	concurrent voices (ASND hardware limit). Clips are decoded once to
	32-byte-aligned PCM, then fed to ASND_SetVoice on each play call.
*/

#include <gccore.h>
#include <asndlib.h>
#include <malloc.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../../stb/stb_vorbis.c"

struct sound_clip {
	void* pcm;			/* 32-byte aligned, big-endian s16 */
	int channels;		/* 1 or 2 */
	int sample_rate;
	uint32_t byte_size; /* multiple of 32 */
};

static bool g_initialized = false;
static float g_lx = 0.0f, g_ly = 0.0f, g_lz = 0.0f;
static float g_fx = 0.0f, g_fy = 0.0f, g_fz = -1.0f;

static uint32_t round_up_32(uint32_t n) {
	return (n + 31u) & ~31u;
}

bool plat_sound_init(void) {
	ASND_Init();
	ASND_Pause(0);
	g_initialized = true;
	return true;
}

void plat_sound_destroy(void) {
	if(!g_initialized)
		return;
	ASND_Pause(1);
	ASND_End();
	g_initialized = false;
}

void* plat_sound_load_file(const char* path) {
	if(!path)
		return NULL;

	int channels = 0;
	int sample_rate = 0;
	short* pcm = NULL;
	int samples = stb_vorbis_decode_filename(path, &channels, &sample_rate, &pcm);
	if(samples <= 0 || !pcm || channels < 1 || channels > 2) {
		if(pcm)
			free(pcm);
		return NULL;
	}

	uint32_t raw_bytes = (uint32_t)samples * (uint32_t)channels * sizeof(short);
	uint32_t aligned = round_up_32(raw_bytes);
	void* buf = memalign(32, aligned);
	if(!buf) {
		free(pcm);
		return NULL;
	}
	memcpy(buf, pcm, raw_bytes);
	if(aligned > raw_bytes)
		memset((char*)buf + raw_bytes, 0, aligned - raw_bytes);
	free(pcm);

	/* ASND reads via DSP DMA — flush CPU caches so the DSP sees the data. */
	DCFlushRange(buf, aligned);

	struct sound_clip* clip = malloc(sizeof(*clip));
	if(!clip) {
		free(buf);
		return NULL;
	}
	clip->pcm = buf;
	clip->channels = channels;
	clip->sample_rate = sample_rate;
	clip->byte_size = aligned;
	return clip;
}

void plat_sound_free(void* handle) {
	struct sound_clip* clip = handle;
	if(!clip)
		return;
	free(clip->pcm);
	free(clip);
}

/* Same spatial model as the PC backend — inverse-square distance falloff
   plus equal-gain horizontal pan, then mapped into ASND's 0..255 range. */
static void spatial_gain(float sx, float sy, float sz, float vol,
						 int* vol_l, int* vol_r) {
	float dx = sx - g_lx;
	float dy = sy - g_ly;
	float dz = sz - g_lz;
	float dist = sqrtf(dx * dx + dy * dy + dz * dz);

	float atten = 16.0f / (16.0f + dist * dist);
	if(atten > 1.0f) atten = 1.0f;
	float gain = vol * atten;

	float rx = g_fz;
	float rz = -g_fx;
	float rlen = sqrtf(rx * rx + rz * rz);
	float pan = 0.0f;
	if(rlen > 0.0001f && dist > 0.0001f)
		pan = (dx * rx + dz * rz) / (dist * rlen); /* -1..1 */

	float right = 0.5f + 0.5f * pan; /* 0..1 */
	float lv = gain * (1.0f - right);
	float rv = gain * right;
	if(lv < 0.0f) lv = 0.0f;
	if(lv > 1.0f) lv = 1.0f;
	if(rv < 0.0f) rv = 0.0f;
	if(rv > 1.0f) rv = 1.0f;
	*vol_l = (int)(lv * 255.0f);
	*vol_r = (int)(rv * 255.0f);
}

void plat_sound_play(void* handle, float volume, float pitch,
					 float x, float y, float z, bool positional) {
	if(!g_initialized)
		return;
	struct sound_clip* clip = handle;
	if(!clip)
		return;
	if(pitch <= 0.0f) pitch = 1.0f;
	if(volume < 0.0f) volume = 0.0f;

	int vl, vr;
	if(positional) {
		spatial_gain(x, y, z, volume, &vl, &vr);
		if(vl == 0 && vr == 0)
			return;
	} else {
		int v = (int)(volume * 255.0f);
		if(v > 255) v = 255;
		if(v < 0) v = 0;
		vl = vr = v;
	}

	int voice = ASND_GetFirstUnusedVoice();
	if(voice < 0)
		return;

	int format = (clip->channels == 2) ? VOICE_STEREO_16BIT : VOICE_MONO_16BIT;

	/* ASND's "pitch" argument is the actual playback sample rate in Hz. */
	int rate = (int)((float)clip->sample_rate * pitch);
	if(rate < 1000) rate = 1000;
	if(rate > 144000) rate = 144000;

	ASND_SetVoice(voice, format, rate, 0,
				  clip->pcm, (int)clip->byte_size,
				  vl, vr, NULL);
}

void plat_sound_listener_set(float x, float y, float z,
							 float fx, float fy, float fz) {
	g_lx = x;
	g_ly = y;
	g_lz = z;
	g_fx = fx;
	g_fy = fy;
	g_fz = fz;
}
