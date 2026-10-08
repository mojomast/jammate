#!/usr/bin/env python3
"""Factually record the local capture/playback environment. Read-only.

Never installs anything and never touches a device's state. Reports which
capture/playback binaries and kernel device nodes exist so a task note can say
plainly whether physical evidence could be produced here.
"""
import argparse
import json
import os
import platform
import shutil
import subprocess
import sys

PROGRAMS = ("arecord", "aplay", "pw-record", "pw-cli", "pactl", "sox",
            "ffmpeg", "ffplay", "jackd")


def which_all():
    return {name: shutil.which(name) for name in PROGRAMS}


def read_text(path):
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as fh:
            return fh.read(4096)
    except OSError:
        return None


def probe():
    info = {
        "os": platform.system().lower(),
        "platform": platform.platform(),
        "python": sys.version.split()[0],
        "dev_snd_present": os.path.isdir("/dev/snd"),
        "proc_asound_cards": read_text("/proc/asound/cards"),
        "pipewire_socket": os.path.exists(
            os.path.join(os.environ.get("XDG_RUNTIME_DIR", ""), "pipewire-0")),
        "programs": which_all(),
    }
    if info["os"] == "windows":
        info["note"] = ("ASIO availability is reported by the running Guitar "
                        "Companion Standalone build via -DASIOSDK_DIR; no "
                        "read-only registry probe is attempted.")
    return info


def main(argv=None):
    p = argparse.ArgumentParser(description="Read-only device environment probe.")
    p.add_argument("--out", default=None, help="write JSON here as well")
    args = p.parse_args(argv)
    info = probe()
    text = json.dumps(info, indent=2, sort_keys=True)
    print(text)
    if args.out:
        with open(args.out, "w", encoding="utf-8") as fh:
            fh.write(text + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
