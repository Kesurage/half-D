#!/usr/bin/env python3
"""v0.5.3 audit: model the client's GL_CheckExtension resolution chain
EXACTLY as the code runs it, for every name in every dllfunc_t table:

  1. GL_GetProcAddress(name)            -> native ES3 core | gl2_shim export | engine extra
  2. special-case retries               -> glClearDepthf / glDepthRangef / Stubs
  3. esStubs exact match                -> client stub wrappers
  4. ARB strip (name minus "ARB")       -> resolve again
  5. EXT strip (name minus "EXT")       -> resolve again

Any name with no path disables its whole extension on device (the v0.5.2
field failure: one name, glDetachObjectARB, killed GL_ARB_shader_objects).
Output: the definitive list of names that still need a stub/rename.
"""
import re, sys

CLIENT = "game/client/render/r_opengl.cpp"
SHIM = "engine/ref/gl/gl2_shim/gl2_shim.c"

src = open(CLIENT).read()

# --- every name in every dllfunc_t table ---
names = []
for m in re.finditer(r'static const dllfunc_t (\w+)\[\]\s*=\s*\{(.*?)\n\};', src, re.S):
    table = m.group(1)
    for e in re.finditer(r'\{\s*"((?:gl|wgl|glX)[A-Za-z0-9]+)"\s*,', m.group(2)):
        names.append((table, e.group(1)))

# --- esStubs table ---
es = re.search(r'esStubs\[\]\s*=\s*\{(.*?)\n\t\t\t\};', src, re.S)
esstubs = set(re.findall(r'\{\s*"(gl[A-Za-z0-9]+)"', es.group(1))) if es else set()

# --- special-case retry map ---
special = {
    "glClearDepth": "glClearDepthf", "glDepthRange": "glDepthRangef",
    "glDrawRangeElements": "glDrawRangeElementsEXT", "glDrawBuffers": "glDrawBuffersEXT",
    "glDrawBuffer": "@Stub_DrawBuffer", "glGetDoublev": "@Stub_GetDoublev",
    "glAlphaFunc": "@Stub_AlphaFunc", "glClipPlane": "@Stub_ClipPlane",
}

# --- shim exports ---
shim_src = open(SHIM).read()
shim_exports = set(re.findall(r'Q_strcmp\(\s*name,\s*"(gl[A-Za-z0-9]+)"', shim_src))
shim_exports |= set(re.findall(r'Q_stricmp\(\s*name,\s*"(gl[A-Za-z0-9]+)"', shim_src))

# --- native ES3 core (from the v0.5.0 curated list) ---
es3 = set(open("tools/es3_core.txt").read().split()) if False else None
# inline: reuse the list from the original audit by extracting it
old = open("tools/audit_es_surface.py").read()
es3 = set(re.search(r'es3 = set\("""(.*?)"""\.split\(\)\)', old, re.S).group(1).split())

def resolve(n, depth=0):
    """return True if n resolves at this level (native/shim)"""
    return n in es3 or n in shim_exports

problems, ok = [], 0
seen = set()
for table, n in names:
    if n in seen: continue
    seen.add(n)
    if resolve(n):
        ok += 1; continue
    # special-case retries
    s = special.get(n)
    if s and (s.startswith("@") or resolve(s)):
        ok += 1; continue
    # esStubs
    if n in esstubs:
        ok += 1; continue
    # ARB / EXT strip
    for suf in ("ARB", "EXT"):
        if n.endswith(suf):
            if resolve(n[:-3]):
                ok += 1; n = None; break
    if n is None: continue
    problems.append((table, n))

print(f"unique names in client tables : {len(seen)}")
print(f"esStubs entries               : {len(esstubs)}")
print(f"shim exports                  : {len(shim_exports)}")
print(f"resolve OK                    : {ok}")
print(f"\nUNRESOLVED ({len(problems)}) — these disable their whole extension on ES3:")
cur = None
for table, n in problems:
    if table != cur:
        print(f"  [{table}]")
        cur = table
    print(f"    {n}")
