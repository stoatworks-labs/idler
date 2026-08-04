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
	if nm -gU "$BINARY" | grep -q plugMain; then
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
step "sweep -- no dead controls"
#-----------------------------------------------------------------------------
if python3 tools/sweep.py --build "$BUILD"; then ok "sweep"; else bad "sweep"; fi

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
