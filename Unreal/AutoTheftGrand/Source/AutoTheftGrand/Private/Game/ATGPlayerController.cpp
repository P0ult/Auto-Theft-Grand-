#include "Game/ATGPlayerController.h"
#include "Game/ATGCoords.h"
#include "Game/ATGWorld.h"
#include "Sim/Game.h"

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
	// pause and the map (the HUD's keys in the browser game)
	if (WasInputKeyJustPressed(EKeys::Escape) || WasInputKeyJustPressed(EKeys::P) || WasInputKeyJustPressed(EKeys::Gamepad_Special_Right)) G->paused = !G->paused;
	if (WasInputKeyJustPressed(EKeys::M) || WasInputKeyJustPressed(EKeys::Gamepad_DPad_Down)) bMapOpen = !bMapOpen;
}

void AATGPlayerController::FeedInput(atg::Game& G, double Dt) {
	atg::Input& In = G.input;
	// keys: tell the simulation about changes (it keeps its own held / pressed / released sets)
	TSet<FString> Now = ScriptKeys;
	for (const auto& K : KeyCodes()) if (IsInputKeyDown(K.Key)) Now.Add(K.Value);
	for (const FString& C : Now) if (!Held.Contains(C)) In.KeyDown(TCHAR_TO_UTF8(*C));
	for (const FString& C : Held) if (!Now.Contains(C)) In.KeyUp(TCHAR_TO_UTF8(*C));
	Held = Now;
	// mouse: the browser's movementY is positive downwards
	In.mouse.dx = MouseDx * MouseSensitivity / 0.07; // (Unreal reports mouse movement in its own units: ~0.07 per pixel)
	In.mouse.dy = -MouseDy * MouseSensitivity / 0.07;
	MouseDx = MouseDy = 0;
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
	const double Rx = GetInputAnalogKeyState(EKeys::Gamepad_RightX), Ry = -GetInputAnalogKeyState(EKeys::Gamepad_RightY);
	const double Lt = GetInputAnalogKeyState(EKeys::Gamepad_LeftTriggerAxis), Rt = GetInputAnalogKeyState(EKeys::Gamepad_RightTriggerAxis);
	const bool bPad = bAny || FMath::Abs(Lx) + FMath::Abs(Ly) + FMath::Abs(Rx) + FMath::Abs(Ry) + Lt + Rt > 0.01 || In.gp.connected;
	In.SetPad(bPad, Lx, Ly, Rx, Ry, Lt, Rt, B);
	(void)Dt;
}
