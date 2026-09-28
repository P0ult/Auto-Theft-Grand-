// A drivable car (port of src/entities/vehicle.js): the same bicycle-model tyre physics with slip angles,
// weight transfer, traction circles and the handbrake, run in the game's own axes (metres, y up) and
// shown through the actor's transform. Ground contact comes from line traces at the four wheels; walls
// from short rays out to the body's outline; street furniture, trunks and other cars are resolved the way
// the browser game did (circles and oriented boxes).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "ATGCar.generated.h"

class UBoxComponent;
class USpotLightComponent;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;
class AATGCharacter;
class AATGWorld;
namespace atg { struct CarDef; }

UCLASS()
class AATGCar : public APawn {
	GENERATED_BODY()
public:
	AATGCar();

	// spawn a car of a given type (vehicledefs id) at a spot in game coordinates; Color 0 = the def's own pick
	static AATGCar* SpawnCar(UWorld* World, const FString& Type, double X, double Z, double Yaw, uint32 Color = 0, bool bParked = true);
	static const TArray<AATGCar*>& All() { return Cars; }

	virtual void Tick(float Dt) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	// ---- driver input (from the player controller)
	float Throttle = 0, Brake = 0, Steer = 0;
	bool bHandbrake = false;

	// ---- occupants
	AATGCharacter* Driver = nullptr;
	void PutIn(AATGCharacter* Who);
	void TakeOut(AATGCharacter* Who);
	FVector DoorWorld() const;           // where you stand to get in (game coordinates)
	FVector SeatLocal() const;           // driver's hips (game coordinates, car frame)

	// ---- state in game coordinates
	FString Type;
	const atg::CarDef* Def = nullptr;
	double X = 0, Y = 0, Z = 0, Yaw = 0;
	double VelX = 0, VelZ = 0, Vy = 0, R = 0;
	double SteerAngle = 0;
	double Health = 1000;
	bool bParked = true, bAirborne = false, bSunk = false;
	double Speed() const { return VelX * FMath::Sin(Yaw) + VelZ * FMath::Cos(Yaw); }
	double SpeedAbs() const { return FMath::Sqrt(VelX * VelX + VelZ * VelZ); }
	double HalfW() const;
	double HalfL() const;
	double CamDist() const;
	double CamHeight() const;

	void Damage(double Amount);

private:
	static TArray<AATGCar*> Cars;
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> Pivot;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UBoxComponent> Box;
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> Body;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Paint;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Trim;
	UPROPERTY(VisibleAnywhere) TArray<TObjectPtr<USceneComponent>> WheelPivots;
	UPROPERTY(VisibleAnywhere) TArray<TObjectPtr<UStaticMeshComponent>> Wheels;
	UPROPERTY(VisibleAnywhere) TObjectPtr<USpotLightComponent> HeadLight;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> PaintMat;

	void Setup(const FString& InType, uint32 Color);
	void Step(double H);
	void AfterPhysics(double Dt);
	void ResolveContact(double Nx, double Nz, double Depth, double Px, double Pz, bool bSoft);
	void UpdateVisual(double Dt);
	double Surface(double Lx, double Lz, double YRef) const;

	AATGWorld* World = nullptr;
	double Mass = 1500, Inertia = 1, A = 1.4, B = 1.4;
	double AxLong = 0, AyLat = 0, RearGrip = 1;
	double BodyPitch = 0, BodyRoll = 0, BodyPitchV = 0, BodyRollV = 0, BodyY = 0, BodyYV = 0;
	double GroundVy = 0, GroundPitch = 0, GroundRoll = 0;
	double WheelRot = 0, SlipRear = 0, Wheelspin = 0, SurfaceGrip = 1;
	double LastHit = -10, Time = 0, Idle = 0;
	double SeatY = 0.5, SeatZ = 0, SeatX = 0.38, DoorX = 1.5, DoorZ = 0, CgH = 0.6;
};
