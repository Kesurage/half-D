/*
r_opengl.cpp - OpenGL dynamically linkage
Copyright (C) 2010 Uncle Mike

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#define EXTERN

#include "hud.h"
#include "utils.h"
#include "r_local.h"
#include "r_weather.h"
#include "mathlib.h"

#define MAX_RESERVED_UNIFORMS		22	// dynamic lighting estimates 42 uniforms... 

glState_t glState;
glConfig_t glConfig;

static void APIENTRY Stub_DrawBuffer( GLenum mode )
{
	if( pglDrawBuffersARB )
	{
		GLenum b = mode;
		pglDrawBuffersARB( 1, &b );
	}
}

static void APIENTRY Stub_GetDoublev( GLenum pname, GLdouble *params )
{
	GLfloat f[16] = { 0 };
	if( pglGetFloatv )
		pglGetFloatv( pname, f );
	for( int i = 0; i < 16; i++ ) params[i] = (GLdouble)f[i];
}

static void APIENTRY Stub_AlphaFunc( GLenum func, GLclampf ref )
{
}

static void APIENTRY Stub_ClipPlane( GLenum plane, const GLdouble *equation )
{
}

// half-D: native GLES3 path (ref_gles3compat). The renderer was written
// against desktop GL2 / gl4es, which fakes the whole fixed-function API.
// On a real GLES3 context the opengl_110 table must still resolve, so the
// functions below satisfy the table for everything the renderer never
// actually calls (verified call-site-by-call-site with
// tools/audit_es_surface.py). Functions that ARE called got real
// implementations: matrix state lives in the engine gl2_shim (exported
// through R_GetProcAddress), and the ARB shader-object API maps to core
// GL2 entry points.
static void APIENTRY Stub_NoOp( void ) {}
static void APIENTRY Stub_NoOp1( GLenum a ) {}
static void APIENTRY Stub_NoOp2( GLenum a, GLenum b ) {}
static void APIENTRY Stub_NoOp3( GLenum a, GLenum b, GLenum c ) {}
static void APIENTRY Stub_NoOp4( GLenum a, GLenum b, GLenum c, GLenum d ) {}
static void APIENTRY Stub_TexGenf( GLenum coord, GLenum pname, GLfloat param ) {}
static void APIENTRY Stub_TexGenfv( GLenum coord, GLenum pname, const GLfloat *params ) {}
static void APIENTRY Stub_TexGeni( GLenum coord, GLenum pname, GLint param ) {}
static void APIENTRY Stub_PolygonMode( GLenum face, GLenum mode )
{
	// GLES has no polygon mode; everything renders filled. The renderer
	// only ever sets GL_FILL outside debug wireframe.
}
static void APIENTRY Stub_PointSize( GLfloat size ) {}
static void APIENTRY Stub_RasterPos2f( GLfloat x, GLfloat y ) {}
static void APIENTRY Stub_GetTexEnviv( GLenum target, GLenum pname, GLint *params )
{
	if( params ) *params = 0;
}
static void APIENTRY Stub_GetTexLevelParameteriv( GLenum target, GLint level, GLenum pname, GLint *params )
{
	if( params ) *params = 0;
}
static void APIENTRY Stub_GetTexLevelParameterfv( GLenum target, GLint level, GLenum pname, GLfloat *params )
{
	if( params ) *params = 0.0f;
}
static void APIENTRY Stub_GetClipPlane( GLenum plane, GLdouble *equation )
{
	int i;
	for( i = 0; i < 4; i++ ) equation[i] = 0.0;
}
static GLboolean APIENTRY Stub_IsList( GLuint list ) { return GL_FALSE; }
static void *APIENTRY Stub_MapBufferARB( GLenum target, GLenum access ) { return NULL; }
static GLboolean APIENTRY Stub_UnmapBufferARB( GLenum target ) { return GL_TRUE; }
static void APIENTRY Stub_NormalPointer( GLenum type, GLsizei stride, const GLvoid *ptr ) {}
static void APIENTRY Stub_VertexAttrib4ubv( GLuint index, const GLubyte *v )
{
	if( pglVertexAttrib4fARB && v )
		pglVertexAttrib4fARB( index, v[0] / 255.0f, v[1] / 255.0f, v[2] / 255.0f, v[3] / 255.0f );
}
static GLhandleARB APIENTRY Stub_GetHandleARB( GLenum pname ) { return 0; }
static void APIENTRY Stub_GetAttachedObjectsARB( GLhandleARB containerObj, GLsizei maxCount, GLsizei *count, GLhandleARB *obj ) 
{
	if( count ) *count = 0;
}
static void APIENTRY Stub_GetObjectParameterfvARB( GLhandleARB obj, GLenum pname, GLfloat *params )
{
	if( params ) *params = 0.0f;
}

// half-D: ARB shader-object API mapped onto core GL2 entry points
static GLhandleARB APIENTRY Stub_glCreateShaderObjectARB( GLenum shaderType )
{
	return pglCreateShader ? pglCreateShader( shaderType ) : 0;
}

static GLhandleARB APIENTRY Stub_glCreateProgramObjectARB( void )
{
	return pglCreateProgram ? pglCreateProgram() : 0;
}

static void APIENTRY Stub_glAttachObjectARB( GLhandleARB containerObj, GLhandleARB obj )
{
	if( pglAttachShader ) pglAttachShader( containerObj, obj );
}

// half-D: the last unresolved ARB shader-object name (v0.5.2 field failure —
// one missing member disabled the whole GL_ARB_shader_objects check and with
// it the custom renderer)
static void APIENTRY Stub_glDetachObjectARB( GLhandleARB containerObj, GLhandleARB obj )
{
	if( pglDetachShader ) pglDetachShader( containerObj, obj );
}

// half-D: ES3 has no GL_POLYGON draw mode; a convex polygon is a triangle
// fan from its first vertex. Used by the decal batcher (r_brushdecals).
static void APIENTRY Stub_MultiDrawArrays( GLenum mode, const GLint *first, const GLsizei *count, GLsizei drawcount )
{
	if( !pglDrawArrays )
		return;

	GLenum fanMode = ( mode == GL_POLYGON ) ? GL_TRIANGLE_FAN : mode;

	for( GLsizei i = 0; i < drawcount; i++ )
	{
		if( count[i] > 0 )
			pglDrawArrays( fanMode, first[i], count[i] );
	}
}

// half-D: ES3 exposes only the unsigned query result read; occlusion sample
// counts and the 0/1 availability flag both fit an int unchanged
static void APIENTRY Stub_GetQueryObjectivARB( GLuint id, GLenum pname, GLint *params )
{
	if( pglGetQueryObjectuivARB )
		pglGetQueryObjectuivARB( id, pname, (GLuint *)params );
}

// half-D: fixed-function texenv vector state — the gl2 shim ignores scalar
// texenv too, and the client never calls this at runtime
static void APIENTRY Stub_TexEnvfv( GLenum target, GLenum pname, const GLfloat *params )
{
}

static void APIENTRY Stub_glDeleteObjectARB( GLhandleARB obj )
{
	if( !pglIsProgram || !obj )
		return;

	// the ARB object model unifies shaders and programs; core GL split them
	if( pglIsProgram( obj ))
	{
		if( pglDeleteProgram ) pglDeleteProgram( obj );
	}
	else
	{
		if( pglDeleteShader ) pglDeleteShader( obj );
	}
}

static void APIENTRY Stub_glUseProgramObjectARB( GLhandleARB progObj )
{
	if( pglUseProgram ) pglUseProgram( progObj );
}

static void APIENTRY Stub_glGetInfoLogARB( GLhandleARB obj, GLsizei maxLength, GLsizei *length, GLcharARB *infoLog )
{
	if( pglIsProgram && pglIsProgram( obj ))
	{
		if( pglGetProgramInfoLog ) pglGetProgramInfoLog( obj, maxLength, length, infoLog );
	}
	else
	{
		if( pglGetShaderInfoLog ) pglGetShaderInfoLog( obj, maxLength, length, infoLog );
	}
}

static void APIENTRY Stub_glGetObjectParameterivARB( GLhandleARB obj, GLenum pname, GLint *params )
{
	GLboolean isProgram;

	if( !params )
		return;

	if( !pglIsProgram || !pglGetProgramiv || !pglGetShaderiv )
	{
		*params = 0;
		return;
	}

	isProgram = pglIsProgram( obj );

	switch( pname )
	{
	case GL_OBJECT_COMPILE_STATUS_ARB:
		// compile status only exists on shader objects
		pglGetShaderiv( obj, GL_COMPILE_STATUS, params );
		break;
	case GL_OBJECT_LINK_STATUS_ARB:
	case GL_OBJECT_VALIDATE_STATUS_ARB:
		pglGetProgramiv( obj, isProgram ? GL_LINK_STATUS : GL_COMPILE_STATUS, params );
		if( pname == GL_OBJECT_VALIDATE_STATUS_ARB && isProgram )
			pglGetProgramiv( obj, GL_VALIDATE_STATUS, params );
		break;
	case GL_OBJECT_INFO_LOG_LENGTH_ARB:
		if( isProgram ) pglGetProgramiv( obj, GL_INFO_LOG_LENGTH, params );
		else pglGetShaderiv( obj, GL_INFO_LOG_LENGTH, params );
		break;
	case GL_OBJECT_ATTACHED_OBJECTS_ARB:
		pglGetProgramiv( obj, GL_ATTACHED_SHADERS, params );
		break;
	case GL_OBJECT_ACTIVE_UNIFORMS_ARB:
		pglGetProgramiv( obj, GL_ACTIVE_UNIFORMS, params );
		break;
	case GL_OBJECT_SHADER_SOURCE_LENGTH_ARB:
		pglGetShaderiv( obj, GL_SHADER_SOURCE_LENGTH, params );
		break;
	case GL_OBJECT_DELETE_STATUS_ARB:
		if( isProgram ) pglGetProgramiv( obj, GL_DELETE_STATUS, params );
		else pglGetShaderiv( obj, GL_DELETE_STATUS, params );
		break;
	default:
		*params = 0;
		break;
	}
}

static void APIENTRY Stub_glShaderSourceARB( GLhandleARB shaderObj, GLsizei count, const GLcharARB **string, const GLint *length )
{
	if( pglShaderSource ) pglShaderSource( shaderObj, count, (const GLchar **)string, length );
}

static void APIENTRY Stub_glCompileShaderARB( GLhandleARB shaderObj )
{
	if( pglCompileShader ) pglCompileShader( shaderObj );
}

static void APIENTRY Stub_glLinkProgramARB( GLhandleARB programObj )
{
	if( pglLinkProgram ) pglLinkProgram( programObj );
}

static const dllfunc_t opengl_110funcs[] =
{
{ "glClearColor"         	, (void **)&pglClearColor },
{ "glClear"              	, (void **)&pglClear },
{ "glAlphaFunc"          	, (void **)&pglAlphaFunc },
{ "glBlendFunc"          	, (void **)&pglBlendFunc },
{ "glCullFace"           	, (void **)&pglCullFace },
{ "glDrawBuffer"         	, (void **)&pglDrawBuffer },
{ "glReadBuffer"         	, (void **)&pglReadBuffer },
{ "glEnable"             	, (void **)&pglEnable },
{ "glDisable"            	, (void **)&pglDisable },
{ "glEnableClientState"  	, (void **)&pglEnableClientState },
{ "glDisableClientState" 	, (void **)&pglDisableClientState },
{ "glGetBooleanv"        	, (void **)&pglGetBooleanv },
{ "glGetDoublev"         	, (void **)&pglGetDoublev },
{ "glGetFloatv"          	, (void **)&pglGetFloatv },
{ "glGetIntegerv"        	, (void **)&pglGetIntegerv },
{ "glGetError"           	, (void **)&pglGetError },
{ "glGetString"          	, (void **)&pglGetString },
{ "glFinish"             	, (void **)&pglFinish },
{ "glFlush"              	, (void **)&pglFlush },
{ "glClearDepth"         	, (void **)&pglClearDepth },
{ "glDepthFunc"          	, (void **)&pglDepthFunc },
{ "glDepthMask"          	, (void **)&pglDepthMask },
{ "glDepthRange"         	, (void **)&pglDepthRange },
{ "glFrontFace"          	, (void **)&pglFrontFace },
{ "glDrawElements"       	, (void **)&pglDrawElements },
{ "glDrawArrays"       	, (void **)&pglDrawArrays },
{ "glColorMask"          	, (void **)&pglColorMask },
{ "glVertexPointer"      	, (void **)&pglVertexPointer },
{ "glNormalPointer"      	, (void **)&pglNormalPointer },
{ "glColorPointer"       	, (void **)&pglColorPointer },
{ "glTexCoordPointer"    	, (void **)&pglTexCoordPointer },
{ "glColor3f"            	, (void **)&pglColor3f },
{ "glColor3fv"           	, (void **)&pglColor3fv },
{ "glColor4f"            	, (void **)&pglColor4f },
{ "glColor4fv"           	, (void **)&pglColor4fv },
{ "glColor3ub"           	, (void **)&pglColor3ub },
{ "glColor4ub"           	, (void **)&pglColor4ub },
{ "glColor4ubv"          	, (void **)&pglColor4ubv },
{ "glTexCoord1f"         	, (void **)&pglTexCoord1f },
{ "glTexCoord2f"         	, (void **)&pglTexCoord2f },
{ "glTexCoord3f"         	, (void **)&pglTexCoord3f },
{ "glTexCoord4f"         	, (void **)&pglTexCoord4f },
{ "glTexCoord1fv"        	, (void **)&pglTexCoord1fv },
{ "glTexCoord2fv"        	, (void **)&pglTexCoord2fv },
{ "glTexCoord3fv"        	, (void **)&pglTexCoord3fv },
{ "glTexCoord4fv"        	, (void **)&pglTexCoord4fv },
{ "glTexGenf"            	, (void **)&pglTexGenf },
{ "glTexGenfv"           	, (void **)&pglTexGenfv },
{ "glTexGeni"            	, (void **)&pglTexGeni },
{ "glVertex2f"           	, (void **)&pglVertex2f },
{ "glVertex3f"           	, (void **)&pglVertex3f },
{ "glVertex3fv"          	, (void **)&pglVertex3fv },
{ "glNormal3f"           	, (void **)&pglNormal3f },
{ "glNormal3fv"          	, (void **)&pglNormal3fv },
{ "glBegin"              	, (void **)&pglBegin },
{ "glEnd"                	, (void **)&pglEnd },
{ "glLineWidth"          	, (void**)&pglLineWidth },
{ "glPointSize"          	, (void**)&pglPointSize },
{ "glMatrixMode"         	, (void **)&pglMatrixMode },
{ "glOrtho"              	, (void **)&pglOrtho },
{ "glRasterPos2f"        	, (void **) &pglRasterPos2f },
{ "glFrustum"            	, (void **)&pglFrustum },
{ "glViewport"           	, (void **)&pglViewport },
{ "glPushMatrix"         	, (void **)&pglPushMatrix },
{ "glPopMatrix"          	, (void **)&pglPopMatrix },
{ "glPushAttrib"         	, (void **)&pglPushAttrib },
{ "glPopAttrib"          	, (void **)&pglPopAttrib },
{ "glLoadIdentity"       	, (void **)&pglLoadIdentity },
{ "glLoadMatrixd"        	, (void **)&pglLoadMatrixd },
{ "glLoadMatrixf"        	, (void **)&pglLoadMatrixf },
{ "glMultMatrixd"        	, (void **)&pglMultMatrixd },
{ "glMultMatrixf"        	, (void **)&pglMultMatrixf },
{ "glRotated"            	, (void **)&pglRotated },
{ "glRotatef"            	, (void **)&pglRotatef },
{ "glScaled"             	, (void **)&pglScaled },
{ "glScalef"             	, (void **)&pglScalef },
{ "glTranslated"         	, (void **)&pglTranslated },
{ "glTranslatef"         	, (void **)&pglTranslatef },
{ "glReadPixels"         	, (void **)&pglReadPixels },
{ "glDrawPixels"         	, (void **)&pglDrawPixels },
{ "glStencilFunc"        	, (void **)&pglStencilFunc },
{ "glStencilMask"        	, (void **)&pglStencilMask },
{ "glStencilOp"          	, (void **)&pglStencilOp },
{ "glClearStencil"       	, (void **)&pglClearStencil },
{ "glIsEnabled"          	, (void **)&pglIsEnabled },
{ "glIsList"             	, (void **)&pglIsList },
{ "glIsTexture"          	, (void **)&pglIsTexture },
{ "glTexEnvf"            	, (void **)&pglTexEnvf },
{ "glTexEnvfv"           	, (void **)&pglTexEnvfv },
{ "glTexEnvi"            	, (void **)&pglTexEnvi },
{ "glTexParameterf"      	, (void **)&pglTexParameterf },
{ "glTexParameterfv"     	, (void **)&pglTexParameterfv },
{ "glTexParameteri"      	, (void **)&pglTexParameteri },
{ "glHint"               	, (void **)&pglHint },
{ "glPixelStoref"        	, (void **)&pglPixelStoref },
{ "glPixelStorei"        	, (void **)&pglPixelStorei },
{ "glGenTextures"        	, (void **)&pglGenTextures },
{ "glDeleteTextures"     	, (void **)&pglDeleteTextures },
{ "glBindTexture"        	, (void **)&pglBindTexture },
{ "glTexImage1D"         	, (void **)&pglTexImage1D },
{ "glTexImage2D"         	, (void **)&pglTexImage2D },
{ "glTexSubImage1D"      	, (void **)&pglTexSubImage1D },
{ "glTexSubImage2D"      	, (void **)&pglTexSubImage2D },
{ "glCopyTexImage1D"     	, (void **)&pglCopyTexImage1D },
{ "glCopyTexImage2D"     	, (void **)&pglCopyTexImage2D },
{ "glCopyTexSubImage1D"  	, (void **)&pglCopyTexSubImage1D },
{ "glCopyTexSubImage2D"  	, (void **)&pglCopyTexSubImage2D },
{ "glScissor"            	, (void **)&pglScissor },
{ "glGetTexImage"		, (void **)&pglGetTexImage },
{ "glGetTexEnviv"        	, (void **)&pglGetTexEnviv },
{ "glPolygonOffset"      	, (void **)&pglPolygonOffset },
{ "glPolygonMode"        	, (void **)&pglPolygonMode },
{ "glPolygonStipple"     	, (void **)&pglPolygonStipple },
{ "glClipPlane"          	, (void **)&pglClipPlane },
{ "glGetClipPlane"       	, (void **)&pglGetClipPlane },
{ "glShadeModel"         	, (void **)&pglShadeModel },
{ "glGetTexLevelParameteriv"	, (void **)&pglGetTexLevelParameteriv },
{ "glGetTexLevelParameterfv"	, (void **)&pglGetTexLevelParameterfv },
{ "glFogfv"              	, (void **)&pglFogfv },
{ "glFogf"               	, (void **)&pglFogf },
{ "glFogi"               	, (void **)&pglFogi },
{ "glTexImage3D"				, (void **)&pglTexImage3D },
{ "glTexSubImage3D"				, (void **)&pglTexSubImage3D },
{ "glCopyTexSubImage3D"			, (void **)&pglCopyTexSubImage3D },
{ NULL, NULL }
};

static const dllfunc_t opengl_200funcs[] =
{
{ "glCreateShader"				, (void **)&pglCreateShader },
{ "glAttachShader"				, (void **)&pglAttachShader },
{ "glDetachShader"				, (void **)&pglDetachShader },
{ "glCompileShader"				, (void **)&pglCompileShader },
{ "glShaderSource"				, (void **)&pglShaderSource },
{ "glGetShaderSource"			, (void **)&pglGetShaderSource },
{ "glGetShaderiv"				, (void **)&pglGetShaderiv },
{ "glDeleteShader"		        , (void **)&pglDeleteShader },
{ "glUseProgram"				, (void **)&pglUseProgram },
{ "glLinkProgram"				, (void **)&pglLinkProgram },
{ "glValidateProgram"			, (void **)&pglValidateProgram },
{ "glIsProgram"					, (void **)&pglIsProgram }, // half-D: ARB-object wrapper
{ "glCreateProgram"				, (void **)&pglCreateProgram },
{ "glDeleteProgram"				, (void **)&pglDeleteProgram },
{ "glGetShaderInfoLog"			, (void **)&pglGetShaderInfoLog },
{ "glGetProgramInfoLog"			, (void **)&pglGetProgramInfoLog },
{ "glGetActiveUniform"			, (void **)&pglGetActiveUniform },
{ "glGetUniformLocation"		, (void **)&pglGetUniformLocation },
{ "glGetProgramiv"              , (void **)&pglGetProgramiv },
{ "glBlendEquation"             , (void **)&pglBlendEquation },
{ "glVertexAttribPointer"		, (void **)&pglVertexAttribPointerARB },
{ "glEnableVertexAttribArray"	, (void **)&pglEnableVertexAttribArrayARB },
{ "glDisableVertexAttribArray"	, (void **)&pglDisableVertexAttribArrayARB },
{ "glBindAttribLocation"		, (void **)&pglBindAttribLocationARB },
{ "glGetActiveAttrib"			, (void **)&pglGetActiveAttribARB },
{ "glGetAttribLocation"			, (void **)&pglGetAttribLocationARB },
{ "glTexImage3D"				, (void **)&pglTexImage3D },
{ "glTexSubImage3D"				, (void **)&pglTexSubImage3D },
{ "glCopyTexSubImage3D"			, (void **)&pglCopyTexSubImage3D },
{ "glDrawRangeElements"			, (void **)&pglDrawRangeElements },
{ "glDrawBuffers"				, (void **)&pglDrawBuffersARB },
{ "glMultiTexCoord1f"			, (void **)&pglMultiTexCoord1f },
{ "glMultiTexCoord2f"			, (void **)&pglMultiTexCoord2f },
{ "glMultiTexCoord3f"			, (void **)&pglMultiTexCoord3f },
{ "glMultiTexCoord4f"			, (void **)&pglMultiTexCoord4f },
{ "glActiveTexture"				, (void **)&pglActiveTexture },
{ "glUniformMatrix2fv"			, (void **)&pglUniformMatrix2fvARB },
{ "glUniformMatrix3fv"			, (void **)&pglUniformMatrix3fvARB },
{ "glUniformMatrix4fv"			, (void **)&pglUniformMatrix4fvARB },
{ "glUniform1f"					, (void **)&pglUniform1fARB },
{ "glUniform2f"					, (void **)&pglUniform2fARB },
{ "glUniform3f"					, (void **)&pglUniform3fARB },
{ "glUniform4f"					, (void **)&pglUniform4fARB },
{ "glUniform1i"					, (void **)&pglUniform1iARB },
{ "glUniform2i"					, (void **)&pglUniform2iARB },
{ "glUniform3i"					, (void **)&pglUniform3iARB },
{ "glUniform4i"					, (void **)&pglUniform4iARB },
{ "glUniform1fv"				, (void **)&pglUniform1fvARB },
{ "glUniform2fv"				, (void **)&pglUniform2fvARB },
{ "glUniform3fv"				, (void **)&pglUniform3fvARB },
{ "glUniform4fv"				, (void **)&pglUniform4fvARB },
{ "glUniform1iv"				, (void **)&pglUniform1ivARB },
{ "glUniform2iv"				, (void **)&pglUniform2ivARB },
{ "glUniform3iv"				, (void **)&pglUniform3ivARB },
{ "glUniform4iv"				, (void **)&pglUniform4ivARB },
{ "glVertexAttrib2f"            , (void **)&pglVertexAttrib2fARB },
{ "glVertexAttrib2fv"           , (void **)&pglVertexAttrib2fvARB },
{ "glVertexAttrib3fv"           , (void **)&pglVertexAttrib3fvARB },
{ "glVertexAttrib4fv"           , (void **)&pglVertexAttrib4fvARB },
{ "glVertexAttrib4ubv"			, (void **)&pglVertexAttrib4ubvARB },
{ "glMultiDrawArrays"			, (void **)&pglMultiDrawArrays },
{ NULL, NULL }
};

static const dllfunc_t drawrangeelementsfuncs[] =
{
{ "glDrawRangeElements" , (void **)&pglDrawRangeElements },
{ NULL, NULL }
};

static const dllfunc_t drawrangeelementsextfuncs[] =
{
{ "glDrawRangeElementsEXT" , (void **)&pglDrawRangeElementsEXT },
{ NULL, NULL }
};

static const dllfunc_t multitexturefuncs[] =
{
{ "glMultiTexCoord1fARB"     , (void **)&pglMultiTexCoord1f },
{ "glMultiTexCoord2fARB"     , (void **)&pglMultiTexCoord2f },
{ "glMultiTexCoord3fARB"     , (void **)&pglMultiTexCoord3f },
{ "glMultiTexCoord4fARB"     , (void **)&pglMultiTexCoord4f },
{ "glActiveTextureARB"       , (void **)&pglActiveTexture },
{ "glActiveTextureARB"       , (void **)&pglActiveTextureARB },
{ "glClientActiveTextureARB" , (void **)&pglClientActiveTexture },
{ "glClientActiveTextureARB" , (void **)&pglClientActiveTextureARB },
{ NULL, NULL }
};

static const dllfunc_t blendseparatefunc[] =
{
{ "glBlendFuncSeparateEXT", (void **)&pglBlendFuncSeparate },
{ NULL, NULL }
};

static const dllfunc_t shaderobjectsfuncs[] =
{
{ "glDeleteObjectARB"             , (void **)&pglDeleteObjectARB },
{ "glGetHandleARB"                , (void **)&pglGetHandleARB },
{ "glDetachObjectARB"             , (void **)&pglDetachObjectARB },
{ "glCreateShaderObjectARB"       , (void **)&pglCreateShaderObjectARB },
{ "glShaderSourceARB"             , (void **)&pglShaderSourceARB },
{ "glCompileShaderARB"            , (void **)&pglCompileShaderARB },
{ "glCreateProgramObjectARB"      , (void **)&pglCreateProgramObjectARB },
{ "glAttachObjectARB"             , (void **)&pglAttachObjectARB },
{ "glLinkProgramARB"              , (void **)&pglLinkProgramARB },
{ "glUseProgramObjectARB"         , (void **)&pglUseProgramObjectARB },
{ "glValidateProgramARB"          , (void **)&pglValidateProgramARB },
{ "glUniform1fARB"                , (void **)&pglUniform1fARB },
{ "glUniform2fARB"                , (void **)&pglUniform2fARB },
{ "glUniform3fARB"                , (void **)&pglUniform3fARB },
{ "glUniform4fARB"                , (void **)&pglUniform4fARB },
{ "glUniform1iARB"                , (void **)&pglUniform1iARB },
{ "glUniform2iARB"                , (void **)&pglUniform2iARB },
{ "glUniform3iARB"                , (void **)&pglUniform3iARB },
{ "glUniform4iARB"                , (void **)&pglUniform4iARB },
{ "glUniform1fvARB"               , (void **)&pglUniform1fvARB },
{ "glUniform2fvARB"               , (void **)&pglUniform2fvARB },
{ "glUniform3fvARB"               , (void **)&pglUniform3fvARB },
{ "glUniform4fvARB"               , (void **)&pglUniform4fvARB },
{ "glUniform1ivARB"               , (void **)&pglUniform1ivARB },
{ "glUniform2ivARB"               , (void **)&pglUniform2ivARB },
{ "glUniform3ivARB"               , (void **)&pglUniform3ivARB },
{ "glUniform4ivARB"               , (void **)&pglUniform4ivARB },
{ "glUniformMatrix2fvARB"         , (void **)&pglUniformMatrix2fvARB },
{ "glUniformMatrix3fvARB"         , (void **)&pglUniformMatrix3fvARB },
{ "glUniformMatrix4fvARB"         , (void **)&pglUniformMatrix4fvARB },
{ "glGetObjectParameterfvARB"     , (void **)&pglGetObjectParameterfvARB },
{ "glGetObjectParameterivARB"     , (void **)&pglGetObjectParameterivARB },
{ "glGetInfoLogARB"               , (void **)&pglGetInfoLogARB },
{ "glGetAttachedObjectsARB"       , (void **)&pglGetAttachedObjectsARB },
{ "glGetUniformLocationARB"       , (void **)&pglGetUniformLocationARB },
{ "glGetActiveUniformARB"         , (void **)&pglGetActiveUniformARB },
{ "glGetUniformfvARB"             , (void **)&pglGetUniformfvARB },
{ "glGetUniformivARB"             , (void **)&pglGetUniformivARB },
{ "glGetShaderSourceARB"          , (void **)&pglGetShaderSourceARB },
{ "glVertexAttribPointerARB"      , (void **)&pglVertexAttribPointerARB },
{ "glEnableVertexAttribArrayARB"  , (void **)&pglEnableVertexAttribArrayARB },
{ "glDisableVertexAttribArrayARB" , (void **)&pglDisableVertexAttribArrayARB },
{ "glBindAttribLocationARB"       , (void **)&pglBindAttribLocationARB },
{ "glGetActiveAttribARB"          , (void **)&pglGetActiveAttribARB },
{ "glGetAttribLocationARB"        , (void **)&pglGetAttribLocationARB },
{ "glVertexAttrib2f"              , (void **)&pglVertexAttrib2fARB },
{ "glVertexAttrib2fv"             , (void **)&pglVertexAttrib2fvARB },
{ "glVertexAttrib3fv"             , (void **)&pglVertexAttrib3fvARB },
{ "glVertexAttrib4f"              , (void **)&pglVertexAttrib4fARB },
{ "glVertexAttrib4fv"             , (void **)&pglVertexAttrib4fvARB },
{ "glVertexAttrib4ubv"            , (void **)&pglVertexAttrib4ubvARB },
{ NULL, NULL }
};

static const dllfunc_t vertexshaderfuncs[] =
{
{ "glVertexAttribPointerARB"      , (void **)&pglVertexAttribPointerARB },
{ "glEnableVertexAttribArrayARB"  , (void **)&pglEnableVertexAttribArrayARB },
{ "glDisableVertexAttribArrayARB" , (void **)&pglDisableVertexAttribArrayARB },
{ "glBindAttribLocationARB"       , (void **)&pglBindAttribLocationARB },
{ "glGetActiveAttribARB"          , (void **)&pglGetActiveAttribARB },
{ "glGetAttribLocationARB"        , (void **)&pglGetAttribLocationARB },
{ NULL, NULL }
};

static const dllfunc_t vbofuncs[] =
{
{ "glBindBufferARB"    , (void **)&pglBindBufferARB },
{ "glDeleteBuffersARB" , (void **)&pglDeleteBuffersARB },
{ "glGenBuffersARB"    , (void **)&pglGenBuffersARB },
{ "glIsBufferARB"      , (void **)&pglIsBufferARB },
{ "glMapBufferARB"     , (void **)&pglMapBufferARB },
{ "glUnmapBufferARB"   , (void **)&pglUnmapBufferARB },
{ "glBufferDataARB"    , (void **)&pglBufferDataARB },
{ "glBufferSubDataARB" , (void **)&pglBufferSubDataARB },
{ NULL, NULL }
};

static const dllfunc_t vaofuncs[] =
{
{ "glBindVertexArray"    , (void **)&pglBindVertexArray },
{ "glDeleteVertexArrays" , (void **)&pglDeleteVertexArrays },
{ "glGenVertexArrays"    , (void **)&pglGenVertexArrays },
{ "glIsVertexArray"      , (void **)&pglIsVertexArray },
{ NULL, NULL }
};

static const dllfunc_t fbofuncs[] =
{
{ "glIsRenderbuffer"                      , (void **)&pglIsRenderbuffer },
{ "glBindRenderbuffer"                    , (void **)&pglBindRenderbuffer },
{ "glDeleteRenderbuffers"                 , (void **)&pglDeleteRenderbuffers },
{ "glGenRenderbuffers"                    , (void **)&pglGenRenderbuffers },
{ "glRenderbufferStorage"                 , (void **)&pglRenderbufferStorage },
{ "glGetRenderbufferParameteriv"          , (void **)&pglGetRenderbufferParameteriv },
{ "glIsFramebuffer"                       , (void **)&pglIsFramebuffer },
{ "glBindFramebuffer"                     , (void **)&pglBindFramebuffer },
{ "glDeleteFramebuffers"                  , (void **)&pglDeleteFramebuffers },
{ "glGenFramebuffers"                     , (void **)&pglGenFramebuffers },
{ "glCheckFramebufferStatus"              , (void **)&pglCheckFramebufferStatus },
{ "glFramebufferTexture1D"                , (void **)&pglFramebufferTexture1D },
{ "glFramebufferTexture2D"                , (void **)&pglFramebufferTexture2D },
{ "glFramebufferTexture3D"                , (void **)&pglFramebufferTexture3D },
{ "glFramebufferRenderbuffer"             , (void **)&pglFramebufferRenderbuffer },
{ "glGetFramebufferAttachmentParameteriv" , (void **)&pglGetFramebufferAttachmentParameteriv },
{ "glGenerateMipmap"                      , (void **)&pglGenerateMipmap },
{ NULL, NULL}
};

static const dllfunc_t occlusionfunc[] =
{
{ "glGenQueriesARB"        , (void **)&pglGenQueriesARB },
{ "glDeleteQueriesARB"     , (void **)&pglDeleteQueriesARB },
{ "glIsQueryARB"           , (void **)&pglIsQueryARB },
{ "glBeginQueryARB"        , (void **)&pglBeginQueryARB },
{ "glEndQueryARB"          , (void **)&pglEndQueryARB },
{ "glGetQueryivARB"        , (void **)&pglGetQueryivARB },
{ "glGetQueryObjectivARB"  , (void **)&pglGetQueryObjectivARB },
{ "glGetQueryObjectuivARB" , (void **)&pglGetQueryObjectuivARB },
{ NULL, NULL }
};

static const dllfunc_t binaryshaderfuncs[] =
{
{ "glProgramBinary"              , (void **)&pglProgramBinaryARB },
{ "glGetProgramBinary"           , (void **)&pglGetProgramBinaryARB },
{ NULL, NULL }
};
// half-D v0.8.9 RE-ENABLE for gl4es. The v0.4.4 disable existed because a
// binary-loaded program has no tracked shader sources and gl4es's
// fpe_CustomShader crashed reading them (SEGV at 0x20 in fpe.c, Mali-G57).
// That crash is now guarded in our gl4es fork (source-less programs are
// returned uncustomized), and matrices keep working for such programs:
// gl4es_useProgramBinary -> fill_program discovers the injected _gl4es_*
// matrix uniforms of the linked program BY NAME, and fpe.c pushes MVP
// through those locations on every draw. The hint setter glProgramParameteri
// was never exported by gl4es; it is optional per spec and every use is
// null-checked, so the table no longer requires it. GL_InitGPUShader also
// rejects any cached binary that does not expose _gl4es_ModelViewProjection-
// Matrix, and the whole cache stays switchable off via gl_binaryshader 0.
// The payoff is the v0.8.8 field log's #1 cost: 714 permutations at ~450 ms
// each recompiled on EVERY launch (5.5 min of 1-2 fps plus a 30 s map-load
// freeze) now persist as program binaries in the app-private cache dir.

#ifndef CALLBACK
#define CALLBACK
#endif // CALLBACK

/*
========================
DebugCallback

For ARB_debug_output
========================
*/
static void CALLBACK GL_DebugOutput( GLuint source, GLuint type, GLuint id, GLuint severity, GLint length, const GLcharARB *message, GLvoid *userParam )
{
	switch( type )
	{
	case GL_DEBUG_TYPE_ERROR_ARB:
		gEngfuncs.Con_Printf( va( "^1OpenGL Error:^7 %s\n", message ));
		break;
	case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR_ARB:
		gEngfuncs.Con_Printf( va( "^3OpenGL Warning:^7 %s\n", message ));
		break;
	case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR_ARB:
		gEngfuncs.Con_Printf( va( "^3OpenGL Warning:^7 %s\n", message ));
		break;
	case GL_DEBUG_TYPE_PORTABILITY_ARB:
		if( developer_level < DEV_EXTENDED )
			return;
		gEngfuncs.Con_Printf( va( "^3OpenGL Warning:^7 %s\n", message ));
		break;
	case GL_DEBUG_TYPE_PERFORMANCE_ARB:
		gEngfuncs.Con_Printf( va( "OpenGL Perfomance: %s\n", message ));
		break;
	case GL_DEBUG_TYPE_OTHER_ARB:
	default:
		if( developer_level < DEV_EXTENDED )
			return;
		gEngfuncs.Con_Printf( va( "OpenGL: %s\n", message ));
		break;
	}
}

/*
=================
GL_SetExtension
=================
*/
void GL_SetExtension( int r_ext, int enable )
{
	if( r_ext >= 0 && r_ext < R_EXTCOUNT )
		glConfig.extension[r_ext] = enable ? GL_TRUE : GL_FALSE;
	else ALERT( at_error, "GL_SetExtension: invalid extension %d\n", r_ext );
}

/*
=================
GL_Support
=================
*/
bool GL_Support( int r_ext )
{
	if( r_ext >= 0 && r_ext < R_EXTCOUNT )
		return glConfig.extension[r_ext] ? true : false;
	ConPrintf( "^3Error:^7 GL_Support: invalid extension %d\n", r_ext );
	return false;		
}

/*
=================
GL_CheckExtension
=================
*/
void GL_CheckExtension( const char *name, const dllfunc_t *funcs, const char *cvarname, int r_ext, bool cvar_from_engine = false )
{
	const dllfunc_t	*func;
	cvar_t		*parm;

	ALERT( at_aiconsole, "GL_CheckExtension: %s ", name );

	if(( name[2] == '_' || name[3] == '_' ) && !glConfig.es_context && !Q_strstr( glConfig.extensions_string, name ))
	{
		// half-D: desktop drivers advertise capabilities as GL_ARB_*/GL_EXT_*
		// extension strings. Native GLES3 has most of them in core and never
		// lists the strings (GL_ARB_multitexture being the one that disabled
		// the whole custom renderer in the 09:28 field crash — multitexture
		// is core in ES3). On an ES context let the function table decide:
		// if the entry points resolve, the capability exists.
		GL_SetExtension( r_ext, false );	// update render info
		ALERT( at_aiconsole, "- ^1failed\n" );
		return;
	}

	if( cvarname )
	{
		// NOTE: engine will be ignore cvar value if variable already exitsts (e.g. created on exec opengl.cfg)
		// so this call just update variable description (because Host_WriteOpenGLConfig won't archive cvars without it)
		if( cvar_from_engine ) parm = CVAR_GET_POINTER( cvarname );
		else parm = CVAR_REGISTER( (char *)cvarname, "1", FCVAR_GLCONFIG );

		// half-D: cvar_from_engine reads the ENGINE's capability cvars, which
		// the desktop GL ref creates from GL_ARB_* extension strings. The native
		// GLES ref never probes those legacy names, so the cvar is missing or
		// archived 0 — and the client renderer disabled itself on device even
		// though every function resolved (09:55 field crash). On an ES context
		// the function table is the source of truth; ignore the cvar value.
		if(( !glConfig.es_context && !CVAR_TO_BOOL( parm )) || ( !CVAR_TO_BOOL( r_extensions ) && r_ext != R_OPENGL_110 ))
		{
			ALERT( at_aiconsole, "- disabled\n" );
			GL_SetExtension( r_ext, false );
			return; // nothing to process at
		}
		GL_SetExtension( r_ext, true );
	}

	// clear exports
	for( func = funcs; func && func->name; func++ )
		*func->func = NULL;

	GL_SetExtension( r_ext, true ); // predict extension state
	for( func = funcs; func && func->name != NULL; func++ )
	{
		// functions are cleared before all the extensions are evaluated
		void *p = (void *)GL_GetProcAddress( func->name );
		if( !p )
		{
			if( !Q_strcmp( func->name, "glClearDepth" ) )
				p = (void *)GL_GetProcAddress( "glClearDepthf" );
			else if( !Q_strcmp( func->name, "glDepthRange" ) )
				p = (void *)GL_GetProcAddress( "glDepthRangef" );
			else if( !Q_strcmp( func->name, "glDrawRangeElements" ) )
				p = (void *)GL_GetProcAddress( "glDrawRangeElementsEXT" );
			else if( !Q_strcmp( func->name, "glDrawBuffers" ) )
				p = (void *)GL_GetProcAddress( "glDrawBuffersEXT" );
			else if( !Q_strcmp( func->name, "glDrawBuffer" ) )
				p = (void *)Stub_DrawBuffer;
			else if( !Q_strcmp( func->name, "glGetDoublev" ) )
				p = (void *)Stub_GetDoublev;
			else if( !Q_strcmp( func->name, "glAlphaFunc" ) )
				p = (void *)Stub_AlphaFunc;
			else if( !Q_strcmp( func->name, "glClipPlane" ) )
				p = (void *)Stub_ClipPlane;
		}

		// half-D: native GLES3 path. The engine gl2_shim answers the
		// fixed-function entry points, core ES3 answers the modern ones,
		// and whatever remains falls through to stubs below.
		if( !p )
		{
			static const struct
			{
				const char *name;
				void *ptr;
			} esStubs[] =
			{
			{ "glTexImage1D", (void *)Stub_NoOp4 },
			{ "glTexSubImage1D", (void *)Stub_NoOp3 },
			{ "glCopyTexImage1D", (void *)Stub_NoOp4 },
			{ "glCopyTexSubImage1D", (void *)Stub_NoOp3 },
			{ "glDrawPixels", (void *)Stub_NoOp4 },
			{ "glTexGenf", (void *)Stub_TexGenf },
			{ "glTexGenfv", (void *)Stub_TexGenfv },
			{ "glTexGeni", (void *)Stub_TexGeni },
			{ "glPolygonMode", (void *)Stub_PolygonMode },
			{ "glPolygonStipple", (void *)Stub_NoOp1 },
			{ "glPointSize", (void *)Stub_PointSize },
			{ "glRasterPos2f", (void *)Stub_RasterPos2f },
			{ "glPushAttrib", (void *)Stub_NoOp1 },
			{ "glPopAttrib", (void *)Stub_NoOp },
			{ "glGetTexImage", (void *)Stub_NoOp4 },
			{ "glGetTexEnviv", (void *)Stub_GetTexEnviv },
			{ "glPixelStoref", (void *)Stub_NoOp3 },
			{ "glGetTexLevelParameteriv", (void *)Stub_GetTexLevelParameteriv },
			{ "glGetTexLevelParameterfv", (void *)Stub_GetTexLevelParameterfv },
			{ "glGetClipPlane", (void *)Stub_GetClipPlane },
			{ "glIsList", (void *)Stub_IsList },
			{ "glMapBufferARB", (void *)Stub_MapBufferARB },
			{ "glUnmapBufferARB", (void *)Stub_UnmapBufferARB },
			{ "glNormalPointer", (void *)Stub_NormalPointer },
			{ "glVertexAttrib4ubv", (void *)Stub_VertexAttrib4ubv },
			{ "glFramebufferTexture1D", (void *)Stub_NoOp4 },
			{ "glFramebufferTexture3D", (void *)Stub_NoOp3 },
			{ "glGetHandleARB", (void *)Stub_GetHandleARB },
			{ "glGetAttachedObjectsARB", (void *)Stub_GetAttachedObjectsARB },
			{ "glGetObjectParameterfvARB", (void *)Stub_GetObjectParameterfvARB },
			{ "glCreateShaderObjectARB", (void *)Stub_glCreateShaderObjectARB },
			{ "glCreateProgramObjectARB", (void *)Stub_glCreateProgramObjectARB },
			{ "glAttachObjectARB", (void *)Stub_glAttachObjectARB },
			{ "glDetachObjectARB", (void *)Stub_glDetachObjectARB },
			{ "glDeleteObjectARB", (void *)Stub_glDeleteObjectARB },
			{ "glUseProgramObjectARB", (void *)Stub_glUseProgramObjectARB },
			{ "glMultiDrawArrays", (void *)Stub_MultiDrawArrays },
			{ "glGetQueryObjectivARB", (void *)Stub_GetQueryObjectivARB },
			{ "glTexEnvfv", (void *)Stub_TexEnvfv },
			{ "glGetInfoLogARB", (void *)Stub_glGetInfoLogARB },
			{ "glGetObjectParameterivARB", (void *)Stub_glGetObjectParameterivARB },
			{ "glShaderSourceARB", (void *)Stub_glShaderSourceARB },
			{ "glCompileShaderARB", (void *)Stub_glCompileShaderARB },
			{ "glLinkProgramARB", (void *)Stub_glLinkProgramARB },
			{ NULL, NULL }
			};

			for( int i = 0; esStubs[i].name; i++ )
			{
				if( !Q_strcmp( func->name, esStubs[i].name ))
				{
					p = esStubs[i].ptr;
					break;
				}
			}
		}

		// half-D: ARB/EXT-suffixed names whose core form exists natively on
		// GLES3 (glGenBuffersARB -> glGenBuffers, glBlendFuncSeparateEXT ->
		// glBlendFuncSeparate, ...) or in the gl2_shim (glMultiTexCoord2fARB
		// -> glMultiTexCoord2f)
		if( !p )
		{
			size_t len = Q_strlen( func->name );
			char corename[64];

			if( len > 3 && len < sizeof( corename ) + 3 && !Q_strcmp( func->name + len - 3, "ARB" ))
			{
				Q_strncpy( corename, func->name, len - 2 );
				p = (void *)GL_GetProcAddress( corename );
			}

			if( !p && len > 3 && len < sizeof( corename ) + 3 && !Q_strcmp( func->name + len - 3, "EXT" ))
			{
				Q_strncpy( corename, func->name, len - 2 );
				p = (void *)GL_GetProcAddress( corename );
			}
		}
		*func->func = p;
		if( !p )
			GL_SetExtension( r_ext, false ); // one or more functions are invalid, extension will be disabled
	}

	if( GL_Support( r_ext ))
		ALERT( at_aiconsole, "- ^2enabled\n" );
}
#ifdef WIN32
// credit: user8197171 on stackoverflow
#include <intrin.h>
#include <array>
const char* UTIL_GetCpuInfo( void )
{
	// 4 is essentially hardcoded due to the __cpuid function requirements.
	// NOTE: Results are limited to whatever the sizeof(int) * 4 is...
	std::array<int, 4> integerBuffer = {};
	constexpr size_t sizeofIntegerBuffer = sizeof( int ) * integerBuffer.size();

	std::array<char, 64> charBuffer = {};

	// The information you wanna query __cpuid for.
	// https://learn.microsoft.com/en-us/cpp/intrinsics/cpuid-cpuidex?view=vs-2019
	constexpr std::array<int, 3> functionIds = {
		// Manufacturer
		//  EX: "Intel(R) Core(TM"
		0x8000'0002,
		// Model
		//  EX: ") i7-8700K CPU @"
		0x8000'0003,
		// Clockspeed
		//  EX: " 3.70GHz"
		0x8000'0004
	};

	std::string cpu;

	for( int id : functionIds )
	{
		// Get the data for the current ID.
		__cpuid( integerBuffer.data(), id );

		// Copy the raw data from the integer buffer into the character buffer
		std::memcpy( charBuffer.data(), integerBuffer.data(), sizeofIntegerBuffer );

		// Copy that data into a std::string
		cpu += std::string( charBuffer.data() );
	}

	return cpu.c_str();
}
#endif

static void GL_InitExtensions( void )
{
#ifdef WIN32
	Msg( "CPU: %s\n", UTIL_GetCpuInfo() );
#endif

	// the renderer requires an OpenGL or OpenGL ES context
	int ctxType = RENDER_GET_PARM( PARM_GL_CONTEXT_TYPE, 0 );
	if( ctxType != CONTEXT_TYPE_GL && ctxType != CONTEXT_TYPE_GLES_2_X && ctxType != CONTEXT_TYPE_GLES_1_X )
	{
		g_fRenderInitialized = false;
		HOST_ERROR( "This game requires an OpenGL or OpenGL ES context.\nSoftware contexts are not supported.\n" );
		return;
	}

	// initialize gl extensions
	GL_CheckExtension( "OpenGL 1.1.0", opengl_110funcs, NULL, R_OPENGL_110 );
	GL_CheckExtension( "OpenGL 2.0", opengl_200funcs, NULL, R_OPENGL_200 );

	if( !GL_Support( R_OPENGL_110 ))
	{
		ConPrintf( "^1Error:^7 OpenGL 1.0 can't be installed. Custom renderer disabled.\n" );
		g_fRenderInitialized = false;
		return;
	}

	// get our various GL strings
	glConfig.vendor_string = pglGetString( GL_VENDOR );
	glConfig.renderer_string = pglGetString( GL_RENDERER );
	glConfig.version_string = pglGetString( GL_VERSION );
	glConfig.extensions_string = pglGetString( GL_EXTENSIONS );

	if( Q_stristr( glConfig.renderer_string, "geforce" ))
		glConfig.hardware_type = GLHW_NVIDIA;
	else if( Q_stristr( glConfig.renderer_string, "quadro fx" ))
		glConfig.hardware_type = GLHW_NVIDIA;
	else if( Q_stristr(glConfig.renderer_string, "rv770" ))
		glConfig.hardware_type = GLHW_RADEON;
	else if( Q_stristr(glConfig.renderer_string, "radeon hd" ))
		glConfig.hardware_type = GLHW_RADEON;
	else if( Q_stristr( glConfig.renderer_string, "eah4850" ) || Q_stristr( glConfig.renderer_string, "eah4870" ))
		glConfig.hardware_type = GLHW_RADEON;
	else if( Q_stristr( glConfig.renderer_string, "radeon" ))
		glConfig.hardware_type = GLHW_RADEON;
	else glConfig.hardware_type = GLHW_GENERIC;

	glConfig.version = Q_atof( glConfig.version_string );

	// half-D: native GLES contexts report "OpenGL ES 3.x ..." — Q_atof stops
	// at the leading text and returns 0. Skip the prefix, and remember the
	// context type: the shader loader emits GLSL ES 300 and injects the
	// fixed-function matrices as uniforms on this path.
	{
		const char *es = Q_stristr( glConfig.version_string, "OpenGL ES" );

		if( es != NULL )
		{
			glConfig.es_context = true;
			glConfig.version = Q_atof( es + 10 ); // skip "OpenGL ES"
		}
	}

	Msg( "GL_VERSION: %g%s\n", glConfig.version, glConfig.es_context ? " (GLES)" : "" );

	// half-D: shim matrix query for uniform injection (native GLES3 only)
	if( glConfig.es_context )
	{
		pglHalfDGetMatrix = (void (*)( GLenum, GLfloat * ))GL_GetProcAddress( "halfD_GL2GetMatrix" );
		if( !pglHalfDGetMatrix )
			ConPrintf( "^3half-D: gl2_shim matrix query unavailable — fixed-function matrices will not reach shaders^7\n" );
	}

	GL_CheckExtension( "glDrawRangeElements", drawrangeelementsfuncs, "gl_drawrangeelments", R_DRAW_RANGEELEMENTS_EXT );

	if( !GL_Support( R_DRAW_RANGEELEMENTS_EXT ))
		GL_CheckExtension( "GL_EXT_draw_range_elements", drawrangeelementsextfuncs, "gl_drawrangeelments", R_DRAW_RANGEELEMENTS_EXT );

	// we don't care if it's an extension or not, they are identical functions, so keep it simple in the rendering code
	if( pglDrawRangeElementsEXT == NULL ) pglDrawRangeElementsEXT = pglDrawRangeElements;

	if( !GL_Support( R_DRAW_RANGEELEMENTS_EXT ))
	{
		ConPrintf( "^1Error:^7 GL_EXT_draw_range_elements not supported. Custom renderer disabled\n" );
		g_fRenderInitialized = false;
		return;
	}

	// multitexture
	glConfig.max_texture_units = 1;
	GL_CheckExtension( "GL_ARB_multitexture", multitexturefuncs, "gl_arb_multitexture", R_ARB_MULTITEXTURE, true );

	if( GL_Support( R_ARB_MULTITEXTURE ))
	{
		pglGetIntegerv( GL_MAX_TEXTURE_UNITS, &glConfig.max_texture_units );
		// half-D: GL_MAX_TEXTURE_UNITS is a fixed-function ES1/desktop enum;
		// native GLES3 rejects the query and leaves the value at 0. The
		// engine ref falls back to texture image units (gl_opengl.c) — do the
		// same, it's what the bound-texture code actually cares about.
		if( glConfig.max_texture_units <= 1 )
			pglGetIntegerv( GL_MAX_TEXTURE_IMAGE_UNITS_ARB, &glConfig.max_texture_units );
	}

	if( glConfig.max_texture_units <= 1 )
		GL_SetExtension( R_ARB_MULTITEXTURE, false );

	if( !GL_Support( R_ARB_MULTITEXTURE ))
	{
		ConPrintf( "^1Error:^7 GL_ARB_multitexture is not supported. Custom renderer disabled.\n" );
		g_fRenderInitialized = false;
		return;
	}

	// 3d texture support
	GL_CheckExtension( "GL_EXT_texture3D", NULL, "gl_texture_3d", R_TEXTURE_3D_EXT, true );

	if( GL_Support( R_TEXTURE_3D_EXT ))
	{
		pglGetIntegerv( GL_MAX_3D_TEXTURE_SIZE, &glConfig.max_3d_texture_size );

		if( glConfig.max_3d_texture_size < 32 )
		{
			GL_SetExtension( R_TEXTURE_3D_EXT, false );
			ALERT( at_error, "GL_EXT_texture3D reported bogus GL_MAX_3D_TEXTURE_SIZE, disabled\n" );
		}
	}

	// 2d texture array support
	GL_CheckExtension( "GL_EXT_texture_array", NULL, "gl_texture_2d_array", R_TEXTURE_ARRAY_EXT, true );

	if( !GL_Support( R_TEXTURE_ARRAY_EXT ))
		ALERT( at_warning, "GL_EXT_texture_array not supported. Landscapes will be unavailable\n" );

	// occlusion queries
	GL_CheckExtension( "GL_ARB_occlusion_query", occlusionfunc, "gl_occlusion_queries", R_OCCLUSION_QUERIES_EXT );

	// separate blend
	GL_CheckExtension( "GL_EXT_blend_func_separate", blendseparatefunc, "gl_separate_blend", R_SEPARATE_BLENDFUNC_EXT );

	// hardware cubemaps
	GL_CheckExtension( "GL_ARB_texture_cube_map", NULL, "gl_texture_cubemap", R_TEXTURECUBEMAP_EXT, true );

//	GL_CheckExtension( "GL_ARB_seamless_cube_map", NULL, "gl_seamless_cubemap", R_SEAMLESS_CUBEMAP ); // remove for now, unless we really need it

	if( GL_Support( R_TEXTURECUBEMAP_EXT ))
		pglGetIntegerv( GL_MAX_CUBE_MAP_TEXTURE_SIZE_ARB, &glConfig.max_cubemap_size );
	else
		ALERT( at_warning, "GL_ARB_texture_cube_map not supported. Cubemap reflections and omni shadows will be disabled.\n" );

	GL_CheckExtension( "GL_ARB_texture_non_power_of_two", NULL, "gl_texture_npot", R_ARB_TEXTURE_NPOT_EXT, true );

	GL_CheckExtension( "GL_ARB_draw_buffers", NULL, "gl_draw_buffers", R_DRAW_BUFFERS_EXT );

	GL_CheckExtension( "GL_ARB_vertex_buffer_object", vbofuncs, "gl_vertex_buffer_object", R_ARB_VERTEX_BUFFER_OBJECT_EXT );

	if( !GL_Support( R_ARB_VERTEX_BUFFER_OBJECT_EXT ))
	{
		ALERT( at_error, "GL_ARB_vertex_buffer_object not support. Custom renderer disabled\n" );
		g_fRenderInitialized = false;
		return;
	}

	GL_CheckExtension( "GL_ARB_vertex_array_object", vaofuncs, "gl_vertex_array_object", R_ARB_VERTEX_ARRAY_OBJECT_EXT );

	if( !GL_Support( R_ARB_VERTEX_ARRAY_OBJECT_EXT ))
	{
		ALERT( at_error, "GL_ARB_vertex_array_object not support. Custom renderer disabled\n" );
		g_fRenderInitialized = false;
		return;
	}

	GL_CheckExtension( "GL_EXT_gpu_shader4", NULL, "gl_ext_gpu_shader4", R_EXT_GPU_SHADER4 );
	if( !GL_Support( R_EXT_GPU_SHADER4 ) )
		ALERT( at_warning, "GL_EXT_gpu_shader4 not support. Shadows from omni lights will be disabled\n" );

	// vp and fp shaders
	GL_CheckExtension( "GL_ARB_shader_objects", shaderobjectsfuncs, "gl_shaderobjects", R_SHADER_OBJECTS_EXT );

	if( !GL_Support( R_SHADER_OBJECTS_EXT ))
	{
		ConPrintf( "^1Error:^7 GL_ARB_shader_objects not support. Custom renderer disabled\n" );
		g_fRenderInitialized = false;
		return;
	}

	GL_CheckExtension( "GL_ARB_shading_language_100", NULL, "gl_shading_language", R_SHADER_GLSL100_EXT );

	if( !GL_Support( R_SHADER_GLSL100_EXT ))
	{
		ConPrintf( "^1Error:^7 GL_ARB_shading_language_100 not support. Custom renderer disabled\n" );
		g_fRenderInitialized = false;
		return;
	}

	GL_CheckExtension( "GL_ARB_vertex_shader", vertexshaderfuncs, "gl_vertexshader", R_VERTEX_SHADER_EXT );

	if( !GL_Support( R_VERTEX_SHADER_EXT ))
	{
		ConPrintf( "^1Error:^7 GL_ARB_vertex_shader not support. Custom renderer disabled\n" );
		g_fRenderInitialized = false;
		return;
	}

	GL_CheckExtension( "GL_ARB_fragment_shader", NULL, "gl_pixelshader", R_FRAGMENT_SHADER_EXT );

	if( !GL_Support( R_FRAGMENT_SHADER_EXT ))
	{
		ConPrintf( "^1Error:^7 GL_ARB_fragment_shader not support. Custom renderer disabled\n" );
		g_fRenderInitialized = false;
		return;
	}

	GL_CheckExtension( "GL_ARB_depth_texture", NULL, "gl_depthtexture", R_DEPTH_TEXTURE );
	GL_CheckExtension( "GL_ARB_shadow", NULL, "gl_arb_shadow", R_SHADOW_EXT );

	if( !GL_Support( R_DEPTH_TEXTURE ) || !GL_Support( R_SHADOW_EXT ))
	{
		ALERT( at_warning, "GL_ARB_depth_texture or GL_ARB_shadow not supported. Dynamic shadows are disabled\n" );
		tr.shadows_notsupport = true;
	}

	glConfig.max_2d_texture_size = 0;
	pglGetIntegerv( GL_MAX_TEXTURE_SIZE, &glConfig.max_2d_texture_size );
	if( glConfig.max_2d_texture_size <= 0 ) glConfig.max_2d_texture_size = 256;

	// FBO support
	GL_CheckExtension( "GL_ARB_framebuffer_object", fbofuncs, "gl_framebuffers", R_FRAMEBUFFER_OBJECT );

	GL_CheckExtension( "GL_ARB_get_program_binary", binaryshaderfuncs, "gl_binaryshader", R_BINARY_SHADER_EXT );

	// half-D v0.4.4: no optional resolution of the hint setter here — see the
	// binaryshaderfuncs table above. gl4es must stay without gl_binaryshader.

	// check for hardware skinning
	pglGetIntegerv( GL_MAX_VERTEX_UNIFORM_COMPONENTS_ARB, &glConfig.max_vertex_uniforms );
	pglGetIntegerv( GL_MAX_VERTEX_ATTRIBS_ARB, &glConfig.max_vertex_attribs );
	pglGetIntegerv( GL_MAX_VARYING_FLOATS_ARB, &glConfig.max_varying_floats );

	if( glConfig.hardware_type == GLHW_RADEON && glConfig.max_vertex_uniforms > 512 )
		glConfig.max_vertex_uniforms /= 4; // only radeon returns count of floats other returns count of vec4

	if( !glConfig.max_vertex_uniforms )
	{
		pglGetIntegerv( GL_MAX_VERTEX_UNIFORM_VECTORS, &glConfig.max_vertex_uniforms );
		glConfig.max_vertex_uniforms *= 4;
	}

	if( !glConfig.max_varying_floats )
	{
		pglGetIntegerv( GL_MAX_VARYING_VECTORS, &glConfig.max_varying_floats );
		glConfig.max_varying_floats *= 4;
	}

#if defined(__ANDROID__) || defined(ANDROID)
	// Mobile GPUs (Adreno, Mali) have a hardware limit of 256 vec4 vertex uniform vectors.
	// 76 bones require 152 vec4 vectors (76 quat + 76 pos), which together with u_GammaTable[64]
	// and ~22 other vertex uniform vectors (~238 vec4 total) stays safely below 256 while
	// supporting full NPC skeletons.
	glConfig.max_skinning_bones = bound( 32, ( Q_max( glConfig.max_vertex_uniforms - MAX_RESERVED_UNIFORMS, 0 ) / 7 ), 76 );
#else
	glConfig.max_skinning_bones = bound( 0, ( Q_max( glConfig.max_vertex_uniforms - MAX_RESERVED_UNIFORMS, 0 ) / 7 ), MAXSTUDIOBONES );
#endif

	if( glConfig.max_skinning_bones < 32 )
	{
		ALERT( at_error, "Hardware Skinning not supported. Custom renderer disabled\n" );
		g_fRenderInitialized = false;
		return;
	}
	else if( glConfig.max_skinning_bones < MAXSTUDIOBONES )
	{
		ALERT( at_warning, "Hardware Skinning has a limitation (max %i bones)\n", glConfig.max_skinning_bones );

		// g-cont. this produces too many shaders...
//		glConfig.uniforms_economy = true;
	}

	ALERT( at_aiconsole, "GL_InitExtensions: max vertex uniforms %i\n", glConfig.max_vertex_uniforms );
	ALERT( at_aiconsole, "GL_InitExtensions: max varying floats %i\n", glConfig.max_varying_floats );
	ALERT( at_aiconsole, "GL_InitExtensions: MaxSkinned bones %i\n", glConfig.max_skinning_bones );

	glConfig.max_texture_units = RENDER_GET_PARM( PARM_MAX_IMAGE_UNITS, 0 );
}

/*
===============
GL_SetDefaultState
===============
*/
static void GL_SetDefaultState( void )
{
	if( RENDER_GET_PARM( PARM_STENCIL_ACTIVE, 0 ))
		glState.stencilEnabled = true;
	else glState.stencilEnabled = false;

	glState.frameBuffer = FBO_MAIN;
}

void R_CreateSpotLightTexture( void )
{
	int width = 64, height = 64;
	byte *data = new byte[width*height*4];
	byte *p = data;

	for( int i = 0; i < height; i++ )
	{
		float dy = (height * 0.5f - i + 0.5f) / (height * 0.5f);

		for( int j = 0; j < width; j++ )
		{
			float dx = (width * 0.5f - j + 0.5f) / (width * 0.5f);
			float r = cos( M_PI / 2.0f * sqrt(dx * dx + dy * dy));
			float c;

			r = (r < 0) ? 0 : r * r;
			c = 0xFF * r;
			p[0] = (c <= 0xFF) ? c : 0xFF;
			p[1] = (c <= 0xFF) ? c : 0xFF;
			p[2] = (c <= 0xFF) ? c : 0xFF;
			p[3] = (c <= 0xff) ? c : 0xFF;
			p += 4;
		}
	}

	tr.spotlightTexture = CREATE_TEXTURE( "*spotlight", width, height, data, TF_SPOTLIGHT ); 
	delete [] data;
}

static void R_InitAlphaContrast( void )
{
	byte	data[64*64*4];
	int	size = 64;

	memset( data, size, size * size * 4 );

	tr.alphaContrastTexture = CREATE_TEXTURE( "*acont", size, size, data, TF_ALPHACONTRAST );
}

/*
==================
R_InitWhiteCubemap
==================
*/
static void R_InitStubCubemaps( void )
{
	byte dataCM[4*4*6*4];
	int	size = 4;

	if( !GL_Support( R_TEXTURECUBEMAP_EXT ))
		return;

	// white cubemap - just stub for pointlights
	memset( dataCM, 0xFF, sizeof( dataCM ));
	tr.whiteCubeTexture = CREATE_TEXTURE( "*whiteCube", size, size, dataCM, TF_NOMIPMAP|TF_CUBEMAP|TF_CLAMP );
	memset( dataCM, 0x00, sizeof( dataCM ) );
	tr.blackCubeTexture = CREATE_TEXTURE( "*blackCube", size, size, dataCM, TF_NOMIPMAP|TF_CUBEMAP|TF_CLAMP );
	tr.depthCubemap = CREATE_TEXTURE( "depthCube", 8, 8, NULL, TF_SHADOW_CUBEMAP );
	if( !tr.depthCubemap )
		tr.depthCubemap = tr.whiteCubeTexture; // half-D: shadowless pointlight stub (no depth cubemaps via gl4es)
}

static void GL_InitTextures( void )
{
	// NOTE: these textures already created by engine
	// get their texturenums for local using
	tr.defaultTexture = FIND_TEXTURE( REF_DEFAULT_TEXTURE );   	// use for bad textures
	tr.whiteTexture = FIND_TEXTURE( REF_WHITE_TEXTURE );
	tr.grayTexture = FIND_TEXTURE( REF_GRAY_TEXTURE );
	tr.blackTexture = FIND_TEXTURE( REF_BLACK_TEXTURE );
	tr.depthTexture = CREATE_TEXTURE( "*depth", 8, 8, NULL, TF_SHADOW );
	if( !tr.depthTexture )
	{
		// half-D: no GL_ARB_depth_texture through gl4es — the depth stub fails
		// and every dlight pass used to abort ("shadow texture for the light
		// is missing"), leaving dynamic lights completely unrendered. Give
		// shadowless lights a plain white 8x8 stub so the light pass runs.
		byte depthStub[8*8];
		memset( depthStub, 0xFF, sizeof( depthStub ));
		tr.depthTexture = CREATE_TEXTURE( "*depth_stub", 8, 8, depthStub, TF_NOMIPMAP|TF_CLAMP|TF_LUMINANCE );
		ConPrintf( "half-D: depth textures unsupported, dynamic lights will be shadowless\n" );
	}

	R_InitStubCubemaps();

	R_CreateSpotLightTexture();

	R_InitAlphaContrast();

	// load water animation
	if( !tr.lowmemory )
	{
		for( int i = 0; ; i++ )
		{
			char path[256];
			Q_snprintf( path, sizeof( path ), "gfx/water/water_normal_%i", i );
			int water_texture = 0;
			if( IMAGE_EXISTS( path ) ) 
				water_texture = LOAD_TEXTURE( path, NULL, 0, TF_NORMALMAP );
			if( water_texture > 0 )
				tr.waterTextures.AddToTail( water_texture );
			else break;
		}
	}

	if( !(tr.volumetric_light_texture = LOAD_TEXTURE( "sprites/diffusion/volumetric.dds", NULL, 0, 0 )) )
		ConPrintf( "^1Error:^7 sprites/diffusion/volumetric.dds couldn't be loaded, env_volumetric_light won't work.\n" );
}

/*
==================
R_Init
==================
*/
bool R_Init( void )
{
	R_InitRefState();
	GL_InitExtensions();

	// half-D v0.7.0: performance mode. The custom GL2 renderer is disabled
	// by default on this fork: on the target device it measured 11-18 fps
	// at best through gl4es (219k-258k epoly in car scenes — the models
	// carry no studioLOD data, so LOD cannot reduce them) with shader-
	// compile storms up to 31 s at map load, and the precompile alternative
	// cost 6+ minutes of 0.5-1 fps menu on every launch. The engine's
	// native renderer draws the game instead: no client shader compiles at
	// all, upstream-tested batching. All client render hooks already no-op
	// on g_fRenderInitialized == false (the v0.5.2 veto path).
	// gl_diffusion_renderer 1 (the v0.7.3 default) is the full shader look.
	//
	// half-D v0.7.1: GL_InitExtensions MUST run first even when gating.
	// v0.7.0 returned before it, leaving every client pgl pointer NULL —
	// the crosshair HUD draws with pglBindVertexArray/pglDrawArrays and
	// crashed with SIGSEGV at 0x0 on device (14:52 log). Resolving the
	// table is the v0.5.2/v0.6.x-proven state: those calls then hit real
	// (harmless, no-op) GL entry points. HUD_RenderFrame returns 0 below
	// via g_fRenderInitialized == false, so the engine ref draws the frame.
	if( g_fRenderInitialized && gl_diffusion_renderer && !CVAR_TO_BOOL( gl_diffusion_renderer ))
	{
		g_fRenderInitialized = false;
		CVAR_SET_FLOAT( "gl_renderer", 0 );
		CVAR_SET_FLOAT( "ui_shadersdone", 1 ); // mainui hides every menu until this is 1
		ConPrintf( "^3half-D v0.8.10 performance mode:^7 the game's custom shader renderer is disabled; the engine renderer draws the game (this game needs the custom renderer — expect missing materials/lights).\n" );
		ConPrintf( "^3half-D:^7 full shader look: set gl_diffusion_renderer 1.\n" );
		return false;
	}

	if( !g_fRenderInitialized )
	{
		CVAR_SET_FLOAT( "gl_renderer", 0 );
		R_Shutdown();
		return false;
	}

	if( gEngfuncs.CheckParm( "-lowmem", NULL ) )
	{
		tr.lowmemory = true;
		ConPrintf( "^3Warning:^7 low memory mode is active!\n" );
	}
	
	GL_InitGPUShaders();
	GL_InitTextures();
	GL_InitRandomTable();
	GL_SetDefaultState();

	R_InitWeather();
	R_GrassInit();
	InitPostEffects();
	
	pglPointSize( 10.0f );

	// init default lights - helpers to precache shaders
	memset( &tr.defSpotlight, 0, sizeof( tr.defSpotlight ) );
	memset( &tr.defOmnilight, 0, sizeof( tr.defOmnilight ) );
	tr.defSpotlight.pointlight = false;
	tr.defOmnilight.pointlight = true;

	// half-D v0.7.4: one unambiguous startup line. The v0.7.3 field report
	// ("1 fps, everything broken") could not be attributed from the log
		// because ref, renderer mode and compile mode were never stated
		// together. This line settles all three for every future log.
		// half-D v0.8.1: compile mode states the rate limiter too.
		// half-D v0.8.5: renderer label no longer hardcodes "(gl4es)" —
		// the ref is already printed first (v0.8.5 defaults to gles3compat,
		// the native ES3 path); es_context=yes on it is EXPECTED, not a bug.
		ConPrintf( "^2half-D v0.8.10:^7 ref=%s renderer=custom es_context=%s binary_shader_cache=%s compile=%s\n",
		CVAR_GET_STRING( "r_refdll_loaded" ),
		glConfig.es_context ? "yes" : "no",
		GL_Support( R_BINARY_SHADER_EXT ) ? "yes" : "no",
		GL_Support( R_BINARY_SHADER_EXT ) ? "binary-cache+load-phase" : "post-load-phase+first-use-inline(r_shader_lazy_hz 0)" );

	return true;
}

/*
===============
R_Shutdown
===============
*/
void R_Shutdown( void )
{
	g_StudioRenderer.DestroyAllModelInstances();
	g_StudioRenderer.FreeMeshCacheVL();

	R_FreeCinematics();
	R_ResetWeather();
	R_GrassShutdown();
	GL_FreeGPUShaders();

	// now all extensions are disabled
	memset( glConfig.extension, 0, sizeof( glConfig.extension[0] ) * R_EXTCOUNT );
}

/*
=================
GL_CheckForErrors

obsolete
=================
*/
void GL_CheckForErrors_( const char *filename, const int fileline )
{
	int	err;
	const char	*str;

	if( !CVAR_TO_BOOL( gl_check_errors ))
		return;

	if(( err = pglGetError( )) == GL_NO_ERROR )
		return;

	switch( err )
	{
	case GL_STACK_OVERFLOW:
		str = "GL_STACK_OVERFLOW";
		break;
	case GL_STACK_UNDERFLOW:
		str = "GL_STACK_UNDERFLOW";
		break;
	case GL_INVALID_ENUM:
		str = "GL_INVALID_ENUM";
		break;
	case GL_INVALID_VALUE:
		str = "GL_INVALID_VALUE";
		break;
	case GL_INVALID_OPERATION:
		str = "GL_INVALID_OPERATION";
		break;
	case GL_OUT_OF_MEMORY:
		str = "GL_OUT_OF_MEMORY";
		break;
	default:
		str = "UNKNOWN ERROR";
		break;
	}

	gEngfuncs.Con_Printf( "^3OpenGL Error:^7 %s (called at %s:%i)\n", str, filename, fileline );
}
