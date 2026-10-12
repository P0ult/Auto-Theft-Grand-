#include "Game/ATGMeshComponent.h"
#include "DynamicMeshBuilder.h"
#include "Engine/Engine.h"
#include "Materials/Material.h"
#include "Materials/MaterialRenderProxy.h"
#include "Misc/CoreDelegates.h"
#include "PrimitiveSceneProxy.h"
#include "PrimitiveUniformShaderParametersBuilder.h"
#include "PrimitiveViewRelevance.h"
#include "RayTracingInstance.h"
#include "RenderUtils.h"
#include "SceneInterface.h"
#include "SceneManagement.h"
#include "SceneView.h"
#include "StaticMeshResources.h"

namespace {

struct FATGMeshSection {
	UMaterialInterface* Material = nullptr;
	FStaticMeshVertexBuffers VertexBuffers;
	FDynamicMeshIndexBuffer32 IndexBuffer;
	FLocalVertexFactory VertexFactory;
	bool bVisible = true;
#if RHI_RAYTRACING
	FRayTracingGeometry RayTracingGeometry;
#endif
	explicit FATGMeshSection(ERHIFeatureLevel::Type Level) : VertexFactory(Level, "FATGMeshSection") {}

	// (render thread only)
	void Release() {
		VertexBuffers.PositionVertexBuffer.ReleaseResource();
		VertexBuffers.StaticMeshVertexBuffer.ReleaseResource();
		VertexBuffers.ColorVertexBuffer.ReleaseResource();
		IndexBuffer.ReleaseResource();
		VertexFactory.ReleaseResource();
#if RHI_RAYTRACING
		RayTracingGeometry.ReleaseResource();
#endif
	}
};

// Sections whose proxy has gone, waiting for the render thread's end of frame
FCriticalSection GGraveLock;
TArray<FATGMeshSection*> GGrave;

void DrainGrave() {
	TArray<FATGMeshSection*> Dead;
	{
		FScopeLock Lock(&GGraveLock);
		Dead = MoveTemp(GGrave);
		GGrave.Reset();
	}
	for (FATGMeshSection* S : Dead) {
		S->Release();
		delete S;
	}
}

void HookGrave() {
	static bool bHooked = false;
	if (bHooked) return;
	bHooked = true;
	FCoreDelegates::OnEndFrameRT.AddStatic(&DrainGrave);
}

class FATGMeshProxy final : public FPrimitiveSceneProxy {
public:
	FATGMeshProxy(UATGMeshComponent* C)
		: FPrimitiveSceneProxy(C)
		, MaterialRelevance(C->GetMaterialRelevance(GetScene().GetShaderPlatform()))
		, bRayTraced(C->bRayTraced) {
		const int32 Num = C->GetNumSections();
		Sections.AddZeroed(Num);
		for (int32 Si = 0; Si < Num; Si++) {
			const FProcMeshSection* Src = C->GetProcMeshSection(Si);
			if (!Src || Src->ProcIndexBuffer.Num() == 0 || Src->ProcVertexBuffer.Num() == 0) continue;
			FATGMeshSection* S = new FATGMeshSection(GetScene().GetFeatureLevel());
			TArray<FDynamicMeshVertex> Verts;
			Verts.SetNumUninitialized(Src->ProcVertexBuffer.Num());
			for (int32 Vi = 0; Vi < Verts.Num(); Vi++) {
				const FProcMeshVertex& P = Src->ProcVertexBuffer[Vi];
				FDynamicMeshVertex& V = Verts[Vi];
				V.Position = (FVector3f)P.Position;
				V.Color = P.Color;
				V.TextureCoordinate[0] = FVector2f(P.UV0);
				V.TextureCoordinate[1] = FVector2f(P.UV1);
				V.TextureCoordinate[2] = FVector2f(P.UV2);
				V.TextureCoordinate[3] = FVector2f(P.UV3);
				V.TangentX = P.Tangent.TangentX;
				V.TangentZ = P.Normal;
				V.TangentZ.Vector.W = P.Tangent.bFlipTangentY ? -127 : 127;
			}
			S->IndexBuffer.Indices = Src->ProcIndexBuffer;
			S->VertexBuffers.InitFromDynamicVertex(&S->VertexFactory, Verts, 4);
			BeginInitResource(&S->VertexBuffers.PositionVertexBuffer);
			BeginInitResource(&S->VertexBuffers.StaticMeshVertexBuffer);
			BeginInitResource(&S->VertexBuffers.ColorVertexBuffer);
			BeginInitResource(&S->IndexBuffer);
			BeginInitResource(&S->VertexFactory);
			S->Material = C->GetMaterial(Si);
			if (!S->Material) S->Material = UMaterial::GetDefaultMaterial(MD_Surface);
			S->bVisible = Src->bSectionVisible;
			Sections[Si] = S;
#if RHI_RAYTRACING
			if (bRayTraced && IsRayTracingEnabled()) {
				ENQUEUE_RENDER_COMMAND(ATGMeshInitRayTracing)([S, Name = C->GetFName()](FRHICommandListImmediate& RHICmdList) {
					FRayTracingGeometryInitializer Init;
					Init.DebugName = Name;
					Init.IndexBuffer = S->IndexBuffer.IndexBufferRHI;
					Init.TotalPrimitiveCount = S->IndexBuffer.Indices.Num() / 3;
					Init.GeometryType = RTGT_Triangles;
					Init.bFastBuild = true;
					Init.bAllowUpdate = false;
					FRayTracingGeometrySegment Seg;
					Seg.VertexBuffer = S->VertexBuffers.PositionVertexBuffer.VertexBufferRHI;
					Seg.MaxVertices = S->VertexBuffers.PositionVertexBuffer.GetNumVertices();
					Seg.NumPrimitives = Init.TotalPrimitiveCount;
					Init.Segments.Add(Seg);
					S->RayTracingGeometry.SetInitializer(Init);
					S->RayTracingGeometry.InitResource(RHICmdList);
				});
			}
#endif
		}
		HookGrave();
	}

	virtual ~FATGMeshProxy() {
		// (this can run on a background task: the render thread frees the sections at the end of the frame)
		FScopeLock Lock(&GGraveLock);
		for (FATGMeshSection* S : Sections) if (S) GGrave.Add(S);
	}

	virtual SIZE_T GetTypeHash() const override {
		static size_t UniquePointer;
		return reinterpret_cast<size_t>(&UniquePointer);
	}

	virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const override {
		const bool bWireframe = AllowDebugViewmodes() && ViewFamily.EngineShowFlags.Wireframe;
		FColoredMaterialRenderProxy* Wire = nullptr;
		if (bWireframe) {
			Wire = new FColoredMaterialRenderProxy(GEngine->WireframeMaterial ? GEngine->WireframeMaterial->GetRenderProxy() : nullptr, FLinearColor(0, 0.5f, 1.f));
			Collector.RegisterOneFrameMaterialProxy(Wire);
		}
		for (const FATGMeshSection* S : Sections) {
			if (!S || !S->bVisible) continue;
			FMaterialRenderProxy* Mat = bWireframe ? Wire : S->Material->GetRenderProxy();
			for (int32 Vi = 0; Vi < Views.Num(); Vi++) {
				if (!(VisibilityMap & (1 << Vi))) continue;
				FMeshBatch& Mesh = Collector.AllocateMesh();
				FMeshBatchElement& E = Mesh.Elements[0];
				E.IndexBuffer = &S->IndexBuffer;
				Mesh.bWireframe = bWireframe;
				Mesh.VertexFactory = &S->VertexFactory;
				Mesh.MaterialRenderProxy = Mat;
				FDynamicPrimitiveUniformBuffer& Ub = Collector.AllocateOneFrameResource<FDynamicPrimitiveUniformBuffer>();
				FPrimitiveUniformShaderParametersBuilder Builder;
				BuildUniformShaderParameters(Builder);
				Ub.Set(Collector.GetRHICommandList(), Builder);
				E.PrimitiveUniformBufferResource = &Ub.UniformBuffer;
				E.FirstIndex = 0;
				E.NumPrimitives = S->IndexBuffer.Indices.Num() / 3;
				E.MinVertexIndex = 0;
				E.MaxVertexIndex = S->VertexBuffers.PositionVertexBuffer.GetNumVertices() - 1;
				Mesh.ReverseCulling = IsLocalToWorldDeterminantNegative();
				Mesh.Type = PT_TriangleList;
				Mesh.DepthPriorityGroup = SDPG_World;
				Mesh.bCanApplyViewModeOverrides = false;
				Collector.AddMesh(Vi, Mesh);
			}
		}
	}

	virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override {
		FPrimitiveViewRelevance R;
		R.bDrawRelevance = IsShown(View);
		R.bShadowRelevance = IsShadowCast(View);
		R.bDynamicRelevance = true;
		R.bRenderInMainPass = ShouldRenderInMainPass();
		R.bUsesLightingChannels = GetLightingChannelMask() != GetDefaultLightingChannelMask();
		R.bRenderCustomDepth = ShouldRenderCustomDepth();
		R.bTranslucentSelfShadow = bCastVolumetricTranslucentShadow;
		MaterialRelevance.SetPrimitiveViewRelevance(R);
		R.bVelocityRelevance = DrawsVelocity() && R.bOpaque && R.bRenderInMainPass;
		return R;
	}

	virtual bool CanBeOccluded() const override { return !MaterialRelevance.bDisableDepthTest; }
	virtual uint32 GetMemoryFootprint() const override { return sizeof(*this) + FPrimitiveSceneProxy::GetAllocatedSize(); }

#if RHI_RAYTRACING
	virtual bool IsRayTracingRelevant() const override { return bRayTraced; }
	virtual bool HasRayTracingRepresentation() const override { return bRayTraced; }

	virtual void GetDynamicRayTracingInstances(FRayTracingInstanceCollector& Collector) override final {
		if (!bRayTraced) return;
		TConstArrayView<const FSceneView*> Views = Collector.GetViews();
		const uint32 VisibilityMap = Collector.GetVisibilityMap();
		const int32 First = FMath::CountTrailingZeros(VisibilityMap);
		if (!Views.IsValidIndex(First)) return;
		const FSceneView* FirstView = Views[First];
		for (const FATGMeshSection* S : Sections) {
			if (!S || !S->bVisible || !S->RayTracingGeometry.IsValid()) continue;
			FRayTracingInstance Inst;
			Inst.Geometry = &S->RayTracingGeometry;
			Inst.InstanceTransforms.Add(GetLocalToWorld());
			FMeshBatch Mesh;
			Mesh.VertexFactory = &S->VertexFactory;
			Mesh.SegmentIndex = 0;
			Mesh.MaterialRenderProxy = S->Material->GetRenderProxy();
			Mesh.ReverseCulling = IsLocalToWorldDeterminantNegative();
			Mesh.Type = PT_TriangleList;
			Mesh.DepthPriorityGroup = SDPG_World;
			Mesh.bCanApplyViewModeOverrides = false;
			Mesh.CastRayTracedShadow = IsShadowCast(FirstView);
			FMeshBatchElement& E = Mesh.Elements[0];
			E.IndexBuffer = &S->IndexBuffer;
			FDynamicPrimitiveUniformBuffer& Ub = Collector.AllocateOneFrameResource<FDynamicPrimitiveUniformBuffer>();
			FPrimitiveUniformShaderParametersBuilder Builder;
			BuildUniformShaderParameters(Builder);
			Ub.Set(Collector.GetRHICommandList(), Builder);
			E.PrimitiveUniformBufferResource = &Ub.UniformBuffer;
			E.FirstIndex = 0;
			E.NumPrimitives = S->IndexBuffer.Indices.Num() / 3;
			E.MinVertexIndex = 0;
			E.MaxVertexIndex = S->VertexBuffers.PositionVertexBuffer.GetNumVertices() - 1;
			Inst.Materials.Add(Mesh);
			for (int32 Vi = 0; Vi < Views.Num(); Vi++) if (VisibilityMap & (1 << Vi)) Collector.AddRayTracingInstance(Vi, Inst);
		}
	}
#endif

private:
	TArray<FATGMeshSection*> Sections;
	FMaterialRelevance MaterialRelevance;
	bool bRayTraced = true;
};

} // namespace

void ATGMeshGraveyard::Flush() { DrainGrave(); }

FPrimitiveSceneProxy* UATGMeshComponent::CreateSceneProxy() {
	return GetNumSections() > 0 ? new FATGMeshProxy(this) : nullptr;
}
