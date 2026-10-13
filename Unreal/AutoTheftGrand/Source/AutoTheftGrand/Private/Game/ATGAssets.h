// The free (CC0) art the game draws instead of its generated look where it has it. Tools/fetch_assets.mjs
// downloads it into SourceAssets/ (not in git); the ATGImport commandlet (Tools/import.sh) turns it into assets
// under /Game/ATG/Imported, which a package then cooks. All of it is optional: without these assets the game
// draws what it always drew.
#pragma once

#include "CoreMinimal.h"

class UTexture2D;
class UStaticMesh;

namespace ATGAssets {
	// the ground and wall texture sets (ambientCG ids)
	extern const TCHAR* const SurfaceSets[];
	extern const int32 NumSurfaceSets;
	// one texture of a set: Kind 'C' colour, 'N' normal map, 'M' masks (R roughness, G occlusion, B height)
	UTexture2D* Surface(const TCHAR* Set, TCHAR Kind);
	// true when every surface set was imported (the materials then use them)
	bool HasSurfaces();
	// the model that replaces a kind of prop ("hydrant", "trashcan", "bench", "palm0", ...) or stands for a kind of
	// street dressing (ATGDressing), or null
	UStaticMesh* PropMesh(const FString& Type);
	// of those kinds, the ones imported, with a collision radius and height in metres from the model's bounds
	TMap<FString, FVector2D> DressingSizes(const TArray<FString>& Kinds);
#if WITH_EDITOR
	// the average linear colour of a texture's source image (the materials use it to keep the game's palette)
	FLinearColor Mean(UTexture2D* Tex);
	// import whatever in SourceAssets has no asset yet (everything with bForce); returns how many assets it made
	int32 ImportAll(bool bForce);
#endif
}
