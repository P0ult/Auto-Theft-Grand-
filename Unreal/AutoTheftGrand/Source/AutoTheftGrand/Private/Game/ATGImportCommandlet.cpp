#include "Game/ATGImportCommandlet.h"
#include "AutoTheftGrand.h"
#include "Game/ATGAssets.h"
#include "Game/ATGMaterials.h"

int32 UATGImportCommandlet::Main(const FString& Params) {
#if WITH_EDITOR
	const int32 Made = ATGAssets::ImportAll(Params.Contains(TEXT("-force")));
	ATGMaterials::EnsureAssets();
	UE_LOG(LogATG, Display, TEXT("ATGImport: done, %d assets made; surfaces %s"), Made, ATGAssets::HasSurfaces() ? TEXT("on") : TEXT("off"));
	return 0;
#else
	return 1;
#endif
}
