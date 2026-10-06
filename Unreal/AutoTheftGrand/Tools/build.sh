#!/bin/sh
# Builds the editor target from Git Bash. Prints only errors, warnings from the project's own files and the result.
UE="${UE_ROOT:-/c/Program Files/Epic Games/UE_5.8}"
HERE="$(cd "$(dirname "$0")/.." && pwd -W 2>/dev/null || pwd)"
LOG="${TMPDIR:-/tmp}/atg-build.log"
"$UE/Engine/Build/BatchFiles/Build.bat" ${TARGET:-AutoTheftGrandEditor} Win64 ${CONFIG:-Development} -Project="$HERE/AutoTheftGrand.uproject" -WaitMutex > "$LOG" 2>&1
RC=$?
grep -E "error|AutoTheftGrand.*warning|Result:" "$LOG" | grep -v "^\s*$" | head -${MAXLINES:-80}
echo "exit $RC (full log: $LOG)"
exit $RC
