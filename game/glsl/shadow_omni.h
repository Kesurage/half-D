/*
shadow_omni.h - shadow for omnidirectional dlights and PCF filtering
Copyright (C) 2015 Uncle Mike
This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.
This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#ifndef SHADOW_OMNI_H
#define SHADOW_OMNI_H

#define NUM_SAMPLES		4.0

#ifdef GL4ES
// half-D: GLSL ES 1.00 has no samplerCubeShadow/shadowCube (GLSL 1.30).
// The omnidirectional shadow cubemap is sampled as a regular samplerCube
// and the depth comparison is done manually — see shadow_proj.h
uniform samplerCube	u_ShadowMap;
vec2 shadowCube_hdes( vec4 c )
{
	return vec2( step( c.w, textureCube( u_ShadowMap, c.xyz ).r ));
}
#define shadowCube( tex, c ) shadowCube_hdes( c )
#else
uniform samplerCubeShadow	u_ShadowMap;
#endif

#define SHADOW_VOGEL_DISK  // uncomment this to enable smoothing

float depthCube( in vec3 coord, const float scale, const float bias )
{
	float fs_z = max( abs( coord.x ), max( abs( coord.y ), abs( coord.z ) ) ) - 4.0;	// acne remove factor
	float depth = ((fs_z * scale + bias) / fs_z) * 0.5 + 0.5;

	return shadowCube( u_ShadowMap, vec4( coord, depth ) ).r;
}

float ShadowOmni( in vec3 I, in vec4 params )
{
#if defined( SHADOW_PCF2X2 ) || defined( SHADOW_PCF3X3 ) || defined( SHADOW_VOGEL_DISK )
	vec3 forward, right, up;
	forward = normalize( I );
	MakeNormalVectors( forward, right, up );

	float shadow = 0.0;
#endif

#if defined( SHADOW_PCF2X2 ) || defined( SHADOW_PCF3X3 )
#if defined( SHADOW_PCF2X2 )
	float filterWidth = params.x * length( I ) * 2.0;	// PCF2X2
#elif defined( SHADOW_PCF3X3 )
	float filterWidth = params.x * length( I ) * 3.0;	// PCF3X3
#else	// dead code
	float filterWidth = params.x * length( I );	 // Hardware PCF1X1
#endif
	// compute step size for iterating through the kernel
	float stepSize = NUM_SAMPLES * filterWidth / NUM_SAMPLES;

	for( float i = -filterWidth; i < filterWidth; i += stepSize )
	{
		for( float j = -filterWidth; j < filterWidth; j += stepSize )
		{
			shadow += depthCube( I + right * i + up * j, params.z, params.w );
		}
	}

	// return average of the samples
	shadow *= (4.0 / (NUM_SAMPLES * NUM_SAMPLES));
	return shadow;
#elif defined( SHADOW_VOGEL_DISK )
	const float stepSize = 0.8;//1.1;
	float rotation = InterleavedGradientNoise( gl_FragCoord.xy ) * 6.2832;

	vec2 vogel;
	vogel = stepSize * VogelDiskSample( 0, 16, rotation );
	shadow += depthCube( I + right * vogel.x + up * vogel.y, params.z, params.w );
	vogel = stepSize * VogelDiskSample( 1, 16, rotation );
	shadow += depthCube( I + right * vogel.x + up * vogel.y, params.z, params.w );
	vogel = stepSize * VogelDiskSample( 2, 16, rotation );
	shadow += depthCube( I + right * vogel.x + up * vogel.y, params.z, params.w );
	vogel = stepSize * VogelDiskSample( 3, 16, rotation );
	shadow += depthCube( I + right * vogel.x + up * vogel.y, params.z, params.w );
	vogel = stepSize * VogelDiskSample( 4, 16, rotation );
	shadow += depthCube( I + right * vogel.x + up * vogel.y, params.z, params.w );
	vogel = stepSize * VogelDiskSample( 5, 16, rotation );
	shadow += depthCube( I + right * vogel.x + up * vogel.y, params.z, params.w );
	vogel = stepSize * VogelDiskSample( 6, 16, rotation );
	shadow += depthCube( I + right * vogel.x + up * vogel.y, params.z, params.w );
	vogel = stepSize * VogelDiskSample( 7, 16, rotation );
	shadow += depthCube( I + right * vogel.x + up * vogel.y, params.z, params.w );
	vogel = stepSize * VogelDiskSample( 8, 16, rotation );
	shadow += depthCube( I + right * vogel.x + up * vogel.y, params.z, params.w );
	vogel = stepSize * VogelDiskSample( 9, 16, rotation );
	shadow += depthCube( I + right * vogel.x + up * vogel.y, params.z, params.w );
	vogel = stepSize * VogelDiskSample( 10, 16, rotation );
	shadow += depthCube( I + right * vogel.x + up * vogel.y, params.z, params.w );
	vogel = stepSize * VogelDiskSample( 11, 16, rotation );
	shadow += depthCube( I + right * vogel.x + up * vogel.y, params.z, params.w );
	vogel = stepSize * VogelDiskSample( 12, 16, rotation );
	shadow += depthCube( I + right * vogel.x + up * vogel.y, params.z, params.w );
	vogel = stepSize * VogelDiskSample( 13, 16, rotation );
	shadow += depthCube( I + right * vogel.x + up * vogel.y, params.z, params.w );
	vogel = stepSize * VogelDiskSample( 14, 16, rotation );
	shadow += depthCube( I + right * vogel.x + up * vogel.y, params.z, params.w );
	vogel = stepSize * VogelDiskSample( 15, 16, rotation );
	shadow += depthCube( I + right * vogel.x + up * vogel.y, params.z, params.w );

	shadow *= 0.0625;
	return shadow;
#else
	return depthCube( I, params.z, params.w ); // no shadow smoothing at all
#endif
}

#endif//SHADOW_OMNI_H