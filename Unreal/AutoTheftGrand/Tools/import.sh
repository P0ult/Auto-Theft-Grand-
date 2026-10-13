#!/bin/sh
# Turns the downloads in SourceAssets/ (node Tools/fetch_assets.mjs) into assets under Content/ATG/Imported and
# makes any missing generated materials, without opening the editor (the ATGImport commandlet).
#   Tools/import.sh            import what is new
#   Tools/import.sh -force     import everything again
UE="${UE_ROOT:-/c/Program Files/Epic Games/UE_5.8}"
HERE="$(cd "$(dirname "$0")/.." && pwd -W 2>/dev/null || pwd)"
LOG="${TMPDIR:-/tmp}/atg-import.log"
"$UE/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "$HERE/AutoTheftGrand.uproject" -run=ATGImport "$@" -unattended -nopause -nosplash -NullRHI > "$LOG" 2>&1
RC=$?
grep -E "ATGImport|Textured material|Creating M_ATG|Error:|Fatal" "$LOG" | sed -E 's/^\[[^]]*\]\[[ 0-9]*\]//' | tail -${MAXLINES:-60}
echo "exit $RC (full log: $LOG)"
exit $RC
