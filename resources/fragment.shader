#version 120

uniform sampler2D tex;
uniform bool enable_texture;
uniform bool enable_alpha;

uniform bool enable_fog;
uniform vec2 fog_delta;
uniform float fog_distance;
uniform vec3 fog_color;

uniform bool enable_lighting;

/* Blinn-Phong directional term. sun_dir points from the fragment toward the
   light (sun or moon); sun_strength is a 0..1 intensity that tracks the
   day/night brightness curve so moonlight is much weaker than sunlight. */
uniform vec3 sun_dir;
uniform float sun_strength;

varying vec3 v_pos;
varying vec4 v_color;
varying vec2 v_texcoord;
varying float v_sky;

void main() {
	vec4 tex_color = vec4(1.0);

	if(enable_texture)
		tex_color = texture2D(tex, v_texcoord);

	float v_fog = 0.0;

	if(enable_fog)
		v_fog = clamp((length(fog_delta + v_pos.xz) - (fog_distance - 9.0)) / 8.0, 0.0, 1.0);

	vec4 frag = v_color * tex_color;

	if(enable_lighting) {
		/* Voxel faces are axis-aligned, so the cross-product of screen-space
		   position derivatives gives an exact cardinal unit normal for free —
		   no extra vertex attribute, no VBO bloat. */
		vec3 N = normalize(cross(dFdx(v_pos), dFdy(v_pos)));

		/* Lambert diffuse, gated by sky visibility (indoor/cave surfaces don't
		   receive direct sunlight). */
		float NdotL = max(dot(N, sun_dir), 0.0);
		float diffuse = NdotL * sun_strength * v_sky;

		/* Subtle Blinn specular. View direction is approximated by the sun
		   direction — for matte voxel surfaces this collapses specular into a
		   soft rim aligned with the light, which is cheap and visually stable
		   without needing a camera-position uniform per chunk. */
		vec3 H = normalize(sun_dir + vec3(0.0, 1.0, 0.0));
		float spec = pow(max(dot(N, H), 0.0), 16.0) * sun_strength * v_sky * 0.08;

		/* Keep the baked Gouraud lightmap as the base level (1.0 = no change)
		   and add the directional contribution on top, clamped so bright
		   surfaces don't blow out. */
		float shade = clamp(1.0 + diffuse * 0.35 + spec, 0.0, 1.6);
		frag.rgb *= shade;
	}

	gl_FragColor = vec4(mix(frag.rgb, fog_color, v_fog), frag.a);

	if(enable_alpha && gl_FragColor.a < 0.0625)
		discard;
}
