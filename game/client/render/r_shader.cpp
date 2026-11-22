/*
r_shader.cpp - glsl shaders
Copyright (C) 2014 Uncle Mike

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include "hud.h"
#include "utils.h"
#include "const.h"
#include "r_local.h"
#include <mathlib.h>
#include <stringlib.h>
#include "r_shader.h"
#include "virtualfs.h"
#include "r_world.h"
#include "crclib.h"
#include "r_shaderlist.h"
#include <chrono> // half-D v0.8.3: wall-clock budgets in the load phase and the queue drain

static bool bPrecompiled = false;
static bool bCompilingShaders = false;
static int compiled_outdated_total = 0;
static int shaders_processed_total = 0;
static int compiled_count_total = 0;
static int compiled_failed_total = 0;

#define SHADERS_HASH_SIZE	(MAX_GLSL_PROGRAMS >> 2)
#define MAX_FILE_STACK	64

static char	filenames_stack[MAX_FILE_STACK][256];
glsl_program_t	glsl_programs[MAX_GLSL_PROGRAMS];
glsl_program_t	*glsl_programsHashTable[SHADERS_HASH_SIZE];
unsigned int	num_glsl_programs;
static int	file_stack_pos;
ref_shaders_t	glsl;

static const char *GL_GetDriverString( void )
{
	static char driver[256] = { 0 };
	if( driver[0] ) return driver;

	const char *vendor = (const char *)pglGetString( GL_VENDOR );
	const char *renderer = (const char *)pglGetString( GL_RENDERER );
	const char *version = (const char *)pglGetString( GL_VERSION );

	Q_snprintf( driver, sizeof( driver ), "%s_%s_%s", vendor ? vendor : "", renderer ? renderer : "", version ? version : "" );
	return driver;
}

#define SHADER_CACHE_REVISION 1 // bump this number when you edit a shared #include file in glsl folder

static char *GL_GetShaderCacheFilename( const char *glname, const char *options, char *filename, size_t size )
{
	const char *driver = GL_GetDriverString();

	if( !glname ) glname = "";
	if( !options ) options = "";

	char salt[160];
	Q_snprintf( salt, sizeof( salt ), "r%i|%i|%i|%i|%f|%i|%i",
		SHADER_CACHE_REVISION,
		glConfig.max_skinning_bones,
		MAX_LIGHTSTYLES,
		MAXLIGHTMAPS,
		GRASS_ANIM_DIST,
		(int)GL_Support( R_EXT_GPU_SHADER4 ),
		(int)GL_Support( R_TEXTURE_ARRAY_EXT ) );

	// separate hash for all parts
	unsigned int crc = 0;
	CRC32_Init( &crc );
	CRC32_ProcessBuffer( &crc, glname, Q_strlen( glname ) );
	CRC32_ProcessBuffer( &crc, "|", 1 );
	CRC32_ProcessBuffer( &crc, options, Q_strlen( options ) );
	CRC32_ProcessBuffer( &crc, "|", 1 );
	CRC32_ProcessBuffer( &crc, salt, Q_strlen( salt ) );
	CRC32_ProcessBuffer( &crc, "|", 1 );
	CRC32_ProcessBuffer( &crc, driver, Q_strlen( driver ) );
	crc = CRC32_Final( crc );

	Q_snprintf( filename, size, ".shadercache/%08x.bin", crc );
	return filename;
}

static bool GL_IsSourceNewerThanCache( const char *vpname, const char *fpname, const char *defines, const char *cache_filename )
{
	// We need to check both VP and FP if they exist
	char vp_path[256] = { 0 };
	char fp_path[256] = { 0 };

	Q_snprintf( vp_path, sizeof( vp_path ), "glsl/%s_vp.glsl", vpname );
	Q_snprintf( fp_path, sizeof( fp_path ), "glsl/%s_fp.glsl", fpname );

	for( int i = 0; vp_path[i]; i++ ) vp_path[i] = tolower( vp_path[i] );
	for( int i = 0; fp_path[i]; i++ ) fp_path[i] = tolower( fp_path[i] );

	int retval = 0;

	// Compare VP vs cache
	if( COMPARE_FILE_TIME( vp_path, cache_filename, &retval ) )
	{
		if( retval > 0 ) // glsl in newer
			return true;
	}

	// Compare FP vs cache
	if( COMPARE_FILE_TIME( fp_path, cache_filename, &retval ) )
	{
		if( retval > 0 ) // glsl in newer
			return true;
	}

	return false;
}

void GL_EncodeNormal( char *options, int texturenum )
{
	if( RENDER_GET_PARM( PARM_TEX_GLFORMAT, texturenum ) == GL_COMPRESSED_RED_GREEN_RGTC2_EXT )
	{
		GL_AddShaderDirective( options, "NORMAL_RG_PARABOLOID" );
	}
	else if( RENDER_GET_PARM( PARM_TEX_GLFORMAT, texturenum ) == GL_COMPRESSED_LUMINANCE_ALPHA_3DC_ATI )
	{
		GL_AddShaderDirective( options, "NORMAL_3DC_PARABOLOID" );
	}
	else if( RENDER_GET_PARM( PARM_TEX_ENCODE, texturenum ) == DXT_ENCODE_NORMAL_AG_PARABOLOID )
	{
		GL_AddShaderDirective( options, "NORMAL_AG_PARABOLOID" );
	}
	else if( RENDER_GET_PARM( PARM_TEX_GLFORMAT, texturenum ) == GL_COMPRESSED_RGBA_S3TC_DXT5_EXT )
	{
		// implicit DXT5NM format (Paranoia2 v 1.2 old stuff)
		if( FBitSet( RENDER_GET_PARM( PARM_TEX_FLAGS, texturenum ), TF_HAS_ALPHA ) )
			GL_AddShaderDirective( options, "NORMAL_AG_PARABOLOID" );
	}
}

static char *GL_PrintInfoLog( GLhandleARB object )
{
	static char	msg[8192];
	int		maxLength = 0;

	pglGetObjectParameterivARB( object, GL_OBJECT_INFO_LOG_LENGTH_ARB, &maxLength );

	if( maxLength >= sizeof( msg ))
	{
		ALERT( at_warning, "GL_PrintInfoLog: message exceeds %i symbols\n", sizeof( msg ));
		maxLength = sizeof( msg ) - 1;
	}

	pglGetInfoLogARB( object, maxLength, &maxLength, msg );

	return msg;
}

static char *GL_PrintShaderSource( GLhandleARB object )
{
	static char	msg[8192];
	int		maxLength = 0;
	
	pglGetObjectParameterivARB( object, GL_OBJECT_SHADER_SOURCE_LENGTH_ARB, &maxLength );

	if( maxLength >= sizeof( msg ))
	{
		ALERT( at_warning, "GL_PrintShaderSource: message exceeds %i symbols\n", sizeof( msg ));
		maxLength = sizeof( msg ) - 1;
	}

	pglGetShaderSourceARB( object, maxLength, &maxLength, msg );

	return msg;
}

static bool GL_PushFileStack( const char *filename )
{
	if( file_stack_pos < MAX_FILE_STACK )
	{
		Q_strncpy( filenames_stack[file_stack_pos], filename, sizeof( filenames_stack[0] ));
		file_stack_pos++;
		return true;
	}

	ALERT( at_error, "GL_PushFileStack: stack overflow\n" );
	return false;
}

static bool GL_PopFileStack( void )
{
	file_stack_pos--;

	if( file_stack_pos < 0 )
	{
		ALERT( at_error, "GL_PushFileStack: stack underflow\n" );
		return false;
	}

	return true;
}

static bool GL_CheckFileStack( const char *filename )
{
	for( int i = 0; i < file_stack_pos; i++ )
	{
		if( !Q_stricmp( filenames_stack[i], filename ))
			return true;
	}
	return false;
}

static bool GL_LoadSource( const char *filename, CVirtualFS *file )
{
	if( GL_CheckFileStack( filename ))
	{
		ALERT( at_error, "recursive include for %s\n", filename );
		return false;
	}

	int size;
	char *source = (char *)gEngfuncs.COM_LoadFile((char *)filename, 5, &size );
	if( !source )
	{
		char lowername[256];
		Q_strncpy( lowername, filename, sizeof( lowername ) );
		for( int i = 0; lowername[i]; i++ )
			lowername[i] = tolower( lowername[i] );
		source = (char *)gEngfuncs.COM_LoadFile( lowername, 5, &size );
	}
	if( !source )
	{
		ALERT( at_error, "couldn't load %s\n", filename );
		return false;
	}

	GL_PushFileStack( filename );
	file->Write( source, size );
	file->Seek( 0, SEEK_SET ); // rewind

	gEngfuncs.COM_FreeFile( source );

	return true;
}

// half-D: native GLES3 path — rewrite the desktop GLSL 130 sources to
// GLSL ES 300 on the fly. GLSL 130 and 300 es are close cousins: only the
// legacy keywords, texture builtins, fragment output and fixed-function
// matrix inputs differ. Everything below is textual; the compiler does the
// rest. See logs/dev-log.md v0.5.0 for the full mapping table.
static bool HD_ReplaceToken( char *line, size_t linesize, const char *from, const char *to )
{
	size_t fromLen = Q_strlen( from );
	size_t toLen = Q_strlen( to );
	size_t search = 0;

	while( search < linesize )
	{
		char *p = (char *)Q_strstr( line + search, from );

		if( p == NULL )
			break;

		// token boundary: the chars before and after must not be [A-Za-z0-9_]
		char prev = ( p == line ) ? '\0' : p[-1];
		char next = p[fromLen];

		if(( prev == '\0' || !( isalpha( prev ) || isdigit( prev ) || prev == '_' )) &&
		   ( next == '\0' || !( isalpha( next ) || isdigit( next ) || next == '_' )))
		{
			if( toLen > fromLen && Q_strlen( line ) + toLen - fromLen >= linesize )
				return false; // would overflow the line buffer

			memmove( p + toLen, p + fromLen, Q_strlen( p + fromLen ) + 1 );
			memcpy( p, to, toLen );
			search = ( p - line ) + toLen;
		}
		else
		{
			// not a standalone token: continue searching after this one
			search = ( p - line ) + fromLen;
		}
	}

	return true;
}

static void HD_TransformLineES( char *line, size_t linesize, GLenum shaderType )
{
	bool isVertex = ( shaderType == GL_VERTEX_SHADER_ARB );

	// GLSL ES 300 reserved the word "sample"; texfetch.h uses it as a local
	HD_ReplaceToken( line, linesize, "sample", "hd_sample" );

	// legacy interpolation keyword -> in/out depending on stage
	HD_ReplaceToken( line, linesize, "varying", isVertex ? "out" : "in" );

	if( isVertex )
	{
		// legacy attribute keyword (vertex only)
		HD_ReplaceToken( line, linesize, "attribute", "in" );

		// fixed-function matrices -> uniforms uploaded by GL_BindShader
		HD_ReplaceToken( line, linesize, "gl_ModelViewProjectionMatrix", "u_hdMVP" );
		HD_ReplaceToken( line, linesize, "gl_ModelViewMatrix", "u_hdMV" );
		HD_ReplaceToken( line, linesize, "gl_TextureMatrix[0]", "u_hdTexMat0" );

		// fixed-function vertex inputs -> shim-fed attributes. The engine
		// gl2_shim uploads client-side arrays at its own fixed locations
		// (position 0, color 1, texcoord0 2) which don't match this
		// renderer's attr_* convention, so declare explicit locations.
		HD_ReplaceToken( line, linesize, "gl_MultiTexCoord0", "hd_MultiTexCoord0" );
		HD_ReplaceToken( line, linesize, "gl_Vertex", "hd_Position" );
		HD_ReplaceToken( line, linesize, "gl_Normal", "hd_Normal" );
		HD_ReplaceToken( line, linesize, "gl_Color", "hd_glColor" );
		HD_ReplaceToken( line, linesize, "gl_TexCoord[0]", "hd_TexCoord0" );
		// clip planes are stubbed on this path; eat the assignment
		HD_ReplaceToken( line, linesize, "gl_ClipVertex", "hd_ClipVertexUnused" );
	}
	else
	{
		HD_ReplaceToken( line, linesize, "gl_TexCoord[0]", "hd_TexCoord0" );
	}
}

static void GL_ParseFile( const char *filename, CVirtualFS *file, CVirtualFS *out, GLenum shaderType )
{
	char	*pfile, token[256];
	int	ret, fileline = 1;
	char	line[2048];

//	out->Printf( "#file %s\n", filename );	// OpenGL doesn't support #file :-(
	out->Printf( "#line 0\n" );

	do
	{
		ret = file->Gets( line, sizeof( line ));
		pfile = line;

		// NOTE: if first keyword it's not an '#include' just ignore it
		pfile = COM_ParseFile( pfile, token );
		if( !Q_strcmp( token, "#include" ))
		{
			CVirtualFS incfile;
			char incname[256];

			pfile = COM_ParseLine( pfile, token );
			Q_snprintf( incname, sizeof( incname ), "glsl/%s", token );
			if( !GL_LoadSource( incname, &incfile ))
			{
				fileline++;
				continue;
                              }
			GL_ParseFile( incname, &incfile, out, shaderType );
//			out->Printf( "#file %s\n", filename );	// OpenGL doesn't support #file :-(
			out->Printf( "#line %i\n", fileline );
		}
		else
		{
			// half-D: GLSL requires #extension before any non-preprocessor
			// token, and the ES prelude already emits declarations — a body
			// directive (e.g. from a mod shader) can never be legal there.
			// Comment it out instead of failing the compile (the stock
			// game shaders carry none; grep-verified).
			if( glConfig.es_context && !Q_strcmp( token, "#extension" ))
			{
				out->Printf( "// [half-D] ES: %s", line );
				fileline++;
				continue;
			}

			// half-D: rewrite legacy GLSL to ES 300 on native GLES contexts
			if( glConfig.es_context )
				HD_TransformLineES( line, sizeof( line ), shaderType );
			out->Printf( "%s\n", line );
		}
		fileline++;
	} while( ret != EOF );

	GL_PopFileStack();
}

static bool GL_ProcessShader( const char *filename, CVirtualFS *out, bool compatible, GLenum shaderType, const char *defines = NULL )
{
	CVirtualFS file;

	file_stack_pos = 0;	// debug

	if( !GL_LoadSource( filename, &file ))
		return false;

	// add internal defines
	// half-D: native GLES contexts take GLSL ES 300 — the source is still
	// authored as desktop GLSL 130 and rewritten line-by-line in GL_ParseFile
	if( glConfig.es_context )
		out->Printf( "#version 300 es\n" );
	else
		out->Printf( "#version 130\n" );
	out->Printf( "#ifdef GL_ES\n" );
	out->Printf( "precision highp float;\n" );
	out->Printf( "precision highp int;\n" );
	out->Printf( "precision highp sampler2D;\n" );
	out->Printf( "precision highp samplerCube;\n" );
	out->Printf( "precision highp sampler2DShadow;\n" );
	out->Printf( "precision highp samplerCubeShadow;\n" );
	out->Printf( "#endif\n" );
	out->Printf( "#ifndef M_PI\n#define M_PI 3.14159265358979323846\n#endif\n" );
	out->Printf( "#ifndef M_PI2\n#define M_PI2 6.28318530717958647692\n#endif\n" );
	out->Printf( "#ifndef round\n#define round( x ) floor( (x) + 0.5 )\n#endif\n" );
	if( glConfig.es_context )
	{
		// half-D: GLSL ES 300 compatibility shims. GLSL 130 kept the
		// GLSL 1.10 texture builtins and gl_FragColor; 300 es dropped them.
		out->Printf( "#define texture2D texture\n" );
		out->Printf( "#define texture2DLod textureLod\n" );
		out->Printf( "#define texture2DProj textureProj\n" );
		out->Printf( "#define textureCube texture\n" );
		out->Printf( "#define textureCubeLod textureLod\n" );

		if( shaderType == GL_FRAGMENT_SHADER_ARB )
		{
			// 300 es removed gl_FragColor; every fragment shader in this
			// renderer writes the single output through it
			out->Printf( "layout(location = 0) out vec4 hd_FragColor;\n" );
			out->Printf( "#define gl_FragColor hd_FragColor\n" );
		}
		else
		{
			// fixed-function vertex inputs rewritten in GL_ParseFile: the
			// engine gl2_shim feeds client-side arrays at its fixed
			// locations (position 0, color 1, texcoord0 2)
			out->Printf( "layout(location = 0) in vec4 hd_Position;\n" );
			out->Printf( "layout(location = 1) in vec4 hd_glColor;\n" );
			out->Printf( "layout(location = 2) in vec4 hd_MultiTexCoord0;\n" );
			out->Printf( "layout(location = 3) in vec3 hd_Normal;\n" );
			// fixed-function matrices -> uniforms (uploaded by GL_BindShader)
			out->Printf( "uniform mat4 u_hdMVP;\n" );
			out->Printf( "uniform mat4 u_hdMV;\n" );
			out->Printf( "uniform mat4 u_hdTexMat0;\n" );
			// gl_TexCoord[0] varying rewritten in GL_ParseFile
			out->Printf( "out vec4 hd_TexCoord0;\n" );
			// gl_ClipVertex assignments land here; clip planes are stubbed
			out->Printf( "vec4 hd_ClipVertexUnused;\n" );
		}

		if( shaderType == GL_FRAGMENT_SHADER_ARB )
		{
			out->Printf( "in vec4 hd_TexCoord0;\n" );
			out->Printf( "uniform mat4 u_hdMVP;\n" );
			out->Printf( "uniform mat4 u_hdMV;\n" );
		}
	}
	if( GL_Support( R_EXT_GPU_SHADER4 ))
	{
		// half-D: GL_EXT_gpu_shader4 is a desktop extension — ES3 has the
		// integer/bitwise features in core. Emitting the directive on a
		// native GLES context is a double fault: the extension doesn't
		// exist there, and the Mali compiler additionally rejects any
		// #extension that follows a non-preprocessor token (P0001, the
		// 11:17 device crash — this directive sat after the uniform
		// injections above). Keep the define, drop the directive on ES.
		if( !glConfig.es_context )
			out->Printf( "#extension GL_EXT_gpu_shader4 : require\n" ); // support bitwise ops
		out->Printf( "#define GLSL_gpu_shader4\n" );
		if( GL_Support( R_TEXTURE_ARRAY_EXT ))
			out->Printf( "#define GLSL_ALLOW_TEXTURE_ARRAY\n" );
	}
	else if( GL_Support( R_TEXTURE_ARRAY_EXT ))
	{
	//	out->Printf( "#extension GL_EXT_texture_array : require\n" ); // support texture arrays
		out->Printf( "#define GLSL_ALLOW_TEXTURE_ARRAY\n" );
	}
	if( defines ) out->Print( defines );

	// user may override this constants
	out->Printf( "#ifndef MAXSTUDIOBONES\n#define MAXSTUDIOBONES %i\n#endif\n", glConfig.max_skinning_bones );
	out->Printf( "#ifndef MAX_LIGHTSTYLES\n#define MAX_LIGHTSTYLES %i\n#endif\n", MAX_LIGHTSTYLES );
	out->Printf( "#ifndef MAXLIGHTMAPS\n#define MAXLIGHTMAPS %i\n#endif\n", MAXLIGHTMAPS );
	out->Printf( "#ifndef GRASS_ANIM_DIST\n#define GRASS_ANIM_DIST %f\n#endif\n", GRASS_ANIM_DIST );

	GL_ParseFile( filename, &file, out, shaderType );
	out->Write( "", 1 ); // terminator

	return true;
}

static void GL_LoadGPUShader( glsl_program_t *shader, const char *name, GLenum shaderType, bool compatible, const char *defines = NULL )
{
	char		filename[256];
	CVirtualFS	source;
	GLint		compiled;

	ASSERT( shader != NULL );

	switch( shaderType )
	{
	case GL_VERTEX_SHADER_ARB:
		Q_snprintf( filename, sizeof( filename ), "glsl/%s_vp.glsl", name );
		break;
	case GL_FRAGMENT_SHADER_ARB:
		Q_snprintf( filename, sizeof( filename ), "glsl/%s_fp.glsl", name );
		break;
	default:
		ALERT( at_error, "GL_LoadGPUShader: unknown shader type %p\n", shaderType );
		return;
	}

	for( int i = 0; filename[i]; i++ )
		filename[i] = tolower( filename[i] );

	// load includes, add some directives
	if( !GL_ProcessShader( filename, &source, compatible, shaderType, defines ) )
		return;

	GLcharARB *buffer = (GLcharARB *)source.GetBuffer();
	int bufferSize = (int)source.GetSize();

	ALERT( at_aiconsole, "\nloading '%s'\n", filename );
	GLhandleARB	object = pglCreateShaderObjectARB( shaderType );
	pglShaderSourceARB( object, 1, (const GLcharARB **)&buffer, &bufferSize );

	// compile shader
	pglCompileShaderARB( object );

	// check if shader compiled
	pglGetObjectParameterivARB( object, GL_OBJECT_COMPILE_STATUS_ARB, &compiled );

	if( !compiled )
	{
		// half-D: ALERT() and the developer_level prints are invisible in
		// normal mode — always log shader compile failures, throttled
		static int compileFailsLogged = 0;
		if( compileFailsLogged < 25 )
		{
			Msg( "^1half-D: couldn't compile %s^7 options:%s\n", filename, GL_PretifyListOptions( defines ));
			Msg( "%s", GL_PrintInfoLog( object ));
			compileFailsLogged++;
		}

		if( developer_level ) Msg( "%s", GL_PrintInfoLog( object ));
		if( developer_level ) Msg( "Shader options:%s\n", GL_PretifyListOptions( defines ));
		ALERT( at_error, "Couldn't compile %s\n", filename );
		pglDeleteObjectARB( object );
		return;
	}

	if( shaderType == GL_VERTEX_SHADER_ARB )
		shader->status |= SHADER_VERTEX_COMPILED;
	else shader->status |= SHADER_FRAGMENT_COMPILED;

	// attach shader to program
	pglAttachObjectARB( shader->handle, object );

	// delete shader, no longer needed
	pglDeleteObjectARB( object );
}

static void GL_LinkProgram( glsl_program_t *shader )
{
	GLint	linked = 0;

	if( !shader ) return;

	pglLinkProgramARB( shader->handle );

	pglGetObjectParameterivARB( shader->handle, GL_OBJECT_LINK_STATUS_ARB, &linked );
	if( !linked ) ALERT( at_error, "%s\n%s shader failed to link\n", GL_PrintInfoLog( shader->handle ), shader->name );
	else shader->status |= SHADER_PROGRAM_LINKED;
}

static bool GL_ValidateProgram( glsl_program_t *shader )
{
	GLint validated = 0;

	if( !shader ) return false;

	pglValidateProgramARB( shader->handle );

	pglGetObjectParameterivARB( shader->handle, GL_OBJECT_VALIDATE_STATUS_ARB, &validated );

	if( !validated )
		ALERT( at_error, "%s\n%s shader failed to validate\n", GL_PrintInfoLog( shader->handle ), shader->name );

	return validated;
}

int GL_UniformTypeToDwordCount( GLuint type, bool align = false )
{
	switch( type )
	{
	case GL_INT:
	case GL_UNSIGNED_INT:
	case GL_SAMPLER_1D_ARB:
	case GL_SAMPLER_2D_ARB:
	case GL_SAMPLER_3D_ARB:
	case GL_SAMPLER_CUBE_ARB:
	case GL_SAMPLER_1D_SHADOW_ARB:
	case GL_SAMPLER_2D_SHADOW_ARB:
	case GL_SAMPLER_2D_RECT_ARB:
	case GL_SAMPLER_2D_RECT_SHADOW_ARB:
	case GL_SAMPLER_CUBE_SHADOW_EXT:
		return align ? 4 : 1;	// int[1]
	case GL_FLOAT:
		return align ? 4 : 1;	// float[1]
	case GL_FLOAT_VEC2_ARB:
		return align ? 4 : 2;	// float[2]
	case GL_FLOAT_VEC3_ARB:
		return align ? 4 : 3;	// float[3]
	case GL_FLOAT_VEC4_ARB:
		return 4;	// float[4]
	case GL_FLOAT_MAT2_ARB:
		return 4;	// float[2][2]
	case GL_FLOAT_MAT3_ARB:
		return align ? 12 : 9;	// float[3][3]
	case GL_FLOAT_MAT4_ARB:
		return 16;// float[4][4]
	default: return align ? 4 : 1; // assume error
	}
}

const char *GL_UniformTypeToName( GLuint type )
{
	switch( type )
	{
	case GL_INT:
		return "int";
	case GL_UNSIGNED_INT:
		return "uint";
	case GL_FLOAT:
		return "float";
	case GL_FLOAT_VEC2_ARB:
		return "vec2";
	case GL_FLOAT_VEC3_ARB:
		return "vec3";
	case GL_FLOAT_VEC4_ARB:
		return "vec4";
	case GL_FLOAT_MAT2_ARB:
		return "mat2";
	case GL_FLOAT_MAT3_ARB:
		return "mat3";
	case GL_FLOAT_MAT4_ARB:
		return "mat4";
	case GL_SAMPLER_1D_ARB:
		return "sampler1D";
	case GL_SAMPLER_2D_ARB:
		return "sampler2D";
	case GL_SAMPLER_3D_ARB:
		return "sampler3D";
	case GL_SAMPLER_CUBE_ARB:
		return "samplerCube";
	case GL_SAMPLER_1D_ARRAY_EXT:
		return "sampler1DArray";
	case GL_SAMPLER_2D_ARRAY_EXT:
		return "sampler2DArray";
	case GL_SAMPLER_1D_SHADOW_ARB:
		return "sampler1DShadow";
	case GL_SAMPLER_2D_SHADOW_ARB:
		return "sampler2DShadow";
	case GL_SAMPLER_2D_RECT_ARB:
		return "sampler2DRect";
	case GL_SAMPLER_2D_RECT_SHADOW_ARB:
		return "sampler2DRectShadow";
	case GL_SAMPLER_CUBE_SHADOW_EXT:
		return "samplerCubeShadow";
	default:	return "???";
	}
}

void GL_ShowProgramUniforms( glsl_program_t *shader )
{
	int	count, size;
	char	uniformName[256];
	int	total_uniforms_used = 0;
	GLuint	type;

	if( !shader || developer_level < DEV_EXTENDED )
		return;
	
	// install the executables in the program object as part of current state.
	pglUseProgramObjectARB( shader->handle );

	// query the number of active uniforms
	pglGetObjectParameterivARB( shader->handle, GL_OBJECT_ACTIVE_UNIFORMS_ARB, &count );

	// Loop over each of the active uniforms, and set their value
	for( int i = 0; i < count; i++ )
	{
		pglGetActiveUniformARB( shader->handle, i, sizeof( uniformName ), NULL, &size, &type, uniformName );
		if( developer_level >= DEV_EXTENDED )
		{
			if( size != 1 )
			{
				char *end = Q_strchr( uniformName, '[' );
				if( end ) *end = '\0'; // cutoff [0]
			}

			if( size == 1 ) ALERT( at_aiconsole, "uniform %s %s;\n", GL_UniformTypeToName( type ), uniformName );
			else ALERT( at_aiconsole, "uniform %s %s[%i];\n", GL_UniformTypeToName( type ), uniformName, size );
		}
		total_uniforms_used += GL_UniformTypeToDwordCount( type ) * size;
	}

	if( total_uniforms_used >= glConfig.max_vertex_uniforms )
		ALERT( at_error, "used uniforms %i is overflowed max count %i\n", total_uniforms_used, glConfig.max_vertex_uniforms );
	else ALERT( at_aiconsole, "used uniforms %i from %i\n", total_uniforms_used, glConfig.max_vertex_uniforms );

	int max_shader_uniforms = total_uniforms_used;

	if( max_shader_uniforms > ( glConfig.max_skinning_bones * 7 ))
		max_shader_uniforms -= ( glConfig.max_skinning_bones * 7 );

	ALERT( at_aiconsole, "%s used %i uniforms\n", shader->name, max_shader_uniforms );

	if( max_shader_uniforms > glConfig.peak_used_uniforms )
	{
		glConfig.peak_used_uniforms = max_shader_uniforms;
		tr.show_uniforms_peak = true;
	}

	pglUseProgramObjectARB( GL_NONE );
}

void GL_BindShader( glsl_program_t *shader )
{
	if( !shader && RI->currentshader )
	{
		pglUseProgram( GL_NONE );
		RI->currentshader = NULL;
	}
	else if( shader != RI->currentshader && shader != NULL && shader->handle )
	{
		pglUseProgram( shader->handle );
		r_stats.num_shader_binds++;
		RI->currentshader = shader;

		// half-D: native GLES3 path — the shaders were rewritten from
		// gl_ModelViewProjectionMatrix/gl_ModelViewMatrix to uniforms; feed
		// them the shim's tracked matrices every bind (~2 glUniform calls,
		// negligible next to the draw itself)
		if( glConfig.es_context && pglHalfDGetMatrix )
		{
			float mat[16];

			if( shader->u_hdMVP >= 0 )
			{
				pglHalfDGetMatrix( 0 /* combined MVP */, mat );
				pglUniformMatrix4fvARB( shader->u_hdMVP, 1, GL_FALSE, mat );
			}

			if( shader->u_hdMV >= 0 )
			{
				pglHalfDGetMatrix( GL_MODELVIEW_MATRIX, mat );
				pglUniformMatrix4fvARB( shader->u_hdMV, 1, GL_FALSE, mat );
			}

			if( shader->u_hdTexMat0 >= 0 )
			{
				pglHalfDGetMatrix( GL_TEXTURE_MATRIX, mat );
				pglUniformMatrix4fvARB( shader->u_hdTexMat0, 1, GL_FALSE, mat );
			}
		}
	}
}

static glsl_program_t *GL_InitGPUShader( const char *glname, const char *vpname, const char *fpname, const char *defines = NULL )
{
	const char	*find;
	unsigned int	i, hash;
	glsl_program_t	*prog;

	if( !GL_Support( R_SHADER_GLSL100_EXT ))
		return NULL;

	ASSERT( glname != NULL );

	if( !defines ) defines = "";

	find = va( "%s %s", glname, defines );
	hash = COM_HashKey( find, SHADERS_HASH_SIZE );

	// check for coexist
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
		}
	}

	// find free spot
	for( i = 1; i < num_glsl_programs; i++ )
		if( !glsl_programs[i].name[0] )
			break;

	if( i >= MAX_GLSL_PROGRAMS )
	{
		ALERT( at_error, "GL_InitGPUShader: GLSL shaders limit exceeded (%i max)\n", MAX_GLSL_PROGRAMS );
		return NULL;
	}

	// alloc new shader
	glsl_program_t *shader = &glsl_programs[i];
	memset( shader, 0, sizeof( *shader ) );

	if( i == num_glsl_programs )
		num_glsl_programs++;

	Q_strncpy( shader->name, glname, sizeof( shader->name ));
	Q_strncpy( shader->options, defines, sizeof( shader->options ));
	shader->handle = pglCreateProgramObjectARB();

	if( !shader->handle )
	{
		ALERT( at_error, "GL_InitGPUShader: ^1failed to create program object for %s^7\n", glname );
		memset( shader, 0, sizeof( *shader ) );
		if( i == num_glsl_programs - 1 ) num_glsl_programs--;
		return NULL;
	}

	const bool bSupportBinaryShader = GL_Support( R_BINARY_SHADER_EXT );

	// try load from file
	if( bSupportBinaryShader )
	{
		if( pglProgramParameteriARB )
			pglProgramParameteriARB( shader->handle, GL_PROGRAM_BINARY_RETRIEVABLE_HINT, GL_TRUE );

		char filename[256];
		GL_GetShaderCacheFilename( glname, defines ? defines : "", filename, sizeof( filename ) );

		int filesize;
		byte *data = (byte *)gEngfuncs.COM_LoadFile( filename, 5, &filesize );

		if( data )
		{
			if( filesize > (int)sizeof( GLuint ) )
			{
				if( !GL_IsSourceNewerThanCache( vpname, fpname, defines, filename ) )
				{
					GLuint format = *(GLuint *)data;
					byte *binary = data + sizeof( GLuint );
					int bin_size = filesize - sizeof( GLuint );

					pglProgramBinaryARB( shader->handle, format, binary, bin_size );

					GLint linked = 0;
					pglGetObjectParameterivARB( shader->handle, GL_OBJECT_LINK_STATUS_ARB, &linked );

					if( linked )
					{
						shader->status = SHADER_VERTEX_COMPILED | SHADER_FRAGMENT_COMPILED | SHADER_PROGRAM_LINKED | SHADER_STATUS_FROMBINARY;
						shader->nextHash = NULL;

						ALERT( at_aiconsole, "\n^2Loaded shader binary:^7 %s\n", filename );

						gEngfuncs.COM_FreeFile( data );

						// add to hash table
						shader->nextHash = glsl_programsHashTable[hash];
						glsl_programsHashTable[hash] = shader;
						return shader;
					}
					else
					{
						ALERT( at_warning, "^1Cached binary %s FAILED to link:^7 %s\n", filename, GL_PrintInfoLog( shader->handle ) );
						pglDeleteObjectARB( shader->handle );
						shader->handle = pglCreateProgramObjectARB();  // Recreate
						if( pglProgramParameteriARB )
							pglProgramParameteriARB( shader->handle, GL_PROGRAM_BINARY_RETRIEVABLE_HINT, GL_TRUE );
					}
				}
				else
				{
					ALERT( at_aiconsole, "^3Shader binary for %s is outdated and will be recompiled.^7\n", glname );
					compiled_outdated_total++;
				}
			}

			gEngfuncs.COM_FreeFile( data );
		}
	}

	// Fallback: compile from source
	if( vpname )
		GL_LoadGPUShader( shader, vpname, GL_VERTEX_SHADER_ARB, false, defines );

	if( fpname )
		GL_LoadGPUShader( shader, fpname, GL_FRAGMENT_SHADER_ARB, false, defines );

	if( vpname && FBitSet( shader->status, SHADER_VERTEX_COMPILED ) )
	{
		pglBindAttribLocationARB( shader->handle, ATTR_INDEX_POSITION, "attr_Position" );
		pglBindAttribLocationARB( shader->handle, ATTR_INDEX_TEXCOORD0, "attr_TexCoord0" );
		pglBindAttribLocationARB( shader->handle, ATTR_INDEX_TEXCOORD1, "attr_TexCoord1" );
		pglBindAttribLocationARB( shader->handle, ATTR_INDEX_TEXCOORD2, "attr_TexCoord2" );
		pglBindAttribLocationARB( shader->handle, ATTR_INDEX_NORMAL, "attr_Normal" );
		pglBindAttribLocationARB( shader->handle, ATTR_INDEX_BONE_INDEXES, "attr_BoneIndexes" );
		pglBindAttribLocationARB( shader->handle, ATTR_INDEX_BONE_WEIGHTS, "attr_BoneWeights" );
		pglBindAttribLocationARB( shader->handle, ATTR_INDEX_LIGHT_STYLES, "attr_LightStyles" );
		pglBindAttribLocationARB( shader->handle, ATTR_INDEX_LIGHT_COLOR, "attr_LightColor" );
		pglBindAttribLocationARB( shader->handle, ATTR_INDEX_LIGHT_VECS, "attr_LightVecs" );
		pglBindAttribLocationARB( shader->handle, ATTR_INDEX_TANGENT, "attr_Tangent" );
		pglBindAttribLocationARB( shader->handle, ATTR_INDEX_BINORMAL, "attr_Binormal" );
	}

	GL_LinkProgram( shader );

	if( !FBitSet( shader->status, SHADER_PROGRAM_LINKED ) || !FBitSet( shader->status, SHADER_VERTEX_COMPILED ) || !FBitSet( shader->status, SHADER_FRAGMENT_COMPILED ) )
	{
		// half-D: ALERT() is silenced without developer mode — 279 permutations
		// failed on device with zero explanation. Always log, throttled.
		static int failedLogged = 0;
		if( failedLogged < 25 )
		{
			Msg( "^1half-D: shader FAILED:^7 %s [%s] (vp %s, fp %s, linked %s)\n", glname,
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

	// half-D: cache the injected fixed-function matrix uniform locations
	// (native GLES3 path — GL_BindShader uploads them on every bind)
	shader->u_hdMVP = -1;
	shader->u_hdMV = -1;
	shader->u_hdTexMat0 = -1;
	if( glConfig.es_context && shader->handle )
	{
		shader->u_hdMVP = pglGetUniformLocationARB( shader->handle, "u_hdMVP" );
		shader->u_hdMV = pglGetUniformLocationARB( shader->handle, "u_hdMV" );
		shader->u_hdTexMat0 = pglGetUniformLocationARB( shader->handle, "u_hdTexMat0" );
	}

	// Cache binary if supported
	if( bSupportBinaryShader )
	{
		char filename[256];
		GL_GetShaderCacheFilename( glname, defines ? defines : "", filename, sizeof( filename ) );

		GLint bin_length = 0;
		pglGetProgramiv( shader->handle, GL_PROGRAM_BINARY_LENGTH, &bin_length );
		if( bin_length <= 0 )
		{
			// Fallback for drivers that only accept old name
			pglGetObjectParameterivARB( shader->handle, GL_PROGRAM_BINARY_LENGTH, &bin_length );
		}

		ALERT( at_aiconsole, "Program binary length for %s: %d bytes\n", glname, bin_length );

		if( bin_length > 0 )
		{
			byte *buffer = (byte *)Mem_Alloc( bin_length + sizeof( GLuint ) );
			GLuint format = 0;
			GLint ret_len = 0;

			pglGetProgramBinaryARB( shader->handle, bin_length, &ret_len, &format, buffer + sizeof( GLuint ) );

			if( ret_len == bin_length )
			{
				*(GLuint *)buffer = format;
				gRenderfuncs.pfnSaveFile( filename, buffer, bin_length + sizeof( GLuint ) );
				ALERT( at_aiconsole, "^2Saved shader cache to:^7 %s (%d bytes)\n", filename, bin_length );
			}
			else
			{
				ALERT( at_warning, "^1Failed to retrieve program binary for %s^7\n", glname );
			}

			Mem_Free( buffer );
		}
	}

	// add to hash table
	shader->nextHash = glsl_programsHashTable[hash];
	glsl_programsHashTable[hash] = shader;

	return shader;
}

static void GL_FreeGPUShader( glsl_program_t *shader )
{
	if( shader && shader->handle )
	{
		unsigned int	hash;
		glsl_program_t	*cur;
		glsl_program_t	**prev;
		const char	*find;

		find = va( "%s %s", shader->name, shader->options );

		// remove from hash table
		hash = COM_HashKey( find, SHADERS_HASH_SIZE );
		prev = &glsl_programsHashTable[hash];

		while( 1 )
		{
				cur = *prev;
				if( !cur ) break;

				if( cur == shader )
			{
					*prev = cur->nextHash;
				break;
			}
				prev = &cur->nextHash;
		}

		pglDeleteObjectARB( shader->handle );
		memset( shader, 0, sizeof( *shader ));
	}
}

static glsl_program_t *GL_CreateUberShader( GLuint slot, const char *glname, const char *defines = NULL, pfnShaderCallback pfnInitUniforms = NULL )
{
	if( !GL_Support( R_SHADER_GLSL100_EXT ))
		return NULL;

	if( slot >= MAX_GLSL_PROGRAMS )
	{
		ALERT( at_error, "GL_CreateUberShader: GLSL shaders limit exceeded (%i max)\n", MAX_GLSL_PROGRAMS );
		return NULL;
	}

	if( !defines ) defines = "";

	// alloc new shader
	glsl_program_t *shader = &glsl_programs[slot];
	memset( shader, 0, sizeof( *shader ) );

	const bool bSupportBinaryShader = GL_Support( R_BINARY_SHADER_EXT );

	if( bSupportBinaryShader )
	{
		char filename[256];
		GL_GetShaderCacheFilename( glname, defines, filename, sizeof( filename ) );

		int filesize;
		byte *data = (byte *)gEngfuncs.COM_LoadFile( filename, 5, &filesize );

		if( data && filesize > sizeof( GLuint ) )
		{
			if( !GL_IsSourceNewerThanCache( glname, glname, defines, filename ) )
			{
				GLuint format = *(GLuint *)data;
				byte *binary = data + sizeof( GLuint );
				int bin_size = filesize - sizeof( GLuint );

				shader->handle = pglCreateProgramObjectARB();
				pglProgramBinaryARB( shader->handle, format, binary, bin_size );

				GLint linked = 0;
				pglGetObjectParameterivARB( shader->handle, GL_OBJECT_LINK_STATUS_ARB, &linked );

				if( linked )
				{
					shader->status = SHADER_VERTEX_COMPILED | SHADER_FRAGMENT_COMPILED | SHADER_PROGRAM_LINKED | SHADER_UBERSHADER | SHADER_STATUS_FROMBINARY;
					Q_strncpy( shader->name, glname, sizeof( shader->name ) );
					Q_strncpy( shader->options, defines, sizeof( shader->options ) );

					if( pfnInitUniforms ) pfnInitUniforms( shader );
					GL_ShowProgramUniforms( shader );  // If dev mode

					gEngfuncs.COM_FreeFile( data );
					ALERT( at_aiconsole, "\n^2GL_CreateUberShader: Loaded shader binary:^7 %s\n", filename );

					if( slot == num_glsl_programs )
					{
						if( num_glsl_programs == MAX_GLSL_PROGRAMS )
						{
							ALERT( at_error, "^1GL_CreateUberShader: GLSL shaders limit exceeded (%i max)^7\n", MAX_GLSL_PROGRAMS );
							GL_FreeGPUShader( shader );
							return NULL;
						}
						num_glsl_programs++;
					}

					return shader;
				}
				else
				{
					ALERT( at_warning, "^1GL_CreateUberShader: Failed to load shader binary %s:^7 %s\n", filename, GL_PrintInfoLog( shader->handle ) );
					pglDeleteObjectARB( shader->handle );
					shader->handle = 0;
				}
			}
			else
				ALERT( at_aiconsole, "^3GL_CreateUberShader Shader binary for %s is outdated and will be recompiled.^7\n", glname );

			gEngfuncs.COM_FreeFile( data );
		}
	}

	shader->handle = pglCreateProgramObjectARB();
	if( !shader->handle )
		return NULL; // some bad happens

	if( bSupportBinaryShader && pglProgramParameteriARB )
		pglProgramParameteriARB( shader->handle, GL_PROGRAM_BINARY_RETRIEVABLE_HINT, GL_TRUE );

	Q_strncpy( shader->name, glname, sizeof( shader->name ));
	Q_strncpy( shader->options, defines, sizeof( shader->options ));

	GL_LoadGPUShader( shader, glname, GL_VERTEX_SHADER_ARB, true, defines );
	GL_LoadGPUShader( shader, glname, GL_FRAGMENT_SHADER_ARB, true, defines );

	if( FBitSet( shader->status, SHADER_VERTEX_COMPILED ))
	{
		pglBindAttribLocationARB( shader->handle, ATTR_INDEX_POSITION, "attr_Position" );
		pglBindAttribLocationARB( shader->handle, ATTR_INDEX_TEXCOORD0, "attr_TexCoord0" );
		pglBindAttribLocationARB( shader->handle, ATTR_INDEX_TEXCOORD1, "attr_TexCoord1" );
		pglBindAttribLocationARB( shader->handle, ATTR_INDEX_TEXCOORD2, "attr_TexCoord2" );
		pglBindAttribLocationARB( shader->handle, ATTR_INDEX_NORMAL, "attr_Normal" );
		pglBindAttribLocationARB( shader->handle, ATTR_INDEX_BONE_INDEXES, "attr_BoneIndexes" );
		pglBindAttribLocationARB( shader->handle, ATTR_INDEX_BONE_WEIGHTS, "attr_BoneWeights" );
		pglBindAttribLocationARB( shader->handle, ATTR_INDEX_LIGHT_STYLES, "attr_LightStyles" );
		pglBindAttribLocationARB( shader->handle, ATTR_INDEX_LIGHT_COLOR, "attr_LightColor" );
		pglBindAttribLocationARB( shader->handle, ATTR_INDEX_LIGHT_VECS, "attr_LightVecs" );
		pglBindAttribLocationARB( shader->handle, ATTR_INDEX_TANGENT, "attr_Tangent" );
		pglBindAttribLocationARB( shader->handle, ATTR_INDEX_BINORMAL, "attr_Binormal" );
	}

	GL_LinkProgram( shader );

	if( pfnInitUniforms != NULL && FBitSet( shader->status, SHADER_PROGRAM_LINKED ))
		pfnInitUniforms( shader ); // register shader uniforms

	shader->status |= SHADER_UBERSHADER; // it's UberShader!

	ALERT( at_aiconsole, "^2CompileUberShader #%i:^7 %s\n%s\n", slot, glname, defines );

	if( developer_level && bPrecompiled )
	{
		// development aid - log shaders here if they were compiled/loaded long after the initialization stage
		char log_filename[256] = "shaders_unlisted.log";
		FILE *log_file = fopen( log_filename, "a" );  // Append mode
		if( log_file )
		{
			fprintf( log_file, "%s | %s\n", glname, defines ? defines : "" );  // Format: glname | defines
			fclose( log_file );
		}
	}

	if( bSupportBinaryShader )
	{
		char filename[256];
		GL_GetShaderCacheFilename( glname, defines, filename, sizeof( filename ) );

		GLint bin_length = 0;
		pglGetProgramiv( shader->handle, GL_PROGRAM_BINARY_LENGTH, &bin_length );
		if( bin_length <= 0 )
		{
			// Fallback for drivers that only accept old name
			pglGetObjectParameterivARB( shader->handle, GL_PROGRAM_BINARY_LENGTH, &bin_length );
		}

		if( bin_length > 0 )
		{
			byte *buffer = (byte *)Mem_Alloc( bin_length + sizeof( GLuint ) );
			GLuint format;
			GLint ret_len = 0;

			pglGetProgramBinaryARB( shader->handle, bin_length, &ret_len, &format, buffer + sizeof( GLuint ) );

			if( ret_len == bin_length )
			{
				*(GLuint *)buffer = format;
				gRenderfuncs.pfnSaveFile( filename, buffer, bin_length + sizeof( GLuint ) );
				ALERT( at_aiconsole, "^2Cached shader binary:^7 %s\n", filename );
			}

			Mem_Free( buffer );
		}
	}

	if( slot == num_glsl_programs )
		num_glsl_programs++;

	// all done, program is loaded

	return shader;
}

// half-D v0.8.1 ── rate-limited lazy compilation ───────────────────────────
// v0.8.0's budget system failed on device in two ways. The "family
// fallback" drew surfaces with a wrong permutation — blank textures. And
// the frame-start drain still paid one full ~460 ms compile per frame
// (the deadline was checked between compiles, not during one) — a ~2 fps
// drain and another watchdog near-miss. v0.8.1 does neither:
//   * a lazy miss either compiles inline (spending one rate-limiter token)
//     or returns NULL. Every caller already handles NULL by skipping that
//     surface/mesh this frame (SURF_NODRAW / STUDIO_NF_NODRAW, retried
//     next frame and cleared on success) — an unseen material pops in a
//     moment later; it never renders wrong, never blank.
//   * tokens refill at r_shader_lazy_hz per second (default 2), so a new
//     area costs at most two ~460 ms hitches per second while its shaders
//     compile, instead of one 35 s frozen frame.
//   * the deferred queue drains at frame starts under the same limit, and
//     UNLIMITED at map load (R_NewMap), behind the loading plaque.
// r_shader_lazy_hz 0 restores the pre-v0.8.0 compile-on-first-use behavior.
static float g_flShaderTokens = 1.0f;
static std::chrono::steady_clock::time_point g_tShaderLastRefill = std::chrono::steady_clock::now();

struct pending_shader_t
{
	char glname[64];
	char options[MAX_OPTIONS_LENGTH];
	pfnShaderCallback pfnInitUniforms;
};

#define MAX_PENDING_SHADERS 1024
static pending_shader_t g_PendingShaders[MAX_PENDING_SHADERS];
static int g_iNumPendingShaders = 0;
static int g_iShadersDeferredTotal = 0;  // session counter, printed by the heartbeat

static glsl_program_t *GL_FindUberShader( const char *glname, const char *options = NULL, pfnShaderCallback pfnInitUniforms = NULL );

static void R_QueuePendingShader( const char *glname, const char *options, pfnShaderCallback pfnInitUniforms )
{
	if( g_iNumPendingShaders >= MAX_PENDING_SHADERS )
		return; // queue full: stays lazy, will compile on a later first use

	// dedupe: every draw of the same material misses the cache and lands
	// here again — without this check the queue fills with duplicates
	for( int i = 0; i < g_iNumPendingShaders; i++ )
	{
		if( !Q_strcmp( g_PendingShaders[i].glname, glname ? glname : "" )
			&& !Q_strcmp( g_PendingShaders[i].options, options ? options : "" ))
			return;
	}

	pending_shader_t &p = g_PendingShaders[g_iNumPendingShaders];
	Q_strncpy( p.glname, glname ? glname : "", sizeof( p.glname ));
	Q_strncpy( p.options, options ? options : "", sizeof( p.options ));
	p.pfnInitUniforms = pfnInitUniforms;
	g_iNumPendingShaders++;
	g_iShadersDeferredTotal++;
}

// called from HUD_RenderFrame (rate-limited, or per-frame-budgeted during
// the v0.8.3 load phase): refills tokens and drains the queue
void R_ShadersFrameStart( bool bUnlimited, float flDrainBudgetSeconds )
{
	auto now = std::chrono::steady_clock::now();

	if( bUnlimited || !r_shader_lazy_hz || r_shader_lazy_hz->value <= 0.0f )
	{
		g_flShaderTokens = 1e9f; // no limiting: map load or escape hatch
	}
	else
	{
		g_flShaderTokens = Q_min( 1.0f, g_flShaderTokens
			+ std::chrono::duration<float>( now - g_tShaderLastRefill ).count() * r_shader_lazy_hz->value );
	}

	g_tShaderLastRefill = now;

	// half-D v0.8.2: the unlimited (map-load) drain is wall-clock bounded.
	// The v0.8.1 device log measured a 4489 ms frame from ~10 queued compiles
	// at ~460 ms each; the v0.7.4 measurement was 50-70 queued compiles =
	// 23-32 s of frozen main thread = system watchdog kill, read by the user
	// as "crashes when I open a menu / chapter". Bound the drain: the rest
	// stays queued and drains at r_shader_lazy_hz in the first seconds of
	// play. Inline load-time lookups are unaffected (they run behind the
	// plaque, and the v0.8.3 per-frame load phase owns the systematic pass there).
	// half-D v0.8.3: -1 keeps the legacy behavior (one-shot 2s map-load
	// drain); >= 0 is the per-frame budget of the load phase
	if( flDrainBudgetSeconds < 0.0f )
		flDrainBudgetSeconds = bUnlimited ? 2.0f : 0.0f;
	auto drainStart = std::chrono::steady_clock::now();

	while( g_iNumPendingShaders > 0 && g_flShaderTokens >= 1.0f )
	{
		if( flDrainBudgetSeconds > 0.0f
			&& std::chrono::duration<float>( std::chrono::steady_clock::now() - drainStart ).count() >= flDrainBudgetSeconds )
			break; // budget spent — the rest drains at r_shader_lazy_hz, no multi-second freeze

		pending_shader_t p = g_PendingShaders[0];
		g_iNumPendingShaders--;

		if( g_iNumPendingShaders > 0 )
			memmove( &g_PendingShaders[0], &g_PendingShaders[1], g_iNumPendingShaders * sizeof( pending_shader_t ));

		// may already have been compiled by an inline path meanwhile
		unsigned int hash = COM_HashKey( va( "%s %s", p.glname, p.options ), SHADERS_HASH_SIZE );
		bool bExists = false;

		for( glsl_program_t *prog = glsl_programsHashTable[hash]; prog != NULL; prog = prog->nextHash )
		{
			if( !Q_strcmp( prog->name, p.glname ) && !Q_strcmp( prog->options, p.options ))
			{
				bExists = true;
				break;
			}
		}

		if( !bExists )
		{
			g_flShaderTokens -= 1.0f;
			GL_FindUberShader( p.glname, p.options, p.pfnInitUniforms );
		}
	}
}

int R_ShadersPendingCount( void ) { return g_iNumPendingShaders; }
int R_ShadersDeferredTotal( void ) { return g_iShadersDeferredTotal; }

static glsl_program_t *GL_FindUberShader( const char *glname, const char *options, pfnShaderCallback pfnInitUniforms )
{
	const char	*find;
	unsigned int	i, hash;
	glsl_program_t	*prog;

	if( !GL_Support( R_SHADER_GLSL100_EXT ))
		return NULL;

	ASSERT( glname != NULL );

	if( !options ) options = "";

	find = va( "%s %s", glname, options );
	hash = COM_HashKey( find, SHADERS_HASH_SIZE );

	// check for coexist
	for( prog = glsl_programsHashTable[hash]; prog != NULL; prog = prog->nextHash )
	{
		if( !Q_strcmp( prog->name, glname ) && !Q_strcmp( prog->options, options ))
		{
			// half-D v0.8.2: negative cache, same as GL_InitGPUShader above.
			// A handle-0 entry is a permutation that already failed to
			// compile/link on this driver. Returning it made the caller
			// treat the dead program as success, while GL_BindShader (which
			// checks ->handle) silently kept the PREVIOUS shader bound —
			// those surfaces drew with a wrong, stale shader: the
			// "no texture / garbage" look. Return NULL so the caller skips
			// the surface this frame instead of drawing it wrong.
			if( !prog->handle )
				return NULL;

			prog->status |= SHADER_STATUS_HASHED;
			return prog;
		}
	}

	// half-D v0.8.1: rate limiter. A compile through gl4es costs ~460 ms —
	// any in-frame compile is a stall, so cap how many can happen per
	// second (r_shader_lazy_hz, default 2). Out of tokens: queue the real
	// permutation and return NULL. Every caller handles NULL by skipping
	// that surface/mesh this frame and retrying the next one — no wrong
	// permutation is ever drawn (the v0.8.0 family fallback drew blanks).
	if( r_shader_lazy_hz && r_shader_lazy_hz->value > 0.0f )
	{
		if( g_flShaderTokens < 1.0f )
		{
			R_QueuePendingShader( glname, options, pfnInitUniforms );
			return NULL;
		}

		g_flShaderTokens -= 1.0f;
	}

	// find free spot
	for( i = 1; i < num_glsl_programs; i++ )
		if( !glsl_programs[i].name[0] )
			break;

	if( i >= MAX_GLSL_PROGRAMS )
	{
		ALERT( at_error, "GL_FindUberShader: GLSL shaders limit exceeded (%i max)\n", MAX_GLSL_PROGRAMS );
		return NULL;
	}

	prog = GL_CreateUberShader( i, glname, options, pfnInitUniforms );

	if( prog != NULL )
	{
		// add to hash table
		prog->nextHash = glsl_programsHashTable[hash];
		glsl_programsHashTable[hash] = prog;
	}

	return prog;
}

void GL_AddShaderDirective( char *options, const char *directive )
{
	Q_strncat( options, va( "#define %s\n", directive ), MAX_OPTIONS_LENGTH );
}

void GL_AddShaderDefine( char *options, const char *directive )
{
	Q_strncat( options, directive, MAX_OPTIONS_LENGTH );
}

bool GL_FindShaderDirective( glsl_program_t *shader, const char *directive )
{
	if( Q_stristr( shader->options, directive ))
		return true;
	return false;
}

static void GL_InitSolidBmodelUniforms( glsl_program_t *shader )
{
	ASSERT( shader != NULL );

	const bool bWater = GL_FindShaderDirective( shader, "BMODEL_WATER" );
	const bool bRefractedWater = (bWater && GL_FindShaderDirective( shader, "BMODEL_WATER_REFRACTION" ));

	shader->u_ColorMap = pglGetUniformLocationARB( shader->handle, "u_ColorMap" );
	shader->u_LightMap = pglGetUniformLocationARB( shader->handle, "u_LightMap" );
	shader->u_ScreenMap = pglGetUniformLocationARB( shader->handle, "u_ScreenMap" );
	shader->u_NormalMap = pglGetUniformLocationARB( shader->handle, "u_NormalMap" );

	if( GL_FindShaderDirective( shader, "REFLECTION_CUBEMAP" ) ) // diffusioncubemaps
	{
		shader->u_CubemapBox = pglGetUniformLocationARB( shader->handle, "u_CubemapBox" );
		shader->u_Cubemap = pglGetUniformLocationARB( shader->handle, "u_Cubemap" );
		shader->u_ReflectScale = pglGetUniformLocationARB( shader->handle, "u_ReflectScale" );
	}

	if( GL_FindShaderDirective( shader, "BMODEL_INTERIOR" ) )
	{
		shader->u_InteriorMap = pglGetUniformLocationARB( shader->handle, "u_InteriorMap" );
		shader->u_RealTime = pglGetUniformLocationARB( shader->handle, "u_RealTime" );
		shader->u_InteriorParams = pglGetUniformLocationARB( shader->handle, "u_InteriorParams" );
	}
	else if( GL_FindShaderDirective( shader, "BMODEL_REFLECTION_PLANAR" ) || GL_FindShaderDirective( shader, "BMODEL_WATER_PLANAR" ) )
	{
		shader->u_PlanarReflectScale = pglGetUniformLocationARB( shader->handle, "u_PlanarReflectScale" );
		if( GL_FindShaderDirective( shader, "BMODEL_WATER_PLANAR" ) )
			shader->u_WaterTex = pglGetUniformLocationARB( shader->handle, "u_WaterTex" );
	}

	shader->u_BrushParams = pglGetUniformLocationARB( shader->handle, "u_BrushParams" );

	if( bRefractedWater )
	{
		shader->u_ScreenSizeInv = pglGetUniformLocationARB( shader->handle, "u_ScreenSizeInv" );
	//	shader->u_zFar = pglGetUniformLocationARB( shader->handle, "u_zFar" );
	}

	if( !bWater )
		shader->u_DeluxeMap = pglGetUniformLocationARB( shader->handle, "u_DeluxeMap" );

	if( GL_FindShaderDirective( shader, "BMODEL_SPECULAR" ) )
	{
		shader->u_GlossScale = pglGetUniformLocationARB( shader->handle, "u_GlossScale" );
		shader->u_GlossSmoothness = pglGetUniformLocationARB( shader->handle, "u_GlossSmoothness" );
	}

	if( GL_FindShaderDirective( shader, "BMODEL_EMBOSS" ) )
		shader->u_EmbossScale = pglGetUniformLocationARB( shader->handle, "u_EmbossScale" );

	shader->u_Fresnel = pglGetUniformLocationARB( shader->handle, "u_Fresnel" );

	if( GL_FindShaderDirective( shader, "BMODEL_MULTI_LAYERS" ) )
		shader->u_LayerMap = pglGetUniformLocationARB( shader->handle, "u_LayerMap" );

	shader->u_DepthMap = pglGetUniformLocationARB( shader->handle, "u_DepthMap" );

	shader->u_LightStyleValues = pglGetUniformLocationARB( shader->handle, "u_LightStyleValues" );
	shader->u_ModelMatrix = pglGetUniformLocationARB( shader->handle, "u_ModelMatrix" );
	shader->u_TexOffset = pglGetUniformLocationARB( shader->handle, "u_TexOffset" );
	shader->u_RenderColor = pglGetUniformLocationARB( shader->handle, "u_RenderColor" );

	// absent on non-sun variants, so these resolve to -1 there
	shader->u_SunShadowMap = pglGetUniformLocationARB( shader->handle, "u_SunShadowMap" );
	shader->u_SunMatrix = pglGetUniformLocationARB( shader->handle, "u_SunMatrix" );
	shader->u_SunShadowParams = pglGetUniformLocationARB( shader->handle, "u_SunShadowParams" );

	GL_BindShader( shader );
	pglUniform1iARB( shader->u_ColorMap, GL_TEXTURE0 );
	pglUniform1iARB( shader->u_LightMap, GL_TEXTURE1 );
	if( !bWater )
		pglUniform1iARB( shader->u_DeluxeMap, GL_TEXTURE2 );
	pglUniform1iARB( shader->u_ScreenMap, GL_TEXTURE3 );
	pglUniform1iARB( shader->u_NormalMap, GL_TEXTURE4 );

	if( GL_FindShaderDirective( shader, "BMODEL_MULTI_LAYERS" ) )
		pglUniform1iARB( shader->u_LayerMap, GL_TEXTURE5 );
	else if( GL_FindShaderDirective( shader, "BMODEL_INTERIOR" ) )
		pglUniform1iARB( shader->u_InteriorMap, GL_TEXTURE5 );
	else if( GL_FindShaderDirective( shader, "BMODEL_WATER_PLANAR" ) )
		pglUniform1iARB( shader->u_WaterTex, GL_TEXTURE5 );

	if( bRefractedWater )
		pglUniform1iARB( shader->u_DepthMap, GL_TEXTURE6 );

	if( GL_FindShaderDirective( shader, "BMODEL_SUN_SHADOW" ) )
		pglUniform1iARB( shader->u_SunShadowMap, GL_TEXTURE7 );

	if( GL_FindShaderDirective( shader, "REFLECTION_CUBEMAP" ) )  // diffusioncubemaps
	{
		if( bWater )
			pglUniform1iARB( shader->u_CubemapBox, GL_TEXTURE2 ); // water won't use deluxmap so I'll use it here
		else
			pglUniform1iARB( shader->u_CubemapBox, GL_TEXTURE6 ); // other brushes won't use depthmap so I'll use it here
	}

	GL_BindShader( GL_NONE );

	GL_ValidateProgram( shader );
	GL_ShowProgramUniforms( shader );
}

static void GL_InitBmodelDlightUniforms( glsl_program_t *shader )
{
	ASSERT( shader != NULL );

	shader->u_ColorMap = pglGetUniformLocationARB( shader->handle, "u_ColorMap" );
	shader->u_ProjectMap = pglGetUniformLocationARB( shader->handle, "u_ProjectMap" );
	shader->u_ShadowMap = pglGetUniformLocationARB( shader->handle, "u_ShadowMap" ); // shadow2D or shadowCube
	shader->u_NormalMap = pglGetUniformLocationARB( shader->handle, "u_NormalMap" );
	if( GL_FindShaderDirective( shader, "BMODEL_WATER_REFRACTION" ) )
	{
		shader->u_DepthMap = pglGetUniformLocationARB( shader->handle, "u_DepthMap" );
		shader->u_ScreenSizeInv = pglGetUniformLocationARB( shader->handle, "u_ScreenSizeInv" );
	}

	if( GL_FindShaderDirective( shader, "BMODEL_MULTI_LAYERS" ))
		shader->u_LayerMap = pglGetUniformLocationARB( shader->handle, "u_LayerMap" );

	shader->u_ModelMatrix = pglGetUniformLocationARB( shader->handle, "u_ModelMatrix" );
	shader->u_TexOffset = pglGetUniformLocationARB( shader->handle, "u_TexOffset" );
	shader->u_LightViewProjectionMatrix = pglGetUniformLocationARB( shader->handle, "u_LightViewProjectionMatrix" );
	shader->u_ScreenSizeInv = pglGetUniformLocationARB( shader->handle, "u_ScreenSizeInv" );
	shader->u_RenderColor = pglGetUniformLocationARB( shader->handle, "u_RenderColor" );
	shader->u_DynLightBrightness = pglGetUniformLocationARB( shader->handle, "u_DynLightBrightness" );
	shader->u_LightParams = pglGetUniformLocationARB( shader->handle, "u_LightParams" );
	if( GL_FindShaderDirective( shader, "BMODEL_SPECULAR" ) )
	{
		shader->u_GlossScale = pglGetUniformLocationARB( shader->handle, "u_GlossScale" );
		shader->u_GlossSmoothness = pglGetUniformLocationARB( shader->handle, "u_GlossSmoothness" );
	}
	if( GL_FindShaderDirective( shader, "BMODEL_EMBOSS" ) )
		shader->u_EmbossScale = pglGetUniformLocationARB( shader->handle, "u_EmbossScale" );

	if( GL_FindShaderDirective( shader, "BMODEL_INTERIOR" ) )
	{
		shader->u_InteriorMap = pglGetUniformLocationARB( shader->handle, "u_InteriorMap" );
		shader->u_RealTime = pglGetUniformLocationARB( shader->handle, "u_RealTime" );
		shader->u_InteriorParams = pglGetUniformLocationARB( shader->handle, "u_InteriorParams" );
	}

	GL_BindShader( shader );
	pglUniform1iARB( shader->u_ColorMap, GL_TEXTURE0 );
	pglUniform1iARB( shader->u_ProjectMap, GL_TEXTURE2 );	// projection lights only
	pglUniform1iARB( shader->u_ShadowMap, GL_TEXTURE3 );	// shadowmap or cubemap
	if( GL_FindShaderDirective( shader, "BMODEL_WATER_REFRACTION" ) )
		pglUniform1iARB( shader->u_DepthMap, GL_TEXTURE4 );

	if( GL_FindShaderDirective( shader, "BMODEL_MULTI_LAYERS" ))
		pglUniform1iARB( shader->u_LayerMap, GL_TEXTURE5 );

	pglUniform1iARB( shader->u_NormalMap, GL_TEXTURE6 );

	if( GL_FindShaderDirective( shader, "BMODEL_INTERIOR" ) )
		pglUniform1iARB( shader->u_InteriorMap, GL_TEXTURE7 );

	GL_BindShader( GL_NONE );

	GL_ValidateProgram( shader );
	GL_ShowProgramUniforms( shader );
}

static void GL_InitBmodelDepthFillUniforms( glsl_program_t *shader )
{
	ASSERT( shader != NULL );

	shader->u_ColorMap = pglGetUniformLocationARB( shader->handle, "u_ColorMap" );
	shader->u_TexOffset = pglGetUniformLocationARB( shader->handle, "u_TexOffset" );
	shader->u_ModelMatrix = pglGetUniformLocationARB( shader->handle, "u_ModelMatrix" );

	GL_BindShader( shader );
	pglUniform1iARB( shader->u_ColorMap, GL_TEXTURE0 );
	GL_BindShader( GL_NONE );

	GL_ValidateProgram( shader );
	GL_ShowProgramUniforms( shader );
}

static void GL_InitBmodelDecalUniforms( glsl_program_t *shader )
{
	ASSERT( shader != NULL );

	shader->u_DecalMap = pglGetUniformLocationARB( shader->handle, "u_DecalMap" );
	shader->u_ColorMap = pglGetUniformLocationARB( shader->handle, "u_ColorMap" );
	shader->u_LightMap = pglGetUniformLocationARB( shader->handle, "u_LightMap" );

	shader->u_LightStyleValues = pglGetUniformLocationARB( shader->handle, "u_LightStyleValues" );
	shader->u_ModelMatrix = pglGetUniformLocationARB( shader->handle, "u_ModelMatrix" );
	shader->u_FogParams = pglGetUniformLocationARB( shader->handle, "u_FogParams" );

	GL_BindShader( shader );
	pglUniform1iARB( shader->u_DecalMap, GL_TEXTURE0 );
	pglUniform1iARB( shader->u_ColorMap, GL_TEXTURE1 ); // detect alpha holes
	pglUniform1iARB( shader->u_LightMap, GL_TEXTURE2 );
	GL_BindShader( GL_NONE );

	GL_ValidateProgram( shader );
	GL_ShowProgramUniforms( shader );
}

static void GL_InitSolidStudioUniforms( glsl_program_t *shader )
{
	ASSERT( shader != NULL );

	shader->u_ColorMap = pglGetUniformLocationARB( shader->handle, "u_ColorMap" );
	shader->u_BoneQuaternion = pglGetUniformLocationARB( shader->handle, "u_BoneQuaternion" );
	shader->u_BonePosition = pglGetUniformLocationARB( shader->handle, "u_BonePosition" );
	if( GL_FindShaderDirective( shader, "STUDIO_SPECULAR" ) )
	{
		shader->u_GlossScale = pglGetUniformLocationARB( shader->handle, "u_GlossScale" );
		shader->u_GlossSmoothness = pglGetUniformLocationARB( shader->handle, "u_GlossSmoothness" );
	}
	if( GL_FindShaderDirective( shader, "STUDIO_EMBOSS" ) )
		shader->u_EmbossScale = pglGetUniformLocationARB( shader->handle, "u_EmbossScale" );
	shader->u_NormalMap = pglGetUniformLocationARB( shader->handle, "u_NormalMap" );
	shader->u_ColorMask = pglGetUniformLocationARB( shader->handle, "u_ColorMask" );
	shader->u_MeshParams = pglGetUniformLocationARB( shader->handle, "u_MeshParams" );
	shader->u_StudioLighting = pglGetUniformLocationARB( shader->handle, "u_StudioLighting" );
	shader->u_AmbientCube = pglGetUniformLocationARB( shader->handle, "u_AmbientCube" );

	// absent on non-sun variants, so these resolve to -1 there
	shader->u_SunShadowMap = pglGetUniformLocationARB( shader->handle, "u_SunShadowMap" );
	shader->u_SunMatrix = pglGetUniformLocationARB( shader->handle, "u_SunMatrix" );
	shader->u_SunShadowParams = pglGetUniformLocationARB( shader->handle, "u_SunShadowParams" );
	shader->u_SunParams = pglGetUniformLocationARB( shader->handle, "u_SunParams" );

	if( GL_FindShaderDirective( shader, "REFLECTION_CUBEMAP" ) ) // diffusioncubemaps
	{
		shader->u_CubemapBox = pglGetUniformLocationARB( shader->handle, "u_CubemapBox" );
		shader->u_Cubemap = pglGetUniformLocationARB( shader->handle, "u_Cubemap" );
		shader->u_ReflectScale = pglGetUniformLocationARB( shader->handle, "u_ReflectScale" );
		shader->u_Fresnel = pglGetUniformLocationARB( shader->handle, "u_Fresnel" );
	}
	
	if( GL_FindShaderDirective( shader, "STUDIO_INTERIOR" ) )
	{
		shader->u_InteriorMap = pglGetUniformLocationARB( shader->handle, "u_InteriorMap" );
		shader->u_InteriorParams = pglGetUniformLocationARB( shader->handle, "u_InteriorParams" );
	}
	else if( GL_FindShaderDirective( shader, "STUDIO_TEXTURE_BLEND" ) )
	{
		shader->u_BlendTexture = pglGetUniformLocationARB( shader->handle, "u_BlendTexture" );
	}

	if( GL_FindShaderDirective( shader, "STUDIO_VERTEX_LIGHTING" ))
	{ 
		shader->u_GammaTable = pglGetUniformLocationARB( shader->handle, "u_GammaTable" );
	}

	shader->u_RenderColor = pglGetUniformLocationARB( shader->handle, "u_RenderColor" );
	shader->u_StudioParams = pglGetUniformLocationARB( shader->handle, "u_StudioParams" );
	if( GL_FindShaderDirective( shader, "STUDIO_SWAY_FOLIAGE" ) )
		shader->u_FoliageSwayHeight = pglGetUniformLocationARB( shader->handle, "u_FoliageSwayHeight" );

	GL_BindShader( shader );
	pglUniform1iARB( shader->u_ColorMap, GL_TEXTURE0 );
	pglUniform1iARB( shader->u_NormalMap, GL_TEXTURE1 );
	if( GL_FindShaderDirective( shader, "REFLECTION_CUBEMAP" ) )  // diffusioncubemaps
		pglUniform1iARB( shader->u_CubemapBox, GL_TEXTURE2 );
	
	if( GL_FindShaderDirective( shader, "STUDIO_INTERIOR" ) )
		pglUniform1iARB( shader->u_InteriorMap, GL_TEXTURE4 );
	else if( GL_FindShaderDirective( shader, "STUDIO_TEXTURE_BLEND" ) ) // they can't be together
	{
		pglUniform1iARB( shader->u_BlendTexture, GL_TEXTURE4 );
		// the amount of dirt on the car is being sent through u_MeshParams[2].y
	}

	if( GL_FindShaderDirective( shader, "STUDIO_HAS_COLORMASK" ) )
		pglUniform1iARB( shader->u_ColorMask, GL_TEXTURE5 );

	if( GL_FindShaderDirective( shader, "STUDIO_SUN_SHADOW" ) )
		pglUniform1iARB( shader->u_SunShadowMap, GL_TEXTURE6 );

	GL_BindShader( GL_NONE );

	GL_ValidateProgram( shader );
	GL_ShowProgramUniforms( shader );
}

static void GL_InitStudioDlightUniforms( glsl_program_t *shader )
{
	ASSERT( shader != NULL );

	shader->u_ColorMap = pglGetUniformLocationARB( shader->handle, "u_ColorMap" );
	shader->u_ProjectMap = pglGetUniformLocationARB( shader->handle, "u_ProjectMap" );
	shader->u_ShadowMap = pglGetUniformLocationARB( shader->handle, "u_ShadowMap" );
	shader->u_BoneQuaternion = pglGetUniformLocationARB( shader->handle, "u_BoneQuaternion" );
	shader->u_BonePosition = pglGetUniformLocationARB( shader->handle, "u_BonePosition" );
	shader->u_DynLightBrightness = pglGetUniformLocationARB( shader->handle, "u_DynLightBrightness" );
	shader->u_RenderColor = pglGetUniformLocationARB( shader->handle, "u_RenderColor" );
	if( GL_FindShaderDirective( shader, "STUDIO_SPECULAR" ) )
	{
		shader->u_GlossScale = pglGetUniformLocationARB( shader->handle, "u_GlossScale" );
		shader->u_GlossSmoothness = pglGetUniformLocationARB( shader->handle, "u_GlossSmoothness" );
	}
	if( GL_FindShaderDirective( shader, "STUDIO_EMBOSS" ) )
		shader->u_EmbossScale = pglGetUniformLocationARB( shader->handle, "u_EmbossScale" );
	shader->u_NormalMap = pglGetUniformLocationARB( shader->handle, "u_NormalMap" );
	shader->u_ColorMask = pglGetUniformLocationARB( shader->handle, "u_ColorMask" );
	shader->u_LightViewProjectionMatrix = pglGetUniformLocationARB( shader->handle, "u_LightViewProjectionMatrix" );
	shader->u_LightParams = pglGetUniformLocationARB( shader->handle, "u_LightParams" );
	shader->u_FogParams = pglGetUniformLocationARB( shader->handle, "u_FogParams" );
	shader->u_MeshParams = pglGetUniformLocationARB( shader->handle, "u_MeshParams" );

	if( GL_FindShaderDirective( shader, "STUDIO_INTERIOR" ) )
	{
		shader->u_InteriorMap = pglGetUniformLocationARB( shader->handle, "u_InteriorMap" );
		shader->u_InteriorParams = pglGetUniformLocationARB( shader->handle, "u_InteriorParams" );
	}
	else if( GL_FindShaderDirective( shader, "STUDIO_TEXTURE_BLEND" ) )
	{
		shader->u_BlendTexture = pglGetUniformLocationARB( shader->handle, "u_BlendTexture" );
	}

	shader->u_RealTime = pglGetUniformLocationARB( shader->handle, "u_RealTime" );
	shader->u_FoliageSwayHeight = pglGetUniformLocationARB( shader->handle, "u_FoliageSwayHeight" );

	GL_BindShader( shader );
	pglUniform1iARB( shader->u_ColorMap, GL_TEXTURE0 );
	pglUniform1iARB( shader->u_ProjectMap, GL_TEXTURE1 );
	pglUniform1iARB( shader->u_ShadowMap, GL_TEXTURE2 ); // optional
	pglUniform1iARB( shader->u_NormalMap, GL_TEXTURE3 );
	if( GL_FindShaderDirective( shader, "STUDIO_INTERIOR" ) )
		pglUniform1iARB( shader->u_InteriorMap, GL_TEXTURE4 );
	else if( GL_FindShaderDirective( shader, "STUDIO_TEXTURE_BLEND" ) )
		pglUniform1iARB( shader->u_BlendTexture, GL_TEXTURE4 );

	if( GL_FindShaderDirective( shader, "STUDIO_HAS_COLORMASK" ) )
		pglUniform1iARB( shader->u_ColorMask, GL_TEXTURE5 );

	GL_BindShader( GL_NONE );

	GL_ValidateProgram( shader );
	GL_ShowProgramUniforms( shader );
}

static void GL_InitStudioDepthFillUniforms( glsl_program_t *shader )
{
	ASSERT( shader != NULL );

	shader->u_ColorMap = pglGetUniformLocationARB( shader->handle, "u_ColorMap" );
	shader->u_RealTime = pglGetUniformLocationARB( shader->handle, "u_RealTime" );
	shader->u_FoliageSwayHeight = pglGetUniformLocationARB( shader->handle, "u_FoliageSwayHeight" );

	shader->u_BoneQuaternion = pglGetUniformLocationARB( shader->handle, "u_BoneQuaternion" );
	shader->u_BonePosition = pglGetUniformLocationARB( shader->handle, "u_BonePosition" );

	GL_BindShader( shader );
	pglUniform1iARB( shader->u_ColorMap, GL_TEXTURE0 );
	GL_BindShader( GL_NONE );

	GL_ValidateProgram( shader );
	GL_ShowProgramUniforms( shader );
}

static void GL_InitGrassSolidUniforms( glsl_program_t *shader )
{
	ASSERT( shader != NULL );

	shader->u_ColorMap = pglGetUniformLocationARB( shader->handle, "u_ColorMap" );
	shader->u_NormalMap = pglGetUniformLocationARB( shader->handle, "u_NormalMap" );

	shader->u_ModelMatrix = pglGetUniformLocationARB( shader->handle, "u_ModelMatrix" );
	shader->u_LightStyleValues = pglGetUniformLocationARB( shader->handle, "u_LightStyleValues" );
	shader->u_GammaTable = pglGetUniformLocationARB( shader->handle, "u_GammaTable" );
	shader->u_GrassParams = pglGetUniformLocationARB( shader->handle, "u_GrassParams" );
	shader->u_GenericCondition = pglGetUniformLocationARB( shader->handle, "u_GenericCondition" );
	shader->u_GrassWind = pglGetUniformLocationARB( shader->handle, "u_GrassWind" );

	// only on the sun variant, -1 elsewhere
	shader->u_SunShadowMap = pglGetUniformLocationARB( shader->handle, "u_SunShadowMap" );
	shader->u_SunMatrix = pglGetUniformLocationARB( shader->handle, "u_SunMatrix" );
	shader->u_SunShadowParams = pglGetUniformLocationARB( shader->handle, "u_SunShadowParams" );

	GL_BindShader( shader );
	pglUniform1iARB( shader->u_ColorMap, GL_TEXTURE0 );
	pglUniform1iARB( shader->u_NormalMap, GL_TEXTURE1 );
	if( shader->u_SunShadowMap != -1 )
		pglUniform1iARB( shader->u_SunShadowMap, GL_TEXTURE2 );
	GL_BindShader( GL_NONE );

	GL_ValidateProgram( shader );
	GL_ShowProgramUniforms( shader );
}

static void GL_InitGrassDlightUniforms( glsl_program_t *shader )
{
	ASSERT( shader != NULL );

	shader->u_ColorMap = pglGetUniformLocationARB( shader->handle, "u_ColorMap" );
	shader->u_ProjectMap = pglGetUniformLocationARB( shader->handle, "u_ProjectMap" );
	shader->u_ShadowMap = pglGetUniformLocationARB( shader->handle, "u_ShadowMap" );
	shader->u_NormalMap = pglGetUniformLocationARB( shader->handle, "u_NormalMap" );

	shader->u_LightViewProjectionMatrix = pglGetUniformLocationARB( shader->handle, "u_LightViewProjectionMatrix" );
	shader->u_ModelMatrix = pglGetUniformLocationARB( shader->handle, "u_ModelMatrix" );
	shader->u_LightParams = pglGetUniformLocationARB( shader->handle, "u_LightParams" );
	shader->u_GenericCondition = pglGetUniformLocationARB( shader->handle, "u_GenericCondition" );
	shader->u_GrassWind = pglGetUniformLocationARB( shader->handle, "u_GrassWind" );

	GL_BindShader( shader );
	pglUniform1iARB( shader->u_ColorMap, GL_TEXTURE0 );
	pglUniform1iARB( shader->u_ProjectMap, GL_TEXTURE1 );// projection texture or XY attenuation
	pglUniform1iARB( shader->u_ShadowMap, GL_TEXTURE2 ); // shadowmap or cubemap
	pglUniform1iARB( shader->u_NormalMap, GL_TEXTURE3 );
	GL_BindShader( GL_NONE );

	GL_ValidateProgram( shader );
	GL_ShowProgramUniforms( shader );
}

static void GL_InitGenericDlightUniforms( glsl_program_t *shader )
{
	ASSERT( shader != NULL );

	shader->u_ColorMap = pglGetUniformLocationARB( shader->handle, "u_ColorMap" );
	shader->u_ProjectMap = pglGetUniformLocationARB( shader->handle, "u_ProjectMap" );
	shader->u_ShadowMap = pglGetUniformLocationARB( shader->handle, "u_ShadowMap" );

	shader->u_LightViewProjectionMatrix = pglGetUniformLocationARB( shader->handle, "u_LightViewProjectionMatrix" );
	shader->u_LightParams = pglGetUniformLocationARB( shader->handle, "u_LightParams" );

	GL_BindShader( shader );
	pglUniform1iARB( shader->u_ColorMap, GL_TEXTURE0 );
	pglUniform1iARB( shader->u_ProjectMap, GL_TEXTURE1 );
	pglUniform1iARB( shader->u_ShadowMap, GL_TEXTURE2 ); // optional
	GL_BindShader( GL_NONE );

	GL_ValidateProgram( shader );
	GL_ShowProgramUniforms( shader );
}

#if !STUDIODECAL_DISABLE
static void GL_InitStudioDecalUniforms( glsl_program_t *shader )
{
	ASSERT( shader != NULL );

	shader->u_DecalMap = pglGetUniformLocationARB( shader->handle, "u_DecalMap" );
	shader->u_ColorMap = pglGetUniformLocationARB( shader->handle, "u_ColorMap" );
	shader->u_FogParams = pglGetUniformLocationARB( shader->handle, "u_FogParams" );

	GL_BindShader( shader );
	pglUniform1iARB( shader->u_DecalMap, GL_TEXTURE0 );
	pglUniform1iARB( shader->u_ColorMap, GL_TEXTURE1 );
	GL_BindShader( GL_NONE );

	GL_ValidateProgram( shader );
	GL_ShowProgramUniforms( shader );
}
#endif

static void GL_InitSkyBoxUniforms( glsl_program_t *shader )
{
	ASSERT( shader != NULL );

	shader->u_ColorMap = pglGetUniformLocationARB( shader->handle, "u_ColorMap" );

	shader->u_LightDir = pglGetUniformLocationARB( shader->handle, "u_LightDir" );
	shader->u_LightDiffuse = pglGetUniformLocationARB( shader->handle, "u_LightDiffuse" );
	shader->u_ViewOrigin = pglGetUniformLocationARB( shader->handle, "u_ViewOrigin" );
	shader->u_FogParams = pglGetUniformLocationARB( shader->handle, "u_FogParams" );
	shader->u_GenericCondition = pglGetUniformLocationARB( shader->handle, "u_GenericCondition" );

	GL_BindShader( shader );
	pglUniform1iARB( shader->u_ColorMap, GL_TEXTURE0 );
	GL_BindShader( GL_NONE );

	GL_ValidateProgram( shader );
	GL_ShowProgramUniforms( shader );
}

static void GL_InitPostProcessUniforms( glsl_program_t *shader )
{
	ASSERT( shader != NULL );

	shader->u_BlurFactor = pglGetUniformLocationARB( shader->handle, "u_BlurFactor" );
	shader->u_ScreenMap = pglGetUniformLocationARB( shader->handle, "u_ScreenMap" );

	GL_BindShader( shader );
	pglUniform1iARB( shader->u_ScreenMap, GL_TEXTURE0 );
	GL_BindShader( GL_NONE );

	GL_ValidateProgram( shader );
	GL_ShowProgramUniforms( shader );
}

static void GL_InitBilateralBlurUniforms( glsl_program_t *shader )
{
	ASSERT( shader != NULL );

	shader->u_ScreenMap = pglGetUniformLocationARB( shader->handle, "u_ScreenMap" );
	shader->u_DepthMap = pglGetUniformLocationARB( shader->handle, "u_DepthMap" );
	shader->u_ScreenSizeInv = pglGetUniformLocationARB( shader->handle, "u_ScreenSizeInv" );
	shader->u_zFar = pglGetUniformLocationARB( shader->handle, "u_zFar" );

	GL_BindShader( shader );
	pglUniform1iARB( shader->u_ScreenMap, GL_TEXTURE0 );
	pglUniform1iARB( shader->u_DepthMap, GL_TEXTURE1 );
	GL_BindShader( GL_NONE );

	GL_ValidateProgram( shader );
	GL_ShowProgramUniforms( shader );
}

static void GL_InitGenericFogUniforms( glsl_program_t *shader )
{
	ASSERT( shader != NULL );

	shader->u_ColorMap = pglGetUniformLocationARB( shader->handle, "u_ColorMap" );

	shader->u_FogParams = pglGetUniformLocationARB( shader->handle, "u_FogParams" );

	GL_BindShader( shader );
	pglUniform1iARB( shader->u_ColorMap, GL_TEXTURE0 );
	GL_BindShader( GL_NONE );

	GL_ValidateProgram( shader );
	GL_ShowProgramUniforms( shader );
}

static void GL_InitGenShaftsUniforms( glsl_program_t *shader )
{
	ASSERT( shader != NULL );

	shader->u_DepthMap = pglGetUniformLocationARB( shader->handle, "u_DepthMap" );
	shader->u_ScreenMap = pglGetUniformLocationARB( shader->handle, "u_ScreenMap" );
	shader->u_zFar = pglGetUniformLocationARB( shader->handle, "u_zFar" );

	GL_BindShader( shader );
	pglUniform1iARB( shader->u_DepthMap, GL_TEXTURE0 );
	pglUniform1iARB( shader->u_ScreenMap, GL_TEXTURE1 );
	GL_BindShader( GL_NONE );

	GL_ValidateProgram( shader );
	GL_ShowProgramUniforms( shader );
}

static void GL_InitDrawShaftsUniforms( glsl_program_t *shader )
{
	ASSERT( shader != NULL );

	shader->u_ColorMap = pglGetUniformLocationARB( shader->handle, "u_ColorMap" );
	shader->u_DepthMap = pglGetUniformLocationARB( shader->handle, "u_DepthMap" );
	shader->u_ScreenMap = pglGetUniformLocationARB( shader->handle, "u_ScreenMap" );
	shader->u_Sunshafts = pglGetUniformLocationARB( shader->handle, "u_Sunshafts" );

	GL_BindShader( shader );
	pglUniform1iARB( shader->u_ScreenMap, GL_TEXTURE0 );
	pglUniform1iARB( shader->u_ColorMap, GL_TEXTURE1 );
	pglUniform1iARB( shader->u_DepthMap, GL_TEXTURE2 );
	GL_BindShader( GL_NONE );

	GL_ValidateProgram( shader );
	GL_ShowProgramUniforms( shader );
}

static void GL_InitGenSSAOUniforms( glsl_program_t *shader )
{
	ASSERT( shader != NULL );

	shader->u_DepthMap = pglGetUniformLocationARB( shader->handle, "u_DepthMap" );
	shader->u_zFar = pglGetUniformLocationARB( shader->handle, "u_zFar" );

	GL_BindShader( shader );
	pglUniform1iARB( shader->u_DepthMap, GL_TEXTURE0 );
	GL_BindShader( GL_NONE );

	GL_ValidateProgram( shader );
	GL_ShowProgramUniforms( shader );
}

static void GL_InitGenHBAOUniforms( glsl_program_t *shader )
{
	ASSERT( shader != NULL );

	shader->u_DepthMap = pglGetUniformLocationARB( shader->handle, "u_DepthMap" );
	shader->u_HBAOParams = pglGetUniformLocationARB( shader->handle, "u_HBAOParams" );

	GL_BindShader( shader );
	pglUniform1iARB( shader->u_DepthMap, GL_TEXTURE0 );
	GL_BindShader( GL_NONE );

	GL_ValidateProgram( shader );
	GL_ShowProgramUniforms( shader );
}

static void GL_InitDrawSSAOUniforms( glsl_program_t *shader )
{
	ASSERT( shader != NULL );

	shader->u_ScreenMap = pglGetUniformLocationARB( shader->handle, "u_ScreenMap" );
	shader->u_AOMap = pglGetUniformLocationARB( shader->handle, "u_AOMap" );
	shader->u_DepthMap = pglGetUniformLocationARB( shader->handle, "u_DepthMap" );
	shader->u_GenericCondition = pglGetUniformLocationARB( shader->handle, "u_GenericCondition" );
	shader->u_FogParams = pglGetUniformLocationARB( shader->handle, "u_FogParams" );
	shader->u_zFar = pglGetUniformLocationARB( shader->handle, "u_zFar" );

	GL_BindShader( shader );
	pglUniform1iARB( shader->u_ScreenMap, GL_TEXTURE0 );
	pglUniform1iARB( shader->u_AOMap, GL_TEXTURE1 );
	pglUniform1iARB( shader->u_DepthMap, GL_TEXTURE2 );
	GL_BindShader( GL_NONE );

	GL_ValidateProgram( shader );
	GL_ShowProgramUniforms( shader );
}

static void GL_InitMotionBlurUniforms( glsl_program_t *shader )
{
	ASSERT( shader != NULL );

	shader->u_ColorMap = pglGetUniformLocationARB( shader->handle, "u_ColorMap" );
	shader->u_MotionBlur = pglGetUniformLocationARB( shader->handle, "u_MotionBlur" );

	GL_BindShader( shader );
	pglUniform1iARB( shader->u_ColorMap, GL_TEXTURE0 );
	GL_BindShader( GL_NONE );

	GL_ValidateProgram( shader );
	GL_ShowProgramUniforms( shader );
}

static void GL_InitHorizontalBlurUniforms( glsl_program_t *shader )
{
	ASSERT( shader != NULL );

	shader->u_ColorMap = pglGetUniformLocationARB( shader->handle, "u_ColorMap" );
	shader->u_Accum = pglGetUniformLocationARB( shader->handle, "u_Accum" );

	GL_BindShader( shader );
	pglUniform1iARB( shader->u_ColorMap, GL_TEXTURE0 );
	GL_BindShader( GL_NONE );

	GL_ValidateProgram( shader );
	GL_ShowProgramUniforms( shader );
}

static void GL_InitHeatDistortionUniforms( glsl_program_t *shader )
{
	ASSERT( shader != NULL );

	shader->u_ColorMap = pglGetUniformLocationARB( shader->handle, "u_ColorMap" );
	shader->u_DepthMap = pglGetUniformLocationARB( shader->handle, "u_DepthMap" );
	shader->u_GenericCondition = pglGetUniformLocationARB( shader->handle, "u_GenericCondition" );

	GL_BindShader( shader );
	pglUniform1iARB( shader->u_ColorMap, GL_TEXTURE0 );
	pglUniform1iARB( shader->u_DepthMap, GL_TEXTURE1 );
	GL_BindShader( GL_NONE );

	GL_ValidateProgram( shader );
	GL_ShowProgramUniforms( shader );
}

static void GL_InitLensFlareUniforms( glsl_program_t *shader )
{
	ASSERT( shader != NULL );

	shader->u_LightOrigin = pglGetUniformLocationARB( shader->handle, "u_LightOrigin" );
	shader->u_ScreenSizeInv = pglGetUniformLocationARB( shader->handle, "u_ScreenSizeInv" );
	shader->u_Accum = pglGetUniformLocationARB( shader->handle, "u_Accum" );

	GL_ValidateProgram( shader );
	GL_ShowProgramUniforms( shader );
}

static void GL_InitMonochromeUniforms( glsl_program_t *shader )
{
	ASSERT( shader != NULL );

	shader->u_ColorMap = pglGetUniformLocationARB( shader->handle, "u_ColorMap" );
	shader->u_Accum = pglGetUniformLocationARB( shader->handle, "u_Accum" );

	GL_BindShader( shader );
	pglUniform1iARB( shader->u_ColorMap, GL_TEXTURE0 );
	GL_BindShader( GL_NONE );

	GL_ValidateProgram( shader );
	GL_ShowProgramUniforms( shader );
}

static void GL_InitEnhanceUniforms( glsl_program_t *shader )
{
	ASSERT( shader != NULL );

	shader->u_ColorMap = pglGetUniformLocationARB( shader->handle, "u_ColorMap" );
	shader->u_ScreenSizeInv = pglGetUniformLocationARB( shader->handle, "u_ScreenSizeInv" );

	GL_BindShader( shader );
	pglUniform1iARB( shader->u_ColorMap, GL_TEXTURE0 );
	GL_BindShader( GL_NONE );

	GL_ValidateProgram( shader );
	GL_ShowProgramUniforms( shader );
}

static void GL_InitFSRUniforms( glsl_program_t *shader, bool bEASU )
{
	ASSERT( shader != NULL );

	shader->u_ColorMap = pglGetUniformLocationARB( shader->handle, "u_ColorMap" );
	shader->u_ScreenSizeInv = pglGetUniformLocationARB( shader->handle, "u_ScreenSizeInv" );
	if( bEASU )
		shader->u_GenericCondition = pglGetUniformLocationARB( shader->handle, "u_GenericCondition" );

	GL_BindShader( shader );
	pglUniform1iARB( shader->u_ColorMap, GL_TEXTURE0 );
	GL_BindShader( GL_NONE );

	GL_ValidateProgram( shader );
	GL_ShowProgramUniforms( shader );
}

static void GL_InitSMAAEdgeDetectUniforms( glsl_program_t *shader )
{
	ASSERT( shader != NULL );

	shader->u_ColorMap = pglGetUniformLocationARB( shader->handle, "u_ColorMap" );
	shader->u_ScreenSizeInv = pglGetUniformLocationARB( shader->handle, "u_ScreenSizeInv" );

	GL_BindShader( shader );
	pglUniform1iARB( shader->u_ColorMap, GL_TEXTURE0 );
	GL_BindShader( GL_NONE );

	GL_ValidateProgram( shader );
	GL_ShowProgramUniforms( shader );
}

static void GL_InitSMAABlendWeightUniforms( glsl_program_t *shader )
{
	ASSERT( shader != NULL );

	shader->u_ColorMap = pglGetUniformLocationARB( shader->handle, "u_ColorMap" );
	shader->u_SearchTex = pglGetUniformLocationARB( shader->handle, "u_SearchTex" );
	shader->u_AreaTex = pglGetUniformLocationARB( shader->handle, "u_AreaTex" );
	shader->u_ScreenSizeInv = pglGetUniformLocationARB( shader->handle, "u_ScreenSizeInv" );

	GL_BindShader( shader );
	pglUniform1iARB( shader->u_ColorMap, GL_TEXTURE0 );
	pglUniform1iARB( shader->u_SearchTex, GL_TEXTURE1 );
	pglUniform1iARB( shader->u_AreaTex, GL_TEXTURE2 );
	GL_BindShader( GL_NONE );

	GL_ValidateProgram( shader );
	GL_ShowProgramUniforms( shader );
}

static void GL_InitSMAANeighborBlendUniforms( glsl_program_t *shader )
{
	ASSERT( shader != NULL );

	shader->u_ColorMap = pglGetUniformLocationARB( shader->handle, "u_ColorMap" );
	shader->u_BlendTexture = pglGetUniformLocationARB( shader->handle, "u_BlendTexture" );
	shader->u_ScreenSizeInv = pglGetUniformLocationARB( shader->handle, "u_ScreenSizeInv" );

	GL_BindShader( shader );
	pglUniform1iARB( shader->u_ColorMap, GL_TEXTURE0 );
	pglUniform1iARB( shader->u_BlendTexture, GL_TEXTURE1 );
	GL_BindShader( GL_NONE );

	GL_ValidateProgram( shader );
	GL_ShowProgramUniforms( shader );
}

static void GL_InitBloomUniforms( glsl_program_t *shader )
{
	ASSERT( shader != NULL );

	shader->u_ScreenMap = pglGetUniformLocationARB( shader->handle, "u_ScreenMap" );
	shader->u_ScreenSizeInv = pglGetUniformLocationARB( shader->handle, "u_ScreenSizeInv" );

	GL_BindShader( shader );
	pglUniform1iARB( shader->u_ScreenMap, GL_TEXTURE0 );
	GL_BindShader( GL_NONE );

	GL_ValidateProgram( shader );
	GL_ShowProgramUniforms( shader );
}

static void GL_InitTonemapUniforms( glsl_program_t *shader )
{
	ASSERT( shader != NULL );

	shader->u_ScreenMap = pglGetUniformLocationARB( shader->handle, "u_ScreenMap" );
	shader->u_HDRExposure = pglGetUniformLocationARB( shader->handle, "u_HDRExposure" );
	shader->u_GenericCondition = pglGetUniformLocationARB( shader->handle, "u_GenericCondition" );

	GL_BindShader( shader );
	pglUniform1iARB( shader->u_ScreenMap, GL_TEXTURE0 );
	pglUniform1iARB( shader->u_HDRExposure, GL_TEXTURE1 );
	GL_BindShader( GL_NONE );

	GL_ValidateProgram( shader );
	GL_ShowProgramUniforms( shader );
}

static void GL_InitGenLuminanceUniforms( glsl_program_t *shader )
{
	ASSERT( shader != NULL );

	shader->u_ScreenMap = pglGetUniformLocationARB( shader->handle, "u_ScreenMap" );
	shader->u_ScreenSizeInv = pglGetUniformLocationARB( shader->handle, "u_ScreenSizeInv" );

	GL_BindShader( shader );
	pglUniform1iARB( shader->u_ScreenMap, GL_TEXTURE0 );
	GL_BindShader( GL_NONE );

	GL_ValidateProgram( shader );
	GL_ShowProgramUniforms( shader );
}

static void GL_InitGenExposureUniforms( glsl_program_t *shader )
{
	ASSERT( shader != NULL );

	shader->u_ScreenMap = pglGetUniformLocationARB( shader->handle, "u_ScreenMap" );
	shader->u_NormalMap = pglGetUniformLocationARB( shader->handle, "u_NormalMap" );
	shader->u_TimeDelta = pglGetUniformLocationARB( shader->handle, "u_TimeDelta" );
	shader->u_MipLod = pglGetUniformLocationARB( shader->handle, "u_MipLod" );

	GL_BindShader( shader );
	pglUniform1iARB( shader->u_ScreenMap, GL_TEXTURE0 );
	pglUniform1iARB( shader->u_NormalMap, GL_TEXTURE1 );
	GL_BindShader( GL_NONE );

	GL_ValidateProgram( shader );
	GL_ShowProgramUniforms( shader );
}

static void GL_InitBlurMipUniforms( glsl_program_t *shader )
{
	ASSERT( shader != NULL );

	shader->u_ScreenMap = pglGetUniformLocationARB( shader->handle, "u_ScreenMap" );
	shader->u_ScreenSizeInv = pglGetUniformLocationARB( shader->handle, "u_ScreenSizeInv" );
	shader->u_MipLod = pglGetUniformLocationARB( shader->handle, "u_MipLod" );
	shader->u_TexCoordClamp = pglGetUniformLocationARB( shader->handle, "u_TexCoordClamp" );
	shader->u_BloomFirstPass = pglGetUniformLocationARB( shader->handle, "u_BloomFirstPass" );

	GL_BindShader( shader );
	pglUniform1iARB( shader->u_ScreenMap, GL_TEXTURE0 );
	GL_BindShader( GL_NONE );

	GL_ValidateProgram( shader );
	GL_ShowProgramUniforms( shader );
}

static void GL_InitScreenWaterUniforms( glsl_program_t *shader )
{
	ASSERT( shader != NULL );

	shader->u_ColorMap = pglGetUniformLocationARB( shader->handle, "u_ColorMap" );
	shader->u_NormalMap = pglGetUniformLocationARB( shader->handle, "u_NormalMap" );
	shader->u_ScreenWater = pglGetUniformLocationARB( shader->handle, "u_ScreenWater" );

	GL_BindShader( shader );
	pglUniform1iARB( shader->u_ColorMap, GL_TEXTURE0 );
	pglUniform1iARB( shader->u_NormalMap, GL_TEXTURE1 );
	GL_BindShader( GL_NONE );

	GL_ValidateProgram( shader );
	GL_ShowProgramUniforms( shader );
}

static void GL_InitGlitchUniforms( glsl_program_t *shader )
{
	ASSERT( shader != NULL );

	shader->u_ColorMap = pglGetUniformLocationARB( shader->handle, "u_ColorMap" );
	shader->u_Glitch = pglGetUniformLocationARB( shader->handle, "u_Glitch" );

	GL_BindShader( shader );
	pglUniform1iARB( shader->u_ColorMap, GL_TEXTURE0 );
	GL_BindShader( GL_NONE );

	GL_ValidateProgram( shader );
	GL_ShowProgramUniforms( shader );
}

static void GL_InitDroneScreenUniforms( glsl_program_t *shader )
{
	ASSERT( shader != NULL );

	shader->u_ColorMap = pglGetUniformLocationARB( shader->handle, "u_ColorMap" );
	shader->u_NormalMap = pglGetUniformLocationARB( shader->handle, "u_NormalMap" );
	shader->u_DroneScreen = pglGetUniformLocationARB( shader->handle, "u_DroneScreen" );

	GL_BindShader( shader );
	pglUniform1iARB( shader->u_ColorMap, GL_TEXTURE0 );
	pglUniform1iARB( shader->u_NormalMap, GL_TEXTURE1 );
	GL_BindShader( GL_NONE );

	GL_ValidateProgram( shader );
	GL_ShowProgramUniforms( shader );
}

static void GL_InitWaterDropsUniforms( glsl_program_t *shader )
{
	ASSERT( shader != NULL );

	shader->u_ColorMap = pglGetUniformLocationARB( shader->handle, "u_ColorMap" );
	shader->u_WaterDrops = pglGetUniformLocationARB( shader->handle, "u_WaterDrops" );

	GL_BindShader( shader );
	pglUniform1iARB( shader->u_ColorMap, GL_TEXTURE0 );
	GL_BindShader( GL_NONE );

	GL_ValidateProgram( shader );
	GL_ShowProgramUniforms( shader );
}

const char *GL_PretifyListOptions( const char *options, bool newlines )
{
	static char output[MAX_OPTIONS_LENGTH + 2];

	if( !options || !*options )
		return "";

	const char *pstart = options;
	char *pout = output, *plimit = output + sizeof( output ) - 2;

	while( (pstart = Q_strstr( pstart, "#define" )) != NULL )
	{
		pstart += 7;

		while( *pstart && *pstart != '\n' && pout < plimit )
			*pout++ = *pstart++;

		if( newlines && *pstart == '\n' && pout < plimit )
			*pout++ = '\n';

		if( *pstart == '\n' )
			pstart++;
	}

	if( pout == output )
		return "";

	*pout++ = ' ';
	*pout = '\0';

	return output;
}

word GL_UberShaderForSolidBmodel( msurface_t *s, bool translucent )
{
	char glname[64];
	char options[MAX_OPTIONS_LENGTH];
	mfaceinfo_t *landscape = NULL;
	mextrasurf_t *es = s->info;
	bool fullBright = false;
	bool mirror = false;
	bool portal = false;
	bool IsLandscape = FBitSet( s->flags, SURF_LANDSCAPE );
	bool movie = FBitSet( s->flags, SURF_MOVIE );

	bool use_cubemaps = false;
	bool use_emboss = false;
	bool use_interior = false;
	bool use_specular = false;
	bool use_bump = false;

	ASSERT( worldmodel != NULL );

	if( FBitSet( s->flags, SURF_REFLECT ) && s->info->subtexture[glState.stack_position] > 0 )
		mirror = true;
	if( FBitSet( s->flags, SURF_PORTAL ) )
		portal = true;

	landscape = s->texinfo->faceinfo;

	if( es->shaderNum[mirror] && es->glsl_sequence[mirror] == tr.glsl_valid_sequence )
		return es->shaderNum[mirror]; // valid

	Q_strncpy( glname, "BmodelSolid", sizeof( glname ));
	memset( options, 0, sizeof( options ));

	texture_t *tx = s->texinfo->texture;

	if( FBitSet( s->flags, SURF_REFLECT ) && !FBitSet( s->flags, SURF_WATER ) && CVAR_TO_BOOL( r_mirrorquality ))
		fullBright = true;

	if( portal && CVAR_TO_BOOL( gl_allow_portals ) )
	{
		fullBright = true;
		GL_AddShaderDirective( options, "BMODEL_PORTAL" );
	}

	if( FBitSet( s->flags, SURF_SCREEN ) && CVAR_TO_BOOL( gl_allow_screens ))
	{
		if( RI->currententity && FBitSet( RI->currententity->curstate.iuser1, CF_MONOCHROME ))
			GL_AddShaderDirective( options, "BMODEL_MONOCHROME" );
		fullBright = true;
	}

	// solid water with lightmaps looks ugly
	// diffusion - allow water to use lightmap
	// P.S. NOTENOTE - translucent is edited and completely excludes all RenderModeTexture brushes, so all glass/water uses lightmaps (not a bad thing tho)
	if( /*FBitSet( s->flags, SURF_WATER ) || */translucent )
		fullBright = true;

	if( tr.materials[tx->gl_texturenum].fullbright )
		fullBright = true;

	if( RI->currententity ) // diffusion
	{
		if( (RI->currententity->curstate.renderfx == kRenderFxFullbright) || (RI->currententity->curstate.renderfx == kRenderFxFullbrightNoShadows) )
			fullBright = true;

		if( RI->currententity->curstate.rendermode == kRenderTransTexture )
			GL_AddShaderDirective( options, "BMODEL_DEFAULTALPHATEST" ); // do not use 0.5 threshold
	}

	if( FBitSet( s->flags, SURF_FULLBRIGHT ) || R_FullBright( ) || fullBright )
	{
		GL_AddShaderDirective( options, "BMODEL_FULLBRIGHT" );
	}
	else
	{
		// process lightstyles
		const bool bSunShadow = ( CVAR_TO_BOOL( r_sun_shadow ) && tr.sunShadowActive );
		bool bSunEmitted = false;

		for( int i = 0; i < MAXLIGHTMAPS && s->styles[i] != LS_NONE; i++ )
		{
			GL_AddShaderDirective( options, va( "BMODEL_APPLY_STYLE%i", i ));

			// blend the realtime sun shadow into the baked sun lightstyle
			if( bSunShadow && s->styles[i] == LS_SUN )
			{
				if( !bSunEmitted )
				{
					GL_AddShaderDirective( options, "BMODEL_SUN_SHADOW" );
					bSunEmitted = true;
				}
				GL_AddShaderDirective( options, va( "BMODEL_SUN_STYLE%i", i ));
			}
		}
	}

	if( RI->currententity && RI->currententity->curstate.rendermode == kRenderTransAlpha && !IsLandscape )
		GL_AddShaderDirective( options, "ALPHA_RESCALING" );

	if( FBitSet( s->flags, SURF_WATER ))
	{
		GL_AddShaderDirective( options, "BMODEL_WATER" );

		if( RI->currententity )
		{
			if( !tr.lowmemory && (gl_water_refraction->value > 0) && (RI->currententity->curstate.renderfx != kRenderFxNoRefraction) )
			{
				if( tr.waterTextures.Count() > 0 )
				{
					GL_AddShaderDirective( options, "BMODEL_WATER_REFRACTION" );
					GL_EncodeNormal( options, tr.waterTextures[0] );
				}
			}
		}
	}

	if( !IsLandscape )
	{
		if( gl_emboss->value > 0 )
		{
			if( tr.materials[tx->gl_texturenum].EmbossScale > 0.0f )
			{
				GL_AddShaderDirective( options, "BMODEL_EMBOSS" );
				use_emboss = true;
			}
		}

		if( gl_specular->value > 0 )
		{
			if( tr.materials[tx->gl_texturenum].GlossScale > 0.0f )
			{
				GL_AddShaderDirective( options, "BMODEL_SPECULAR" );
				use_specular = true;
			}
		}

		if( gl_bump->value > 0 )
		{
			if( tr.materials[tx->gl_texturenum].gl_normalmap_id > 0 )
			{
				GL_AddShaderDirective( options, "BMODEL_BUMP" );
				GL_EncodeNormal( options, tr.materials[tx->gl_texturenum].gl_normalmap_id );
				use_bump = true;
			}
		}
	}
	else // landscape
	{
		if( gl_emboss->value > 0 )
		{
			if( landscape && landscape->terrain && landscape->terrain->layermap.has_emboss )
			{
				GL_AddShaderDirective( options, "BMODEL_EMBOSS" );
				use_emboss = true;
			}
		}

		if( gl_specular->value > 0 )
		{
			if( landscape && landscape->terrain && landscape->terrain->layermap.has_specular )
			{
				GL_AddShaderDirective( options, "BMODEL_SPECULAR" );
				use_specular = true;
			}
		}

		if( gl_bump->value > 0 )
		{
			if( landscape && landscape->terrain && landscape->terrain->layermap.gl_normalmap_id > 0 )
			{
				GL_AddShaderDirective( options, "BMODEL_BUMP" );
				GL_EncodeNormal( options, landscape->terrain->layermap.gl_normalmap_id );
				use_bump = true;
			}
		}
	}

	// diffusioncubemaps	
	if( CVAR_TO_BOOL( gl_cubemaps ) && world->cubemaps_ready && (tr.materials[tx->gl_texturenum].ReflectScale[0] > 0.01f) )
	{
		GL_AddShaderDirective( options, "REFLECTION_CUBEMAP" );
		use_cubemaps = true;
	}

	if( tr.materials[tx->gl_texturenum].gl_interiormap_id > 0 )
	{
		GL_AddShaderDirective( options, "BMODEL_INTERIOR" );
		use_interior = true;
	}

#if 0
	// can't properly draw for beams particles through glass. g-cont
	// disabled for now
	if( translucent )
		GL_AddShaderDirective( options, "BMODEL_TRANSLUCENT" );
#endif
	if( IsLandscape )
	{
		// half-D v0.8.2: multi-layer terrain needs texture arrays. On gl4es
		// (no arrays) terrain.h's sampler2DArray fallback macro only applies
		// under GL_ES, so the requested permutation can never compile — the
		// surface would draw with a stale/failed shader (blank ground).
		// Gate the directives on array support: the surface keeps its base
		// BmodelSolid shader and renders single-layer instead of not at all.
		if( landscape && landscape->terrain && landscape->terrain->layermap.gl_diffuse_id != 0
			&& GL_Support( R_TEXTURE_ARRAY_EXT ))
		{
			GL_AddShaderDirective( options, va( "TERRAIN_NUM_LAYERS %i", landscape->terrain->numLayers ));
			GL_AddShaderDirective( options, "BMODEL_MULTI_LAYERS" );
		}
	}

	if( FBitSet( s->flags, SURF_REFLECT ) && s->info->subtexture[glState.stack_position] > 0 )
	{
		GL_AddShaderDirective( options, "BMODEL_REFLECTION_PLANAR" );
		
		if( FBitSet( s->flags, SURF_WATER ) && gl_water_planar->value > 0 )
			GL_AddShaderDirective( options, "BMODEL_WATER_PLANAR" );
	}

	glsl_program_t *shader = GL_FindUberShader( glname, options, &GL_InitSolidBmodelUniforms );

	if( !shader )
	{
		tr.fClearScreen = true; // to avoid ugly blur
		SetBits( s->flags, SURF_NODRAW );
		return 0; // something bad happens
	}

	word shaderNum = (shader - glsl_programs);

	if( use_bump )
		shader->status |= SHADER_USE_BUMP;
	if( use_emboss )
		shader->status |= SHADER_USE_EMBOSS;
	if( use_interior )
		shader->status |= SHADER_USE_INTERIOR;
	if( use_specular )
		shader->status |= SHADER_USE_SPECULAR;
	if( use_cubemaps )
		shader->status |= SHADER_USE_CUBEMAPS;

	es->glsl_sequence[mirror] = tr.glsl_valid_sequence;
	ClearBits( s->flags, SURF_NODRAW );
	es->shaderNum[mirror] = shaderNum;
	
	return shaderNum;
}

word GL_UberShaderForBmodelDlight( const plight_t *pl, msurface_t *s, bool translucent )
{
	bool shadows = (!FBitSet( pl->flags, CF_NOSHADOWS )) ? true : false;

	if( tr.shadows_notsupport || !CVAR_TO_BOOL( gl_shadows ) )
		shadows = false;

	if( pl->pointlight && tr.omni_shadows_notsupport )
		shadows = false;

	char glname[64];
	char options[MAX_OPTIONS_LENGTH];
	mextrasurf_t *es = s->info;
	bool mirrorSurface = false;
	bool portalSurface = false;
	mfaceinfo_t *landscape = NULL;
	bool IsLandscape = FBitSet( s->flags, SURF_LANDSCAPE );

	bool use_emboss = false;
	bool use_interior = false;
	bool use_specular = false;
	bool use_bump = false;
	
	Q_strncpy( glname, "BmodelDlight", sizeof( glname ));

	memset( options, 0, sizeof( options ));

	landscape = s->texinfo->faceinfo;

	if( pl->pointlight )
	{
		if( es->omniLightShaderNum[shadows] && es->glsl_sequence_omni[shadows] == tr.glsl_valid_sequence )
			return es->omniLightShaderNum[shadows]; // valid

		GL_AddShaderDirective( options, "BMODEL_LIGHT_OMNIDIRECTIONAL" );
	}
	else
	{
		if( es->projLightShaderNum[shadows] && es->glsl_sequence_proj[shadows] == tr.glsl_valid_sequence )
			return es->projLightShaderNum[shadows]; // valid
		GL_AddShaderDirective( options, "BMODEL_LIGHT_PROJECTION" );
	}

	texture_t *tx = s->texinfo->texture;

	if( !IsLandscape )
	{
		if( gl_emboss->value > 0 )
		{
			if( tr.materials[tx->gl_texturenum].EmbossScale > 0.0f )
			{
				GL_AddShaderDirective( options, "BMODEL_EMBOSS" );
				use_emboss = true;
			}
		}

		if( gl_specular->value > 0 )
		{
			if( tr.materials[tx->gl_texturenum].GlossScale > 0.0f )
			{
				GL_AddShaderDirective( options, "BMODEL_SPECULAR" );
				use_specular = true;
			}
		}

		if( gl_bump->value > 0 )
		{
			if( tr.materials[tx->gl_texturenum].gl_normalmap_id > 0 )
			{
				GL_AddShaderDirective( options, "BMODEL_BUMP" );
				GL_EncodeNormal( options, tr.materials[tx->gl_texturenum].gl_normalmap_id );
				use_bump = true;
			}
		}
	}
	else // landscape
	{
		// half-D v0.8.2: same texture-array gate as the BmodelSolid path —
		// without arrays the multi-layer permutation cannot compile on this
		// driver; keep the surface on the base (single-layer) shader.
		if( landscape && landscape->terrain && landscape->terrain->layermap.gl_diffuse_id != 0
			&& GL_Support( R_TEXTURE_ARRAY_EXT ))
		{
			GL_AddShaderDirective( options, va( "TERRAIN_NUM_LAYERS %i", landscape->terrain->numLayers ) );
			GL_AddShaderDirective( options, "BMODEL_MULTI_LAYERS" );
		}

		if( gl_emboss->value > 0 )
		{
			if( landscape && landscape->terrain && landscape->terrain->layermap.has_emboss )
			{
				GL_AddShaderDirective( options, "BMODEL_EMBOSS" );
				use_emboss = true;
			}
		}

		if( gl_specular->value > 0 )
		{
			if( landscape && landscape->terrain && landscape->terrain->layermap.has_specular )
			{
				GL_AddShaderDirective( options, "BMODEL_SPECULAR" );
				use_specular = true;
			}
		}

		if( gl_bump->value > 0 )
		{
			if( landscape && landscape->terrain && landscape->terrain->layermap.gl_normalmap_id > 0 )
			{
				GL_AddShaderDirective( options, "BMODEL_BUMP" );
				GL_EncodeNormal( options, landscape->terrain->layermap.gl_normalmap_id );
				use_bump = true;
			}
		}
	}

#if 0
	// can't properly draw for beams particles through glass. g-cont
	// disabled for now
	if( translucent )
		GL_AddShaderDirective( options, "BMODEL_TRANSLUCENT" );
#endif

	// diffusion - don't light up glass too much
	if( RI->currententity && RI->currententity->curstate.rendermode == kRenderTransTexture )
		GL_AddShaderDirective( options, "BMODEL_KRENDERTRANSTEXTURE" );

	if( FBitSet( s->flags, SURF_WATER ))
	{
		GL_AddShaderDirective( options, "BMODEL_WATER" );

		if( !tr.lowmemory && RI->currententity && (gl_water_refraction->value > 0) && (RI->currententity->curstate.renderfx != kRenderFxNoRefraction) )
		{
			if( tr.waterTextures.Count() > 0 )
			{
				GL_AddShaderDirective( options, "BMODEL_WATER_REFRACTION" );
				GL_EncodeNormal( options, tr.waterTextures[0] );
			}
		}
	}

	if( tr.materials[tx->gl_texturenum].gl_interiormap_id > 0 )
	{
		GL_AddShaderDirective( options, "BMODEL_INTERIOR" );
		use_interior = true;
	}

	if( shadows )
		GL_AddShaderDirective( options, "BMODEL_HAS_SHADOWS" );

	glsl_program_t *shader = GL_FindUberShader( glname, options, &GL_InitBmodelDlightUniforms );
	if( !shader )
	{
		SetBits( s->flags, SURF_NODLIGHT );
		return 0; // something bad happens
	}

	word shaderNum = (shader - glsl_programs);

	// done
	ClearBits( s->flags, SURF_NODLIGHT );

	if( pl->pointlight )
	{
		es->omniLightShaderNum[shadows] = shaderNum;
		es->glsl_sequence_omni[shadows] = tr.glsl_valid_sequence;
	}
	else
	{
		es->projLightShaderNum[shadows] = shaderNum;
		es->glsl_sequence_proj[shadows] = tr.glsl_valid_sequence;
	}

	if( use_bump )
		shader->status |= SHADER_USE_BUMP;
	if( use_emboss )
		shader->status |= SHADER_USE_EMBOSS;
	if( use_interior )
		shader->status |= SHADER_USE_INTERIOR;
	if( use_specular )
		shader->status |= SHADER_USE_SPECULAR;

	return shaderNum;
}

word GL_UberShaderForGrassSolid( msurface_t *s, grass_t *g )
{
	// grass can be built (and its shader precached) mid-frame while the sun isn't active, so the sun state is part of the cache check, not just the sequence
	const bool bSunShadow = ( CVAR_TO_BOOL( r_sun_shadow ) && tr.sunShadowActive );

	if( g->vbo.shaderNum && g->vbo.glsl_sequence == tr.glsl_valid_sequence && ( FBitSet( g->vbo.flags, FGRASS_SUNSHADOW ) != 0 ) == bSunShadow )
		return g->vbo.shaderNum; // valid

	char glname[64];
	char options[MAX_OPTIONS_LENGTH];

	Q_strncpy( glname, "GrassSolid", sizeof( glname ));
	memset( options, 0, sizeof( options ));

	if( R_FullBright( ))
	{
		GL_AddShaderDirective( options, "GRASS_FULLBRIGHT" );
	}
	else
	{
		bool bSunEmitted = false;

		// process lightstyles
		for( int i = 0; i < MAXLIGHTMAPS && s->styles[i] != LS_NONE; i++ )
		{
			GL_AddShaderDirective( options, va( "GRASS_APPLY_STYLE%i", i ));

			// grass takes its light from the surface it grows on, so it takes the sun shadow on the same style the surface does
			if( bSunShadow && s->styles[i] == LS_SUN )
			{
				if( !bSunEmitted )
					GL_AddShaderDirective( options, "GRASS_SUN_SHADOW" );
				GL_AddShaderDirective( options, va( "GRASS_SUN_STYLE%i", i ));
				bSunEmitted = true;
			}
		}
	}

	glsl_program_t *shader = GL_FindUberShader( glname, options, &GL_InitGrassSolidUniforms );
	if( !shader )
	{
		SetBits( g->vbo.flags, FGRASS_NODRAW );
		return 0; // something bad happens
	}

	g->vbo.glsl_sequence = tr.glsl_valid_sequence;
	ClearBits( g->vbo.flags, FGRASS_NODRAW );

	if( bSunShadow )
		SetBits( g->vbo.flags, FGRASS_SUNSHADOW );
	else ClearBits( g->vbo.flags, FGRASS_SUNSHADOW );

	return (shader - glsl_programs);
}

word GL_UberShaderForGrassDlight( plight_t *pl, struct grass_s *g )
{
	bool shadows = (!FBitSet( pl->flags, CF_NOSHADOWS )) ? true : false;

	if( tr.shadows_notsupport || !CVAR_TO_BOOL( gl_shadows ) )
		shadows = false;

	if( pl->pointlight && tr.omni_shadows_notsupport )
		shadows = false;

	char glname[64];
	char options[MAX_OPTIONS_LENGTH];

	Q_strncpy( glname, "GrassDlight", sizeof( glname ));
	memset( options, 0, sizeof( options ));

	if( pl->pointlight )
		GL_AddShaderDirective( options, "GRASS_LIGHT_OMNIDIRECTIONAL" );
	else
		GL_AddShaderDirective( options, "GRASS_LIGHT_PROJECTION" );

	if( shadows )
		GL_AddShaderDirective( options, "GRASS_HAS_SHADOWS" );

	glsl_program_t *shader = GL_FindUberShader( glname, options, &GL_InitGrassDlightUniforms );
	if( !shader )
	{
		SetBits( g->vbo.flags, FGRASS_NODLIGHT );
		return 0; // something bad happens
	}

	ClearBits( g->vbo.flags, FGRASS_NODLIGHT );

	return (shader - glsl_programs);
}

word GL_UberShaderForBmodelDecal( decal_t *decal )
{
	char glname[64];
	bool fullbright = R_ModelOpaque( RI->currententity->curstate.rendermode, RI->currententity->curstate.renderamt ) ? false : true;
	char options[MAX_OPTIONS_LENGTH];
	msurface_t *s = decal->psurface;
	mextrasurf_t *es = s->info;

	ASSERT( worldmodel != NULL );

	if( decal->shaderNum && decal->glsl_sequence == tr.glsl_valid_sequence )
		return decal->shaderNum; // valid

	if( r_decals_ext->value > 0 )
	{
		// NOTE: this code executes when decal is placed.
		// this must be remade, so external textures would only load at startup (but how?)
		char diffuse[128];
		Q_snprintf( diffuse, sizeof( diffuse ), "textures/!decals/%s.dds", GET_TEXTURE_NAME( decal->texture ) );
		if( FILE_EXISTS( diffuse ) )
		{
			decal->texture = LOAD_TEXTURE( diffuse, NULL, 0, 0 );
		}
	}

	Q_strncpy( glname, "BmodelDecal", sizeof( glname ));
	memset( options, 0, sizeof( options ));

	// process lightstyles
	for( int i = 0; i < MAXLIGHTMAPS && s->styles[i] != LS_NONE && !fullbright; i++ )
	{
		GL_AddShaderDirective( options, va( "DECAL_APPLY_STYLE%i", i ));
	}

	if( FBitSet( s->flags, SURF_TRANSPARENT ))
		GL_AddShaderDirective( options, "DECAL_ALPHATEST" );

	if( fullbright )
		GL_AddShaderDirective( options, "DECAL_FULLBRIGHT" );

	glsl_program_t *shader = GL_FindUberShader( glname, options, &GL_InitBmodelDecalUniforms );
	if( !shader ) return 0; // something bad happens

	word shaderNum = (shader - glsl_programs);
	decal->glsl_sequence = tr.glsl_valid_sequence;
	decal->shaderNum = shaderNum;

	return shaderNum;
}

word GL_UberShaderForSolidStudio( mstudiomaterial_t *mat, bool vertex_lighting, bool bone_weighting, bool fullbright, int numbones )
{
	char glname[64];
	char options[MAX_OPTIONS_LENGTH];
	bool use_cubemaps = false;
	bool use_emboss = false;
	bool use_interior = false;
	bool use_specular = false;
	bool use_bump = false;

	if( mat->shaderNum && mat->glsl_sequence == tr.glsl_valid_sequence )
		return mat->shaderNum; // valid

	Q_strncpy( glname, "StudioSolid", sizeof( glname ));
	memset( options, 0, sizeof( options ));

	const bool bMasked = FBitSet( mat->flags, STUDIO_NF_MASKED ) ? true : false;

	if( tr.materials[mat->gl_diffuse_id].fullbright ) // .monitor automatically sets this too
		fullbright = true;

	if( RI->currententity && RI->currententity->curstate.rendermode == kRenderTransTexture )
		GL_AddShaderDirective( options, "STUDIO_DEFAULTALPHATEST" ); // do not use 0.5 threshold

	if( numbones > 0 && glConfig.max_skinning_bones < MAXSTUDIOBONES && glConfig.uniforms_economy )
	{
		int num_bones = Q_min( numbones, glConfig.max_skinning_bones );
		GL_AddShaderDefine( options, va( "#define MAXSTUDIOBONES %d\n", num_bones ));
	}
	else if( numbones == 1 )
	{
		GL_AddShaderDefine( options, "#define MAXSTUDIOBONES 1\n" );
	}

	if( bone_weighting )
		GL_AddShaderDirective( options, "STUDIO_BONEWEIGHTING" );

	if( FBitSet( mat->flags, STUDIO_NF_CHROME ))
		GL_AddShaderDirective( options, "STUDIO_HAS_CHROME" );

	if( vertex_lighting )
		GL_AddShaderDirective( options, "STUDIO_VERTEX_LIGHTING" );

	if( fullbright || FBitSet( mat->flags, STUDIO_NF_FULLBRIGHT ) )
	{
		GL_AddShaderDirective( options, "STUDIO_FULLBRIGHT" );
	}

	if( FBitSet( mat->flags, STUDIO_NF_ADDITIVE ) )
	{
		GL_AddShaderDirective( options, "STUDIO_ADDITIVE" );
	}
	else
	{
		if( FBitSet( mat->flags, STUDIO_NF_FLATSHADE ))
			GL_AddShaderDirective( options, "STUDIO_LIGHT_FLATSHADE" );
	}

	// the map was compiled with ambient light probes
	if( world->numleaflights > 0 && !vertex_lighting && !fullbright )
		GL_AddShaderDirective( options, "STUDIO_AMBIENT_PROBES" );

	// receive the realtime sun shadow
	if( CVAR_TO_BOOL( r_sun_shadow ) && tr.sunShadowActive && !fullbright && !FBitSet( mat->flags, STUDIO_NF_FULLBRIGHT|STUDIO_NF_ADDITIVE ))
		GL_AddShaderDirective( options, "STUDIO_SUN_SHADOW" );

	if( gl_emboss->value > 0 )
	{
		if( tr.materials[mat->gl_diffuse_id].EmbossScale > 0.0f )
		{
			GL_AddShaderDirective( options, "STUDIO_EMBOSS" );
			use_emboss = true;
		}
	}

	if( gl_specular->value > 0 )
	{
		if( tr.materials[mat->gl_diffuse_id].GlossScale > 0.0f )
		{
			GL_AddShaderDirective( options, "STUDIO_SPECULAR" );
			use_specular = true;
		}
	}

	if( CVAR_TO_BOOL( gl_bump ) && tr.materials[mat->gl_diffuse_id].gl_normalmap_id > 0 )
	{
		GL_AddShaderDirective( options, "STUDIO_BUMP" );
		GL_EncodeNormal( options, tr.materials[mat->gl_diffuse_id].gl_normalmap_id );
		use_bump = true;
	}

	if( bMasked )
		GL_AddShaderDirective( options, "ALPHA_RESCALING" );

	// diffusioncubemaps		
	if( CVAR_TO_BOOL( gl_cubemaps ) && world->cubemaps_ready && (tr.materials[mat->gl_diffuse_id].ReflectScale[0] > 0.01f) )
	{
		GL_AddShaderDirective( options, "REFLECTION_CUBEMAP" );
		use_cubemaps = true;
	}

	if( tr.materials[mat->gl_diffuse_id].gl_interiormap_id > 0 )
	{
		GL_AddShaderDirective( options, "STUDIO_INTERIOR" );
		use_interior = true;
	}
	else if( tr.materials[mat->gl_diffuse_id].gl_blendtex_id > 0 )
		GL_AddShaderDirective( options, "STUDIO_TEXTURE_BLEND" );

	if( tr.materials[mat->gl_diffuse_id].gl_colormask_id > 0 )
		GL_AddShaderDirective( options, "STUDIO_HAS_COLORMASK" );

	if( tr.materials[mat->gl_diffuse_id].FoliageSwayHeight != 0 )
		GL_AddShaderDirective( options, "STUDIO_SWAY_FOLIAGE" );

	glsl_program_t *shader = GL_FindUberShader( glname, options, &GL_InitSolidStudioUniforms );
	if( !shader )
	{
		SetBits( mat->flags, STUDIO_NF_NODRAW );
		return 0; // something bad happens
	}

	word shaderNum = (shader - glsl_programs);

	// done
	mat->glsl_sequence = tr.glsl_valid_sequence;
	ClearBits( mat->flags, STUDIO_NF_NODRAW );
	mat->shaderNum = shaderNum;

	if( use_bump )
		shader->status |= SHADER_USE_BUMP;
	if( use_emboss )
		shader->status |= SHADER_USE_EMBOSS;
	if( use_interior )
		shader->status |= SHADER_USE_INTERIOR;
	if( use_specular )
		shader->status |= SHADER_USE_SPECULAR;
	if( use_cubemaps )
		shader->status |= SHADER_USE_CUBEMAPS;

	return shaderNum;
}

word GL_UberShaderForDlightStudio( const plight_t *pl, struct mstudiomat_s *mat, bool bone_weighting, int numbones )
{
	bool shadows = (!FBitSet( pl->flags, CF_NOSHADOWS )) ? true : false;

	if( tr.shadows_notsupport || !CVAR_TO_BOOL( gl_shadows ) )
		shadows = false;

	if( pl->pointlight && tr.omni_shadows_notsupport )
		shadows = false;

	char glname[64];
	char options[MAX_OPTIONS_LENGTH];

	bool use_emboss = false;
	bool use_interior = false;
	bool use_specular = false;
	bool use_bump = false;

	Q_strncpy( glname, "StudioDlight", sizeof( glname ));
	memset( options, 0, sizeof( options ));

	if( pl->pointlight )
	{
		if( mat->omniLightShaderNum[shadows] && mat->glsl_sequence_omni[shadows] == tr.glsl_valid_sequence )
			return mat->omniLightShaderNum[shadows]; // valid
		GL_AddShaderDirective( options, "STUDIO_LIGHT_OMNIDIRECTIONAL" );
	}
	else
	{
		if( mat->projLightShaderNum[shadows] && mat->glsl_sequence_proj[shadows] == tr.glsl_valid_sequence )
			return mat->projLightShaderNum[shadows]; // valid
		GL_AddShaderDirective( options, "STUDIO_LIGHT_PROJECTION" );
	}

	if( numbones > 0 && glConfig.max_skinning_bones < MAXSTUDIOBONES && glConfig.uniforms_economy )
	{
		int num_bones = Q_min( numbones, glConfig.max_skinning_bones );
		GL_AddShaderDefine( options, va( "#define MAXSTUDIOBONES %d\n", num_bones ));
	}
	else if( numbones == 1 )
	{
		GL_AddShaderDefine( options, "#define MAXSTUDIOBONES 1\n" );
	}

	if( bone_weighting )
		GL_AddShaderDirective( options, "STUDIO_BONEWEIGHTING" );

	if( FBitSet( mat->flags, STUDIO_NF_CHROME ))
		GL_AddShaderDirective( options, "STUDIO_HAS_CHROME" );

	if( gl_emboss->value > 0 )
	{
		if( tr.materials[mat->gl_diffuse_id].EmbossScale > 0.0f )
		{
			GL_AddShaderDirective( options, "STUDIO_EMBOSS" );
			use_emboss = true;
		}
	}

	if( gl_specular->value > 0 )
	{
		if( tr.materials[mat->gl_diffuse_id].GlossScale > 0.0f )
		{
			GL_AddShaderDirective( options, "STUDIO_SPECULAR" );
			use_specular = true;
		}
	}

	if( CVAR_TO_BOOL( gl_bump ) && tr.materials[mat->gl_diffuse_id].gl_normalmap_id > 0 )
	{
		GL_AddShaderDirective( options, "STUDIO_BUMP" );
		GL_EncodeNormal( options, tr.materials[mat->gl_diffuse_id].gl_normalmap_id );
		use_bump = true;
	}

	if( tr.materials[mat->gl_diffuse_id].gl_interiormap_id > 0 )
	{
		GL_AddShaderDirective( options, "STUDIO_INTERIOR" );
		use_interior = true;
	}
	else if( tr.materials[mat->gl_diffuse_id].gl_blendtex_id > 0 )
		GL_AddShaderDirective( options, "STUDIO_TEXTURE_BLEND" );

	if( tr.materials[mat->gl_diffuse_id].gl_colormask_id > 0 )
		GL_AddShaderDirective( options, "STUDIO_HAS_COLORMASK" );

	if( tr.materials[mat->gl_diffuse_id].FoliageSwayHeight != 0 )
		GL_AddShaderDirective( options, "STUDIO_SWAY_FOLIAGE" );

	if( shadows )
		GL_AddShaderDirective( options, "STUDIO_HAS_SHADOWS" );

	if( RI->currententity && RI->currententity->curstate.rendermode == kRenderTransTexture )
		GL_AddShaderDirective( options, "STUDIO_DEFAULTALPHATEST" ); // do not use 0.5 threshold

	glsl_program_t *shader = GL_FindUberShader( glname, options, &GL_InitStudioDlightUniforms );
	if( !shader )
	{
		SetBits( mat->flags, STUDIO_NF_NODLIGHT );
		return 0; // something bad happens
	}

	word shaderNum = (shader - glsl_programs);

	// done
	ClearBits( mat->flags, STUDIO_NF_NODLIGHT );

	if( pl->pointlight )
	{
		mat->omniLightShaderNum[shadows] = shaderNum;
		mat->glsl_sequence_omni[shadows] = tr.glsl_valid_sequence;
	}
	else
	{
		mat->projLightShaderNum[shadows] = shaderNum;
		mat->glsl_sequence_proj[shadows] = tr.glsl_valid_sequence;
	}

	if( use_bump )
		shader->status |= SHADER_USE_BUMP;
	if( use_emboss )
		shader->status |= SHADER_USE_EMBOSS;
	if( use_interior )
		shader->status |= SHADER_USE_INTERIOR;
	if( use_specular )
		shader->status |= SHADER_USE_SPECULAR;

	return shaderNum;
}

#if !STUDIODECAL_DISABLE
word GL_UberShaderForStudioDecal( mstudiomaterial_t *mat )
{
	char glname[64];
	char options[MAX_OPTIONS_LENGTH];

	Q_strncpy( glname, "StudioDecal", sizeof( glname ));
	memset( options, 0, sizeof( options ));

	if( FBitSet( mat->flags, STUDIO_NF_MASKED ))
		GL_AddShaderDirective( options, "DECAL_ALPHATEST" );

	if( FBitSet( mat->flags, STUDIO_NF_ADDITIVE ))
	{
		// mixed mode: solid & transparent controlled by alpha-channel
		if( FBitSet( mat->flags, STUDIO_NF_MASKED ))
			GL_AddShaderDirective( options, "STUDIO_ALPHA_GLASS" );
	}

	glsl_program_t *shader = GL_FindUberShader( glname, options, &GL_InitStudioDecalUniforms );
	if( !shader ) return 0; // something bad happens

	return (shader - glsl_programs);
}
#endif

word GL_UberShaderForDlightGeneric( const plight_t *pl )
{
	bool shadows = (!FBitSet( pl->flags, CF_NOSHADOWS )) ? true : false;

	if( tr.shadows_notsupport || !CVAR_TO_BOOL( gl_shadows ) )
		shadows = false;

	if( pl->pointlight && tr.omni_shadows_notsupport )
		shadows = false;

	if( pl->pointlight )
	{
		if( tr.omniLightShaderNum[shadows] && tr.glsl_sequence_omni[shadows] == tr.glsl_valid_sequence )
			return tr.omniLightShaderNum[shadows]; // valid
	}
	else
	{
		if( tr.projLightShaderNum[shadows] && tr.glsl_sequence_proj[shadows] == tr.glsl_valid_sequence )
			return tr.projLightShaderNum[shadows]; // valid
	}

	char glname[64];
	char options[MAX_OPTIONS_LENGTH];

	Q_strncpy( glname, "GenericDlight", sizeof( glname ));
	memset( options, 0, sizeof( options ));

	if( pl->pointlight )
		GL_AddShaderDirective( options, "GENERIC_LIGHT_OMNIDIRECTIONAL" );
	else
		GL_AddShaderDirective( options, "GENERIC_LIGHT_PROJECTION" );

	if( shadows )
		GL_AddShaderDirective( options, "GENERIC_HAS_SHADOWS" );

	glsl_program_t *shader = GL_FindUberShader( glname, options, &GL_InitGenericDlightUniforms );
	if( !shader )
	{
		tr.nodlights = true;
		return 0; // something bad happens
	}

	word shaderNum = (shader - glsl_programs);

	// done
	tr.nodlights = false;

	if( pl->pointlight )
	{
		tr.omniLightShaderNum[shadows] = shaderNum;
		tr.glsl_sequence_omni[shadows] = tr.glsl_valid_sequence;
	}
	else
	{
		tr.projLightShaderNum[shadows] = shaderNum;
		tr.glsl_sequence_proj[shadows] = tr.glsl_valid_sequence;
	}

	return shaderNum;
}

void GL_ListGPUShaders( void )
{
	int count = 0;

	for( unsigned int i = 1; i < num_glsl_programs; i++ )
	{
		glsl_program_t *cur = &glsl_programs[i];
		if( !cur->name[0] ) continue;

		const char *options = GL_PretifyListOptions( cur->options );

		if( Q_stricmp( options, "" ))
			Msg( "#%i %s [%s]\n", i, cur->name, options );
		else Msg( "#%i %s\n", i, cur->name );
		count++;
	}

	Msg( "total %i shaders\n", count );
}

bool GL_PrecompileUberShaders( int &processed, int &total )
{
	static bool bPauseSent = false;

	if( bPrecompiled )
	{
		if( bPauseSent )
		{
			if( RENDER_GET_PARM( PARAM_GAMEPAUSED, 0 ))
				ClientCmd( "pause\n" );

			bPauseSent = false;
		}

		if( ui_shadersdone->value != 1 )
			CVAR_SET_FLOAT( "ui_shadersdone", 1 );

		// half-D v0.7.4: also settle the HUD bar flag here, not only at the
		// end of HUD_RenderFrame — if a frame path ever returns early, the
		// "Loading shaders 0/0" bar used to stick forever over the game.
		gHUD.bShadersCompiled = true;
		return true;
	}

	if( !GL_Support( R_BINARY_SHADER_EXT ) )
	{
		// half-D v0.8.3: during the per-frame load phase the stepper owns
		// the bar and the done-flags — settling here mid-phase would hide
		// the progress bar and make the two state machines fight.
		if( R_ShaderLoadPhaseActive() )
			return false;

		// half-D v0.6.1: skip the from-source precompile when the driver has
		// no program binary cache (gl4es). v0.5.0 kept it on to avoid the
		// 13-76 s lazy-compile stalls of the v0.4.x era — but those were the
		// readback cliffs fixed in v0.2-v0.4; a first-use compile through
		// gl4es costs ~100-500 ms. Meanwhile the pass itself became the
		// biggest launch cost on device: 714 permutations at up to 2 s per
		// frame, 6+ minutes of 0.5-1 fps behind the menu, repeated on EVERY
		// launch because nothing can persist the result. The reference port
		// (yalnie/Diffusion-Android) skips the pass here and lets shaders
		// compile on first use — match that exactly.
		Msg( "^3No shader binary cache on this driver — shaders compile on first use (reference behavior)^7\n" );
		bPrecompiled = true;
		CVAR_SET_FLOAT( "ui_shadersdone", 1 );
		// half-D v0.7.4: settle the HUD bar immediately (see the bPrecompiled
		// branch above). v0.7.3 on device kept the bar on screen when frames
		// were slow: nothing was compiling, the flag just never got set.
		gHUD.bShadersCompiled = true;
		return true;
	}

	processed = shaders_processed_total;
	total = iTotalShaders - 1;

	if( shaders_processed_total == iTotalShaders - 1 )
	{
		if( compiled_failed_total == 0 && compiled_outdated_total > 0 )
			Msg( "^2Loaded %d shaders:^7 ^3%d were recompiled.^7\n", compiled_count_total, compiled_outdated_total );
		else if( compiled_failed_total > 0 && compiled_outdated_total == 0 )
			Msg( "^2Loaded %d shaders:^7 ^1%d failed to preload.^7\n", compiled_count_total, compiled_failed_total );
		else if( compiled_failed_total > 0 && compiled_outdated_total > 0 )
			Msg( "^2Loaded %d shaders:^7 ^3%d were recompiled,^7 ^1%d failed to load.^7\n", compiled_count_total, compiled_outdated_total, compiled_failed_total );
		else
			Msg( "^2Loaded %d shaders.^7\n", compiled_count_total );

		bPrecompiled = true;
		compiled_outdated_total = 0;
		return true;
	}

	if( !bCompilingShaders )
	{
		Msg( "^2Loading shaders...^7\n" );
		shaders_processed_total = 0;
		bCompilingShaders = true;
		compiled_count_total = 0;
		compiled_failed_total = 0;
		ClientCmd( "pause\n" );
		bPauseSent = true;
	}

	bPrecompiled = false;

	shaderlist_t curshader = shaders[shaders_processed_total];

	int compiled_shader_counter = 0; // allow to load up to 50 shaders at once if they are loaded from disk cache or from hash
	float fpsmax = fps_max->value;
	if( fpsmax <= 0.0f ) fpsmax = 60.0f;
	float target_frametime = 1.0f / fpsmax;
	if( target_frametime < 0.01f ) target_frametime = 0.01f;
	float mult = target_frametime / g_fFrametime;
	int compiled_shader_max = 20 * mult;
	compiled_shader_max = bound( 3, compiled_shader_max, 50 );

	glsl_program_t *shader;
	while( compiled_shader_counter < compiled_shader_max )
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

		if( !shader )
		{
			// invalid shader
			compiled_shader_counter++;
			compiled_failed_total++;
			// advance shader
			shaders_processed_total++;
			curshader = shaders[shaders_processed_total];
		}
		else if( shader->status & SHADER_STATUS_HASHED )
		{
			// this shader has already been precached during map loading - skip it
			compiled_shader_counter++;
			compiled_count_total++;
			// advance shader
			shaders_processed_total++;
			curshader = shaders[shaders_processed_total];
		}
		else if( shader->status & SHADER_STATUS_FROMBINARY )
		{
			// this shader was loaded from disk - apply uniforms and load some more
			if( !Q_stricmp( curshader.glname, "bmodeldecal" ) )
				GL_InitBmodelDecalUniforms( shader );
			else if( !Q_stricmp( curshader.glname, "bmodeldlight" ) )
				GL_InitBmodelDlightUniforms( shader );
			else if( !Q_stricmp( curshader.glname, "bmodelsolid" ) )
				GL_InitSolidBmodelUniforms( shader );
			else if( !Q_stricmp( curshader.glname, "studiodlight" ) )
				GL_InitStudioDlightUniforms( shader );
			else if( !Q_stricmp( curshader.glname, "studiosolid" ) )
				GL_InitSolidStudioUniforms( shader );
			else if( !Q_stricmp( curshader.glname, "grassdlight" ) )
				GL_InitGrassDlightUniforms( shader );
			else if( !Q_stricmp( curshader.glname, "grasssolid" ) )
				GL_InitGrassSolidUniforms( shader );
			else
				ALERT( at_aiconsole, "^1GL_PrecompileUberShaders: unhandled uniforms for shader %s!^7\n", curshader.glname );

			compiled_shader_counter++;
			compiled_count_total++;
			// advance shader
			shaders_processed_total++;
			curshader = shaders[shaders_processed_total];
		}
		else
		{
			// uncompiled shader
			if( !Q_stricmp( curshader.glname, "bmodeldecal" ) )
				GL_InitBmodelDecalUniforms( shader );
			else if( !Q_stricmp( curshader.glname, "bmodeldlight" ) )
				GL_InitBmodelDlightUniforms( shader );
			else if( !Q_stricmp( curshader.glname, "bmodelsolid" ) )
				GL_InitSolidBmodelUniforms( shader );
			else if( !Q_stricmp( curshader.glname, "studiodlight" ) )
				GL_InitStudioDlightUniforms( shader );
			else if( !Q_stricmp( curshader.glname, "studiosolid" ) )
				GL_InitSolidStudioUniforms( shader );
			else if( !Q_stricmp( curshader.glname, "grassdlight" ) )
				GL_InitGrassDlightUniforms( shader );
			else if( !Q_stricmp( curshader.glname, "grasssolid" ) )
				GL_InitGrassSolidUniforms( shader );
			else
				ALERT( at_aiconsole, "^1GL_PrecompileUberShaders: unhandled uniforms for shader %s!^7\n", curshader.glname );

			compiled_shader_counter++;
		//	compiled_shader_max -= 7; // decrease workload
			compiled_count_total++;
			// advance shader
			shaders_processed_total++;
			curshader = shaders[shaders_processed_total];
		}
	}

//	Msg( "This frame loaded shaders: %i\n", compiled_shader_counter );
	return false;
}

/*
========================
R_ShaderLoadPhaseBegin / R_ShaderLoadPhaseActive / R_ShaderLoadPhaseStep

half-D v0.8.3: the systematic precompile as a per-frame load phase.

History: v0.7.4 compiled this list synchronously at R_NewMap — 45 s in
ONE frame behind the plaque (v0.8.1 log: "precompiled 100 shaders ... in
45.2 s"; a frozen frame the system watchdog can read as a hang). v0.8.2
capped the map-load drains at 2 s, which pushed ~90 permutations back to
the in-game rate limiter: surfaces and studio models stayed shader-less
for minutes (black textures, invisible models) while 460 ms compiles
hitched the game at 1-2/s — the "regressed horribly, still 5 fps" report.

v0.8.3 compiles the same list, but with a wall-clock budget PER FRAME
(R_ShaderLoadPhaseStep from HUD_RenderFrame): the "Loading shaders" HUD
bar shows progress, no single frame exceeds ~2 s (ANR-safe), total wall
time is the same ~45 s once per session. After the phase, rare first-use
permutations compile inline — the reference-port behavior: always
correct rendering, one ~460 ms hitch each (r_shader_lazy_hz > 0 restores
rate limiting as an opt-in).
========================
*/

static bool bSessionDone = false;
static int g_iSyncCursor = -1;
static int g_iSyncSkipped = 0, g_iSyncFailed = 0;
static int g_iSyncHashed = 0, g_iSyncCompiled = 0;

void R_ShaderLoadPhaseBegin( void )
{
	// once per session, and only the no-binary-cache (gl4es) path needs it
	if( bSessionDone || bPrecompiled )
		return;

	if( !GL_Support( R_SHADER_GLSL100_EXT ) || GL_Support( R_BINARY_SHADER_EXT ))
	{
		bSessionDone = true;
		return;
	}

	g_iSyncCursor = 0;
	g_iSyncSkipped = g_iSyncFailed = 0;
	g_iSyncHashed = g_iSyncCompiled = 0;
}

bool R_ShaderLoadPhaseActive( void )
{
	return g_iSyncCursor >= 0;
}

void R_ShaderLoadPhaseStep( float frameBudgetSeconds )
{
	if( g_iSyncCursor < 0 )
		return;

	auto stepStart = std::chrono::steady_clock::now();
	gHUD.shaders_total = iTotalShaders - 1;

	for( ; g_iSyncCursor < iTotalShaders; g_iSyncCursor++ )
	{
		gHUD.shaders_processed = g_iSyncCursor;

		// wall-clock budget checked BETWEEN entries: one ~460 ms compile
		// may overshoot it, but never several — no multi-second frame
		if( std::chrono::duration<float>( std::chrono::steady_clock::now() - stepStart ).count() >= frameBudgetSeconds )
			return;

		shaderlist_t &curshader = shaders[g_iSyncCursor];

		// same unusable-permutation filter as before: shadow samplers and
		// texture arrays do not exist on this driver
		{
			const bool noShadowPath = tr.shadows_notsupport || !CVAR_TO_BOOL( gl_shadows );
			const char *def = curshader.defines ? curshader.defines : "";

			if(( noShadowPath && ( Q_strstr( def, "HAS_SHADOWS" ) || Q_strstr( def, "SUN_SHADOW" ))) ||
				( !GL_Support( R_TEXTURE_ARRAY_EXT ) && Q_strstr( def, "TERRAIN_NUM_LAYERS" )))
			{
				g_iSyncSkipped++;
				continue;
			}
		}

		if( curshader.glname == NULL )
			continue; // end of the list

		glsl_program_t *shader = GL_InitGPUShader( curshader.glname, curshader.glname, curshader.glname, curshader.defines );

		if( !shader )
		{
			g_iSyncFailed++;
			continue;
		}

		if( shader->status & SHADER_STATUS_HASHED )
		{
			g_iSyncHashed++; // already compiled at map precache — free win
			continue;
		}

		if( !Q_stricmp( curshader.glname, "bmodeldecal" ))
			GL_InitBmodelDecalUniforms( shader );
		else if( !Q_stricmp( curshader.glname, "bmodeldlight" ))
			GL_InitBmodelDlightUniforms( shader );
		else if( !Q_stricmp( curshader.glname, "bmodelsolid" ))
			GL_InitSolidBmodelUniforms( shader );
		else if( !Q_stricmp( curshader.glname, "studiodlight" ))
			GL_InitStudioDlightUniforms( shader );
		else if( !Q_stricmp( curshader.glname, "studiosolid" ))
			GL_InitSolidStudioUniforms( shader );
		else if( !Q_stricmp( curshader.glname, "grassdlight" ))
			GL_InitGrassDlightUniforms( shader );
		else if( !Q_stricmp( curshader.glname, "grasssolid" ))
			GL_InitGrassSolidUniforms( shader );
		else
			ALERT( at_aiconsole, "^1R_ShaderLoadPhaseStep: unhandled uniforms for shader %s!^7\n", curshader.glname );

		g_iSyncCompiled++;
	}

	// list exhausted — finalize once per session
	g_iSyncCursor = -1;
	bSessionDone = true;
	bPrecompiled = true;
	shaders_processed_total = iTotalShaders - 1;
	compiled_count_total = g_iSyncCompiled + g_iSyncHashed;
	compiled_failed_total = g_iSyncFailed;
	CVAR_SET_FLOAT( "ui_shadersdone", 1 );
	gHUD.bShadersCompiled = true;

	Msg( "^2half-D v0.8.4: precompiled %d shaders in the per-frame load phase^7 (%d already cached, %d skipped as unusable on this driver, %d failed)\n",
		g_iSyncCompiled + g_iSyncHashed, g_iSyncHashed, g_iSyncSkipped, g_iSyncFailed );
}

void GL_InitGPUShaders( void )
{
	plight_t pl;

	memset( &glsl_programs, 0, sizeof( glsl_programs ));
	memset( &glsl, 0, sizeof( glsl ));
	memset( &pl, 0, sizeof( pl ));
	memset( glsl_programsHashTable, 0, sizeof( glsl_programsHashTable ) );
	tr.glsl_valid_sequence = 1;
	num_glsl_programs = 1; // entry #0 isn't used
	compiled_outdated_total = 0; // this adds up in GL_InitGPUShader

	if( !GL_Support( R_SHADER_GLSL100_EXT ))
		return;

	ADD_COMMAND( "shaderlist", GL_ListGPUShaders );

	glsl_program_t *shader; // generic pointer. help to initialize uniforms

	// prepare sunshafts
	glsl.genSunShafts = shader = GL_InitGPUShader( "GenSunShafts", "generic", "genshafts" );
	GL_InitGenShaftsUniforms( shader );

	// render sunshafts
	glsl.drawSunShafts = shader = GL_InitGPUShader( "DrawSunShafts", "generic", "drawshafts" );
	GL_InitDrawShaftsUniforms( shader );

	// prepare SSAO
	glsl.genSSAO = shader = GL_InitGPUShader( "GenSSAO", "generic", "genssao" );
	GL_InitGenSSAOUniforms( shader );

	// prepare HBAO
	glsl.genHBAO = shader = GL_InitGPUShader( "GenHBAO", "generic", "genhbao" );
	GL_InitGenHBAOUniforms( shader );

	// render SSAO
	glsl.drawSSAO = shader = GL_InitGPUShader( "DrawSSAO", "generic", "drawssao" );
	GL_InitDrawSSAOUniforms( shader );

	// motion blur
	glsl.MotionBlur = shader = GL_InitGPUShader( "MotionBlur", "generic", "motionblur" );
	GL_InitMotionBlurUniforms( shader );

	// horizontal blur
	glsl.HorizontalBlur = shader = GL_InitGPUShader( "HorizontalBlur", "generic", "horizontalblur" );
	GL_InitHorizontalBlurUniforms( shader );

	// monochrome
	glsl.Monochrome = shader = GL_InitGPUShader( "Monochrome", "generic", "monochrome" );
	GL_InitMonochromeUniforms( shader );

	// various screen effects - sharpen, saturation...
	glsl.Enhance = shader = GL_InitGPUShader( "Enhance", "generic", "enhance" );
	GL_InitEnhanceUniforms( shader );

	glsl.FSR_easu = shader = GL_InitGPUShader( "FSR_easu", "generic", "fsr_easu" );
	GL_InitFSRUniforms( shader, true );
	glsl.FSR_rcas = shader = GL_InitGPUShader( "FSR_rcas", "generic", "fsr_rcas" );
	GL_InitFSRUniforms( shader, false );

	glsl.SMAAEdgeDetect = shader = GL_InitGPUShader( "SMAAEdgeDetect", "smaaedgedetect", "smaaedgedetect" );
	GL_InitSMAAEdgeDetectUniforms( shader );
	glsl.SMAABlendWeight = shader = GL_InitGPUShader( "SMAABlendWeight", "smaablendweight", "smaablendweight" );
	GL_InitSMAABlendWeightUniforms( shader );
	glsl.SMAANeighborBlend = shader = GL_InitGPUShader( "SMAANeighborBlend", "smaaneighborblend", "smaaneighborblend" );
	GL_InitSMAANeighborBlendUniforms( shader );

	// screen water
	glsl.ScreenWater = shader = GL_InitGPUShader( "ScreenWater", "generic", "screenwater" );
	GL_InitScreenWaterUniforms( shader );

	// glitch effect
	glsl.Glitch = shader = GL_InitGPUShader( "Glitch", "generic", "glitch" );
	GL_InitGlitchUniforms( shader );

	// drone screen effect
	glsl.DroneScreen = shader = GL_InitGPUShader( "DroneScreen", "generic", "dronescreen" );
	GL_InitDroneScreenUniforms( shader );

	// heat distortion effect
	glsl.HeatDistortion = shader = GL_InitGPUShader( "HeatDistortion", "generic", "heat" );
	GL_InitHeatDistortionUniforms( shader );

	// lens flare
	glsl.LensFlare = shader = GL_InitGPUShader( "LensFlare", "generic", "lensflare" );
	GL_InitLensFlareUniforms( shader );

	// water drops on screen
	glsl.WaterDrops = shader = GL_InitGPUShader( "WaterDrops", "generic", "waterdrops" );
	GL_InitWaterDropsUniforms( shader );

	// tonemap
	glsl.ToneMap = shader = GL_InitGPUShader( "ToneMap", "generic", "tonemap" );
	GL_InitTonemapUniforms( shader );

	glsl.GenExposure = shader = GL_InitGPUShader( "GenExposure", "generic", "generate_exposure" );
	GL_InitGenExposureUniforms( shader );

	glsl.Bloom = shader = GL_InitGPUShader( "Bloom", "generic", "bloom" );
	GL_InitBloomUniforms( shader );

	glsl.BlurMip = shader = GL_InitGPUShader( "BlurMip", "generic", "blurmip" );
	GL_InitBlurMipUniforms( shader );

	glsl.GenLuminance = shader = GL_InitGPUShader( "GenLuminance", "generic", "generate_luminance" );
	GL_InitGenLuminanceUniforms( shader );

	// gaussian blur
	glsl.blurShader = shader = GL_InitGPUShader( "GaussBlur", "generic", "gaussblur" );
	GL_InitPostProcessUniforms( shader );

	glsl.BilateralBlur = shader = GL_InitGPUShader( "BilateralBlur", "generic", "bilateralblur" );
	GL_InitBilateralBlurUniforms( shader );

	// draw sun & skybox
	glsl.skyboxEnv = shader = GL_InitGPUShader( "SkyBoxGeneric", "generic", "skybox" );
	GL_InitSkyBoxUniforms( shader );

	// studio shadowing
	glsl.studioDepthFill[0] = shader = GL_InitGPUShader( "StudioDepth", "studiodepth", "generic", "#define NO_FOG\n" );
	GL_InitStudioDepthFillUniforms( shader );

	glsl.studioDepthFill[1] = shader = GL_InitGPUShader( "StudioDepth", "studiodepth", "generic", "#define STUDIO_BONEWEIGHTING\n#define NO_FOG\n"  );
	GL_InitStudioDepthFillUniforms( shader );

	// bmodel shadowing
	glsl.bmodelDepthFill = shader = GL_InitGPUShader( "BrushDepth", "bmodeldepth", "generic", "#define NO_FOG\n" );
	GL_InitBmodelDepthFillUniforms( shader );

	// grass shadowing
	glsl.grassDepthFill = shader = GL_InitGPUShader( "GrassDepth", "grassdepth", "generic", "#define NO_FOG\n" );
	GL_InitGrassSolidUniforms( shader );

	// fog processing
	glsl.genericFog = shader = GL_InitGPUShader( "GenericFog", "generic", "generic" );
	GL_InitGenericFogUniforms( shader );
	glsl.genericFogUseAlpha = shader = GL_InitGPUShader( "GenericFogUseAlpha", "generic", "generic", "#define FOG_USE_ALPHA\n" );
	GL_InitGenericFogUniforms( shader );

	// HACKHACK: precache generic light shaders
	pl.flags = 0;
	GL_UberShaderForDlightGeneric( &pl );
	pl.flags |= CF_NOSHADOWS;
	GL_UberShaderForDlightGeneric( &pl );
	pl.pointlight = true;
	pl.flags = 0;
	GL_UberShaderForDlightGeneric( &pl );
	pl.flags |= CF_NOSHADOWS;
	GL_UberShaderForDlightGeneric( &pl );
}

void GL_FreeUberShaders( void )
{
	if( !GL_Support( R_SHADER_GLSL100_EXT ))
		return;

	for( unsigned int i = 1; i < num_glsl_programs; i++ )
	{
		if( FBitSet( glsl_programs[i].status, SHADER_UBERSHADER )) 
			GL_FreeGPUShader( &glsl_programs[i] );
	}

	GL_BindShader( GL_NONE );
}

void GL_FreeGPUShaders( void )
{
	if( !GL_Support( R_SHADER_GLSL100_EXT ))
		return;

	for( unsigned int i = 1; i < num_glsl_programs; i++ )
		GL_FreeGPUShader( &glsl_programs[i] );

	memset( glsl_programsHashTable, 0, sizeof( glsl_programsHashTable ) );

	GL_BindShader( GL_NONE );
}