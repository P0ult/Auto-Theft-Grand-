#!/bin/sh
# Packages the game for Windows: builds the game target, cooks the content, and writes a standalone build to
# Packaged/Windows (AutoTheftGrand.exe and its data; no Unreal install needed to run it).
#   Tools/package.sh                   Development (logs to Saved/Logs, the ATG.* console commands work)
#   CONFIG=Shipping Tools/package.sh   Shipping (smaller, no console)
# The editor must have been started once so the generated materials exist in Content/ATG/Materials.
UE="${UE_ROOT:-/c/Program Files/Epic Games/UE_5.8}"
HERE="$(cd "$(dirname "$0")/.." && pwd -W 2>/dev/null || pwd)"
LOG="${TMPDIR:-/tmp}/atg-package.log"
"$UE/Engine/Build/BatchFiles/RunUAT.bat" BuildCookRun -project="$HERE/AutoTheftGrand.uproject" -noP4 -utf8output \
	-platform=Win64 -clientconfig=${CONFIG:-Development} -build -cook -stage -pak -prereqs -archive \
	-archivedirectory="$HERE/Packaged" -ubtargs="-NoUBA" > "$LOG" 2>&1
RC=$?
grep -E "Error:|error |BUILD SUCCESSFUL|BUILD FAILED|AutomationTool exiting" "$LOG" | tail -${MAXLINES:-30}
echo "exit $RC (full log: $LOG)"
exit $RC
