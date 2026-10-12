#include "Game/ATGFonts.h"
#include "AutoTheftGrand.h"
#include "Fonts/CompositeFont.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Styling/CoreStyle.h"

namespace {

struct FATGFontStack {
	TSharedPtr<FStandaloneCompositeFont> Font;
	FString Regular, Bold;
	bool bResolved = false;
};

FATGFontStack GStacks[4];

FString FindFont(std::initializer_list<const TCHAR*> Files) {
	const FString Project = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() / TEXT("Fonts"));
	const FString Windows = FPaths::Combine(FPlatformMisc::GetEnvironmentVariable(TEXT("WINDIR")), TEXT("Fonts"));
	for (const TCHAR* F : Files) {
		for (const FString& Dir : { Project, Windows }) {
			if (Dir.IsEmpty()) continue;
			const FString Path = Dir / F;
			if (IFileManager::Get().FileExists(*Path)) return Path;
		}
	}
	return FString();
}

FATGFontStack& Resolve(EATGFont Which) {
	FATGFontStack& S = GStacks[(int32)Which];
	if (S.bResolved) return S;
	S.bResolved = true;
	switch (Which) {
	case EATGFont::Hud: S.Regular = FindFont({ TEXT("BebasNeue-Regular.ttf"), TEXT("Anton-Regular.ttf"), TEXT("impact.ttf") }); break;
	case EATGFont::Title: S.Regular = FindFont({ TEXT("Anton-Regular.ttf"), TEXT("impact.ttf"), TEXT("ariblk.ttf") }); break;
	case EATGFont::Script: S.Regular = FindFont({ TEXT("PermanentMarker-Regular.ttf"), TEXT("BRUSHSCI.TTF") }); break;
	case EATGFont::Text:
		S.Regular = FindFont({ TEXT("Rubik-Regular.ttf"), TEXT("Rubik-VariableFont_wght.ttf"), TEXT("segoeui.ttf") });
		S.Bold = FindFont({ TEXT("Rubik-Bold.ttf"), TEXT("Rubik-SemiBold.ttf"), TEXT("segoeuib.ttf") });
		break;
	}
	if (!S.Regular.IsEmpty()) {
		S.Font = MakeShared<FStandaloneCompositeFont>(FName("Regular"), S.Regular, EFontHinting::Default, EFontLoadingPolicy::LazyLoad);
		if (!S.Bold.IsEmpty()) S.Font->DefaultTypeface.AppendFont(FName("Bold"), S.Bold, EFontHinting::Default, EFontLoadingPolicy::LazyLoad);
	}
	UE_LOG(LogATG, Log, TEXT("Font %d: %s"), (int32)Which, S.Regular.IsEmpty() ? TEXT("Roboto (no font file found)") : *S.Regular);
	return S;
}

} // namespace

FSlateFontInfo ATGFonts::Get(EATGFont Which, float Px, bool bBold) {
	const float Pt = FMath::Max(1.f, Px * 0.75f);
	const FATGFontStack& S = Resolve(Which);
	if (!S.Font.IsValid()) return FCoreStyle::GetDefaultFontStyle(bBold || Which != EATGFont::Text ? "Bold" : "Regular", Pt);
	return FSlateFontInfo(S.Font, Pt, bBold && !S.Bold.IsEmpty() ? FName("Bold") : FName("Regular"));
}

FString ATGFonts::Describe(EATGFont Which) {
	const FATGFontStack& S = Resolve(Which);
	return S.Regular.IsEmpty() ? TEXT("Roboto") : FPaths::GetCleanFilename(S.Regular);
}

void ATGFonts::Shutdown() {
	for (FATGFontStack& S : GStacks) { S.Font.Reset(); S.bResolved = false; }
}
