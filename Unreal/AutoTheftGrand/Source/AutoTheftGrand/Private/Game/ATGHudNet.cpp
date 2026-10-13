// Multiplayer on the HUD (net.js _tags and _dom): name tags over the other players, the chat feed and the
// ONLINE badge above the radar. The feed's lines are the browser's html (a coloured <b> name, a red weapon
// in the kill lines), drawn as runs of text.
#include "Game/ATGHUD.h"
#include "Game/ATGGameMode.h"
#include "Game/ATGCoords.h"
#include "Game/ATGPainter.h"
#include "Game/ATGNetText.h"
#include "Sim/Game.h"
#include "Sim/Gameplay.h"
#include "Sim/Net.h"
#include "Sim/Ragdoll.h"
#include "Sim/Vehicle.h"

#include "Engine/Canvas.h"
#include "GameFramework/PlayerController.h"

namespace {

FLinearColor HexCol(const FString& H, const FLinearColor& Def) {
	FString T = H.TrimStartAndEnd();
	if (T.StartsWith(TEXT("#"))) T.RightChopInline(1);
	if (T.Len() != 6) return Def;
	const uint32 V = (uint32)FParse::HexNumber(*T);
	return FLinearColor(FColor((V >> 16) & 255, (V >> 8) & 255, V & 255));
}

FString Unescape(const FString& S) {
	return S.Replace(TEXT("&lt;"), TEXT("<")).Replace(TEXT("&gt;"), TEXT(">")).Replace(TEXT("&quot;"), TEXT("\"")).Replace(TEXT("&#39;"), TEXT("'")).Replace(TEXT("&amp;"), TEXT("&"));
}

const TCHAR* FaceOf(const FATGNetRun& R) { return R.bHud ? TEXT("Hud") : R.bBold && R.bItalic ? TEXT("Text BoldItalic") : R.bBold ? TEXT("Text Bold") : R.bItalic ? TEXT("Text Italic") : TEXT("Text"); }

} // namespace

// a little html: <b> (with a style colour), <i>, <span class="kf"> (the kill feed's weapon)
TArray<FATGNetRun> ATGNetRuns(const FString& Html, const FLinearColor& Base, bool bItalic) {
	struct FSt { FLinearColor Col; bool bBold, bItalic, bHud; };
	TArray<FSt> Stack;
	Stack.Add({ Base, false, bItalic, false });
	TArray<FATGNetRun> Out;
	FString Cur;
	auto Flush = [&]() { if (!Cur.IsEmpty()) { const FSt& T = Stack.Last(); Out.Add({ Unescape(Cur), T.Col, T.bBold, T.bItalic, T.bHud }); Cur.Reset(); } };
	for (int32 I = 0; I < Html.Len(); I++) {
		// (as html: a tag starts with a letter or a slash; anything else after '<' is plain text, e.g. "<0-5>")
		const bool bTag = Html[I] == TEXT('<') && I + 1 < Html.Len() && (FChar::IsAlpha(Html[I + 1]) || Html[I + 1] == TEXT('/'));
		if (!bTag) { Cur.AppendChar(Html[I]); continue; }
		const int32 E = Html.Find(TEXT(">"), ESearchCase::CaseSensitive, ESearchDir::FromStart, I);
		if (E < 0) { Cur.AppendChar(Html[I]); continue; }
		const FString Tag = Html.Mid(I + 1, E - I - 1);
		I = E;
		Flush();
		if (Tag.StartsWith(TEXT("/"))) { if (Stack.Num() > 1) Stack.Pop(); continue; }
		FSt N = Stack.Last();
		if (Tag.StartsWith(TEXT("b"))) N.bBold = true;
		else if (Tag.StartsWith(TEXT("i"))) N.bItalic = true;
		else if (Tag.Contains(TEXT("kf"))) { N.bHud = true; N.bItalic = false; N.Col = FLinearColor(FColor(0xff, 0x6b, 0x6b)); }
		// (.hud-subtitles .y / .r / .b / .g: the dialogue's highlights)
		else if (Tag.Contains(TEXT("class=\"y\""))) N.Col = FLinearColor(FColor(0xff, 0xd2, 0x3f));
		else if (Tag.Contains(TEXT("class=\"r\""))) N.Col = FLinearColor(FColor(0xff, 0x6b, 0x6b));
		else if (Tag.Contains(TEXT("class=\"b\""))) N.Col = FLinearColor(FColor(0x7f, 0xb6, 0xff));
		else if (Tag.Contains(TEXT("class=\"g\""))) N.Col = FLinearColor(FColor(0x7d, 0xff, 0x8a));
		const int32 C = Tag.Find(TEXT("color:"));
		if (C >= 0) N.Col = HexCol(Tag.Mid(C + 6, 7), N.Col);
		Stack.Add(N);
	}
	Flush();
	return Out;
}

FString ATGNetLineHtml(bool bSys, const std::string& Name, const std::string& Text, uint32 Col) {
	const FString Html = UTF8_TO_TCHAR(Text.c_str());
	if (bSys) return Html;
	const FString N = FString(UTF8_TO_TCHAR(Name.c_str())).Replace(TEXT("&"), TEXT("&amp;")).Replace(TEXT("<"), TEXT("&lt;"));
	return FString::Printf(TEXT("<b style=\"color:#%06x\">%s</b> %s"), Col & 0xffffff, *N, *Html);
}

void AATGHUD::DrawNet(atg::Game* G) {
	atg::NetSystem* N = G->net;
	if (!N) return;
	const float W = Canvas->ClipX, H = Canvas->ClipY;
	FATGPainter c(Canvas);
	const bool bPlaying = !G->menuOpen && G->gameplay && G->gameplay->state == "playing";
	// ---- name tags (white-outlined names in their colour, a health bar under them)
	if (N->online() && bPlaying) {
		FVector Cam = FVector::ZeroVector;
		if (const APlayerController* PC = PlayerOwner) if (PC->PlayerCameraManager) Cam = PC->PlayerCameraManager->GetCameraLocation();
		for (const auto& KV : N->peers) {
			const atg::NetSystem::Peer& P = *KV.second;
			const atg::RemoteAvatar* Av = P.avatar.get();
			if (!Av || Av->removed) continue;
			const atg::V3 Base = Av->vehicle ? Av->vehicle->pos : Av->ragdolling && Av->ragdoll ? Av->ragdoll->center() : Av->pos;
			const double Up = Av->vehicle ? (Av->vehicle->def.H > 0 ? Av->vehicle->def.H : 1.6) + 0.9 : 2.25;
			const FVector At = ATG::ToUE(Base.x, Base.y + Up, Base.z);
			const double D = FVector::Dist(At, Cam) / 100.0;
			const FVector S = Project(At);
			if (S.Z <= 0 || D >= 320) continue;
			const float K = (float)FMath::Clamp(1.25 - D / 260, 0.55, 1.1) * Ui;
			const double Hp = FMath::Clamp(atg::JNumAt(P.st, "hp", 100) / 100, 0.0, 1.0);
			FString Note;
			if (atg::JTruthy(P.st.get("pz"))) Note += TEXT(" paused");
			if (!atg::JTruthy(P.st.get("pvp"))) Note += TEXT(" passive");
			if (atg::JTruthy(P.st.get("d"))) Note += TEXT(" wasted");
			const FString Name = UTF8_TO_TCHAR(P.name.c_str());
			const float Nw = c.FontWidth(Name, 14 * K, TEXT("Text Bold")), Iw = Note.IsEmpty() ? 0 : c.FontWidth(Note, 11 * K, TEXT("Text Italic"));
			const float X0 = (float)S.X - (Nw + Iw) / 2, Base0 = (float)S.Y - 10 * K;
			const FLinearColor Col(FColor((P.col >> 16) & 255, (P.col >> 8) & 255, P.col & 255));
			for (const FVector2f O : { FVector2f(1, 1), FVector2f(-1, 1), FVector2f(1, -1), FVector2f(-1, -1) }) { c.Fill = FLinearColor::Black; c.FontText(Name, X0 + O.X, Base0 + O.Y, 14 * K, TEXT("Text Bold")); }
			c.Fill = Col; c.FontText(Name, X0, Base0, 14 * K, TEXT("Text Bold"));
			if (!Note.IsEmpty()) { c.Fill = FLinearColor(FColor(0xdd, 0xdd, 0xdd)); c.FontText(Note, X0 + Nw, Base0, 11 * K, TEXT("Text Italic")); }
			// (.hp: 54 x 4, a black border, green)
			const float Bw = 54 * K, Bh = 4 * K, Bx = (float)S.X - Bw / 2, By = Base0 + 3 * K;
			c.Fill = FLinearColor::Black; c.FillRect(Bx - K, By - K, Bw + 2 * K, Bh + 2 * K);
			c.Fill = FLinearColor(0, 0, 0, 0.6f); c.FillRect(Bx, By, Bw, Bh);
			c.Fill = FLinearColor(FColor(0x6f, 0xd4, 0x6f)); c.FillRect(Bx, By, Bw * (float)Hp, Bh);
		}
	}
	// ---- the chat feed: the last seven lines, fading after eleven seconds (left 24, bottom 250)
	{
		TArray<const atg::NetSystem::ChatLine*> Show;
		for (auto It = N->chat.rbegin(); It != N->chat.rend() && Show.Num() < 7; ++It) if (N->nowMs - It->t < 12500) Show.Insert(&*It, 0);
		float Y = H - 250 * Ui;
		const float Px = 14 * Ui, LineH = Px * 1.25f + 6 * Ui;
		for (int32 I = Show.Num() - 1; I >= 0; I--) {
			const atg::NetSystem::ChatLine& L = *Show[I];
			const float A = (float)FMath::Clamp(1.0 - (N->nowMs - L.t - 11000) / 1400, 0.0, 1.0);
			const FString Html = ATGNetLineHtml(L.sys, L.name, L.text, L.col);
			const TArray<FATGNetRun> R = ATGNetRuns(Html, L.sys ? FLinearColor(FColor(0xcf, 0xcf, 0xcf)) : FLinearColor(FColor(0xee, 0xee, 0xee)), L.sys);
			float Tw = 0;
			for (const FATGNetRun& Ru : R) Tw += c.FontWidth(Ru.S, Ru.bHud ? Px * 1.1f : Px, FaceOf(Ru)) + (Ru.bHud ? 8 * Ui : 0);
			Y -= LineH;
			c.Fill = FLinearColor(0, 0, 0, 0.5f * A);
			c.BeginPath(); c.RoundRect(24 * Ui, Y, Tw + 18 * Ui, LineH - 3 * Ui, 3 * Ui); c.FillPath();
			float X = 33 * Ui;
			for (const FATGNetRun& Ru : R) {
				if (Ru.bHud) X += 4 * Ui;
				const float P2 = Ru.bHud ? Px * 1.1f : Px;
				c.Fill = FLinearColor(0, 0, 0, A); c.FontText(Ru.S, X + Ui, Y + LineH * 0.62f + Ui, P2, FaceOf(Ru));
				c.Fill = Ru.Col.CopyWithNewOpacity(A); c.FontText(Ru.S, X, Y + LineH * 0.62f, P2, FaceOf(Ru));
				X += c.FontWidth(Ru.S, P2, FaceOf(Ru)) + (Ru.bHud ? 4 * Ui : 0);
			}
		}
	}
	// ---- the badge: ● ONLINE · ROOM CODE · n players · / chat (left 24, bottom 228)
	if (N->online()) {
		const int32 Np = (int32)N->peers.size();
		const FString Txt = FString::Printf(TEXT("ONLINE · %s · %d PLAYER%s · / CHAT"), N->code.empty() ? TEXT("PUBLIC WORLD") : *(TEXT("ROOM ") + FString(UTF8_TO_TCHAR(N->code.c_str())).ToUpper()), Np + 1, Np ? TEXT("S") : TEXT(""));
		const float X = 24 * Ui, Base = H - 228 * Ui;
		const FLinearColor Dot = N->linkUp ? FLinearColor(FColor(0x3d, 0xdc, 0x84)) : FLinearColor(FColor(0x77, 0x77, 0x77));
		c.Fill = Dot; c.BeginPath(); c.Arc(X + 4.5f * Ui, Base - 5.5f * Ui, 4.5f * Ui, 0, 2 * PI); c.FillPath();
		c.Fill = FLinearColor::Black; c.FontText(Txt, X + 16 * Ui + 2 * Ui, Base + 2 * Ui, 16 * Ui, TEXT("Hud"));
		c.Fill = FLinearColor::White; c.FontText(Txt, X + 16 * Ui, Base, 16 * Ui, TEXT("Hud"));
	}
	(void)W;
}
