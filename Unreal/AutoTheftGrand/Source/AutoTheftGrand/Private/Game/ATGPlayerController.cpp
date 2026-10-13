#include "Game/ATGPlayerController.h"
#include "Game/ATGGameMode.h"
#include "Game/ATGCoords.h"
#include "Game/ATGWorld.h"
#include "Sim/Game.h"
#include "Game/ATGHUD.h"
#include "Sim/Hud.h"
#include "Sim/FreeRoam.h"
#include "ATGMenuSystem.h"
#include "Sim/Traffic.h"
#include "Sim/Peds.h"
#include "Sim/Gameplay.h"
#include "Sim/Net.h"
#include "Game/ATGFonts.h"
#include "Game/ATGNetText.h"
#include "Sim/FreeRoam.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWrapBox.h"

#include "Containers/Ticker.h"
#include "Engine/GameViewportClient.h"
#include "Engine/UserInterfaceSettings.h"
#include "Framework/Application/SlateApplication.h"
#include "Styling/CoreStyle.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SEditableText.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SDPIScaler.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#include "Engine/World.h"
#include "InputCoreTypes.h"

namespace {
// Unreal key -> KeyboardEvent code (built on first use: EKeys' statics live in another module)
const TArray<TPair<FKey, FString>>& KeyCodes() {
	static TArray<TPair<FKey, FString>> K;
	if (K.Num()) return K;
	const FKey Letters[26] = { EKeys::A, EKeys::B, EKeys::C, EKeys::D, EKeys::E, EKeys::F, EKeys::G, EKeys::H, EKeys::I, EKeys::J, EKeys::K, EKeys::L, EKeys::M,
		EKeys::N, EKeys::O, EKeys::P, EKeys::Q, EKeys::R, EKeys::S, EKeys::T, EKeys::U, EKeys::V, EKeys::W, EKeys::X, EKeys::Y, EKeys::Z };
	for (int32 I = 0; I < 26; I++) K.Add({ Letters[I], FString::Printf(TEXT("Key%c"), TEXT('A') + I) });
	const FKey Digits[10] = { EKeys::Zero, EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five, EKeys::Six, EKeys::Seven, EKeys::Eight, EKeys::Nine };
	for (int32 I = 0; I < 10; I++) K.Add({ Digits[I], FString::Printf(TEXT("Digit%d"), I) });
	K.Add({ EKeys::SpaceBar, TEXT("Space") }); K.Add({ EKeys::Enter, TEXT("Enter") }); K.Add({ EKeys::Escape, TEXT("Escape") }); K.Add({ EKeys::Tab, TEXT("Tab") });
	K.Add({ EKeys::LeftShift, TEXT("ShiftLeft") }); K.Add({ EKeys::RightShift, TEXT("ShiftRight") });
	K.Add({ EKeys::LeftControl, TEXT("ControlLeft") }); K.Add({ EKeys::RightControl, TEXT("ControlRight") });
	K.Add({ EKeys::LeftAlt, TEXT("AltLeft") }); K.Add({ EKeys::CapsLock, TEXT("CapsLock") }); K.Add({ EKeys::BackSpace, TEXT("Backspace") });
	K.Add({ EKeys::Up, TEXT("ArrowUp") }); K.Add({ EKeys::Down, TEXT("ArrowDown") }); K.Add({ EKeys::Left, TEXT("ArrowLeft") }); K.Add({ EKeys::Right, TEXT("ArrowRight") });
	K.Add({ EKeys::Slash, TEXT("Slash") }); K.Add({ EKeys::Tilde, TEXT("Backquote") }); K.Add({ EKeys::Comma, TEXT("Comma") }); K.Add({ EKeys::Period, TEXT("Period") });
	K.Add({ EKeys::F1, TEXT("F1") }); K.Add({ EKeys::F2, TEXT("F2") }); K.Add({ EKeys::F3, TEXT("F3") }); K.Add({ EKeys::F4, TEXT("F4") });
	return K;
}
}

// ==================================================================== camera manager
void AATGCameraManager::UpdateViewTarget(FTViewTarget& OutVT, float DeltaTime) {
	AATGWorld* W = AATGWorld::Get(this);
	atg::Game* G = W ? W->Game() : nullptr;
	if (!G) {
		OutVT.POV.Location = FVector(0, 0, 20000);
		OutVT.POV.Rotation = FRotator(-30, 0, 0);
		OutVT.POV.FOV = 90.f;
		return;
	}
	const atg::CameraRig& R = G->rig;
	OutVT.POV.Location = ATG::ToUE(R.camPos);
	const FVector Fwd = ATG::DirToUE(R.camQuat.rotate(atg::V3(0, 0, -1)));
	const FVector Up = ATG::DirToUE(R.camQuat.rotate(atg::V3(0, 1, 0)));
	OutVT.POV.Rotation = FRotationMatrix::MakeFromXZ(Fwd, Up).Rotator();
	// the browser game's field of view is vertical; Unreal's is horizontal
	int32 Wd = 16, Ht = 9;
	if (APlayerController* PC = GetOwningPlayerController()) PC->GetViewportSize(Wd, Ht);
	const double Aspect = Ht > 0 ? (double)Wd / Ht : 16.0 / 9.0;
	OutVT.POV.FOV = (float)FMath::RadiansToDegrees(2 * FMath::Atan(FMath::Tan(FMath::DegreesToRadians(R.camFov) / 2) * Aspect));
}

// ==================================================================== controller
AATGPlayerController::AATGPlayerController() {
	PlayerCameraManagerClass = AATGCameraManager::StaticClass();
	PrimaryActorTick.bTickEvenWhenPaused = true;
	bShouldPerformFullTickWhenPaused = true;
	bShowMouseCursor = false;
}

void AATGPlayerController::BeginPlay() {
	Super::BeginPlay();
	SetInputMode(FInputModeGameOnly());
}

void AATGPlayerController::MenuSystemInput(float Dt)
{
	AATGGameMode* GM = Cast<AATGGameMode>(GetWorld() ? GetWorld()->GetAuthGameMode() : nullptr);
	if (!GM || !GM->IsMenuOpen()) { MenuLastX = MenuLastY = MenuLastTab = 0; bMenuPadActive = false; return; }
	if (!MenuSystem) MenuSystem = GM->GetMenuSystem();
	if (!MenuSystem) return;
	// (typing in the Online tab: Esc gives the keyboard back to the menu, nothing else is a menu key)
	if (IsTyping()) {
		if (WasInputKeyJustPressed(EKeys::Escape) && FSlateApplication::IsInitialized()) FSlateApplication::Get().SetAllUserFocusToGameViewport();
		MenuLastX = MenuLastY = 0;
		return;
	}
	float MouseX = 0.f, MouseY = 0.f;
	GetInputMouseDelta(MouseX, MouseY);
	const bool bMouseMoved = !FMath::IsNearlyZero(MouseX) || !FMath::IsNearlyZero(MouseY);
	const bool bPadMoved = WasInputKeyJustPressed(EKeys::Gamepad_FaceButton_Bottom) || WasInputKeyJustPressed(EKeys::Gamepad_FaceButton_Right)
		|| WasInputKeyJustPressed(EKeys::Gamepad_DPad_Up) || WasInputKeyJustPressed(EKeys::Gamepad_DPad_Down)
		|| WasInputKeyJustPressed(EKeys::Gamepad_DPad_Left) || WasInputKeyJustPressed(EKeys::Gamepad_DPad_Right)
		|| WasInputKeyJustPressed(EKeys::Gamepad_LeftShoulder) || WasInputKeyJustPressed(EKeys::Gamepad_RightShoulder)
		|| WasInputKeyJustPressed(EKeys::Gamepad_Special_Right)
		|| FMath::Abs(GetInputAnalogKeyState(EKeys::Gamepad_LeftX)) > 0.6f || FMath::Abs(GetInputAnalogKeyState(EKeys::Gamepad_LeftY)) > 0.6f;
	if (bMouseMoved) bMenuPadActive = false;
	else if (bPadMoved) bMenuPadActive = true;
	bShowMouseCursor = !bMenuPadActive;
	const bool bMain = MenuSystem->GetMenuState() == FATGMenuSystem::EATGMenuState::MainMenu;
	const float LX = GetInputAnalogKeyState(EKeys::Gamepad_LeftX);
	// Unreal's menu-axis sign here is opposite the simulation's stick convention: a downward pull must
	// produce positive Y (the next row), not the previous row.
	const float LY = GetInputAnalogKeyState(EKeys::Gamepad_LeftY);
	int32 X = 0, Y = 0;
	if (IsInputKeyDown(EKeys::Left) || IsInputKeyDown(EKeys::Gamepad_DPad_Left) || LX < -0.6f) X = -1;
	else if (IsInputKeyDown(EKeys::Right) || IsInputKeyDown(EKeys::Gamepad_DPad_Right) || LX > 0.6f) X = 1;
	if (IsInputKeyDown(EKeys::Up) || IsInputKeyDown(EKeys::Gamepad_DPad_Up) || LY > 0.6f) Y = -1;
	else if (IsInputKeyDown(EKeys::Down) || IsInputKeyDown(EKeys::Gamepad_DPad_Down) || LY < -0.6f) Y = 1;
	if (Y) X = 0;
	const int32 D = Y ? Y : X;
	const bool bTabDir = WasInputKeyJustPressed(EKeys::Gamepad_LeftShoulder) || WasInputKeyJustPressed(EKeys::PageUp) || WasInputKeyJustPressed(EKeys::Gamepad_RightShoulder) || WasInputKeyJustPressed(EKeys::PageDown);
	if (!bMain && bTabDir) {
		GM->MenuCycleTab(WasInputKeyJustPressed(EKeys::Gamepad_LeftShoulder) || WasInputKeyJustPressed(EKeys::PageUp) ? -1 : 1);
		MenuLastX = MenuLastY = 0;
		SystemMenuRepeatT = .38f;
	}
	else if (D && (X != MenuLastX || Y != MenuLastY)) { MenuLastX = X; MenuLastY = Y; SystemMenuRepeatT = .38f; GM->MenuNavigate(X, Y); }
	else if (D) { SystemMenuRepeatT -= FMath::Min(.1f, Dt); if (SystemMenuRepeatT <= 0) { SystemMenuRepeatT=.11f; GM->MenuNavigate(X,Y); } }
	else { MenuLastX=MenuLastY=0; }
	if (WasInputKeyJustPressed(EKeys::Enter) || WasInputKeyJustPressed(EKeys::SpaceBar) || WasInputKeyJustPressed(EKeys::Gamepad_FaceButton_Bottom)) GM->MenuActivate();
	if (WasInputKeyJustPressed(EKeys::Escape) || WasInputKeyJustPressed(EKeys::BackSpace) || WasInputKeyJustPressed(EKeys::P) || WasInputKeyJustPressed(EKeys::Gamepad_Special_Right) || WasInputKeyJustPressed(EKeys::Gamepad_FaceButton_Right)) GM->MenuBack();
}

void AATGPlayerController::PlayerTick(float Dt) {
	Super::PlayerTick(Dt);
	MessageTime = FMath::Max(0.f, MessageTime - Dt);
	// mouse movement and the wheel add up between simulation frames
	float Mx = 0, My = 0;
	GetInputMouseDelta(Mx, My);
	MouseDx += Mx; MouseDy += My;
	if (WasInputKeyJustPressed(EKeys::MouseScrollDown)) Wheel += 1;
	if (WasInputKeyJustPressed(EKeys::MouseScrollUp)) Wheel -= 1;
	AATGWorld* W = AATGWorld::Get(this);
	atg::Game* G = W ? W->Game() : nullptr;
	if (!G) return;
	if (AATGGameMode* GM = Cast<AATGGameMode>(GetWorld()->GetAuthGameMode())) {
		if (GM->IsMenuOpen()) {
			if (bChatOpen) CloseChat(false);
			if (bConsoleOpen) CloseConsole();
			MenuSystemInput(Dt);
			MouseDx = MouseDy = Wheel = 0;
			return; // closing a menu must not reopen it or leak the same input into gameplay.
		}
	}
	MenuSystemInput(Dt);
	// (the chat box has the keyboard: no hotkeys, and FeedInput gives the game nothing)
	if (bChatOpen) {
		if (!G->net || !G->net->online() || !G->gameplay || G->gameplay->state != "playing") CloseChat(false);
		MouseDx = MouseDy = Wheel = 0;
		return;
	}
	if (bConsoleOpen) {
		if (!G->gameplay || G->gameplay->state != "playing") CloseConsole();
		MouseDx = MouseDy = Wheel = 0;
		return;
	}
	if (MenuInput(*G, Dt)) return;
	// pause and the map (the HUD's keys in the browser game)
	// Only handle ESC/P if the menu system is NOT managing menus
	if (WasInputKeyJustPressed(EKeys::Escape) || WasInputKeyJustPressed(EKeys::P) || WasInputKeyJustPressed(EKeys::Gamepad_Special_Right)) {
		if (AATGGameMode* GM = Cast<AATGGameMode>(GetWorld()->GetAuthGameMode())) {
			// Check if menu system is managing menus
			if (!GM->IsMenuOpen()) {
				GM->ShowPauseMenu();
				MouseDx = MouseDy = Wheel = 0;
				return;
			}
			// If menu is open, let the menu system handle ESC (via button clicks)
		}
	}
	// the map: the pause menu on its Map tab (hud.js: input.hit('map') && !this.menuOpen -> openPause('map')); without
	// the menus (scripted runs) the plain full-screen map
	AATGGameMode* MapGM = Cast<AATGGameMode>(GetWorld()->GetAuthGameMode());
	const bool bMenus = MapGM && MapGM->GetMenuSystem();
	if (WasInputKeyJustPressed(EKeys::M) || WasInputKeyJustPressed(EKeys::Gamepad_DPad_Down)) {
		if (bMenus) { if (!MapGM->IsMenuOpen()) { MapGM->ShowPauseMenu(TEXT("Map")); MouseDx = MouseDy = Wheel = 0; return; } }
		else bMapOpen = !bMapOpen;
	}
	if (WasInputKeyJustPressed(EKeys::Slash) && !G->menuOpen && G->net && G->net->online() && G->gameplay && G->gameplay->state == "playing") {
		OpenChat();
		MouseDx = MouseDy = Wheel = 0;
		return;
	}
	// the admin console (hud.js: ` in free roam; elsewhere it says why not)
	if (WasInputKeyJustPressed(EKeys::Tilde) && !G->menuOpen && G->gameplay && G->gameplay->state == "playing" && !G->cutscene) {
		if (G->freeroamActive()) { OpenConsole(); MouseDx = MouseDy = Wheel = 0; return; }
		if (G->hud) G->hud->help("The admin console is a free roam feature.", 3);
	}
	// the phone's Map app (hud.openPause('map'))
	if (G->hudModel && !G->hudModel->pauseRequest.empty()) {
		const bool bTeleport = G->hudModel->pauseRequest == "teleport" && G->freeroam && G->freeroam->active();
		const FString Tab = G->hudModel->pauseRequest == "teleport" ? TEXT("Teleport") : TEXT("Map");
		G->hudModel->pauseRequest.clear();
		if (bMenus) MapGM->ShowPauseMenu(FName(*Tab));
		else if (bTeleport) G->freeroam->openTeleport(); else bMapOpen = true;
	}
}

// A shop menu on screen: the mouse cursor shows, a click or Enter / Space / pad A presses a button, the arrows,
// W / S, the D-pad or the left stick move the highlight (padnav.js: a 0.38 s wait, then every 0.11 s), and
// Esc, Backspace or pad B leaves (hud.js togglePause closes an open menu). True while one is open.
bool AATGPlayerController::MenuInput(atg::Game& G, float Dt) {
	atg::HudModel* M = G.hudModel;
	const bool bOpen = M && !M->menu.kind.empty();
	if (bOpen != bMenuCursor) {
		bMenuCursor = bOpen;
		bShowMouseCursor = bOpen;
		if (bOpen) { FInputModeGameAndUI Mode; Mode.SetHideCursorDuringCapture(false); Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock); SetInputMode(Mode); }
		else SetInputMode(FInputModeGameOnly());
		MenuLastDir = 0;
	}
	if (!bOpen) return false;
	if (WasInputKeyJustPressed(EKeys::Escape) || WasInputKeyJustPressed(EKeys::BackSpace) || WasInputKeyJustPressed(EKeys::Gamepad_FaceButton_Right) || WasInputKeyJustPressed(EKeys::Gamepad_Special_Right)) {
		M->closeOverlay();
		return true;
	}
	AATGHUD* Hud = Cast<AATGHUD>(GetHUD());
	float Mx, My;
	if (Hud && WasInputKeyJustPressed(EKeys::LeftMouseButton) && GetMousePosition(Mx, My)) {
		const int32 Row = Hud->MenuRowAt(Mx, My);
		if (Row >= 0) M->menuPress(Row);
		return true;
	}
	if (WasInputKeyJustPressed(EKeys::Enter) || WasInputKeyJustPressed(EKeys::SpaceBar) || WasInputKeyJustPressed(EKeys::Gamepad_FaceButton_Bottom)) {
		M->menuPress(M->menu.focus);
		return true;
	}
	const float Ly = -GetInputAnalogKeyState(EKeys::Gamepad_LeftY);
	int32 Dir = 0;
	if (IsInputKeyDown(EKeys::Up) || IsInputKeyDown(EKeys::W) || IsInputKeyDown(EKeys::Gamepad_DPad_Up) || Ly > 0.6f) Dir = -1;
	else if (IsInputKeyDown(EKeys::Down) || IsInputKeyDown(EKeys::S) || IsInputKeyDown(EKeys::Gamepad_DPad_Down) || Ly < -0.6f) Dir = 1;
	if (!Dir) MenuLastDir = 0;
	else if (Dir != MenuLastDir) { MenuLastDir = Dir; MenuRepeatT = 0.38f; M->menuMove(Dir); }
	else if ((MenuRepeatT -= FMath::Min(0.1f, Dt)) <= 0) { MenuRepeatT = 0.11f; M->menuMove(Dir); }
	return true;
}

void AATGPlayerController::FeedInput(atg::Game& G, double Dt) {
	atg::Input& In = G.input;
	if (bChatOpen || bConsoleOpen) {
		// (net.js openChat: input.enabled = false, keys cleared)
		for (const FString& C : Held) In.KeyUp(TCHAR_TO_UTF8(*C));
		Held.Reset();
		In.mouse.dx = In.mouse.dy = 0; In.mouse.left = In.mouse.right = In.mouse.leftPressed = In.mouse.rightPressed = false; In.mouse.wheel = 0;
		MouseDx = MouseDy = Wheel = 0; ScriptDx = ScriptDy = 0;
		return;
	}
	// keys: tell the simulation about changes (it keeps its own held / pressed / released sets)
	TSet<FString> Now = ScriptKeys;
	for (const auto& K : KeyCodes()) if (IsInputKeyDown(K.Key)) Now.Add(K.Value);
	for (const FString& C : Now) if (!Held.Contains(C)) In.KeyDown(TCHAR_TO_UTF8(*C));
	for (const FString& C : Held) if (!Now.Contains(C)) In.KeyUp(TCHAR_TO_UTF8(*C));
	Held = Now;
	// mouse: the browser's movementY is positive downwards
	In.mouse.dx = MouseDx * MouseSensitivity / 0.07; // (Unreal reports mouse movement in its own units: ~0.07 per pixel)
	In.mouse.dy = -MouseDy * MouseSensitivity / 0.07 * (In.invertY ? -1 : 1);
	MouseDx = MouseDy = 0;
	In.mouse.dx += ScriptDx; In.mouse.dy += ScriptDy; ScriptDx = ScriptDy = 0;
	// (test scripts hold the buttons as MouseLeft / MouseRight)
	const bool L = IsInputKeyDown(EKeys::LeftMouseButton) || ScriptKeys.Contains(TEXT("MouseLeft")), R = IsInputKeyDown(EKeys::RightMouseButton) || ScriptKeys.Contains(TEXT("MouseRight"));
	In.mouse.leftPressed = L && !In.mouse.left; In.mouse.rightPressed = R && !In.mouse.right;
	In.mouse.left = L; In.mouse.right = R;
	In.mouse.wheel = Wheel;
	Wheel = 0;
	// gamepad (standard layout; the browser's stick y is positive downwards)
	bool B[atg::GP::COUNT] = {};
	const FKey Pad[atg::GP::COUNT] = { EKeys::Gamepad_FaceButton_Bottom, EKeys::Gamepad_FaceButton_Right, EKeys::Gamepad_FaceButton_Left, EKeys::Gamepad_FaceButton_Top,
		EKeys::Gamepad_LeftShoulder, EKeys::Gamepad_RightShoulder, EKeys::Gamepad_LeftTrigger, EKeys::Gamepad_RightTrigger, EKeys::Gamepad_Special_Left, EKeys::Gamepad_Special_Right,
		EKeys::Gamepad_LeftThumbstick, EKeys::Gamepad_RightThumbstick, EKeys::Gamepad_DPad_Up, EKeys::Gamepad_DPad_Down, EKeys::Gamepad_DPad_Left, EKeys::Gamepad_DPad_Right, EKeys::Invalid };
	bool bAny = false;
	for (int32 I = 0; I < atg::GP::COUNT; I++) if (Pad[I].IsValid() && IsInputKeyDown(Pad[I])) { B[I] = true; bAny = true; }
const double Lx = GetInputAnalogKeyState(EKeys::Gamepad_LeftX), Ly = -GetInputAnalogKeyState(EKeys::Gamepad_LeftY);
const double Rx = GetInputAnalogKeyState(EKeys::Gamepad_RightX), Ry = GetInputAnalogKeyState(EKeys::Gamepad_RightY);
	const double Lt = GetInputAnalogKeyState(EKeys::Gamepad_LeftTriggerAxis), Rt = GetInputAnalogKeyState(EKeys::Gamepad_RightTriggerAxis);
	const bool bPad = bAny || FMath::Abs(Lx) + FMath::Abs(Ly) + FMath::Abs(Rx) + FMath::Abs(Ry) + Lt + Rt > 0.01 || In.gp.connected;
	In.SetPad(bPad, Lx, Ly, Rx, Ry, Lt, Rt, B);
	(void)Dt;
}

// ---- multiplayer chat (net.js _dom / openChat / closeChat)
bool AATGPlayerController::IsTyping() {
	if (!FSlateApplication::IsInitialized()) return false;
	const TSharedPtr<SWidget> F = FSlateApplication::Get().GetKeyboardFocusedWidget();
	if (!F.IsValid()) return false;
	const FName T = F->GetType();
	return T == FName(TEXT("SEditableText")) || T == FName(TEXT("SMultiLineEditableText"));
}

void AATGPlayerController::OpenChat() {
	AATGWorld* W = AATGWorld::Get(this);
	atg::Game* G = W ? W->Game() : nullptr;
	UGameViewportClient* VC = GetWorld() ? GetWorld()->GetGameViewport() : nullptr;
	if (bChatOpen || !G || !G->net || !VC) return;
	bChatOpen = true;
	G->net->chatOpen = true;
	FVector2D V(1280, 720);
	VC->GetViewportSize(V);
	// (the HUD's units: 1080 lines tall, like the feed it sits under)
	const float Ui = (float)V.Y / 1080.f;
	const float Dpi = GetDefault<UUserInterfaceSettings>()->GetDPIScaleBasedOnSize(FIntPoint((int32)V.X, (int32)V.Y));
	// `.net-chatbox`: black at 75 %, 4 px corners, a 3 px gold left border, padding 6 x 10, 8 px apart; "Say:" in
	// the display font (18 px, gold), the input 15 px white, min(380px, 60vw) wide
	static const FSlateRoundedBoxBrush Back(FLinearColor(0.f, 0.f, 0.f, 0.75f), 4.f);
	static const FEditableTextStyle Style = FEditableTextStyle(FCoreStyle::Get().GetWidgetStyle<FEditableTextStyle>("NormalEditableText"))
		.SetFont(ATGFonts::Get(EATGFont::Text, 15)).SetColorAndOpacity(FSlateColor(FLinearColor::White));
	const FLinearColor Gold = FLinearColor(FColor(0xe8, 0xb6, 0x4c));
	FSlateFontInfo SayFont = ATGFonts::Get(EATGFont::Hud, 18);
	SayFont.LetterSpacing = 56; // (1 px at 18 px)
	ChatWidget = SNew(SDPIScaler).DPIScale(Ui / FMath::Max(Dpi, 0.1f))[
		SNew(SBox).HAlign(HAlign_Left).VAlign(VAlign_Bottom).Padding(FMargin(24.f, 0.f, 0.f, 196.f))[
			SNew(SBorder).BorderImage(&Back).Padding(FMargin(0.f))[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(3.f)[SNew(SImage).Image(FCoreStyle::Get().GetBrush("WhiteBrush")).ColorAndOpacity(Gold)]]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(7.f, 6.f, 8.f, 6.f))[
					SNew(STextBlock).Text(FText::FromString(TEXT("Say:"))).Font(SayFont).ColorAndOpacity(FSlateColor(Gold))]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0.f, 6.f, 10.f, 6.f))[
					SNew(SBox).WidthOverride(FMath::Min(380.f, (float)V.X / Ui * 0.6f))[
						SAssignNew(ChatText, SEditableText)
						.Style(&Style)
						.HintText(FText::FromString(TEXT("Press Enter to send, Esc to cancel")))
						.ClearKeyboardFocusOnCommit(false)
						.RevertTextOnEscape(false)
						.OnKeyDownHandler_Lambda([this](const FGeometry&, const FKeyEvent& E) {
							if (E.GetKey() == EKeys::Escape) { CloseChat(false); return FReply::Handled(); }
							return FReply::Unhandled();
						})
						.OnTextCommitted_Lambda([this](const FText&, ETextCommit::Type How) {
							// (Enter sends; losing the keyboard closes it, like the input's blur)
							if (How == ETextCommit::OnEnter) CloseChat(true);
							else if (How == ETextCommit::OnUserMovedFocus || How == ETextCommit::OnCleared) CloseChat(false);
						})]]]]];
	VC->AddViewportWidgetContent(ChatWidget.ToSharedRef(), 20);
	// (the game keeps running but gets no input: input.enabled = false, keys cleared)
	G->input.keys.clear(); G->input.pressed.clear(); G->input.released.clear();
	FInputModeGameAndUI Mode;
	Mode.SetWidgetToFocus(ChatText);
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::LockAlways);
	Mode.SetHideCursorDuringCapture(true);
	SetInputMode(Mode);
	if (FSlateApplication::IsInitialized()) FSlateApplication::Get().SetKeyboardFocus(ChatText, EFocusCause::SetDirectly);
}

void AATGPlayerController::CloseChat(bool bSend) {
	if (!bChatOpen) return;
	bChatOpen = false;
	const FString Text = ChatText.IsValid() ? ChatText->GetText().ToString() : FString();
	AATGWorld* W = AATGWorld::Get(this);
	atg::Game* G = W ? W->Game() : nullptr;
	if (G && G->net) {
		G->net->chatOpen = false;
		if (bSend && !Text.TrimStartAndEnd().IsEmpty()) G->net->say(TCHAR_TO_UTF8(*Text));
	}
	// (removed next frame: this can run inside the text box's own key handler)
	TSharedPtr<SWidget> Old = ChatWidget;
	ChatWidget.Reset(); ChatText.Reset();
	if (UGameViewportClient* VC = GetWorld() ? GetWorld()->GetGameViewport() : nullptr)
		if (Old.IsValid()) {
			TWeakObjectPtr<UGameViewportClient> WeakVC = VC;
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakVC, Old](float) { if (WeakVC.IsValid()) WeakVC->RemoveViewportWidgetContent(Old.ToSharedRef()); return false; }));
		}
	SetInputMode(FInputModeGameOnly());
	if (FSlateApplication::IsInitialized()) FSlateApplication::Get().SetAllUserFocusToGameViewport();
	if (G) { G->input.keys.clear(); G->input.pressed.clear(); G->input.released.clear(); }
}

// ---- the admin console (hud.js openConsole / closeConsole; admin.js run)
FString AATGPlayerController::RunConsole(const FString& Line) {
	AATGWorld* W = AATGWorld::Get(this);
	atg::Game* G = W ? W->Game() : nullptr;
	const FString L = Line.TrimStartAndEnd();
	if (!G || !G->freeroam || L.IsEmpty()) return FString();
	ConHist.Remove(L);
	ConHist.Add(L);
	while (ConHist.Num() > 20) ConHist.RemoveAt(0);
	ConIdx = ConHist.Num();
	const FString R = UTF8_TO_TCHAR(G->freeroam->run(TCHAR_TO_UTF8(*L)).c_str());
	ConLines.Add({ true, TEXT("&gt; ") + L.Replace(TEXT("&"), TEXT("&amp;")).Replace(TEXT("<"), TEXT("&lt;")) });
	if (!R.IsEmpty()) ConLines.Add({ false, R });
	while (ConLines.Num() > 14) ConLines.RemoveAt(0);
	RebuildConsoleLog();
	return R;
}

void AATGPlayerController::RebuildConsoleLog() {
	if (!ConsoleLog.IsValid()) return;
	ConsoleLog->ClearChildren();
	// (.con-log div: black at 60 %, padding 3 x 9, 3 px corners, 13 px #d8d8d8; .cmd: gold, monospace)
	static const FSlateRoundedBoxBrush Back(FLinearColor(0.f, 0.f, 0.f, 0.6f), 3.f);
	const FLinearColor Gold(FColor(0xe8, 0xb6, 0x4c)), Text(FColor(0xd8, 0xd8, 0xd8));
	for (const FConLine& C : ConLines) {
		TArray<FString> Rows;
		C.Html.ParseIntoArray(Rows, TEXT("<br>"), false);
		TSharedRef<SVerticalBox> Body = SNew(SVerticalBox);
		for (const FString& Row : Rows) {
			// (a line is as wide as its text, up to the box's width less its padding)
			TSharedRef<SWrapBox> Runs = SNew(SWrapBox).UseAllottedSize(false).PreferredSize(ConWidth - 18.f);
			for (const FATGNetRun& R : ATGNetRuns(Row, C.bCmd ? Gold : Text, false)) {
				const FSlateFontInfo F = ATGFonts::Get(C.bCmd ? EATGFont::Mono : EATGFont::Text, 13, R.bBold, R.bItalic);
				// (word by word, so a long line wraps like html)
				int32 I = 0;
				while (I < R.S.Len()) {
					int32 E = I;
					while (E < R.S.Len() && R.S[E] == TEXT(' ')) E++;
					while (E < R.S.Len() && R.S[E] != TEXT(' ')) E++;
					while (E < R.S.Len() && R.S[E] == TEXT(' ')) E++;
					Runs->AddSlot()[SNew(STextBlock).Text(FText::FromString(R.S.Mid(I, E - I))).Font(F).ColorAndOpacity(FSlateColor(R.Col))];
					I = E;
				}
			}
			Body->AddSlot().AutoHeight()[Runs];
		}
		ConsoleLog->AddSlot().AutoHeight().HAlign(HAlign_Left).Padding(0.f, 0.f, 0.f, 2.f)[
			SNew(SBorder).BorderImage(&Back).Padding(FMargin(9.f, 3.f))[Body]];
	}
}

void AATGPlayerController::OpenConsole() {
	AATGWorld* W = AATGWorld::Get(this);
	atg::Game* G = W ? W->Game() : nullptr;
	UGameViewportClient* VC = GetWorld() ? GetWorld()->GetGameViewport() : nullptr;
	if (bConsoleOpen || !G || !G->freeroam || !VC) return;
	bConsoleOpen = true;
	if (ConLines.IsEmpty())
		ConLines.Add({ false, G->freeroam->allowed() ? FString(TEXT("Admin console. Type <b>help</b> for commands.")) : FString(TEXT("Vehicle spawner console (admin commands are single-player only). Try <b>car zenith</b>.")) });
	FVector2D V(1280, 720);
	VC->GetViewportSize(V);
	const float Ui = (float)V.Y / 1080.f;
	const float Dpi = GetDefault<UUserInterfaceSettings>()->GetDPIScaleBasedOnSize(FIntPoint((int32)V.X, (int32)V.Y));
	// .con-box: left 24, bottom 256, min(560px, 100vw - 48px) wide; .con-row: black at 80 %, 4 px corners, a 3 px
	// green left border, padding 6 x 10, "> " in green monospace, the input 15 px white monospace
	static const FSlateRoundedBoxBrush RowBack(FLinearColor(0.f, 0.f, 0.f, 0.8f), 4.f);
	static const FEditableTextStyle Style = FEditableTextStyle(FCoreStyle::Get().GetWidgetStyle<FEditableTextStyle>("NormalEditableText"))
		.SetFont(ATGFonts::Get(EATGFont::Mono, 15)).SetColorAndOpacity(FSlateColor(FLinearColor::White));
	const FLinearColor Green(FColor(0x6c, 0xff, 0x8a));
	const float Wd = FMath::Min(560.f, (float)V.X / Ui - 48.f);
	ConWidth = Wd;
	ConsoleWidget = SNew(SDPIScaler).DPIScale(Ui / FMath::Max(Dpi, 0.1f))[
		SNew(SBox).HAlign(HAlign_Left).VAlign(VAlign_Bottom).Padding(FMargin(24.f, 0.f, 0.f, 256.f))[
			SNew(SBox).WidthOverride(Wd)[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)[SNew(SBox).MaxDesiredHeight(240.f)[SNew(SScrollBox) + SScrollBox::Slot()[SAssignNew(ConsoleLog, SVerticalBox)]]]
				+ SVerticalBox::Slot().AutoHeight()[
					SNew(SBorder).BorderImage(&RowBack).Padding(FMargin(0.f))[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(3.f)[SNew(SImage).Image(FCoreStyle::Get().GetBrush("WhiteBrush")).ColorAndOpacity(Green)]]
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(7.f, 6.f, 8.f, 6.f))[
							SNew(STextBlock).Text(FText::FromString(TEXT(">"))).Font(ATGFonts::Get(EATGFont::Mono, 15, true)).ColorAndOpacity(FSlateColor(Green))]
						+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(FMargin(0.f, 6.f, 10.f, 6.f))[
							SAssignNew(ConsoleText, SEditableText)
							.Style(&Style)
							.HintText(FText::FromString(TEXT("command — type help, Enter to run, Esc to close")))
							.ClearKeyboardFocusOnCommit(false)
							.RevertTextOnEscape(false)
							.OnKeyDownHandler_Lambda([this](const FGeometry&, const FKeyEvent& E) {
								const FKey K = E.GetKey();
								const bool bEmpty = ConsoleText.IsValid() && ConsoleText->GetText().IsEmpty();
								if (K == EKeys::Escape || (K == EKeys::Tilde && bEmpty)) { CloseConsole(); return FReply::Handled(); }
								if ((K == EKeys::Up || K == EKeys::Down) && ConHist.Num() && ConsoleText.IsValid()) {
									ConIdx = K == EKeys::Up ? FMath::Max(0, ConIdx - 1) : FMath::Min(ConHist.Num(), ConIdx + 1);
									ConsoleText->SetText(FText::FromString(ConHist.IsValidIndex(ConIdx) ? ConHist[ConIdx] : FString()));
									return FReply::Handled();
								}
								return FReply::Unhandled();
							})
							.OnTextCommitted_Lambda([this](const FText& T, ETextCommit::Type How) {
								if (How == ETextCommit::OnEnter) {
									const FString Line = T.ToString().TrimStartAndEnd();
									if (ConsoleText.IsValid()) ConsoleText->SetText(FText::GetEmpty());
									if (Line.IsEmpty()) CloseConsole(); else RunConsole(Line);
								} else if (How == ETextCommit::OnUserMovedFocus || How == ETextCommit::OnCleared) CloseConsole();
							})]]]]]];
	RebuildConsoleLog();
	VC->AddViewportWidgetContent(ConsoleWidget.ToSharedRef(), 21);
	G->input.keys.clear(); G->input.pressed.clear(); G->input.released.clear();
	FInputModeGameAndUI Mode;
	Mode.SetWidgetToFocus(ConsoleText);
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::LockAlways);
	Mode.SetHideCursorDuringCapture(true);
	SetInputMode(Mode);
	if (FSlateApplication::IsInitialized()) FSlateApplication::Get().SetKeyboardFocus(ConsoleText, EFocusCause::SetDirectly);
}

void AATGPlayerController::CloseConsole() {
	if (!bConsoleOpen) return;
	bConsoleOpen = false;
	TSharedPtr<SWidget> Old = ConsoleWidget;
	ConsoleWidget.Reset(); ConsoleText.Reset(); ConsoleLog.Reset();
	if (UGameViewportClient* VC = GetWorld() ? GetWorld()->GetGameViewport() : nullptr)
		if (Old.IsValid()) {
			TWeakObjectPtr<UGameViewportClient> WeakVC = VC;
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakVC, Old](float) { if (WeakVC.IsValid()) WeakVC->RemoveViewportWidgetContent(Old.ToSharedRef()); return false; }));
		}
	SetInputMode(FInputModeGameOnly());
	if (FSlateApplication::IsInitialized()) FSlateApplication::Get().SetAllUserFocusToGameViewport();
	AATGWorld* W = AATGWorld::Get(this);
	if (atg::Game* G = W ? W->Game() : nullptr) { G->input.keys.clear(); G->input.pressed.clear(); G->input.released.clear(); }
}
