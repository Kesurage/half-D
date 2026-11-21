#!/usr/bin/env python3
"""v0.4.1 patch — the round the crash died (hopefully).

v0.4.0 device log verdict:
- Verified working: no more shadow-texture errors, FiraSans menu font,
  program_binary exposed, precompile ran (832 compiled), BC7 raindrop
  confirmed (bptc no, format BC7 SRGB).
- STILL CRASHING when selecting a graphics option. Root cause: the video
  options presets (menus/VideoOptions.cpp) set r_bloom 1, gl_sunshafts 1,
  r_blur 1, gl_smaa 1, gl_water_refraction 1, gl_hbao 1 ... — every one of
  those re-enables a per-frame fullscreen pglCopyTexSubImage2D readback
  path that the mobile preset had turned off at init. Select option ->
  multi-second frames -> GPU watchdog kills game and device.
  FIX A: one hard gate (R_AllowScreenCaptureFX) in all 11 capture
  functions; on the GL 2.x path they stay off no matter what the cvars
  say. gl_mobile_quality 0 opts back in.

- "Loaded 832 shaders: 279 failed to preload" with ZERO error output:
  ALERT() is a no-op without developer mode, so compile/link/load errors
  are all silent. Failure classes identified by code analysis: 169
  HAS_SHADOWS + 196 SUN_SHADOW permutations use sampler2DShadow /
  shadow2DOffset, which gl4es cannot translate (the GL_ES fallbacks in
  shadow_*.h don't apply because gl4es presents desktop GL 2.1). Failed
  permutations are NOT added to the hash table, so if the runtime ever
  requests one it retries a full driver compile EVERY FRAME.
  FIX B: negative cache (failed permutations are remembered and never
  retried this session) + always-on throttled logging of every failure
  with name/options/stage.
  FIX C: precompile skips device-impossible permutations (shadow-sampler
  and texture-array defines) — ~413 of 1111 entries never compiled.

- 25.45 s world load: the heap-stage timers printed NOTHING, so the time
  is before the heap stages. FIX D: time FS_LoadFile (size+duration),
  lump byte-swap, FindBSPX, the raw-lump loading loops and the studio
  cache purge.

- FIX E: ship an uncompressed BGRA raindrop.dds (BC7 original is
  undecodable on Mali/gl4es). Built by tools/make_raindrop.py, packaged
  into the libs zip by CI, overrides the game-data file via VFS order.

- FIX F: heartbeat now reports dips/passes so the next log shows whether
  the ~100 ms scene stage is draw-call bound or geometry bound.
"""
import sys, os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
applied, skipped = [], []

def patch(relpath, old, new, name, count=1):
    path = os.path.join(ROOT, relpath)
    with open(path, 'r', encoding='utf-8') as f:
        text = f.read()
    if new in text:
        skipped.append(name)
        return
    n = text.count(old)
    if n == 0:
        print("FAIL [%s]: anchor not found in %s:\n---\n%s\n---" % (name, relpath, old[:300]))
        sys.exit(1)
    if count and n != count:
        print("FAIL [%s]: anchor found %d times (expected %d) in %s" % (name, n, count, relpath))
        sys.exit(1)
    text = text.replace(old, new)
    with open(path, 'w', encoding='utf-8') as f:
        f.write(text)
    applied.append(name)
    print("ok  [%s] %s" % (name, relpath))

# ---------------------------------------------------------------------------
# FIX A: hard gate for every per-frame screen-capture effect
# ---------------------------------------------------------------------------
patch("game/client/render/r_cvars.h",
"""extern cvar_t *ui_forcenoblur;
""",
"""extern cvar_t *ui_forcenoblur;
extern cvar_t *gl_mobile_quality; // half-D: mobile quality preset master switch
""", "gl_mobile_quality extern")

patch("game/client/render/r_postprocess.cpp",
"""void RenderSunShafts( void )
{
	if( !CVAR_TO_BOOL( gl_sunshafts ) || !tr.screen_depth || !tr.screen_color || !target_rgb[0] )
""",
"""// half-D: every fullscreen pglCopyTexSubImage2D is a CPU readback through
// gl4es (0.5-13 s per frame on Mali — GPU watchdog resets). The mobile
// quality preset turns these effects off, but the video options presets
// re-enable them via archived cvars, which is what crashed the device the
// moment a graphics option was selected. This hard gate ignores the cvars
// on the GL 2.x path; set gl_mobile_quality 0 to opt back in.
static bool R_AllowScreenCaptureFX( void )
{
	if( glConfig.version >= 3.0f )
		return true;
	return !CVAR_TO_BOOL( gl_mobile_quality );
}

void RenderSunShafts( void )
{
	if( !R_AllowScreenCaptureFX() )
		return;

	if( !CVAR_TO_BOOL( gl_sunshafts ) || !tr.screen_depth || !tr.screen_color || !target_rgb[0] )
""", "capture gate: helper + RenderSunShafts")

patch("game/client/render/r_postprocess.cpp",
"""void MotionBlur(void)
{
	static float drunk_amount = 0.0f;
""",
"""void MotionBlur(void)
{
	if( !R_AllowScreenCaptureFX() )
		return;

	static float drunk_amount = 0.0f;
""", "capture gate: MotionBlur")

patch("game/client/render/r_postprocess.cpp",
"""void RenderGaussBlur( float blur1, float blur2 )
{
	if( (blur1 <= 0.0f) && (blur2 <= 0.0f) )
""",
"""void RenderGaussBlur( float blur1, float blur2 )
{
	if( !R_AllowScreenCaptureFX() )
		return;

	if( (blur1 <= 0.0f) && (blur2 <= 0.0f) )
""", "capture gate: RenderGaussBlur")

patch("game/client/render/r_postprocess.cpp",
"""void HorizontalBlur( void )
{
	static float Accum = 0.0f;
""",
"""void HorizontalBlur( void )
{
	if( !R_AllowScreenCaptureFX() )
		return;

	static float Accum = 0.0f;
""", "capture gate: HorizontalBlur")

patch("game/client/render/r_postprocess.cpp",
"""void ScreenWater(void)
{
	float Speed = gEngfuncs.GetLocalPlayer()->curstate.vuser1.x;
""",
"""void ScreenWater(void)
{
	if( !R_AllowScreenCaptureFX() )
		return;

	float Speed = gEngfuncs.GetLocalPlayer()->curstate.vuser1.x;
""", "capture gate: ScreenWater")

patch("game/client/render/r_postprocess.cpp",
"""void Glitch( void )
{
	if( gHUD.GlitchAmount > 0.0f && tr.time > gHUD.GlitchHoldTime )
""",
"""void Glitch( void )
{
	if( !R_AllowScreenCaptureFX() )
		return;

	if( gHUD.GlitchAmount > 0.0f && tr.time > gHUD.GlitchHoldTime )
""", "capture gate: Glitch")

patch("game/client/render/r_postprocess.cpp",
"""void Monochrome( void )
{
	const float MonochromeState = gEngfuncs.GetLocalPlayer()->curstate.vuser1.z;
""",
"""void Monochrome( void )
{
	if( !R_AllowScreenCaptureFX() )
		return;

	const float MonochromeState = gEngfuncs.GetLocalPlayer()->curstate.vuser1.z;
""", "capture gate: Monochrome")

patch("game/client/render/r_postprocess.cpp",
"""void SSAO( void )
{
	if( (!CVAR_TO_BOOL( gl_ssao ) && !CVAR_TO_BOOL( gl_hbao )) || tr.lowmemory )
""",
"""void SSAO( void )
{
	if( !R_AllowScreenCaptureFX() )
		return;

	if( (!CVAR_TO_BOOL( gl_ssao ) && !CVAR_TO_BOOL( gl_hbao )) || tr.lowmemory )
""", "capture gate: SSAO")

patch("game/client/render/r_postprocess.cpp",
"""void Bloom( void )
{
	if( !CVAR_TO_BOOL( r_bloom ) )
""",
"""void Bloom( void )
{
	if( !R_AllowScreenCaptureFX() )
		return;

	if( !CVAR_TO_BOOL( r_bloom ) )
""", "capture gate: Bloom")

patch("game/client/render/r_postprocess.cpp",
"""void FSR( void )
{
	if( gl_fsr->value <= 0.0f )
""",
"""void FSR( void )
{
	if( !R_AllowScreenCaptureFX() )
		return;

	if( gl_fsr->value <= 0.0f )
""", "capture gate: FSR")

patch("game/client/render/r_postprocess.cpp",
"""void DownScale( void )
{
	if( gl_renderscale->value >= 1.0f )
""",
"""void DownScale( void )
{
	if( !R_AllowScreenCaptureFX() )
		return;

	if( gl_renderscale->value >= 1.0f )
""", "capture gate: DownScale")

# ---------------------------------------------------------------------------
# FIX B: sticky shader failures + real logging
# ---------------------------------------------------------------------------
patch("game/client/render/r_shader.cpp",
"""	// check for coexist
	for( prog = glsl_programsHashTable[hash]; prog != NULL; prog = prog->nextHash )
	{
		if( !Q_strcmp( prog->name, glname ) && !Q_strcmp( prog->options, defines ) )
		{
			prog->status |= SHADER_STATUS_HASHED;
			return prog;
""",
"""	// check for coexist
	for( prog = glsl_programsHashTable[hash]; prog != NULL; prog = prog->nextHash )
	{
		if( !Q_strcmp( prog->name, glname ) && !Q_strcmp( prog->options, defines ) )
		{
			// half-D: negative cache — this permutation already failed to
			// compile/link this session. Never retry: each retry was a full
			// driver compile attempt (hundreds of ms, GPU-watchdog bait).
			if( !prog->handle )
				return NULL;

			prog->status |= SHADER_STATUS_HASHED;
			return prog;
""", "negative cache: hash lookup")

patch("game/client/render/r_shader.cpp",
"""	if( !FBitSet( shader->status, SHADER_PROGRAM_LINKED ) || !FBitSet( shader->status, SHADER_VERTEX_COMPILED ) || !FBitSet( shader->status, SHADER_FRAGMENT_COMPILED ) )
	{
		if( shader->handle ) pglDeleteObjectARB( shader->handle );
		memset( shader, 0, sizeof( *shader ) );
		if( i == num_glsl_programs - 1 ) num_glsl_programs--;
		return NULL;
	}
""",
"""	if( !FBitSet( shader->status, SHADER_PROGRAM_LINKED ) || !FBitSet( shader->status, SHADER_VERTEX_COMPILED ) || !FBitSet( shader->status, SHADER_FRAGMENT_COMPILED ) )
	{
		// half-D: ALERT() is silenced without developer mode — 279 permutations
		// failed on device with zero explanation. Always log, throttled.
		static int failedLogged = 0;
		if( failedLogged < 25 )
		{
			Msg( "^1half-D: shader FAILED:^7 %s [%s] (vp %s, fp %s, linked %s)\\n", glname,
				defines ? defines : "",
				FBitSet( shader->status, SHADER_VERTEX_COMPILED ) ? "ok" : "NO",
				FBitSet( shader->status, SHADER_FRAGMENT_COMPILED ) ? "ok" : "NO",
				FBitSet( shader->status, SHADER_PROGRAM_LINKED ) ? "ok" : "NO" );
			if( shader->handle ) Msg( "%s", GL_PrintInfoLog( shader->handle ));
			failedLogged++;
		}

		if( shader->handle ) pglDeleteObjectARB( shader->handle );
		// half-D: keep the slot as a negative-cache entry (see the hash lookup
		// above) so the failed permutation is never recompiled this session
		shader->handle = 0;
		shader->status = SHADER_STATUS_HASHED;
		shader->nextHash = glsl_programsHashTable[hash];
		glsl_programsHashTable[hash] = shader;
		return NULL;
	}
""", "negative cache: failure path + logging")

patch("game/client/render/r_shader.cpp",
"""	if( !compiled )
	{
		if( developer_level ) Msg( "%s", GL_PrintInfoLog( object ));
		if( developer_level ) Msg( "Shader options:%s\\n", GL_PretifyListOptions( defines ));
		ALERT( at_error, "Couldn't compile %s\\n", filename );
		pglDeleteObjectARB( object );
		return;
	}
""",
"""	if( !compiled )
	{
		// half-D: ALERT() and the developer_level prints are invisible in
		// normal mode — always log shader compile failures, throttled
		static int compileFailsLogged = 0;
		if( compileFailsLogged < 25 )
		{
			Msg( "^1half-D: couldn't compile %s^7 options:%s\\n", filename, GL_PretifyListOptions( defines ));
			Msg( "%s", GL_PrintInfoLog( object ));
			compileFailsLogged++;
		}

		if( developer_level ) Msg( "%s", GL_PrintInfoLog( object ));
		if( developer_level ) Msg( "Shader options:%s\\n", GL_PretifyListOptions( defines ));
		ALERT( at_error, "Couldn't compile %s\\n", filename );
		pglDeleteObjectARB( object );
		return;
	}
""", "always-on compile-failure logging")

# ---------------------------------------------------------------------------
# FIX C: skip device-impossible permutations in the precompile
# ---------------------------------------------------------------------------
patch("game/client/render/r_shader.cpp",
"""	while( compiled_shader_counter < compiled_shader_max )
	{
		if( curshader.glname == NULL )
			break; // end of the list - finalize next frame

		shader = GL_InitGPUShader( curshader.glname, curshader.glname, curshader.glname, curshader.defines );
""",
"""	while( compiled_shader_counter < compiled_shader_max )
	{
		if( curshader.glname == NULL )
			break; // end of the list - finalize next frame

		// half-D: skip permutations this device can never use. gl4es has no
		// shadow samplers (sampler2DShadow/shadow2DOffset fail to translate —
		// the GL_ES fallbacks in shadow_*.h don't apply because gl4es presents
		// desktop GL 2.1), and no texture arrays (terrain falls back to 2D
		// single-layer). Without this filter ~413 of 1111 entries burn compile
		// time and surface as failures on every first launch.
		{
			const bool noShadowPath = tr.shadows_notsupport || !CVAR_TO_BOOL( gl_shadows );
			const char *def = curshader.defines ? curshader.defines : "";

			if(( noShadowPath && ( Q_strstr( def, "HAS_SHADOWS" ) || Q_strstr( def, "SUN_SHADOW" ))) ||
				( !GL_Support( R_TEXTURE_ARRAY_EXT ) && Q_strstr( def, "TERRAIN_NUM_LAYERS" )))
			{
				compiled_shader_counter++;
				shaders_processed_total++;
				curshader = shaders[shaders_processed_total];
				continue;
			}
		}

		shader = GL_InitGPUShader( curshader.glname, curshader.glname, curshader.glname, curshader.defines );
""", "precompile device filter")

# ---------------------------------------------------------------------------
# FIX D: load-stage instrumentation (engine)
# ---------------------------------------------------------------------------
patch("engine/engine/common/model.c",
"""	// free sequence files on studiomodels
	Mod_PurgeStudioCache();
""",
"""	// free sequence files on studiomodels
	// half-D: load-stage timing — the device log showed 25 s world loads
	// that were NOT in the BSP heap stages, so time every phase left
	double purgeT0 = Platform_DoubleTime();
	Mod_PurgeStudioCache();
	if( Platform_DoubleTime() - purgeT0 > 1.0 )
		Con_Printf( "load: purge studio cache in %.2f s\\n", Platform_DoubleTime() - purgeT0 );
""", "load timing: studio cache purge")

patch("engine/engine/common/model.c",
"""	byte *buf = FS_LoadFile( tempname, &length, false );
""",
"""	// half-D: time the file read itself (storage I/O was never visible)
	double fsT0 = Platform_DoubleTime();
	byte *buf = FS_LoadFile( tempname, &length, false );
	if( Platform_DoubleTime() - fsT0 > 1.0 )
		Con_Printf( "load: read %s (%.2f MB) in %.2f s\\n", tempname, (float)length / 1048576.0f, Platform_DoubleTime() - fsT0 );
""", "load timing: FS_LoadFile")

patch("engine/engine/common/mod_bmodel.c",
"""	// byte-swap BSP header and lump directory from little-endian
	Mod_SwapBSPLumps( mod_base, bufferlen );
""",
"""	// half-D: load-stage timing — pre-heap phases (the 25 s device world
	// load was in none of the heap stages timed below)
	double swapT0 = Platform_DoubleTime();

	// byte-swap BSP header and lump directory from little-endian
	Mod_SwapBSPLumps( mod_base, bufferlen );
	if( isworld && Platform_DoubleTime() - swapT0 > 0.3 )
		Con_Printf( "load: swap bsp lumps in %.2f s\\n", Platform_DoubleTime() - swapT0 );
""", "load timing: lump swap")

patch("engine/engine/common/mod_bmodel.c",
"""	fs_offset_t bspx_header_offset = Mod_FindBSPX( mod_base, bufferlen );

	// loading base lumps
	for( int i = 0; i < ARRAYSIZE( srclumps ); i++, stat_index++ )
		Mod_LoadLump( mod_base, &srclumps[i], &worldstats[stat_index], flags, LOADLUMP_STANDARD, NULL, bmod );

	// loading extralumps
	for( int i = 0; i < ARRAYSIZE( extlumps ); i++, stat_index++ )
		Mod_LoadLump( mod_base, &extlumps[i], &worldstats[stat_index], flags, LOADLUMP_BSP30EXT, NULL, bmod );

	// loading bspx lumps
	if( bspx_header_offset >= 0 )
	{
		for( int i = 0; i < ARRAYSIZE( bspxlumps ); i++, stat_index++ )
			Mod_LoadLump( mod_base, &bspxlumps[i], &worldstats[stat_index], flags, LOADLUMP_BSPX, mod_base + bspx_header_offset, bmod );
	}
""",
"""	double bspxT0 = Platform_DoubleTime();
	fs_offset_t bspx_header_offset = Mod_FindBSPX( mod_base, bufferlen );
	if( isworld && Platform_DoubleTime() - bspxT0 > 0.3 )
		Con_Printf( "load: find bspx in %.2f s\\n", Platform_DoubleTime() - bspxT0 );

	double rawT0 = Platform_DoubleTime();

	// loading base lumps
	for( int i = 0; i < ARRAYSIZE( srclumps ); i++, stat_index++ )
		Mod_LoadLump( mod_base, &srclumps[i], &worldstats[stat_index], flags, LOADLUMP_STANDARD, NULL, bmod );

	// loading extralumps
	for( int i = 0; i < ARRAYSIZE( extlumps ); i++, stat_index++ )
		Mod_LoadLump( mod_base, &extlumps[i], &worldstats[stat_index], flags, LOADLUMP_BSP30EXT, NULL, bmod );

	// loading bspx lumps
	if( bspx_header_offset >= 0 )
	{
		for( int i = 0; i < ARRAYSIZE( bspxlumps ); i++, stat_index++ )
			Mod_LoadLump( mod_base, &bspxlumps[i], &worldstats[stat_index], flags, LOADLUMP_BSPX, mod_base + bspx_header_offset, bmod );
	}

	if( isworld && Platform_DoubleTime() - rawT0 > 0.3 )
		Con_Printf( "load: raw lump loading in %.2f s\\n", Platform_DoubleTime() - rawT0 );
""", "load timing: find bspx + raw lump loop")

# ---------------------------------------------------------------------------
# FIX F: heartbeat reports draw calls and passes
# ---------------------------------------------------------------------------
patch("game/client/render/r_main.cpp",
"""		gEngfuncs.Con_Printf( "perf: ^2%.1f fps^7 | frame avg %.1f ms, worst %.0f ms | wpoly %d epoly %d spoly %d grass %d | stage ms: scene %.0f postfx %.0f anim %.1f cube %.1f\\n",
			windowFrames / windowS, windowS * 1000.0f / windowFrames, worstMs,
			r_stats.c_world_polys, r_stats.c_studio_polys,
			r_stats.c_sprite_polys, r_stats.c_grass_polys,
""",
"""		gEngfuncs.Con_Printf( "perf: ^2%.1f fps^7 | frame avg %.1f ms, worst %.0f ms | wpoly %d epoly %d spoly %d grass %d | dips %d passes %d binds %d | stage ms: scene %.0f postfx %.0f anim %.1f cube %.1f\\n",
			windowFrames / windowS, windowS * 1000.0f / windowFrames, worstMs,
			r_stats.c_world_polys, r_stats.c_studio_polys,
			r_stats.c_sprite_polys, r_stats.c_grass_polys,
			(int)r_stats.dip_count, (int)r_stats.num_passes, (int)r_stats.num_shader_binds,
""", "heartbeat: dips/passes/binds")

print()
print("applied: %d, skipped (already present): %d" % (len(applied), len(skipped)))
if skipped:
    print("skipped:", ", ".join(skipped))
