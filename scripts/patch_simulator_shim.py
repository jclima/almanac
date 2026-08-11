"""
PlatformIO pre-build script: install Almanac's SecureHttpClient override into
the crosspoint-simulator libdep.

The simulator library (crosspoint-reader/crosspoint-simulator) tracks upstream
CrossPoint and has drifted behind Almanac's freeink-sdk: its SecureHttpClient
shim lacks setUserAgent(), responseComplete() and resolveUrl(), all of which
src/network/TesseraeClient.cpp calls. Without this, [env:simulator] does not
compile.

An -I path cannot fix it: PlatformIO appends build_flags include dirs AFTER
library include dirs, so .pio/libdeps/simulator/simulator/src always wins the
lookup for <SecureHttpClient.h>. So the header is replaced in place instead,
in the same spirit as patch_wolfssl.py / patch_jpegdec.py.

The replacement lives in scripts/simulator_shims/SecureHttpClient.h. This copies
it over the libdep's copy whenever the two differ, so it is idempotent and
re-applies automatically if the library is re-downloaded.
"""

Import("env")  # noqa: F821 (SCons-injected global)
import os
import shutil
import sys


SHIM_DIR = os.path.join(env["PROJECT_DIR"], "scripts", "simulator_shims")  # noqa: F821

# Relative to the libdep root, the files this script owns.
SHIM_FILES = ("SecureHttpClient.h",)


def patch_simulator_shim(env):
    lib_src = os.path.join(
        env["PROJECT_DIR"], ".pio", "libdeps", env["PIOENV"], "simulator", "src"
    )
    # The libdep is fetched during dependency resolution; on a clean tree this
    # script may run before it exists. Nothing to do then -- the build that
    # follows the fetch re-runs this script.
    if not os.path.isdir(lib_src):
        return

    for name in SHIM_FILES:
        source = os.path.join(SHIM_DIR, name)
        target = os.path.join(lib_src, name)
        if not os.path.isfile(source):
            sys.stderr.write(
                "ERROR: simulator shim override missing: %s\n" % source
            )
            raise SystemExit(1)
        # Guard against a silent no-op if the library is restructured upstream:
        # overriding a file that no longer exists would not fix anything.
        if not os.path.isfile(target):
            sys.stderr.write(
                "ERROR: expected simulator shim not found at %s -- the "
                "crosspoint-simulator layout has changed; update %s\n"
                % (target, os.path.basename(__file__))
            )
            raise SystemExit(1)
        if _same_contents(source, target):
            continue
        shutil.copyfile(source, target)
        print("Installed simulator shim override: %s" % name)


def _same_contents(a, b):
    with open(a, "rb") as fa, open(b, "rb") as fb:
        return fa.read() == fb.read()


patch_simulator_shim(env)  # noqa: F821
