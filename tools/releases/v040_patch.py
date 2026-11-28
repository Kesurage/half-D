#!/usr/bin/env python3
"""v0.4.0 patch — 'the log finally talks' round.

Device log (Nov 21, v0.3.0 session) gave exact mechanisms:

  1. MENU/OPTIONS CRASH: GaussBlur() has an ungated "blur when paused (menu
     is shown)" path — RenderGaussBlur() does TWO fullscreen
     pglCopyTexSubImage2D per frame while any menu is open. Through gl4es
     that is the FSR/DownScale CPU-readback cliff again (0.5-13 s frames on
     the Mali -> Android GPU watchdog kills the app and destabilizes the
     device). r_blur 0 only gates water/sky/damage blur, NOT the menu blur.
     FIX: new cvar r_menu_blur (default 1), mobile preset sets it to 0.

  2. LIGHTING DEAD: without GL_ARB_depth_texture the engine turns every
     TF_DEPTHMAP texture into GL_NONE, so tr.depthTexture / tr.depthCubemap
     stubs are 0 and every dlight pass aborts with "shadow texture for the
     light is missing" -> zero dynamic light contribution (the reported
     lighting issues) + per-frame wasted light-list building.
     FIX: plain white 8x8 stub textures when depth textures are unsupported
     (shadowless lights, exactly what the code comment says must exist),
     plus a guard so R_AllocateShadowFramebuffer never binds texnum 0.

  3. 1-13 s FRAME SPIKES: binaryshaderfuncs[] requires glProgramParameteri
     and glProgramParameteriARB in its all-or-nothing table. gl4es exposes
     glGetProgramBinary/glProgramBinary (ES passthrough) but NOT the
     parameteri names, so R_BINARY_SHADER_EXT was false: no shader disk
     cache, no progressive precompile (message "Couldn't precompile
     shaders"), every permutation compiled lazily mid-frame. The table also
     had a copy-paste bug clobbering pglGetProgramivARB.
     FIX: shrink the required table to the two real entry points, resolve
     the hint setter separately (optional), guard its 3 call sites.

  4. CONSOLE FONT: MainUI maps "Tahoma" -> gfx/fonts/tahoma.ttf, which the
     itch package does not ship (device log: "Unable to read font file").
     FIX: map Tahoma to the FiraSans we already bundle (SIL OFL).

  5. 25 s WORLD LOAD: instrument the BSP heap-load stages with timers so
     the next device log names the thief stage.

  6. DIAGNOSTICS: caps2 line += bptc/rgtc/program_binary (BC7 raindrop
     upload failure theory), upload-fail message += pixel format name.
"""
import sys, os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

applied = []
skipped = []

def patch(relpath, old, new, name, count=1):
    """Replace `old` with `new` in `relpath`. Idempotent: skips if new is
    already present; fails loudly if the anchor is missing."""
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
# Fix 1: menu GaussBlur gate (client renderer)
# ---------------------------------------------------------------------------
patch("game/client/render/r_postprocess.cpp",
"""	// this is asked by UI when saving the game, to get unblurred save preview image
	// it will always reset to 0 at the end of every frame
	if( CVAR_TO_BOOL( ui_forcenoblur ) )
		return;

	// blur when paused (menu is shown)
	if( RP_NORMALPASS() )
""",
"""	// this is asked by UI when saving the game, to get unblurred save preview image
	// it will always reset to 0 at the end of every frame
	if( CVAR_TO_BOOL( ui_forcenoblur ) )
		return;

	// half-D: the paused-menu GaussBlur does two fullscreen pglCopyTexSubImage2D
	// captures every frame while any menu is open — through gl4es that is a CPU
	// readback (0.5-13 s per frame on Mali, GPU watchdog resets). r_menu_blur 0
	// (mobile preset) disables it; desktop keeps the original blurred-menu look.
	if( !CVAR_TO_BOOL( r_menu_blur ) )
		pausedblur = 0;

	// blur when paused (menu is shown)
	if( RP_NORMALPASS() )
""", "menu-blur gate in GaussBlur")

# cvar definition
patch("game/client/render/view.cpp",
"""cvar_t *ui_forcenoblur;
cvar_t *cl_background;
""",
"""cvar_t *ui_forcenoblur;
cvar_t *r_menu_blur; // half-D: paused-menu GaussBlur gate (gl4es readback)
cvar_t *cl_background;
""", "r_menu_blur definition")

# cvar registration
patch("game/client/render/view.cpp",
"""	ui_forcenoblur = CVAR_REGISTER( "ui_forcenoblur", "0", FCVAR_UNLOGGED );
""",
"""	ui_forcenoblur = CVAR_REGISTER( "ui_forcenoblur", "0", FCVAR_UNLOGGED );
	// half-D: menu GaussBlur does two fullscreen pglCopyTexSubImage2D per frame —
	// a CPU readback through gl4es (seconds per frame on Mali, watchdog resets).
	// Mobile preset sets it to 0; r_menu_blur 1 restores the blurred-menu look.
	r_menu_blur = CVAR_REGISTER( "r_menu_blur", "1", FCVAR_ARCHIVE );
""", "r_menu_blur registration")

# extern declaration
patch("game/client/render/r_cvars.h",
"""extern cvar_t *ui_forcenoblur;
""",
"""extern cvar_t *ui_forcenoblur;
extern cvar_t *r_menu_blur; // half-D: paused-menu GaussBlur gate (gl4es readback)
""", "r_menu_blur extern")

# preset v5 -> v6
patch("game/client/render/view.cpp",
"""	if( glConfig.version < 3.0f && CVAR_TO_BOOL( gl_mobile_quality ))
	{
		gl_shadows->value = 0.0f;             // dlight shadow passes
""",
"""	if( glConfig.version < 3.0f && CVAR_TO_BOOL( gl_mobile_quality ))
	{
		gl_shadows->value = 0.0f;             // dlight shadow passes
		r_menu_blur->value = 0.0f;            // paused-menu GaussBlur readbacks
""", "preset v6: r_menu_blur off")

patch("game/client/render/view.cpp",
"""	// half-D: mobile quality preset v5 (on yalnie's renderer base). The GL 2.x
""",
"""	// half-D: mobile quality preset v6 (on yalnie's renderer base). The GL 2.x
""", "preset v6 comment")

patch("game/client/render/view.cpp",
"""		ConPrintf( "renderer: mobile quality preset v5 applied (gl_mobile_quality 1)\\n" );
""",
"""		ConPrintf( "renderer: mobile quality preset v6 applied (gl_mobile_quality 1)\\n" );
""", "preset v6 message")

# ---------------------------------------------------------------------------
# Fix 2: shadowless-light stubs when depth textures are unsupported
# ---------------------------------------------------------------------------
patch("game/client/render/r_opengl.cpp",
"""	tr.depthTexture = CREATE_TEXTURE( "*depth", 8, 8, NULL, TF_SHADOW ); 
""",
"""	tr.depthTexture = CREATE_TEXTURE( "*depth", 8, 8, NULL, TF_SHADOW );
	if( !tr.depthTexture )
	{
		// half-D: no GL_ARB_depth_texture through gl4es — the depth stub fails
		// and every dlight pass used to abort ("shadow texture for the light
		// is missing"), leaving dynamic lights completely unrendered. Give
		// shadowless lights a plain white 8x8 stub so the light pass runs.
		byte depthStub[8*8];
		memset( depthStub, 0xFF, sizeof( depthStub ));
		tr.depthTexture = CREATE_TEXTURE( "*depth_stub", 8, 8, depthStub, TF_NOMIPMAP|TF_CLAMP|TF_LUMINANCE );
		ConPrintf( "half-D: depth textures unsupported, dynamic lights will be shadowless\\n" );
	}
""", "depthTexture white stub fallback")

patch("game/client/render/r_opengl.cpp",
"""	tr.depthCubemap = CREATE_TEXTURE( "depthCube", 8, 8, NULL, TF_SHADOW_CUBEMAP );
""",
"""	tr.depthCubemap = CREATE_TEXTURE( "depthCube", 8, 8, NULL, TF_SHADOW_CUBEMAP );
	if( !tr.depthCubemap )
		tr.depthCubemap = tr.whiteCubeTexture; // half-D: shadowless pointlight stub (no depth cubemaps via gl4es)
""", "depthCubemap fallback to whiteCube")

patch("game/client/render/r_shadows.cpp",
"""	// create 3 shadow textures to decrease flashlight first lag
	for( i = 0; i < 4; i++ )
	{
		if( !tr.shadowTextures[i] )
		{
			char txName[16];
			Q_snprintf( txName, sizeof( txName ), "*shadow%i", i );
			tr.shadowTextures[i] = CREATE_TEXTURE( txName, ShadowViewport, ShadowViewport, NULL, TF_SHADOW );
		}
	}
""",
"""	// create 3 shadow textures to decrease flashlight first lag
	for( i = 0; i < 4; i++ )
	{
		if( !tr.shadowTextures[i] )
		{
			char txName[16];
			Q_snprintf( txName, sizeof( txName ), "*shadow%i", i );
			tr.shadowTextures[i] = CREATE_TEXTURE( txName, ShadowViewport, ShadowViewport, NULL, TF_SHADOW );
		}
	}

	// half-D: on gl4es (no GL_ARB_depth_texture) every TF_SHADOW allocation
	// fails and the array stays 0 — say so once instead of failing per light.
	if( !tr.shadowTextures[0] )
		ConPrintf( "half-D: shadow textures unavailable (no depth textures via GL4ES), dlights run shadowless\\n" );
""", "R_ResetShadowTextures diagnostic")

# never bind texnum 0 to the shadow FBO (spotlight branch)
patch("game/client/render/r_shadows.cpp",
"""			tr.shadowTextures[i] = CREATE_TEXTURE( txName, ShadowViewport, ShadowViewport, NULL, TF_SHADOW );
			texture = tr.shadowTextures[i];
		}

		pglBindFramebuffer( GL_FRAMEBUFFER_EXT, sh_framebuffer );
""",
"""			tr.shadowTextures[i] = CREATE_TEXTURE( txName, ShadowViewport, ShadowViewport, NULL, TF_SHADOW );
			texture = tr.shadowTextures[i];
		}

		if( !texture )
			return 0; // half-D: no shadow texture possible (no depth textures) — caller disables

		pglBindFramebuffer( GL_FRAMEBUFFER_EXT, sh_framebuffer );
""", "R_AllocateShadowFramebuffer spotlight guard")

# same for the pointlight branch
patch("game/client/render/r_shadows.cpp",
"""				tr.shadowCubemaps[i] = CREATE_TEXTURE( txName, ShadowViewport, ShadowViewport, NULL, TF_SHADOW_CUBEMAP );
				texture = tr.shadowCubemaps[i];
			}
		}

		pglBindFramebuffer( GL_FRAMEBUFFER_EXT, sh_framebuffer );
""",
"""				tr.shadowCubemaps[i] = CREATE_TEXTURE( txName, ShadowViewport, ShadowViewport, NULL, TF_SHADOW_CUBEMAP );
				texture = tr.shadowCubemaps[i];
			}
		}

		if( !texture )
			return 0; // half-D: no shadow cubemap possible (no depth textures) — caller disables

		pglBindFramebuffer( GL_FRAMEBUFFER_EXT, sh_framebuffer );
""", "R_AllocateShadowFramebuffer pointlight guard")

# ---------------------------------------------------------------------------
# Fix 3: binary shader cache + precompile on gl4es
# ---------------------------------------------------------------------------
patch("game/client/render/r_opengl.cpp",
"""static const dllfunc_t binaryshaderfuncs[] =
{
{ "glProgramBinary"              , (void **)&pglProgramBinaryARB },
{ "glGetProgramBinary"           , (void **)&pglGetProgramBinaryARB },
{ "glProgramParameteri"          , (void **)&pglGetProgramivARB },
{ "glProgramParameteriARB"		 , (void **)&pglProgramParameteriARB },
{ NULL, NULL }
};
""",
"""static const dllfunc_t binaryshaderfuncs[] =
{
{ "glProgramBinary"              , (void **)&pglProgramBinaryARB },
{ "glGetProgramBinary"           , (void **)&pglGetProgramBinaryARB },
{ NULL, NULL }
};
// half-D: the old table also required glProgramParameteri(ARB) — gl4es exposes
// the binary get/set (ES passthrough) but not the hint setter, so the
// all-or-nothing check silently disabled the shader disk cache and the whole
// precompile pass on Android (every permutation compiled lazily mid-frame,
// 1-13 s spikes). The (optional, hint-only) setter is resolved separately
// below, after the extension check. This also removes a copy-paste bug that
// clobbered pglGetProgramivARB with the glProgramParameteri pointer.
""", "binaryshaderfuncs table shrink")

patch("game/client/render/r_opengl.cpp",
"""	GL_CheckExtension( "GL_ARB_get_program_binary", binaryshaderfuncs, "gl_binaryshader", R_BINARY_SHADER_EXT );
""",
"""	GL_CheckExtension( "GL_ARB_get_program_binary", binaryshaderfuncs, "gl_binaryshader", R_BINARY_SHADER_EXT );

	// half-D: hint setter is optional (gl4es does not export it) — call sites
	// check for NULL. Without the hint the driver still serves/accepts binaries.
	pglProgramParameteriARB = (void ( APIENTRY *)( GLuint, GLenum, GLint ))GL_GetProcAddress( "glProgramParameteriARB" );
	if( !pglProgramParameteriARB )
		pglProgramParameteriARB = (void ( APIENTRY *)( GLuint, GLenum, GLint ))GL_GetProcAddress( "glProgramParameteri" );
""", "optional pglProgramParameteriARB resolve")

# guard the three hint call sites
patch("game/client/render/r_shader.cpp",
"""	if( bSupportBinaryShader )
	{
		pglProgramParameteriARB( shader->handle, GL_PROGRAM_BINARY_RETRIEVABLE_HINT, GL_TRUE );

		char filename[256];
""",
"""	if( bSupportBinaryShader )
	{
		if( pglProgramParameteriARB )
			pglProgramParameteriARB( shader->handle, GL_PROGRAM_BINARY_RETRIEVABLE_HINT, GL_TRUE );

		char filename[256];
""", "hint guard: GL_InitGPUShader entry")

patch("game/client/render/r_shader.cpp",
"""						pglDeleteObjectARB( shader->handle );
						shader->handle = pglCreateProgramObjectARB();  // Recreate
						pglProgramParameteriARB( shader->handle, GL_PROGRAM_BINARY_RETRIEVABLE_HINT, GL_TRUE );
""",
"""						pglDeleteObjectARB( shader->handle );
						shader->handle = pglCreateProgramObjectARB();  // Recreate
						if( pglProgramParameteriARB )
							pglProgramParameteriARB( shader->handle, GL_PROGRAM_BINARY_RETRIEVABLE_HINT, GL_TRUE );
""", "hint guard: GL_InitGPUShader recreate")

patch("game/client/render/r_shader.cpp",
"""	if( bSupportBinaryShader )
		pglProgramParameteriARB( shader->handle, GL_PROGRAM_BINARY_RETRIEVABLE_HINT, GL_TRUE );
""",
"""	if( bSupportBinaryShader && pglProgramParameteriARB )
		pglProgramParameteriARB( shader->handle, GL_PROGRAM_BINARY_RETRIEVABLE_HINT, GL_TRUE );
""", "hint guard: GL_CreateUberShader")

# ---------------------------------------------------------------------------
# Fix 4: console font (vendored MainUI)
# ---------------------------------------------------------------------------
patch("mainui/font/FontManager.cpp",
"""	else if( !strcmp( name, "Tahoma" ) )
	{
		Q_strncpy( dataFile, "gfx/fonts/tahoma.ttf", dataFileChars );
		return true;
	}
""",
"""	else if( !strcmp( name, "Tahoma" ) )
	{
		// half-D: tahoma.ttf is proprietary and absent from the itch package —
		// the console font silently failed to load on device. FiraSans (SIL OFL)
		// ships with the port; use it instead.
		Q_strncpy( dataFile, "gfx/fonts/FiraSans-Regular.ttf", dataFileChars );
		return true;
	}
""", "Tahoma -> FiraSans mapping")

# ---------------------------------------------------------------------------
# Fix 5: BSP load-stage timing (engine)
# ---------------------------------------------------------------------------
patch("engine/engine/common/mod_bmodel.c",
"""	// load into heap
	Mod_LoadEntities( mod, bmod );
	Mod_LoadPlanes( mod, bmod );
	Mod_LoadSubmodels( mod, bmod );
	Mod_LoadVertexes( mod, bmod );
	Mod_LoadEdges( mod, bmod );
	Mod_LoadSurfEdges( mod, bmod );
	Mod_LoadTextures( mod, bmod );
	Mod_LoadVisibility( mod, bmod );
	Mod_LoadTexInfo( mod, bmod );
	Mod_LoadSurfaces( mod, bmod );
	Mod_LoadLighting( mod, bmod );
	Mod_LoadMarkSurfaces( mod, bmod );
	Mod_LoadLeafs( mod, bmod );
	Mod_LoadNodes( mod, bmod );
	Mod_LoadClipnodes( mod, bmod );

	// preform some post-initalization
	Mod_MakeHull0( mod, bmod );
	Mod_SetupSubmodels( mod, bmod );
""",
"""	// half-D: load-stage timing — device logs show 25 s world loads and the
	// per-stage cost is otherwise invisible. Print every stage over 0.3 s.
#define BSP_LOAD_STAGE( stage, call ) \\
	do { \\
		double stageT0 = Platform_DoubleTime(); \\
		call; \\
		double stageDt = Platform_DoubleTime() - stageT0; \\
		if( isworld && stageDt > 0.3 ) \\
			Con_Printf( "load: %s in %.2f s\\n", stage, stageDt ); \\
	} while( 0 )

	// load into heap
	BSP_LOAD_STAGE( "entities", Mod_LoadEntities( mod, bmod ));
	BSP_LOAD_STAGE( "planes", Mod_LoadPlanes( mod, bmod ));
	BSP_LOAD_STAGE( "submodels", Mod_LoadSubmodels( mod, bmod ));
	BSP_LOAD_STAGE( "vertexes", Mod_LoadVertexes( mod, bmod ));
	BSP_LOAD_STAGE( "edges", Mod_LoadEdges( mod, bmod ));
	BSP_LOAD_STAGE( "surfedges", Mod_LoadSurfEdges( mod, bmod ));
	BSP_LOAD_STAGE( "textures", Mod_LoadTextures( mod, bmod ));
	BSP_LOAD_STAGE( "visibility", Mod_LoadVisibility( mod, bmod ));
	BSP_LOAD_STAGE( "texinfo", Mod_LoadTexInfo( mod, bmod ));
	BSP_LOAD_STAGE( "surfaces", Mod_LoadSurfaces( mod, bmod ));
	BSP_LOAD_STAGE( "lighting", Mod_LoadLighting( mod, bmod ));
	BSP_LOAD_STAGE( "marksurfaces", Mod_LoadMarkSurfaces( mod, bmod ));
	BSP_LOAD_STAGE( "leafs", Mod_LoadLeafs( mod, bmod ));
	BSP_LOAD_STAGE( "nodes", Mod_LoadNodes( mod, bmod ));
	BSP_LOAD_STAGE( "clipnodes", Mod_LoadClipnodes( mod, bmod ));

	// preform some post-initalization
	BSP_LOAD_STAGE( "makehull0", Mod_MakeHull0( mod, bmod ));
	BSP_LOAD_STAGE( "setupsubmodels", Mod_SetupSubmodels( mod, bmod ));
#undef BSP_LOAD_STAGE
""", "BSP load-stage timing")

# ---------------------------------------------------------------------------
# Fix 6: diagnostics
# ---------------------------------------------------------------------------
patch("engine/ref/gl/gl_opengl.c",
"""	gEngfuncs.Con_Printf( "GL caps2: s3tc %s, etc1 %s, astc_ldr %s, fbo %s\\n",
		Q_stristr( glConfig.extensions_string, "GL_EXT_texture_compression_s3tc" ) ? "yes" : "no",
		Q_stristr( glConfig.extensions_string, "GL_OES_compressed_ETC1_RGB8_texture" ) ? "yes" : "no",
		Q_stristr( glConfig.extensions_string, "GL_KHR_texture_compression_astc_ldr" ) ? "yes" : "no",
		Q_stristr( glConfig.extensions_string, "GL_EXT_framebuffer_object" ) ? "yes" : "no" );
""",
"""	gEngfuncs.Con_Printf( "GL caps2: s3tc %s, etc1 %s, astc_ldr %s, fbo %s, bptc %s, rgtc %s, program_binary %s\\n",
		Q_stristr( glConfig.extensions_string, "GL_EXT_texture_compression_s3tc" ) ? "yes" : "no",
		Q_stristr( glConfig.extensions_string, "GL_OES_compressed_ETC1_RGB8_texture" ) ? "yes" : "no",
		Q_stristr( glConfig.extensions_string, "GL_KHR_texture_compression_astc_ldr" ) ? "yes" : "no",
		Q_stristr( glConfig.extensions_string, "GL_EXT_framebuffer_object" ) ? "yes" : "no",
		Q_stristr( glConfig.extensions_string, "GL_ARB_texture_compression_bptc" ) ? "yes" : "no",
		( Q_stristr( glConfig.extensions_string, "GL_ARB_texture_compression_rgtc" ) || Q_stristr( glConfig.extensions_string, "GL_EXT_texture_compression_rgtc" )) ? "yes" : "no",
		Q_stristr( glConfig.extensions_string, "GL_ARB_get_program_binary" ) ? "yes" : "no" );
""", "caps2: bptc/rgtc/program_binary")

patch("engine/ref/gl/gl_image.c",
"""		// half-D: upload failures (format/compression) were silent
		gEngfuncs.Con_Printf( S_ERROR "half-D: texture upload failed: %s\\n", name );
""",
"""		// half-D: upload failures (format/compression) were silent
		gEngfuncs.Con_Printf( S_ERROR "half-D: texture upload failed: %s (format %s)\\n", name, gEngfuncs.Image_GetPFDesc( pic->type )->name );
""", "upload-fail: pixel format name", count=2)

print()
print("applied: %d, skipped (already present): %d" % (len(applied), len(skipped)))
if skipped:
    print("skipped:", ", ".join(skipped))
