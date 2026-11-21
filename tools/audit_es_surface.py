#!/usr/bin/env python3
"""Audit: every GL function name the client resolves vs what the
gles3compat path provides (native ES3 + gl2_shim exports + planned stubs).

Outputs the list of names that would fail to resolve (NULL) so the client
stub table can be built exhaustively — no guessing.
"""
import re, subprocess, sys

CLIENT = "/home/z/my-project/half-D/game/client/render/r_opengl.cpp"
SHIM = "/home/z/my-project/half-D/engine/ref/gl/gl2_shim/gl2_shim.c"

# 1. all names in the client's dllfunc tables
src = open(CLIENT).read()
names = set(re.findall(r'\{\s*"((?:gl|wgl|glX)[A-Za-z0-9]+)"\s*,', src))

# 2. shim exports (names in GL2_ShimGetProcAddress strcmp list + internal GL2_*)
shim_src = open(SHIM).read()
shim_exports = set(re.findall(r'Q_strcmp\( name, "((?:gl|halfD)[A-Za-z0-9]+)"', shim_src))

# 3. native ES3 / EGL surface: from the GLES3 gl3ext header shipped with the NDK
#    (not available here) — use a curated list of core ES 3.0/3.1/3.2 entries.
es3 = set("""
glActiveTexture glAttachShader glBeginQuery glBeginTransformFeedback glBindAttribLocation
glBindBuffer glBindBufferBase glBindBufferRange glBindFramebuffer glBindImageTexture
glBindProgramPipeline glBindRenderbuffer glBindSampler glBindTexture glBindTransformFeedback
glBindVertexArray glBindVertexBuffer glBlendColor glBlendEquation glBlendEquationSeparate
glBlendFunc glBlendFuncSeparate glBlitFramebuffer glBufferData glBufferSubData
glCheckFramebufferStatus glClear glClearBufferfi glClearBufferfv glClearBufferiv
glClearBufferuiv glClearColor glClearDepthf glClearStencil glClientWaitSync glColorMask
glCompileShader glCompressedTexImage2D glCompressedTexImage3D glCompressedTexSubImage2D
glCompressedTexSubImage3D glCopyBufferSubData glCopyTexImage2D glCopyTexSubImage2D
glCopyTexSubImage3D glCreateProgram glCreateShader glCreateShaderProgramv glCullFace
glDeleteBuffers glDeleteFramebuffers glDeleteProgram glDeleteProgramPipelines
glDeleteQueries glDeleteRenderbuffers glDeleteSamplers glDeleteShader glDeleteSync
glDeleteTextures glDeleteTransformFeedbacks glDeleteVertexArrays glDepthFunc glDepthMask
glDepthRangef glDetachShader glDisable glDisableVertexAttribArray glDispatchCompute
glDispatchComputeIndirect glDrawArrays glDrawArraysIndirect glDrawArraysInstanced
glDrawBuffers glDrawElements glDrawElementsBaseVertex glDrawElementsIndirect
glDrawElementsInstanced glDrawElementsInstancedBaseVertex glDrawRangeElements
glEGLImageTargetTexture2DOES glEnable glEnableVertexAttribArray glEndQuery
glEndTransformFeedback glFenceSync glFinish glFlush glFlushMappedBufferRange
glFramebufferParameteri glFramebufferRenderbuffer glFramebufferTexture
glFramebufferTexture2D glFramebufferTexture3DOES glFramebufferTextureLayer
glFrontFace glGenBuffers glGenFramebuffers glGenProgramPipelines glGenQueries
glGenRenderbuffers glGenSamplers glGenTextures glGenTransformFeedbacks
glGenVertexArrays glGenerateMipmap glGetActiveAttrib glGetActiveUniform
glGetActiveUniformBlockName glGetActiveUniformBlockiv glGetActiveUniformName
glGetActiveUniformsiv glGetAttachedShaders glGetAttribLocation glGetBooleanv
glGetBufferParameteri64v glGetBufferParameteriv glGetBufferPointerv glGetError
glGetFloatv glGetFragDataLocation glGetFramebufferAttachmentParameteriv
glGetFramebufferParameteriv glGetGraphicsResetStatusEXT glGetInteger64i_v
glGetInteger64v glGetIntegerv glGetInternalformativ glGetMultisamplefv
glGetProgramBinary glGetProgramInfoLog glGetProgramInterfaceiv glGetProgramPipelineInfoLog
glGetProgramPipelineiv glGetProgramResourceIndex glGetProgramResourceLocation
glGetProgramResourceName glGetProgramResourceiv glGetProgramiv glGetQueryObjectuiv
glGetQueryiv glGetRenderbufferParameteriv glGetSamplerParameterIiv
glGetSamplerParameterIuiv glGetSamplerParameterfv glGetSamplerParameteriv
glGetShaderInfoLog glGetShaderPrecisionFormat glGetShaderSource glGetShaderiv
glGetString glGetStringi glGetSynciv glGetTexLevelParameterfv glGetTexLevelParameteriv
glGetTexParameterIiv glGetTexParameterIuiv glGetTexParameterfv glGetTexParameteriv
glGetTransformFeedbackVarying glGetUniformBlockIndex glGetUniformIndices
glGetUniformLocation glGetUniformfv glGetUniformiv glGetUniformuiv
glGetVertexAttribIiv glGetVertexAttribIuiv glGetVertexAttribPointerv
glGetVertexAttribfv glGetVertexAttribiv glGetnUniformfv EXT glGetUniformfv
glHint glInvalidateFramebuffer glInvalidateSubFramebuffer glIsBuffer glIsEnabled
glIsFramebuffer glIsProgram glIsProgramPipeline glIsQuery glIsRenderbuffer
glIsSampler glIsShader glIsSync glIsTexture glIsTransformFeedback glIsVertexArray
glLineWidth glLinkProgram glMapBufferRange glMemoryBarrier glMemoryBarrierByRegion
glMinSampleShader glGetProgramBinary glPauseTransformFeedback glPixelStorei
glPolygonOffset glProgramBinary glProgramParameteri glProgramUniform1f
glProgramUniform1fv glReadBuffer glReadPixels glReleaseShaderCompiler glRenderbufferStorage
glRenderbufferStorageMultisample glResumeTransformFeedback glSampleCoverage
glSamplerParameterIiv glSamplerParameterIuiv glSamplerParameterf glSamplerParameterfv
glSamplerParameteri glSamplerParameteriv glScissor glShaderBinary glShaderSource
glStencilFunc glStencilFuncSeparate glStencilMask glStencilMaskSeparate glStencilOp
glStencilOpSeparate glTexBuffer glTexBufferRange glTexImage2D glTexImage3D
glTexParameterIiv glTexParameterIuiv glTexParameterf glTexParameterfv glTexParameteri
glTexParameteriv glTexStorage1D glTexStorage2D glTexStorage2DMultisample glTexStorage3D
glTexStorage3DMultisample glTexSubImage2D glTexSubImage3D glTransformFeedbackVaryings
glUniform1f glUniform1fv glUniform1i glUniform1iv glUniform1ui glUniform1uiv
glUniform2f glUniform2fv glUniform2i glUniform2iv glUniform2ui glUniform2uiv
glUniform3f glUniform3fv glUniform3i glUniform3iv glUniform3ui glUniform3uiv
glUniform4f glUniform4fv glUniform4i glUniform4iv glUniform4ui glUniform4uiv
glUniformBlockBinding glUniformMatrix2fv glUniformMatrix2x3fv glUniformMatrix2x4fv
glUniformMatrix3fv glUniformMatrix3x2fv glUniformMatrix3x4fv glUniformMatrix4fv
glUniformMatrix4x2fv glUniformMatrix4x3fv glUnmapBuffer glUseProgram glUseProgramStages
glValidateProgram glValidateProgramPipeline glVertexAttrib1f glVertexAttrib1fv
glVertexAttrib2f glVertexAttrib2fv glVertexAttrib3f glVertexAttrib3fv
glVertexAttrib4f glVertexAttrib4fv glVertexAttribBinding glVertexAttribDivisor
glVertexAttribFormat glVertexAttribIFormat glVertexAttribIPointer
glVertexAttribPointer glVertexBindingDivisor glViewport glWaitSync
""".split())

def strip_arb(n):
    return n[:-3] if n.endswith("ARB") else None

problems = []
for n in sorted(names):
    if n in shim_exports or n in es3:
        continue
    s = strip_arb(n)
    if s and (s in shim_exports or s in es3):
        continue
    problems.append(n)

print(f"client resolves {len(names)} unique names")
print(f"resolved by shim/native/ARB-strip: {len(names) - len(problems)}")
print(f"\nUNRESOLVED ({len(problems)}) — need client stubs or shim additions:")
for p in problems:
    print("  " + p)
