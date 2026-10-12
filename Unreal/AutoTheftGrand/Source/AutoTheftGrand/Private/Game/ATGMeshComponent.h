// A procedural mesh component with its own scene proxy. It draws the sections the way the engine's procedural mesh
// does, but its proxy never frees GPU resources on the thread that deletes it. Unreal 5.8 deletes scene proxies on a
// background task while the render thread carries on, and the engine's procedural mesh proxy releases its ray tracing
// geometry right there, racing the ray tracing geometry manager (an intermittent crash whenever a filled mesh was
// rebuilt or re-registered). Here the sections go to a graveyard that the render thread empties at the end of each
// frame.
//
// Use it wherever UProceduralMeshComponent was used: CreateMeshSection works as before. SetMeshSectionVisible and
// UpdateMeshSection must not be called (they talk to the engine's proxy type); set the section's bSectionVisible and
// call MarkRenderStateDirty instead.
#pragma once

#include "CoreMinimal.h"
#include "ProceduralMeshComponent.h"
#include "ATGMeshComponent.generated.h"

UCLASS()
class UATGMeshComponent : public UProceduralMeshComponent {
	GENERATED_BODY()
public:
	// false: the mesh stays out of ray tracing (particles, overlays and other meshes rebuilt every frame)
	bool bRayTraced = true;

	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
};

namespace ATGMeshGraveyard {
// Frees the sections of deleted proxies now. The render thread does it at the end of every frame; the game module
// calls this at shutdown, after the render thread has stopped and before the renderer unloads.
void Flush();
}
