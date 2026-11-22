#!/usr/bin/env python3
"""Offline GLES3 validation for the half-D ES 300 shader path.

Replicates the client's GL_ProcessShader/GL_ParseFile preprocessing exactly
(the ES branch added in v0.5.0), then compiles every shader permutation from
r_shaderlist.h — plus the fixed glsl.* pairs from GL_InitGPUShaders — with
glslangValidator as GLSL ES 300, and link-checks each vp/fp pair.

Device profile emulated: the NATIVE GLES3 path as of v0.5.4 — MAXSTUDIOBONES=76,
gpu_shader4 feature defines ON (integer/bitwise are ES3 core; the desktop
extension DIRECTIVE is not emitted on ES), texture_array ON (client-side
check passes on ES), shadow permutations INCLUDED (native ES3 has shadow
samplers — the old gl4es-era filters are gone). Any failure here would be a
shader-compile failure on the phone, so the bar is zero failures.
"""
import os, re, subprocess, sys, tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GLSL = os.path.join(ROOT, "game", "glsl")
VALIDATOR = "/tmp/glslang/build/StandAlone/glslangValidator"

MAXSTUDIOBONES = 76       # Android bound in r_opengl.cpp
MAX_LIGHTSTYLES = 64      # r_const.h
MAXLIGHTMAPS = 4          # com_model.h
GRASS_ANIM_DIST = 512.0   # r_grass.h

# ---- the ES line transform, mirroring HD_TransformLineES ----

def replace_token(line, frm, to):
    out, i, n = [], 0, len(line)
    flen, tlen = len(frm), len(to)
    while i < n:
        j = line.find(frm, i)
        if j < 0:
            out.append(line[i:]); break
        prev = line[j-1] if j > 0 else ""
        nxt = line[j+flen] if j+flen < n else ""
        if prev and (prev.isalnum() or prev == "_"):
            out.append(line[i:j+flen]); i = j+flen; continue
        if nxt and (nxt.isalnum() or nxt == "_"):
            out.append(line[i:j+flen]); i = j+flen; continue
        out.append(line[i:j]); out.append(to); i = j+flen
    return "".join(out)

def transform_line(line, is_vertex):
    line = replace_token(line, "sample", "hd_sample")
    line = replace_token(line, "varying", "out" if is_vertex else "in")
    if is_vertex:
        line = replace_token(line, "attribute", "in")
        line = replace_token(line, "gl_ModelViewProjectionMatrix", "u_hdMVP")
        line = replace_token(line, "gl_ModelViewMatrix", "u_hdMV")
        line = replace_token(line, "gl_TextureMatrix[0]", "u_hdTexMat0")
        line = replace_token(line, "gl_MultiTexCoord0", "hd_MultiTexCoord0")
        line = replace_token(line, "gl_Vertex", "hd_Position")
        line = replace_token(line, "gl_Normal", "hd_Normal")
        line = replace_token(line, "gl_Color", "hd_glColor")
        line = replace_token(line, "gl_TexCoord[0]", "hd_TexCoord0")
        line = replace_token(line, "gl_ClipVertex", "hd_ClipVertexUnused")
    else:
        line = replace_token(line, "gl_TexCoord[0]", "hd_TexCoord0")
    return line

# ---- the prefix, mirroring GL_ProcessShader's ES branch ----

def make_prefix(is_vertex, defines):
    p = []
    p.append("#version 300 es")
    p.append("#ifdef GL_ES")
    p.append("precision highp float;")
    p.append("precision highp int;")
    p.append("precision highp sampler2D;")
    p.append("precision highp samplerCube;")
    p.append("precision highp sampler2DShadow;")
    p.append("precision highp samplerCubeShadow;")
    p.append("#endif")
    p.append("#ifndef M_PI\n#define M_PI 3.14159265358979323846\n#endif")
    p.append("#ifndef M_PI2\n#define M_PI2 6.28318530717958647692\n#endif")
    p.append("#ifndef round\n#define round( x ) floor( (x) + 0.5 )\n#endif")
    p.append("#define texture2D texture")
    p.append("#define texture2DLod textureLod")
    p.append("#define texture2DProj textureProj")
    p.append("#define textureCube texture")
    p.append("#define textureCubeLod textureLod")
    if not is_vertex:
        p.append("layout(location = 0) out vec4 hd_FragColor;")
        p.append("#define gl_FragColor hd_FragColor")
        p.append("in vec4 hd_TexCoord0;")
        p.append("uniform mat4 u_hdMVP;")
        p.append("uniform mat4 u_hdMV;")
    else:
        p.append("layout(location = 0) in vec4 hd_Position;")
        p.append("layout(location = 1) in vec4 hd_glColor;")
        p.append("layout(location = 2) in vec4 hd_MultiTexCoord0;")
        p.append("layout(location = 3) in vec3 hd_Normal;")
        p.append("uniform mat4 u_hdMVP;")
        p.append("uniform mat4 u_hdMV;")
        p.append("uniform mat4 u_hdTexMat0;")
        p.append("out vec4 hd_TexCoord0;")
        p.append("vec4 hd_ClipVertexUnused;")
    # device state after the v0.5.2+ ES capability fixes: both ON
    p.append("#define GLSL_gpu_shader4")
    p.append("#define GLSL_ALLOW_TEXTURE_ARRAY")
    if defines:
        p.append(defines.rstrip("\n"))
    p.append("#ifndef MAXSTUDIOBONES\n#define MAXSTUDIOBONES %d\n#endif" % MAXSTUDIOBONES)
    p.append("#ifndef MAX_LIGHTSTYLES\n#define MAX_LIGHTSTYLES %d\n#endif" % MAX_LIGHTSTYLES)
    p.append("#ifndef MAXLIGHTMAPS\n#define MAXLIGHTMAPS %d\n#endif" % MAXLIGHTMAPS)
    p.append("#ifndef GRASS_ANIM_DIST\n#define GRASS_ANIM_DIST %f\n#endif" % GRASS_ANIM_DIST)
    return "\n".join(p) + "\n"

# ---- include expansion, mirroring GL_ParseFile ----

def parse_file(name, is_vertex, seen):
    path = os.path.join(GLSL, name)
    if not os.path.isfile(path):
        raise FileNotFoundError(path)
    out = ["#line 0"]
    for raw in open(path, encoding="utf-8", errors="replace").read().splitlines():
        m = re.match(r'\s*#\s*include\s+"([^"]+)"', raw)
        if m:
            inc = m.group(1)
            out.append(parse_file(inc, is_vertex, seen))
            out.append("#line 0")
        else:
            out.append(transform_line(raw, is_vertex))
    return "\n".join(out)

def build_shader(basename, is_vertex, defines):
    # GL_LoadGPUShader lowercases the composed filename before loading
    basename = basename.lower()
    suffix = "_vp" if is_vertex else "_fp"
    src = make_prefix(is_vertex, defines)
    src += parse_file(basename + suffix + ".glsl", is_vertex, set()) + "\n"
    return src

# ---- entry extraction from r_shaderlist.h ----

def load_shaderlist():
    text = open(os.path.join(ROOT, "game/client/render/r_shaderlist.h")).read()
    entries = []
    for m in re.finditer(r'\{\s*"([^"]+)"\s*,\s*((?:"(?:[^"\\]|\\.)*"\s*)*)\}', text):
        name, defs = m.group(1), m.group(2)
        dlines = re.findall(r'"((?:[^"\\]|\\.)*)"', defs)
        defines = "".join(d.replace("\\n", "\n").replace('\\"', '"') for d in dlines)
        entries.append((name, defines))
    return entries

# fixed pairs from GL_InitGPUShaders: (glname, vpname, fpname, defines)
FIXED = [
    ("GenSunShafts","generic","genshafts",""), ("DrawSunShafts","generic","drawshafts",""),
    ("GenSSAO","generic","genssao",""), ("GenHBAO","generic","genhbao",""),
    ("DrawSSAO","generic","drawssao",""), ("MotionBlur","generic","motionblur",""),
    ("HorizontalBlur","generic","horizontalblur",""), ("Monochrome","generic","monochrome",""),
    ("Enhance","generic","enhance",""), ("FSR_easu","generic","fsr_easu",""),
    ("FSR_rcas","generic","fsr_rcas",""), ("SMAAEdgeDetect","smaaedgedetect","smaaedgedetect",""),
    ("SMAABlendWeight","smaablendweight","smaablendweight",""),
    ("SMAANeighborBlend","smaaneighborblend","smaaneighborblend",""),
    ("ScreenWater","generic","screenwater",""), ("Glitch","generic","glitch",""),
    ("DroneScreen","generic","dronescreen",""), ("HeatDistortion","generic","heat",""),
    ("LensFlare","generic","lensflare",""), ("WaterDrops","generic","waterdrops",""),
    ("ToneMap","generic","tonemap",""), ("GenExposure","generic","generate_exposure",""),
    ("Bloom","generic","bloom",""), ("BlurMip","generic","blurmip",""),
    ("GenLuminance","generic","generate_luminance",""), ("GaussBlur","generic","gaussblur",""),
    ("BilateralBlur","generic","bilateralblur",""), ("SkyBoxGeneric","generic","skybox",""),
    ("StudioDepth0","studiodepth","generic","#define NO_FOG\n"),
    ("StudioDepth1","studiodepth","generic","#define STUDIO_BONEWEIGHTING\n#define NO_FOG\n"),
]

def check_extension_order(src):
    """Mali (and the GLSL ES spec) reject any #extension that follows a
    non-preprocessor token. The v0.5.3 device crash was exactly this. Assert
    the emitted source never does it."""
    seen_token = False
    for i, line in enumerate(src.splitlines(), 1):
        t = line.strip()
        if not t or t.startswith("//"):
            continue
        if t.startswith("#extension"):
            if seen_token:
                raise AssertionError(f"#extension at line {i} follows non-preprocessor tokens (Mali P0001)")
        elif not t.startswith("#"):
            seen_token = True

def run_validator(files, link):
    args = [VALIDATOR]
    if link:
        args.append("-l")
    args += files
    r = subprocess.run(args, capture_output=True, text=True)
    return r.returncode == 0, (r.stdout + r.stderr)

def main():
    entries = load_shaderlist()
    kept = entries  # native ES3 path: every permutation is live on device
    print(f"shaderlist entries: {len(entries)} total (ALL validated — native path)")

    targets = [("shaderlist", n, n, n, d) for (n, d) in kept]
    targets += [("fixed", g, v, f, d) for (g, v, f, d) in FIXED]

    os.makedirs("/tmp/es300val", exist_ok=True)
    fails = []
    for kind, glname, vp, fp, defines in targets:
        try:
            vsrc = build_shader(vp, True, defines)
            fsrc = build_shader(fp, False, defines)
            check_extension_order(vsrc)
            check_extension_order(fsrc)
        except FileNotFoundError as e:
            fails.append((glname, defines, f"missing file {e}"))
            continue
        vpath = f"/tmp/es300val/{glname}_{abs(hash(defines))}.vert"
        fpath = vpath.replace(".vert", ".frag")
        open(vpath, "w").write(vsrc)
        open(fpath, "w").write(fsrc)
        ok, log = run_validator([vpath, fpath], link=True)
        if not ok:
            fails.append((glname, defines, log.strip()[:2000]))

    print(f"validated: {len(targets)} vp/fp pairs (compile + link, GLSL ES 300)")
    if fails:
        print(f"\nFAILURES: {len(fails)}")
        for name, defs, log in fails[:12]:
            print("=" * 60)
            print("SHADER:", name, "|", defs.replace("\n", " ")[:120])
            print(log)
        sys.exit(1)
    print("ALL CLEAN")

if __name__ == "__main__":
    main()
