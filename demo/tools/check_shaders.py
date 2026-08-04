#!/usr/bin/env python3
"""Prove the demo's GLSL is the plugin's GLSL.

The rule the demo pages are built on is that the shader text is *copied*, not
rewritten — `port()` in the kit handles the version line and the precision
qualifiers and nothing else. Nothing enforces that: `plugin.js` cannot include a
C++ file, so the two copies are two files that happen to agree, and a change to
`source/Shaders.cpp` that is not mirrored here is invisible until somebody
notices the demo behaving differently from the plugin.

This reads both and compares them character for character.

    python3 demo/tools/check_shaders.py

Exit status is 0 when every pass matches, 1 otherwise, so it can go in
`tools/verify.sh`. The only edits it allows for are the two JavaScript template
literal escapes — a backslash before a backtick or a `${` — which are required by
the file format and change no GLSL.

## Why this one differs from the rest of the fleet's

Tinsel, Orrery and Coinop declare their shaders as file-scope constants:

    const char* const kName = R"(...)";

**Idler does not.** Its shaders are *functions* holding a static local:

    const char* SceneVertexShader()
    {
        static const char* source = R"(...)";
        return source;
    }

so the extraction is keyed on the enclosing function name rather than on a
variable name.

`CompositeFragmentShader( bool hasInput )` is a further special case: it does not
hold one literal at all. It **builds** its string by concatenating a version
line, an optional `#define`, and one `R"(...)"` body. Only that body is a literal,
so only the body is compared — against `COMPOSITE_FRAGMENT_BODY` in `plugin.js`,
which the JavaScript assembles the same way in `compositeFragmentShader()`. The
assembly itself is short, sits beside the body in both files, and is checked
below by comparing the two lines that make it up.

## What this does NOT cover

Idler's shaders are 242 lines and almost nothing lives in them — the substance is
a 3D engine and eleven savers on the CPU, hand-ported into `plugin.js`. **Nothing
in this file says anything about that port.** `demo/tools/check_geometry.mjs` is
what covers it: it builds every saver's mesh under Node and compares the triangle
count, the vertex count and the bounding box against `idtest --geometry`.
"""

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
SHADERS_CPP = ROOT / "source" / "Shaders.cpp"
PLUGIN_JS = ROOT / "demo" / "plugin.js"

# C++ function name -> JavaScript constant name.
PASSES = {
    "SceneVertexShader": "SCENE_VERTEX",
    "SceneFragmentShader": "SCENE_FRAGMENT",
    "CompositeVertexShader": "COMPOSITE_VERTEX",
    # The literal body only; see the note above on the concatenation.
    "CompositeFragmentShader": "COMPOSITE_FRAGMENT_BODY",
}


def cpp_literals(text):
    """Every `static const char* source = R"(...)";` keyed by enclosing function.

    The function's name is the nearest `Something(` that precedes the literal at
    the start of a line, which is what the file's layout guarantees.
    """
    pattern = re.compile(
        r"^const char\* (\w+)\(.*?static const char\* source = R\"\((.*?)\)\";",
        re.DOTALL | re.MULTILINE,
    )
    return {m.group(1): m.group(2) for m in pattern.finditer(text)}


def cpp_composite_body(text):
    """The one `R"(...)"` that CompositeFragmentShader concatenates onto."""
    pattern = re.compile(
        r"const char\* CompositeFragmentShader\(.*?source \+= R\"\((.*?)\)\";",
        re.DOTALL,
    )
    match = pattern.search(text)
    return match.group(1) if match else None


def js_literals(text):
    """Every top-level ``const NAME = `...`;`` in the file."""
    pattern = re.compile(r"^const (\w+) = `(.*?)`;$", re.DOTALL | re.MULTILINE)
    return {m.group(1): m.group(2) for m in pattern.finditer(text)}


def unescape(source):
    """Undo the two escapes a template literal forces, and nothing else."""
    return source.replace("\\`", "`").replace("\\${", "${")


def check_assembly(cpp_text, js_text):
    """The composite shader's assembly, which is code rather than a literal.

    Both sides start from the version line and add the same `#define` when there
    is an input. This does not diff them character for character — they are
    different languages — but it does insist that both spellings are present on
    both sides, which is what catches the define being renamed or dropped.
    """
    problems = []

    for needle, where in (
        ('"#version 410 core\\n"', "Shaders.cpp"),
        ('"#define HAS_INPUT 1\\n"', "Shaders.cpp"),
    ):
        if needle not in cpp_text:
            problems.append(f"{where} no longer contains {needle}")

    for needle, where in (
        ("'#version 410 core\\n'", "plugin.js"),
        ("'#define HAS_INPUT 1\\n'", "plugin.js"),
    ):
        if needle not in js_text:
            problems.append(f"{where} no longer contains {needle}")

    return problems


def main():
    cpp_text = SHADERS_CPP.read_text(encoding="utf-8")
    js_text = PLUGIN_JS.read_text(encoding="utf-8")

    cpp = cpp_literals(cpp_text)
    composite = cpp_composite_body(cpp_text)
    if composite is not None:
        cpp["CompositeFragmentShader"] = composite

    js = js_literals(js_text)

    failures = 0

    for cpp_name, js_name in PASSES.items():
        if cpp_name not in cpp:
            print(f"MISSING  {cpp_name} not found in source/Shaders.cpp")
            failures += 1
            continue
        if js_name not in js:
            print(f"MISSING  {js_name} not found in demo/plugin.js")
            failures += 1
            continue

        want = cpp[cpp_name]
        got = unescape(js[js_name])

        if want == got:
            print(f"ok       {cpp_name} == {js_name}  ({len(want)} chars)")
            continue

        failures += 1
        print(f"DRIFTED  {cpp_name} != {js_name}")

        want_lines = want.splitlines()
        got_lines = got.splitlines()
        for i in range(max(len(want_lines), len(got_lines))):
            a = want_lines[i] if i < len(want_lines) else "<end of file>"
            b = got_lines[i] if i < len(got_lines) else "<end of file>"
            if a != b:
                print(f"           line {i + 1}")
                print(f"           Shaders.cpp: {a!r}")
                print(f"           plugin.js:   {b!r}")
                break

    for problem in check_assembly(cpp_text, js_text):
        print(f"ASSEMBLY {problem}")
        failures += 1

    if failures:
        print(f"\n{failures} problem(s). The demo is no longer running the plugin's shader.")
        return 1

    print(f"\nall {len(PASSES)} pass(es) identical")
    print("NOTE: this covers the shaders only. The ported 3D engine and the eleven")
    print("      savers are checked by demo/tools/check_geometry.mjs.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
