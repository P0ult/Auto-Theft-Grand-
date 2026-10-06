#!/bin/sh
# Builds a plain C++ tool (no Unreal) with MSVC from Git Bash: Tools/native.sh out.exe file.cpp [more.cpp ...]
# Adds the generator and simulation sources and include paths. Set VCVARS to point elsewhere if needed.
VCVARS="${VCVARS:-C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars64.bat}"
HERE="$(cd "$(dirname "$0")" && pwd -W)"
OUT="$1"; shift
rm -f "$HERE/$OUT"
SRC="$HERE/../Source/AutoTheftGrand/Private"
OBJ="$HERE/obj"
mkdir -p "$OBJ"
FILES="$*"
# DEBUG=1: unoptimised with symbols and runtime checks (for the crash traces in crashtrace.h)
if [ -n "$DEBUG" ]; then OPT="/Od /Zi /RTC1 /FS /Fd$OBJ/vc.pdb"; LINK="/link /DEBUG"; else OPT="/O2"; LINK=""; fi
for f in "$SRC"/Gen/*.cpp "$SRC"/Sim/*.cpp; do [ -f "$f" ] && FILES="$FILES $f"; done
WIN=""
for f in $FILES; do WIN="$WIN \"$(cd "$(dirname "$f")" && pwd -W)/$(basename "$f")\""; done
cat > "$OBJ/build.bat" <<BAT
@echo off
call "$VCVARS" >nul
cl /nologo /std:c++20 $OPT /EHsc /MP /W3 /wd4244 /wd4267 /wd4305 /wd4996 /D_CRT_SECURE_NO_WARNINGS /I"$SRC" /I"$SRC/Gen" /Fo"$OBJ/" /Fe"$HERE/$OUT" $WIN $LINK
BAT
cmd //c "$(cd "$OBJ" && pwd -W)\build.bat" 2>&1 | grep -v "^[a-zA-Z0-9_]*\.cpp$" | grep -E "error|warning|fatal" | head -${MAXLINES:-60}
[ -f "$HERE/$OUT" ] && echo "built $OUT" || { echo "build failed"; exit 1; }
