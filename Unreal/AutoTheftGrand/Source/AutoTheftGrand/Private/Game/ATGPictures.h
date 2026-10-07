// The walk-in shops' posters and menu boards: the canvases that src/world/interiors.js paints (canvasTex), painted
// once into render targets with FATGPainter. The browser's Impact, Arial and Georgia become the engine's Roboto
// (Impact narrowed to about its width).
#pragma once

#include "CoreMinimal.h"

class UTextureRenderTarget2D;

namespace ATGPictures {
	// a new render target with the named picture ("target", "rules", "burgermenu", "adopt", "neon", "cafemenu",
	// "lotto") painted on it, or null for a name it doesn't know
	UTextureRenderTarget2D* Paint(UObject* Outer, const FString& Name);
}
