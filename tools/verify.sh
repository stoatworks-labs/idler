#!/usr/bin/env bash
#
# verify.sh -- everything that can be checked without a host, in one go.
#
#   tools/verify.sh [--fast]
#
# --fast skips the universal build and checks whatever is already in build/.
# Use it while working; run the whole thing before a tag.
#
# ## Why the release checks are in here
#
# The bundle checks below -- the architecture list, the exported symbol, the
# Info.plist's CFBundleExecutable -- are things the RELEASE workflow does. They
# are duplicated here on purpose.
#
# A check that only ever runs in CI, after a tag, is a check that will catch you
# after the tag. The fleet has already lost a v0.1.0 to a plist that named the
# previous plugin's executable: the build passed, `lipo` passed, `nm` passed,
# the plugin loaded and rendered, and `codesign` failed on a machine nobody was
# sitting at. It takes a second to check here and a force-moved tag to check
# there.

set -euo pipefail

cd "$( dirname "${BASH_SOURCE[0]}" )/.."

FAST=0
[[ "${1:-}" == "--fast" ]] && FAST=1

BUILD=build-verify
PASS=0
FAIL=0

step()  { printf '\n\033[1m== %s\033[0m\n' "$1"; }
ok()    { printf '   \033[32mok\033[0m    %s\n' "$1"; PASS=$(( PASS + 1 )); }
bad()   { printf '   \033[31mFAIL\033[0m  %s\n' "$1"; FAIL=$(( FAIL + 1 )); }

#-----------------------------------------------------------------------------
step "build"
#-----------------------------------------------------------------------------
if [[ $FAST == 1 ]]; then
	BUILD=build
	echo "   --fast: using $BUILD as it stands"
else
	# No -DCMAKE_OSX_ARCHITECTURES, so the CMakeLists' universal default
	# applies. That default has to be exercised by something, and this is it.
	cmake -B "$BUILD" -DCMAKE_BUILD_TYPE=Release > /dev/null
	cmake --build "$BUILD" -j"$( sysctl -n hw.ncpu )" > /dev/null
	echo "   built $BUILD"
fi

#-----------------------------------------------------------------------------
step "bundles"
#-----------------------------------------------------------------------------
for NAME in "Idler" "Idler Mask"; do
	BINARY="$BUILD/$NAME.bundle/Contents/MacOS/$NAME"

	if [[ ! -f "$BINARY" ]]; then
		bad "$NAME: no binary at $BINARY"
		continue
	fi

	# The build log will happily call an arm64-only build a success. lipo is
	# the only thing that actually knows.
	ARCHS=$( lipo -archs "$BINARY" )
	if [[ $FAST == 1 ]]; then
		ok "$NAME: $ARCHS (not checked -- --fast)"
	elif [[ "$ARCHS" == *arm64* && "$ARCHS" == *x86_64* ]]; then
		ok "$NAME: universal ($ARCHS)"
	else
		bad "$NAME: not universal ($ARCHS)"
	fi

	# A bundle can load, export plugMain, and report that it contains no
	# plugins -- that is what happens if the registration's translation unit
	# gets dropped by the linker. Checking the symbol is necessary, not
	# sufficient; --coverage below is what proves a plugin is really in there.
	# Captured, then matched with `case` -- never `nm | grep -q`. This script
	# runs under `set -o pipefail`, and a `grep -q` that finds its match exits
	# immediately, so `nm` upstream takes SIGPIPE and the PIPELINE reports
	# failure even though the symbol is there. It is output-size dependent, so
	# it fails intermittently and on the bigger binary first: it gave a false
	# FAIL on the OFX bundle below while this line got away with it.
	SYMBOLS=$( nm -gU "$BINARY" 2>/dev/null || true )
	case "$SYMBOLS" in
		*plugMain*) true ;;
		*) false ;;
	esac
	if [ $? -eq 0 ]; then
		ok "$NAME: exports plugMain"
	else
		bad "$NAME: no plugMain"
	fi

	# The plist has to name the binary that is actually there. This is the one
	# that cost the fleet a tag: nothing before codesign reads it.
	PLIST="$BUILD/$NAME.bundle/Contents/Info.plist"
	EXECUTABLE=$( /usr/libexec/PlistBuddy -c "Print :CFBundleExecutable" "$PLIST" 2>/dev/null || echo "" )
	if [[ "$EXECUTABLE" == "$NAME" ]]; then
		ok "$NAME: CFBundleExecutable matches the binary"
	else
		bad "$NAME: CFBundleExecutable is '$EXECUTABLE', binary is '$NAME'"
	fi
done

# Ad-hoc sign a COPY, which is the exact command the release job runs. Signing
# the real one would leave a signed bundle behind and make the next run's
# failure mode different from a clean one.
step "codesign (ad-hoc, on a copy)"
TEMP=$( mktemp -d )
trap 'rm -rf "$TEMP"' EXIT
for NAME in "Idler" "Idler Mask"; do
	if [[ -d "$BUILD/$NAME.bundle" ]]; then
		cp -R "$BUILD/$NAME.bundle" "$TEMP/"
		if codesign --force --sign - "$TEMP/$NAME.bundle" 2> "$TEMP/err.txt"; then
			ok "$NAME signs"
		else
			bad "$NAME will not sign: $( cat "$TEMP/err.txt" )"
		fi
	fi
done

#-----------------------------------------------------------------------------
step "speed -- a Speed change does not move the picture"
#-----------------------------------------------------------------------------
# Needs no GPU, so it sits ahead of everything that does: a machine that cannot
# make a GL context can still run it.
if "$BUILD/idtest" --speed; then ok "speed"; else bad "speed"; fi

#-----------------------------------------------------------------------------
step "geometry -- the mesh each saver builds"
#-----------------------------------------------------------------------------
if "$BUILD/idtest" --geometry; then ok "geometry"; else bad "geometry"; fi

#-----------------------------------------------------------------------------
step "coverage -- every saver draws something, at several times"
#-----------------------------------------------------------------------------
if "$BUILD/idtest" --coverage > /dev/null; then ok "coverage"; else bad "coverage"; fi

#-----------------------------------------------------------------------------
step "replay -- the cache does not change the answer"
#-----------------------------------------------------------------------------
if "$BUILD/idtest" --replay; then ok "replay"; else bad "replay"; fi

#-----------------------------------------------------------------------------
step "walk -- the maze roams rather than pacing a few cells"
#-----------------------------------------------------------------------------
if "$BUILD/idtest" --walk --size 160x90; then ok "walk"; else bad "walk"; fi

#-----------------------------------------------------------------------------
step "sweep -- no dead controls"
#-----------------------------------------------------------------------------
if python3 tools/sweep.py --build "$BUILD"; then ok "sweep"; else bad "sweep"; fi

#-----------------------------------------------------------------------------
step "raster -- the software renderer agrees with the GL one"
#-----------------------------------------------------------------------------
# The OFX build cannot use the GL path, so it rasterises Scene in software. Two
# renderers for one plugin is a divergence waiting to happen, and the one people
# would hit is a preset that looks right in Resolume and wrong in Resolve.
if "$BUILD/idtest" --raster; then ok "raster"; else bad "raster"; fi

#-----------------------------------------------------------------------------
step "OFX bundle"
#-----------------------------------------------------------------------------
OFX_BUNDLE="$BUILD/Idler.ofx.bundle"
OFX_BIN="$OFX_BUNDLE/Contents/MacOS/Idler.ofx"

if [ ! -f "$OFX_BIN" ]; then
	bad "no OFX binary at $OFX_BIN"
else
	# Both entry points, or the host finds a bundle containing nothing.
	# Captured and matched with `case`, for the pipefail/SIGPIPE reason above.
	OFX_SYMBOLS=$( nm -gU "$OFX_BIN" 2>/dev/null || true )
	OFX_HAS_BOTH=0
	case "$OFX_SYMBOLS" in
		*_OfxGetPlugin*)
			case "$OFX_SYMBOLS" in
				*_OfxGetNumberOfPlugins*) OFX_HAS_BOTH=1 ;;
			esac ;;
	esac
	if [ "$OFX_HAS_BOTH" = 1 ]; then
		ok "OFX entry points exported"
	else
		bad "OFX entry points missing"
	fi

	# The trap that cost the fleet a tag, and the reason InfoOFX.plist.in is
	# parameterised: a hardcoded name here builds, loads and RENDERS correctly,
	# then fails codesign with a message that never mentions the plist.
	OFX_PLIST="$OFX_BUNDLE/Contents/Info.plist"
	OFX_EXE=$( /usr/libexec/PlistBuddy -c "Print :CFBundleExecutable" "$OFX_PLIST" 2>/dev/null || echo "" )
	if [ "$OFX_EXE" = "Idler.ofx" ]; then
		ok "OFX CFBundleExecutable matches the binary"
	else
		bad "OFX CFBundleExecutable is '$OFX_EXE', binary is 'Idler.ofx'"
	fi

	TEMP=$( mktemp -d )
	cp -R "$OFX_BUNDLE" "$TEMP/Idler.ofx.bundle"
	if codesign --force --deep --sign - "$TEMP/Idler.ofx.bundle" 2> "$TEMP/err.txt"; then
		ok "OFX bundle ad-hoc signs"
	else
		bad "OFX codesign: $( head -2 "$TEMP/err.txt" | tr '\n' ' ' )"
	fi
	rm -rf "$TEMP"

	# A real OFX host loading it, if the bridge's probe is built. Not fatal when
	# it is absent -- it lives in a sibling repo.
	PROBE="$HOME/Projects/resolume-ofx-bridge/build/ofxprobe"
	if [ -x "$PROBE" ]; then
		if "$PROBE" --dir "$BUILD" --render com.stoatworks.idlermask \
		            --size 320x180 --out /tmp/idler-ofx.bmp > /dev/null 2>&1; then
			ok "ofxprobe rendered the mask variant"
		else
			bad "ofxprobe could not render"
		fi
	else
		printf '   --    ofxprobe not built, skipping the host load\n'
	fi
fi

#-----------------------------------------------------------------------------
step "browser demo"
#-----------------------------------------------------------------------------
# demo/ is a hand-written port of this plugin for idler-demo.stoatworks-labs.com.
# Two things can rot in it, and they rot silently:
#
#   - the two shader programs are COPIED from source/Shaders.cpp, so a change
#     here that is not mirrored there leaves the demo running different GLSL;
#   - the 3D engine and the eleven savers are a HAND PORT, which is a second
#     implementation of everything this repo actually does.
#
# check_geometry.mjs is the one that matters. It drives the ported savers under
# exactly the conditions `idtest --geometry` uses and compares the same three
# numbers, so a mis-ported loop bound or a conversion curve that has drifted
# fails here rather than being noticed as "the demo looks a bit different".
if [ -d demo ]; then
	if python3 demo/tools/check_shaders.py > /tmp/idler-shaders.txt 2>&1; then
		ok "demo shaders identical to source/Shaders.cpp"
	else
		bad "demo shaders have drifted -- see /tmp/idler-shaders.txt"
	fi

	if command -v node > /dev/null 2>&1; then
		if node demo/tools/check_geometry.mjs > /tmp/idler-geometry.txt 2>&1; then
			ok "ported savers match idtest --geometry"
		else
			bad "ported savers differ from the plugin -- see /tmp/idler-geometry.txt"
		fi
	else
		printf '   --    node not installed, skipping the ported-saver check\n'
	fi

	# The kit is vendored from stoatworks-backend and must never be edited in
	# place; a fix applied to one copy is a fix the other ten silently lack.
	SYNC="$HOME/Projects/stoatworks-backend/resolume-demo/sync.sh"
	if [ -x "$SYNC" ]; then
		if "$SYNC" --check idler > /dev/null 2>&1; then
			ok "demo/vendor matches the shared kit"
		else
			bad "demo/vendor has drifted from stoatworks-backend/resolume-demo"
		fi
	else
		printf '   --    resolume-demo/sync.sh not present, skipping the kit check\n'
	fi
fi

#-----------------------------------------------------------------------------
step "contact sheet"
#-----------------------------------------------------------------------------
# Asserts nothing, and is still worth regenerating after any change to a saver.
# Every real bug in this repo so far was found by looking at one of these, not
# by an assertion: the pipes beaded at every cell, the maze walls flat enough to
# read as a rendering failure, the Mystify trail spanning a thirtieth of the
# motion. All three passed every test above.
if "$BUILD/idtest" --sheet /tmp/idler-sheet.png > /dev/null; then
	ok "wrote /tmp/idler-sheet.png -- look at it"
else
	bad "contact sheet"
fi

#-----------------------------------------------------------------------------
printf '\n\033[1m%d passed, %d failed\033[0m\n' "$PASS" "$FAIL"
exit $(( FAIL > 0 ? 1 : 0 ))
