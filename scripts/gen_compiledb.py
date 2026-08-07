#!/usr/bin/env python3
"""Synthesize compile_commands.json for clangd from `pio run -t idedata`.

The project's custom_sdkconfig core rebuild emits its own CMake
compile_commands.json that displaces PlatformIO's application database, leaving
no entry for any file under src/ or lib/. This rebuilds the app database from
idedata, which carries the real include paths, defines, and toolchain.

Usage:
    pio run -t idedata -e default > idedata.raw
    python3 gen_compiledb.py idedata.raw [project_root] [-o compile_commands.json]
"""
import json
import os
import sys

SOURCE_DIRS = ("src", "lib", "freeink-sdk")
CXX_EXT = {".cpp", ".cc", ".cxx"}
C_EXT = {".c"}
SKIP_DIRS = {".pio", ".git", ".cache", "build", ".dummy", "fontsrc", "test"}

# GCC-only flags clangd's driver rejects outright. Dropping them changes no
# semantics for indexing; leaving them in produces a driver error per file.
DROP_FLAGS = {"-fno-tree-switch-conversion", "-fstrict-volatile-bitfields"}

# newlib's sys/reent.h leaves these undefined under clangd's driver (GCC supplies
# them internally). Without them every TU reports an unknown-type error in a
# system header. Values match newlib's own defaults for a 32-bit target.
CLANGD_COMPAT_DEFINES = [
    "-D_READ_WRITE_RETURN_TYPE=_ssize_t",
    "-D_READ_WRITE_BUFSIZE_TYPE=int",
]


def project_includes(root):
    """idedata's includes.build omits the project's own src/ and lib/ dirs --
    it lists only the symlinked freeink-sdk libs. Reconstruct what the LDF adds."""
    found = [os.path.join(root, "src")]
    lib_root = os.path.join(root, "lib")
    if os.path.isdir(lib_root):
        found.append(lib_root)
        for name in sorted(os.listdir(lib_root)):
            lib_dir = os.path.join(lib_root, name)
            if not os.path.isdir(lib_dir):
                continue
            found.append(lib_dir)
            for sub in ("src", "include"):
                nested = os.path.join(lib_dir, sub)
                if os.path.isdir(nested):
                    found.append(nested)
    return [p for p in found if os.path.isdir(p)]


def extract_json(path):
    """idedata prints a build log before the JSON blob; take the blob."""
    raw = open(path).read()
    start = raw.find('{"')
    if start < 0:
        start = raw.find("{")
    if start < 0:
        sys.exit(f"no JSON object found in {path}")
    return json.JSONDecoder().raw_decode(raw[start:])[0]


def translation_units(root, build_includes):
    """Project sources PlatformIO actually compiles.

    freeink-sdk/ holds more libs than lib_deps pulls in (FreeInkBook, for one).
    Those are never built, so idedata knows nothing about their include paths --
    emitting entries for them yields guaranteed-broken commands. Keep only the
    freeink-sdk subtrees that appear in the build's include list.
    """
    sdk_root = os.path.join(root, "freeink-sdk")
    sdk_built = {p for p in build_includes if p.startswith(sdk_root)}

    for src_dir in SOURCE_DIRS:
        base = os.path.join(root, src_dir)
        if not os.path.isdir(base):
            continue
        for dirpath, dirnames, filenames in os.walk(base):
            dirnames[:] = [d for d in dirnames if d not in SKIP_DIRS]
            if dirpath.startswith(sdk_root) and not any(
                dirpath.startswith(p) or p.startswith(dirpath) for p in sdk_built
            ):
                continue
            for name in filenames:
                ext = os.path.splitext(name)[1]
                if ext in CXX_EXT or ext in C_EXT:
                    yield os.path.join(dirpath, name), ext in CXX_EXT


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    data = extract_json(sys.argv[1])
    root = os.path.abspath(sys.argv[2]) if len(sys.argv) > 2 else os.getcwd()
    out = os.path.join(root, "compile_commands.json")

    includes = project_includes(root)
    for group in ("build", "compatlib", "toolchain"):
        includes += data["includes"].get(group, [])
    # The toolchain ships both picolibc and newlib headers. The real build links
    # newlib; leaving picolibc on the search path makes clangd resolve stdio.h to
    # picolibc's, whose `struct __file` then collides with the host SDK's
    # `struct __sFILE` -- one typedef-redefinition error in every TU.
    includes = [p for p in includes if "picolibc" not in p]

    # dedupe, keep order
    seen = set()
    inc_flags = []
    for path in includes:
        if path not in seen:
            seen.add(path)
            inc_flags.append(f"-I{path}")
    define_flags = [f"-D{d}" for d in data["defines"]]

    entries = []
    for path, is_cxx in translation_units(root, data["includes"].get("build", [])):
        compiler = data["cxx_path"] if is_cxx else data["cc_path"]
        raw_flags = data["cxx_flags"] if is_cxx else data["cc_flags"]
        flags = [f for f in raw_flags if f not in DROP_FLAGS]
        if not is_cxx:
            # PlatformIO folds build_flags into both C and C++ flag sets, so
            # cc_flags carries -std=gnu++2a. GCC ignores it for C; clang errors.
            flags = [f for f in flags if not f.startswith(("-std=c++", "-std=gnu++"))]
        entries.append({
            "directory": root,
            "file": path,
            "arguments": [compiler] + list(flags) + CLANGD_COMPAT_DEFINES
                         + define_flags + inc_flags + ["-c", path],
        })

    entries.sort(key=lambda e: e["file"])
    with open(out, "w") as fh:
        json.dump(entries, fh, indent=1)
    print(f"wrote {out}: {len(entries)} entries "
          f"({len(inc_flags)} includes, {len(define_flags)} defines)")


if __name__ == "__main__":
    main()
