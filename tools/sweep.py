#!/usr/bin/env python3
"""
sweep.py -- the only thing here that catches a dead control.

Every other test in this repo checks that the picture is *right*. This one
checks that a control is *connected*: it renders the same frame at several
values of one parameter and requires the results to differ. A uniform whose
name does not match the shader, a Settings field nobody reads, a slider wired
to the wrong id -- all of those compile, link, load and render, and all of them
are invisible to everything except this.

    python3 tools/sweep.py [--build DIR] [--verbose]

## Why there is a context table

Idler has eleven savers sharing seven generic scene controls, so **most
controls are supposed to do nothing most of the time**. `Fog` does nothing in
Mystify, which has no depth. `Line Width` does nothing in FlowerBox, which has
no lines. `Text` does nothing in nine of the eleven.

A naive sweep on the default saver would therefore report about half the
parameter list as broken. So each control below names the savers it is
genuinely live in, and is swept there. A control with no context named is
global and swept on the default saver.

That table is the maintenance burden of the generic-control design, and it is
worth being honest that it is one: add a saver and it wants revisiting.

## Why the sample values are not 0, 0.5, 1

Several things here are periodic in time or in a count, and three round numbers
land on the symmetries. Sampling `Phase` at 0, 0.5 and 1 on a saver whose
motion repeats can return the *same frame* three times from a perfectly working
slider. The values below are deliberately awkward.
"""

import argparse
import hashlib
import os
import subprocess
import sys
import tempfile

# Saver indices, matching SaverKind in Controls.h.
MYSTIFY, BEZIERS, CURVES, WINDOWS, STARS, MARQUEE, MAZE, PIPES, OBJECTS, FLOWER, TEXT3D = range(11)

SAVER_NAMES = [
    "Mystify", "Beziers", "Curves and Colors", "Flying Windows",
    "Flying Through Space", "Scrolling Marquee", "3D Maze", "3D Pipes",
    "3D Flying Objects", "3D FlowerBox", "3D Text",
]

# Deliberately not 0, 0.5, 1 -- see the note above.
DEFAULT_VALUES = [0.0, 0.137, 0.611, 1.0]

# (name, savers it is live in, values to try, [extra settings], [effect build],
#  [drive the real clock])
#
# An empty saver list means "global": swept on whatever the default saver is.
CONTROLS = [
    # The generic seven. Each names the savers where it genuinely does
    # something, and the comment says what.
    ("Density",     [MYSTIFY, STARS, PIPES, OBJECTS], DEFAULT_VALUES),   # how many things
    ("Complexity",  [MYSTIFY, CURVES, MAZE, PIPES], DEFAULT_VALUES),     # corners / grid / turn rate
    ("Size",        [MYSTIFY, STARS, PIPES, FLOWER], DEFAULT_VALUES),    # how big
    ("Length",      [MYSTIFY, STARS, PIPES, TEXT3D], DEFAULT_VALUES),    # trail / streak / extrusion
    ("Line Width",  [MYSTIFY, CURVES, MARQUEE], DEFAULT_VALUES),         # stroke weight
    ("Variation",   [MYSTIFY, STARS, OBJECTS, MAZE], DEFAULT_VALUES),    # spread of the set
    ("Shading",     [PIPES, FLOWER, OBJECTS], [0.0, 1.0, 2.0]),          # option: flat/lit/wireframe

    # Motion. Speed drives the CLOCK, and the harness normally pins the clock --
    # so it is swept against the real host clock instead. Without that it reads
    # as dead, correctly: with a pinned time there is nothing for it to scale.
    ("Speed",       [], DEFAULT_VALUES, {}, False, True),
    ("Phase",       [], DEFAULT_VALUES),
    ("Seed",        [MYSTIFY, STARS, PIPES, MAZE], DEFAULT_VALUES),

    # Camera. Dead in the five 2D savers by construction: they are drawn
    # through a fixed orthographic camera that none of these touch.
    ("Field of View",   [MAZE, PIPES, FLOWER, OBJECTS, TEXT3D], DEFAULT_VALUES),
    ("Camera Distance", [PIPES, FLOWER, OBJECTS, TEXT3D], DEFAULT_VALUES),
    ("Camera Tilt",     [PIPES, FLOWER, MAZE], DEFAULT_VALUES),
    ("Fog",             [MAZE, PIPES, OBJECTS], DEFAULT_VALUES),

    # Colour.
    ("Colour Mode",        [], [0.0, 1.0, 2.0, 3.0]),
    # The three colour components do nothing in Classic mode, which is the
    # default -- Classic means "each saver's own palette", and a tint it
    # ignores is the whole point of it. Swept in Tint mode, where they bite.
    ("Colour",             [], DEFAULT_VALUES, {"Colour Mode": 1.0}),
    ("Colour_Green",       [], DEFAULT_VALUES, {"Colour Mode": 1.0}),
    ("Colour_Blue",        [], DEFAULT_VALUES, {"Colour Mode": 1.0}),
    ("Opacity",            [], DEFAULT_VALUES),
    ("Background",         [], DEFAULT_VALUES),
    ("Background_Green",   [], DEFAULT_VALUES),
    ("Background_Blue",    [], DEFAULT_VALUES),
    ("Background Alpha", [], DEFAULT_VALUES),

    # Hue Spread only bites in Spread mode, and Hue Cycle only in Spread or
    # Cycle. Both are sweeps that need a second parameter set first, which is
    # what `setup` is for.
    ("Hue Spread", [], DEFAULT_VALUES, {"Colour Mode": 2.0, "Colour_Green": 0.2, "Colour_Blue": 0.2}),
    ("Hue Cycle",  [], DEFAULT_VALUES, {"Colour Mode": 3.0, "Colour_Green": 0.2, "Colour_Blue": 0.2}),

    # Output. Mask Mode and Mix only mean anything with a clip, so they are
    # swept against the effect build.
    ("Mask Mode", [], [0.0, 1.0, 2.0, 3.0], {}, True),
    ("Mix",       [], DEFAULT_VALUES, {}, True),

    # Audio. idtest writes a synthetic spectrum on every render, so these are
    # measurable offline; without it they would both look dead.
    ("Audio Size",  [MYSTIFY, STARS, FLOWER], DEFAULT_VALUES),
    # Audio Speed drives the clock, like Speed, so it needs the real one.
    ("Audio Speed", [], DEFAULT_VALUES, {}, False, True),
]


def digest(path):
    with open(path, "rb") as handle:
        return hashlib.sha256(handle.read()).hexdigest()[:16]


def render(binary, out, saver, preset, effect, settings, time, host_clock=False):
    clock = "--hosttime" if host_clock else "--time"
    command = [binary, "--out", out, clock, str(time), "--size", "320x180"]
    if saver is not None:
        command += ["--saver", str(saver)]
    if preset is not None:
        command += ["--preset", str(preset)]
    if effect:
        command += ["--effect"]
    for name, value in settings.items():
        command += ["--set", f"{name}={value}"]

    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(f"idtest failed: {result.stderr.strip() or result.stdout.strip()}")
    return digest(out)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", default="build")
    parser.add_argument("--verbose", action="store_true")
    args = parser.parse_args()

    binary = os.path.join(args.build, "idtest")
    if not os.path.exists(binary):
        print(f"no idtest at {binary} -- build first", file=sys.stderr)
        return 2

    failures = []
    checked = 0

    with tempfile.TemporaryDirectory() as work:
        for entry in CONTROLS:
            name = entry[0]
            savers = entry[1]
            values = entry[2]
            setup = entry[3] if len(entry) > 3 else {}
            effect = entry[4] if len(entry) > 4 else False
            host_clock = entry[5] if len(entry) > 5 else False

            contexts = savers if savers else [None]

            for saver in contexts:
                # Each saver is swept on its own preset, so the scene controls
                # are somewhere that saver actually uses them. Sweeping 3D
                # Pipes' Density on Mystify's defaults would prove nothing.
                preset = (saver + 1) if saver is not None else None

                digests = []
                for value in values:
                    settings = dict(setup)
                    settings[name] = value
                    out = os.path.join(work, "frame.png")
                    # A time that is not zero and not a round number, so a
                    # growing saver has grown and a periodic one is not sitting
                    # on a symmetry.
                    digests.append(
                        render(binary, out, saver, preset, effect, settings, 17.3, host_clock))

                distinct = len(set(digests))
                checked += 1

                where = SAVER_NAMES[saver] if saver is not None else "default"
                if distinct < 2:
                    failures.append((name, where))
                    print(f"DEAD  {name:<20} in {where:<22} {distinct} distinct frame(s)")
                elif args.verbose:
                    print(f"ok    {name:<20} in {where:<22} {distinct} distinct frames")

    print()
    print(f"{checked} control/context pairs swept, {len(failures)} dead")
    if failures:
        for name, where in failures:
            print(f"  {name} does nothing in {where}")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
