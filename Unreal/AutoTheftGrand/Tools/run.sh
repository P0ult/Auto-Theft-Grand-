#!/bin/sh
# Runs the game (editor binary, -game) with a test script and prints the game's own log lines.
#   Tools/run.sh Tools/tests/smoke.txt            fixed 1/30 s steps, rendered off screen
#   WINDOWED=1 Tools/run.sh script.txt            in a window instead
# Screenshots land in Saved/Screenshots/ATG/. The full log is Saved/Logs/AutoTheftGrand.log.
UE="${UE_ROOT:-/c/Program Files/Epic Games/UE_5.8}"
HERE="$(cd "$(dirname "$0")/.." && pwd -W 2>/dev/null || pwd)"
SCRIPT="$(cd "$(dirname "$1")" && pwd -W 2>/dev/null || pwd)/$(basename "$1")"
shift
OFF="-RenderOffscreen"
[ -n "$WINDOWED" ] && OFF="-windowed"
"$UE/Engine/Binaries/Win64/UnrealEditor.exe" "$HERE/AutoTheftGrand.uproject" -game $OFF -resx=1280 -resy=720 -ResX=1280 -ResY=720 \
	-benchmark -fps=30 -unattended -nosplash -nosound -log -ATGScript="$SCRIPT" "$@" > /dev/null 2>&1
RC=$?
grep -E "LogATG|Fatal error|Assertion failed|Ensure condition failed|LogScript: Error" "$HERE/Saved/Logs/AutoTheftGrand.log" | sed -E 's/^\[[^]]*\]\[[ 0-9]*\]//' | grep -v "Creating M_ATG\|Creating MPC" | tail -${MAXLINES:-60}
echo "exit $RC"
