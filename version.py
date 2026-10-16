# PlatformIO pre-script: firmware version and build environment as defines.
#   FW_VERSION  "1.2.0" for a tagged build (tag v1.2.0), otherwise e.g. "1.2.0-3-gd15a417-dirty"
#               (commits after the last tag) or "0.0.0-gd15a417" (no tag yet).
#   FW_ENV      PlatformIO environment, the key of this build in the release manifest.
import os
import subprocess

Import("env")


def git(*args):
    try:
        return subprocess.check_output(["git"] + list(args), cwd=env["PROJECT_DIR"],
                                       stderr=subprocess.DEVNULL).decode().strip()
    except Exception:
        return ""


if os.environ.get("GITHUB_REF_TYPE") == "tag":
    version = os.environ["GITHUB_REF_NAME"]
else:
    version = git("describe", "--tags", "--dirty", "--match", "v[0-9]*")
    if not version:
        version = "0.0.0-g" + (git("rev-parse", "--short", "HEAD") or "unknown")
        if git("status", "--porcelain", "--untracked-files=no"):
            version += "-dirty"
if version.startswith("v"):
    version = version[1:]

print("Firmware version: %s (%s)" % (version, env["PIOENV"]))
env.Append(CPPDEFINES=[
    ("FW_VERSION", env.StringifyMacro(version)),
    ("FW_ENV", env.StringifyMacro(env["PIOENV"])),
])
