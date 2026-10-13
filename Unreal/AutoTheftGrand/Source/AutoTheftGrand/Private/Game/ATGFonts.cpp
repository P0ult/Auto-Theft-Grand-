#include "Game/ATGFonts.h"
#include "AutoTheftGrand.h"
#include "Engine/Font.h"
#include "Fonts/CompositeFont.h"
#include "UObject/Package.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Styling/CoreStyle.h"

namespace {

// (a runtime-cached UFont holding the composite font: Slate draws either, but the canvas only draws fonts it can
// find through a UFont)
struct FATGFontStack {
	UFont* Font = nullptr;
	FString Regular, Bold, BoldItalic, Italic;
	bool bResolved = false;
};

FATGFontStack GStacks[5];

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
		S.Bold = FindFont({ TEXT("Rubik-SemiBold.ttf"), TEXT("Rubik-Bold.ttf"), TEXT("segoeuib.ttf") });
		S.BoldItalic = FindFont({ TEXT("Rubik-SemiBoldItalic.ttf"), TEXT("Rubik-BoldItalic.ttf"), TEXT("segoeuiz.ttf") });
		S.Italic = FindFont({ TEXT("Rubik-Italic.ttf"), TEXT("segoeuii.ttf") });
		break;
	case EATGFont::Arial:
		S.Regular = FindFont({ TEXT("arial.ttf") });
		S.Bold = FindFont({ TEXT("arialbd.ttf") });
		break;
	}
	if (!S.Regular.IsEmpty()) {
		S.Font = NewObject<UFont>(GetTransientPackage(), NAME_None, RF_Transient);
		S.Font->FontCacheType = EFontCacheType::Runtime;
		S.Font->LegacyFontName = FName("Regular");
		FCompositeFont& C = S.Font->GetMutableInternalCompositeFont();
		C = FCompositeFont(FName("Regular"), S.Regular, EFontHinting::Default, EFontLoadingPolicy::LazyLoad);
		if (!S.Bold.IsEmpty()) C.DefaultTypeface.AppendFont(FName("Bold"), S.Bold, EFontHinting::Default, EFontLoadingPolicy::LazyLoad);
		if (!S.BoldItalic.IsEmpty()) C.DefaultTypeface.AppendFont(FName("BoldItalic"), S.BoldItalic, EFontHinting::Default, EFontLoadingPolicy::LazyLoad);
		if (!S.Italic.IsEmpty()) C.DefaultTypeface.AppendFont(FName("Italic"), S.Italic, EFontHinting::Default, EFontLoadingPolicy::LazyLoad);
		// symbols the display and text fonts lack (stars, ticks, arrows) come from Segoe UI Symbol, else Roboto
		FString Sym = FindFont({ TEXT("seguisym.ttf") });
		if (Sym.IsEmpty()) Sym = FPaths::EngineContentDir() / TEXT("Slate/Fonts/Roboto-Regular.ttf");
		C.FallbackTypeface.Typeface.AppendFont(FName("Regular"), Sym, EFontHinting::Default, EFontLoadingPolicy::LazyLoad);
		C.FallbackTypeface.Typeface.AppendFont(FName("Bold"), Sym, EFontHinting::Default, EFontLoadingPolicy::LazyLoad);
		S.Font->AddToRoot();
	}
	UE_LOG(LogATG, Log, TEXT("Font %d: %s"), (int32)Which, S.Regular.IsEmpty() ? TEXT("Roboto (no font file found)") : *S.Regular);
	return S;
}

} // namespace

FSlateFontInfo ATGFonts::Get(EATGFont Which, float Px, bool bBold, bool bItalic) {
	const float Pt = FMath::Max(1.f, Px * 0.75f);
	const FATGFontStack& S = Resolve(Which);
	if (!S.Font) return FCoreStyle::GetDefaultFontStyle(bBold && bItalic ? "BoldItalic" : bBold || (Which != EATGFont::Text && Which != EATGFont::Arial) ? "Bold" : "Regular", Pt);
	const FName Face = bBold && bItalic && !S.BoldItalic.IsEmpty() ? FName("BoldItalic") : bBold && !S.Bold.IsEmpty() ? FName("Bold")
		: bItalic && !S.Italic.IsEmpty() ? FName("Italic") : FName("Regular");
	return FSlateFontInfo(S.Font, Pt, Face);
}

FString ATGFonts::Describe(EATGFont Which) {
	const FATGFontStack& S = Resolve(Which);
	return S.Regular.IsEmpty() ? TEXT("Roboto") : FPaths::GetCleanFilename(S.Regular);
}

void ATGFonts::Shutdown() {
	// (the fonts are rooted transient objects; the engine purges them on its way out, so only forget them here)
	for (FATGFontStack& S : GStacks) { S.Font = nullptr; S.bResolved = false; }
}
