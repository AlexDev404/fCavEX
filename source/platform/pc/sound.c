/*
	Copyright (c) 2026 fCavEX contributors

	PC audio backend: miniaudio device + custom software mixer, OGG Vorbis
	decoded via stb_vorbis. Decoded PCM is cached in memory per clip; each
	playback allocates a "voice" from a fixed pool.
*/

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define STB_VORBIS_HEADER_ONLY
#include "../../stb/stb_vorbis.c"

#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MA_NO_DECODING
#define MINIAUDIO_IMPLEMENTATION
#include "../../miniaudio/miniaudio.h"

/* Expose the stb_vorbis implementation after miniaudio so stb_vorbis'
   static helpers don't collide with miniaudio internals. */
#undef STB_VORBIS_HEADER_ONLY
#include "../../stb/stb_vorbis.c"

#define SOUND_MAX_VOICES 32
#define SOUND_OUTPUT_CHANNELS 2
#define SOUND_OUTPUT_RATE 44100

struct sound_clip {
	int16_t* pcm;	/* interleaved s16 */
	int channels;	/* 1 or 2 */
	int sample_rate;
	size_t frames;
};

struct voice {
	struct sound_clip* clip; /* NULL = inactive */
	double cursor;			 /* fractional frame index, allows resampling */
	float volume_l;
	float volume_r;
	double rate_ratio;		 /* clip_rate / device_rate * pitch */
};

static ma_device g_device;
static ma_mutex g_mutex;
static struct voice g_voices[SOUND_MAX_VOICES];
static bool g_initialized = false;

static float g_listener_x, g_listener_y, g_listener_z;
static float g_listener_fx = 0.0f, g_listener_fy = 0.0f, g_listener_fz = -1.0f;

static inline int16_t clamp_i16(int32_t v) {
	if(v > 32767) return 32767;
	if(v < -32768) return -32768;
	return (int16_t)v;
}

static void mix_callback(ma_device* device, void* output, const void* input,
						 ma_uint32 frame_count) {
	(void)device;
	(void)input;
	int16_t* out = (int16_t*)output;
	memset(out, 0, frame_count * SOUND_OUTPUT_CHANNELS * sizeof(int16_t));

	ma_mutex_lock(&g_mutex);
	for(int vi = 0; vi < SOUND_MAX_VOICES; vi++) {
		struct voice* v = &g_voices[vi];
		if(!v->clip)
			continue;
		struct sound_clip* c = v->clip;

		for(ma_uint32 f = 0; f < frame_count; f++) {
			size_t idx = (size_t)v->cursor;
			if(idx >= c->frames) {
				v->clip = NULL;
				break;
			}
			int16_t sL, sR;
			if(c->channels == 1) {
				sL = sR = c->pcm[idx];
			} else {
				sL = c->pcm[idx * 2];
				sR = c->pcm[idx * 2 + 1];
			}
			int32_t mL = out[f * 2 + 0] + (int32_t)(sL * v->volume_l);
			int32_t mR = out[f * 2 + 1] + (int32_t)(sR * v->volume_r);
			out[f * 2 + 0] = clamp_i16(mL);
			out[f * 2 + 1] = clamp_i16(mR);
			v->cursor += v->rate_ratio;
		}
	}
	ma_mutex_unlock(&g_mutex);
}

bool plat_sound_init(void) {
	ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
	cfg.playback.format = ma_format_s16;
	cfg.playback.channels = SOUND_OUTPUT_CHANNELS;
	cfg.sampleRate = SOUND_OUTPUT_RATE;
	cfg.dataCallback = mix_callback;

	if(ma_device_init(NULL, &cfg, &g_device) != MA_SUCCESS)
		return false;
	if(ma_mutex_init(&g_mutex) != MA_SUCCESS) {
		ma_device_uninit(&g_device);
		return false;
	}
	if(ma_device_start(&g_device) != MA_SUCCESS) {
		ma_mutex_uninit(&g_mutex);
		ma_device_uninit(&g_device);
		return false;
	}
	memset(g_voices, 0, sizeof(g_voices));
	g_initialized = true;
	return true;
}

void plat_sound_destroy(void) {
	if(!g_initialized)
		return;
	ma_device_stop(&g_device);
	ma_mutex_uninit(&g_mutex);
	ma_device_uninit(&g_device);
	g_initialized = false;
}

void* plat_sound_load_file(const char* absolute_path) {
	if(!absolute_path)
		return NULL;

	int channels = 0;
	int sample_rate = 0;
	short* pcm = NULL;
	int samples
		= stb_vorbis_decode_filename(absolute_path, &channels, &sample_rate, &pcm);
	if(samples <= 0 || !pcm || channels < 1 || channels > 2)
		return NULL;

	struct sound_clip* clip = malloc(sizeof(*clip));
	if(!clip) {
		free(pcm);
		return NULL;
	}
	clip->pcm = pcm;
	clip->channels = channels;
	clip->sample_rate = sample_rate;
	clip->frames = (size_t)samples;
	return clip;
}

void plat_sound_free(void* handle) {
	struct sound_clip* clip = handle;
	if(!clip)
		return;
	free(clip->pcm);
	free(clip);
}

/* Compute stereo volumes from source position relative to listener.
   Equal-gain horizontal pan, inverse-square distance attenuation. */
static void spatial_gain(float sx, float sy, float sz, float vol,
						 float* volL, float* volR) {
	float dx = sx - g_listener_x;
	float dy = sy - g_listener_y;
	float dz = sz - g_listener_z;
	float dist = sqrtf(dx * dx + dy * dy + dz * dz);

	float atten = 16.0f / (16.0f + dist * dist);
	if(atten > 1.0f) atten = 1.0f;
	float gain = vol * atten;

	/* listener-relative right vector (forward x up, up=(0,1,0)) */
	float rx = g_listener_fz;
	float rz = -g_listener_fx;
	float rlen = sqrtf(rx * rx + rz * rz);
	float pan = 0.0f;
	if(rlen > 0.0001f && dist > 0.0001f) {
		pan = (dx * rx + dz * rz) / (dist * rlen); /* -1..1 */
	}

	float right = 0.5f + 0.5f * pan; /* 0..1 */
	*volL = gain * (1.0f - right);
	*volR = gain * right;
}

void plat_sound_play(void* handle, float volume, float pitch,
					 float x, float y, float z, bool positional) {
	if(!g_initialized)
		return;
	struct sound_clip* clip = handle;
	if(!clip)
		return;
	if(pitch <= 0.0f)
		pitch = 1.0f;
	if(volume < 0.0f)
		volume = 0.0f;

	float volL, volR;
	if(positional) {
		spatial_gain(x, y, z, volume, &volL, &volR);
		if(volL <= 0.0001f && volR <= 0.0001f)
			return;
	} else {
		volL = volR = volume;
	}

	ma_mutex_lock(&g_mutex);
	struct voice* slot = NULL;
	for(int i = 0; i < SOUND_MAX_VOICES; i++) {
		if(!g_voices[i].clip) {
			slot = &g_voices[i];
			break;
		}
	}
	if(slot) {
		slot->clip = clip;
		slot->cursor = 0.0;
		slot->volume_l = volL;
		slot->volume_r = volR;
		slot->rate_ratio
			= ((double)clip->sample_rate / (double)SOUND_OUTPUT_RATE) * pitch;
	}
	ma_mutex_unlock(&g_mutex);
}

void plat_sound_listener_set(float x, float y, float z,
							 float fx, float fy, float fz) {
	g_listener_x = x;
	g_listener_y = y;
	g_listener_z = z;
	g_listener_fx = fx;
	g_listener_fy = fy;
	g_listener_fz = fz;
}
