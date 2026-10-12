// The browser game's four font stacks (css/style.css :root), loaded at runtime:
//   Hud    'Bebas Neue', 'Anton', Impact          (the HUD, menu buttons, tabs)
//   Title  'Anton', Impact, 'Arial Black'         (the logo, mission passed / WASTED)
//   Script 'Permanent Marker', 'Brush Script MT'  (Los Soles, zone names, chapter titles)
//   Text   'Rubik', 'Segoe UI', Roboto            (body text)
// Each stack takes the first font it finds: Content/Fonts/<file> (the Google fonts, under the SIL Open Font
// License, if they have been put there), then Windows' own fonts, then the engine's Roboto. Sizes are CSS pixels
// at the current DPI scale: pass Px and the Slate size is Px * 0.75 (Slate sizes are points at 96 dpi).
#pragma once

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"

enum class EATGFont : uint8 { Hud, Title, Script, Text };

namespace ATGFonts {
// Bold picks the stack's bold face where it has one (Rubik / Segoe UI / Roboto); the display fonts have one weight.
FSlateFontInfo Get(EATGFont Font, float Px, bool bBold = false);
// which file each stack ended up with (for the log and the tests)
FString Describe(EATGFont Font);
// let the fonts go (the game module's shutdown)
void Shutdown();
}
