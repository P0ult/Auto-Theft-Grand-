// See ATGAssets.h. The editor half reads the downloads (ambientCG texture sets as JPGs, Poly Haven models as
// glTF) and saves them as textures, material instances and static meshes; the runtime half only loads them.
#include "Game/ATGAssets.h"
#include "AutoTheftGrand.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "Game/ATGMaterials.h"
#include "HAL/FileManager.h"
#include "ImageCore.h"
#include "ImageUtils.h"
#include "Materials/MaterialInstanceConstant.h"
#include "MeshDescription.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "StaticMeshAttributes.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#endif

namespace {
const TCHAR* const TexDir = TEXT("/Game/ATG/Imported/Textures/");
const TCHAR* const PropDir = TEXT("/Game/ATG/Imported/Props/");

FString ObjectPath(const TCHAR* Dir, const FString& Name) { return FString::Printf(TEXT("%s%s.%s"), Dir, *Name, *Name); }

// which downloaded model stands in for which kind of prop, and which of its parts make it (Poly Haven files
// often hold a clean and a worn copy side by side, or a kit of parts): the parts are centred on the ground, and
// the bench is shortened to the game's 1.9 m
struct FPropSpec { const TCHAR* Source; const TCHAR* Type; TArray<const TCHAR*> Nodes; double ScaleX; };
const TArray<FPropSpec>& PropSpecs() {
	static const TArray<FPropSpec> Specs = {
		{ TEXT("fire_hydrant"), TEXT("hydrant"), { TEXT("fire_hydrant"), TEXT("fire_hydrant_cap_01"), TEXT("fire_hydrant_cap_02"), TEXT("fire_hydrant_cap_03"), TEXT("fire_hydrant_chain") }, 1.0 },
		{ TEXT("metal_trash_can"), TEXT("trashcan"), { TEXT("metal_trash_can"), TEXT("metal_trash_can_handle_left"), TEXT("metal_trash_can_handle_right") }, 1.0 },
		{ TEXT("modular_street_seating"), TEXT("bench"), { TEXT("crossbar"), TEXT("legs_double"), TEXT("legs_single"), TEXT("suspended_support_01"), TEXT("back_support_r"),
			TEXT("back_support_l"), TEXT("arm_rest_01"), TEXT("arm_rest_02"), TEXT("seat"), TEXT("seat_back") }, 0.78 },
		{ TEXT("concrete_road_barrier"), TEXT(""), {}, 1.0 },
		{ TEXT("utility_box_01"), TEXT(""), {}, 1.0 },
		{ TEXT("utility_box_02"), TEXT(""), {}, 1.0 },
	};
	return Specs;
}
}

const TCHAR* const ATGAssets::SurfaceSets[] = { TEXT("Asphalt031"), TEXT("Concrete034"), TEXT("PavingStones150"), TEXT("Grass004"), TEXT("Ground037"), TEXT("Bricks105") };
const int32 ATGAssets::NumSurfaceSets = UE_ARRAY_COUNT(ATGAssets::SurfaceSets);

UTexture2D* ATGAssets::Surface(const TCHAR* Set, TCHAR Kind) {
	return LoadObject<UTexture2D>(nullptr, *ObjectPath(TexDir, FString::Printf(TEXT("T_%s_%c"), Set, Kind)), nullptr, LOAD_NoWarn | LOAD_Quiet);
}

bool ATGAssets::HasSurfaces() {
	for (int32 i = 0; i < NumSurfaceSets; i++) for (TCHAR K : { TEXT('C'), TEXT('N'), TEXT('M') }) if (!Surface(SurfaceSets[i], K)) return false;
	return true;
}

UStaticMesh* ATGAssets::PropMesh(const FString& Type) {
	for (const FPropSpec& S : PropSpecs()) {
		if (Type != S.Type) continue;
		return LoadObject<UStaticMesh>(nullptr, *ObjectPath(PropDir, FString(TEXT("SM_")) + S.Source), nullptr, LOAD_NoWarn | LOAD_Quiet);
	}
	return nullptr;
}

#if WITH_EDITOR
namespace {
FString SourceDir() { return FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("SourceAssets")); }

void SaveAsset(UObject* Asset) {
	UPackage* Pkg = Asset->GetOutermost();
	FAssetRegistryModule::AssetCreated(Asset);
	Pkg->MarkPackageDirty();
	const FString File = FPackageName::LongPackageNameToFilename(Pkg->GetName(), FPackageName::GetAssetPackageExtension());
	FSavePackageArgs Args;
	Args.TopLevelFlags = RF_Public | RF_Standalone;
	Args.SaveFlags = SAVE_NoError;
	if (!UPackage::SavePackage(Pkg, Asset, *File, Args)) UE_LOG(LogATG, Warning, TEXT("Could not save %s"), *File);
}

// a new asset, or the old one to fill again
template <class T> T* NewAsset(const TCHAR* Dir, const FString& Name) {
	UPackage* Pkg = CreatePackage(*(FString(Dir) + Name));
	Pkg->FullyLoad();
	if (T* Old = FindObject<T>(Pkg, *Name)) return Old;
	return NewObject<T>(Pkg, FName(*Name), RF_Public | RF_Standalone);
}

bool Exists(const TCHAR* Dir, const FString& Name) {
	return FPackageName::DoesPackageExist(FString(Dir) + Name);
}

enum class ETex : uint8 { Color, Normal, Masks };

UTexture2D* SaveTexture(const FString& Name, const FImage& Img, ETex Kind, bool bFlipGreen) {
	UTexture2D* T = NewAsset<UTexture2D>(TexDir, Name);
	T->Source.Init(Img);
	T->SRGB = Kind == ETex::Color;
	T->CompressionSettings = Kind == ETex::Normal ? TC_Normalmap : Kind == ETex::Masks ? TC_Masks : TC_Default;
	T->LODGroup = Kind == ETex::Normal ? TEXTUREGROUP_WorldNormalMap : TEXTUREGROUP_World;
	T->bFlipGreenChannel = bFlipGreen; // (OpenGL normal maps, as glTF has them; Unreal's are DirectX)
	T->PostEditChange();
	SaveAsset(T);
	UE_LOG(LogATG, Display, TEXT("ATGImport: %s (%dx%d)"), *Name, Img.SizeX, Img.SizeY);
	return T;
}

// a JPG or PNG as 8-bit BGRA; data maps (normals, roughness) are kept as they are, not gamma converted
bool LoadImage(const FString& File, FImage& Out, bool bData) {
	if (!FImageUtils::LoadImage(*File, Out)) { UE_LOG(LogATG, Warning, TEXT("ATGImport: could not read %s"), *File); return false; }
	if (bData) Out.GammaSpace = EGammaSpace::Linear;
	Out.ChangeFormat(ERawImageFormat::BGRA8, bData ? EGammaSpace::Linear : EGammaSpace::sRGB);
	return true;
}

// ---- ambientCG sets: <Id>_2K-JPG_Color / _NormalDX / _Roughness / _AmbientOcclusion / _Displacement
int32 ImportSurfaces(bool bForce) {
	int32 Made = 0;
	for (int32 s = 0; s < ATGAssets::NumSurfaceSets; s++) {
		const FString Id = ATGAssets::SurfaceSets[s];
		const FString Base = SourceDir() / TEXT("ambientcg") / Id / (Id + TEXT("_2K-JPG_"));
		if (!FPaths::FileExists(Base + TEXT("Color.jpg"))) { UE_LOG(LogATG, Warning, TEXT("ATGImport: %s is not in SourceAssets (run node Tools/fetch_assets.mjs)"), *Id); continue; }
		const FString C = TEXT("T_") + Id + TEXT("_C"), N = TEXT("T_") + Id + TEXT("_N"), M = TEXT("T_") + Id + TEXT("_M");
		if (!bForce && Exists(TexDir, C) && Exists(TexDir, N) && Exists(TexDir, M)) continue;
		FImage Img;
		if (LoadImage(Base + TEXT("Color.jpg"), Img, false)) { SaveTexture(C, Img, ETex::Color, false); Made++; }
		if (LoadImage(Base + TEXT("NormalDX.jpg"), Img, true)) { SaveTexture(N, Img, ETex::Normal, false); Made++; }
		// the masks: roughness in red, ambient occlusion in green (white where the set has none), height in blue
		FImage Rough;
		if (!LoadImage(Base + TEXT("Roughness.jpg"), Rough, true)) continue;
		FImage Masks(Rough.SizeX, Rough.SizeY, ERawImageFormat::BGRA8, EGammaSpace::Linear);
		TArrayView64<FColor> Out = Masks.AsBGRA8();
		TArrayView64<const FColor> R = static_cast<const FImage&>(Rough).AsBGRA8();
		FImage Ao, Height;
		const bool bAo = FPaths::FileExists(Base + TEXT("AmbientOcclusion.jpg")) && LoadImage(Base + TEXT("AmbientOcclusion.jpg"), Ao, true) && Ao.SizeX == Rough.SizeX && Ao.SizeY == Rough.SizeY;
		const bool bH = LoadImage(Base + TEXT("Displacement.jpg"), Height, true) && Height.SizeX == Rough.SizeX && Height.SizeY == Rough.SizeY;
		for (int64 i = 0; i < Out.Num(); i++) {
			Out[i] = FColor(R[i].R, bAo ? static_cast<const FImage&>(Ao).AsBGRA8()[i].R : 255, bH ? static_cast<const FImage&>(Height).AsBGRA8()[i].R : 128, 255);
		}
		SaveTexture(M, Masks, ETex::Masks, false);
		Made++;
	}
	return Made;
}

// ---- glTF (the subset Poly Haven writes: one .bin, float positions / normals / uvs, u16 or u32 indices)
struct FGltf {
	TSharedPtr<FJsonObject> J;
	FString Dir;
	TArray<TArray<uint8>> Buffers;

	bool Load(const FString& File) {
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *File)) return false;
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), J) || !J.IsValid()) return false;
		Dir = FPaths::GetPath(File);
		for (const TSharedPtr<FJsonValue>& B : J->GetArrayField(TEXT("buffers"))) {
			TArray<uint8>& Data = Buffers.AddDefaulted_GetRef();
			const FString Uri = B->AsObject()->GetStringField(TEXT("uri"));
			if (Uri.Contains(TEXT("..")) || Uri.Contains(TEXT(":")) || !FFileHelper::LoadFileToArray(Data, *(Dir / Uri))) return false;
		}
		return true;
	}
	TArray<TSharedPtr<FJsonValue>> Arr(const TCHAR* Name) const { return J->HasField(Name) ? J->GetArrayField(Name) : TArray<TSharedPtr<FJsonValue>>(); }
	TSharedPtr<FJsonObject> Obj(const TCHAR* Name, int32 i) const { return Arr(Name)[i]->AsObject(); }
	static double Num(const TSharedPtr<FJsonObject>& O, const TCHAR* F, double Def) { double V = Def; O->TryGetNumberField(F, V); return V; }

	// an accessor's values as doubles, Comps per element
	bool Read(int32 Accessor, int32 Comps, TArray<double>& Out) const {
		const TSharedPtr<FJsonObject> A = Obj(TEXT("accessors"), Accessor);
		const TSharedPtr<FJsonObject> V = Obj(TEXT("bufferViews"), (int32)Num(A, TEXT("bufferView"), 0));
		const TArray<uint8>& Buf = Buffers[(int32)Num(V, TEXT("buffer"), 0)];
		const int32 Type = (int32)Num(A, TEXT("componentType"), 5126), Count = (int32)Num(A, TEXT("count"), 0);
		const int32 Size = Type == 5126 || Type == 5125 ? 4 : Type == 5123 || Type == 5122 ? 2 : 1;
		const int64 Start = (int64)Num(V, TEXT("byteOffset"), 0) + (int64)Num(A, TEXT("byteOffset"), 0);
		const int64 Stride = (int64)Num(V, TEXT("byteStride"), Size * Comps);
		if (Start + Stride * (Count - 1) + Size * Comps > Buf.Num()) return false;
		Out.SetNumUninitialized(Count * Comps);
		for (int32 i = 0; i < Count; i++) for (int32 c = 0; c < Comps; c++) {
			const uint8* P = Buf.GetData() + Start + Stride * i + Size * c;
			double X = 0;
			switch (Type) {
			case 5126: { float F; FMemory::Memcpy(&F, P, 4); X = F; break; }
			case 5125: { uint32 U; FMemory::Memcpy(&U, P, 4); X = U; break; }
			case 5123: { uint16 U; FMemory::Memcpy(&U, P, 2); X = U; break; }
			case 5121: X = *P; break;
			default: return false;
			}
			Out[i * Comps + c] = X;
		}
		return true;
	}
	// a node's own transform (translation, rotation, scale; a matrix is not used by these files)
	FTransform Local(const TSharedPtr<FJsonObject>& N) const {
		FTransform T;
		const TArray<TSharedPtr<FJsonValue>>* V;
		if (N->TryGetArrayField(TEXT("translation"), V) && V->Num() == 3) T.SetTranslation(FVector((*V)[0]->AsNumber(), (*V)[1]->AsNumber(), (*V)[2]->AsNumber()));
		if (N->TryGetArrayField(TEXT("rotation"), V) && V->Num() == 4) T.SetRotation(FQuat((*V)[0]->AsNumber(), (*V)[1]->AsNumber(), (*V)[2]->AsNumber(), (*V)[3]->AsNumber()));
		if (N->TryGetArrayField(TEXT("scale"), V) && V->Num() == 3) T.SetScale3D(FVector((*V)[0]->AsNumber(), (*V)[1]->AsNumber(), (*V)[2]->AsNumber()));
		return T;
	}
	// the image file a texture index points at
	FString ImageOf(int32 Texture) const {
		const int32 Src = (int32)Num(Obj(TEXT("textures"), Texture), TEXT("source"), -1);
		if (Src < 0) return FString();
		const FString Uri = Obj(TEXT("images"), Src)->GetStringField(TEXT("uri"));
		return Uri.Contains(TEXT("..")) || Uri.Contains(TEXT(":")) ? FString() : Dir / Uri;
	}
};

int32 TextureIndex(const TSharedPtr<FJsonObject>& O, const TCHAR* Field) {
	const TSharedPtr<FJsonObject>* T;
	return O.IsValid() && O->TryGetObjectField(Field, T) ? (int32)FGltf::Num(*T, TEXT("index"), -1) : -1;
}

// one image of a model as a texture (shared by the materials that use it)
UTexture2D* ModelTexture(const FString& Source, const FString& File, ETex Kind, bool bForce, TMap<FString, UTexture2D*>& Done) {
	if (File.IsEmpty()) return nullptr;
	if (UTexture2D** T = Done.Find(File)) return *T;
	const FString Name = TEXT("T_") + Source + TEXT("_") + FPaths::GetBaseFilename(File).Replace(TEXT("."), TEXT("_"));
	UTexture2D* T = nullptr;
	if (!bForce && Exists(TexDir, Name)) T = LoadObject<UTexture2D>(nullptr, *ObjectPath(TexDir, Name));
	FImage Img;
	if (!T && LoadImage(File, Img, Kind != ETex::Color)) T = SaveTexture(Name, Img, Kind, Kind == ETex::Normal);
	Done.Add(File, T);
	return T;
}

int32 ImportModel(const FPropSpec& Spec, bool bForce) {
	const FString Name = FString(TEXT("SM_")) + Spec.Source;
	if (!bForce && Exists(PropDir, Name)) return 0;
	const FString File = SourceDir() / TEXT("polyhaven") / Spec.Source / (FString(Spec.Source) + TEXT(".gltf"));
	FGltf G;
	if (!FPaths::FileExists(File)) { UE_LOG(LogATG, Warning, TEXT("ATGImport: %s is not in SourceAssets"), Spec.Source); return 0; }
	if (!G.Load(File)) { UE_LOG(LogATG, Warning, TEXT("ATGImport: could not read %s"), *File); return 0; }

	// the chosen nodes with their world transforms (in glTF's axes and metres)
	TArray<TPair<int32, FTransform>> Picked;
	TFunction<void(int32, const FTransform&)> Walk = [&](int32 Ni, const FTransform& Parent) {
		const TSharedPtr<FJsonObject> N = G.Obj(TEXT("nodes"), Ni);
		const FTransform W = G.Local(N) * Parent;
		const FString NodeName = N->HasField(TEXT("name")) ? N->GetStringField(TEXT("name")) : FString();
		const bool bWant = Spec.Nodes.Num() == 0 || Spec.Nodes.ContainsByPredicate([&](const TCHAR* S) { return NodeName == S; });
		if (bWant && N->HasField(TEXT("mesh"))) Picked.Add({ (int32)N->GetNumberField(TEXT("mesh")), W });
		const TArray<TSharedPtr<FJsonValue>>* Kids;
		if (N->TryGetArrayField(TEXT("children"), Kids)) for (const TSharedPtr<FJsonValue>& K : *Kids) Walk((int32)K->AsNumber(), W);
	};
	const TSharedPtr<FJsonObject> Scene = G.Obj(TEXT("scenes"), (int32)FGltf::Num(G.J, TEXT("scene"), 0));
	for (const TSharedPtr<FJsonValue>& R : Scene->GetArrayField(TEXT("nodes"))) Walk((int32)R->AsNumber(), FTransform::Identity);
	if (Picked.Num() == 0) { UE_LOG(LogATG, Warning, TEXT("ATGImport: no parts picked from %s"), Spec.Source); return 0; }

	// gather the triangles per material
	struct FVert { FVector P, N; FVector2D UV; };
	TMap<int32, TArray<FVert>> ByMat;
	FBox Bounds(ForceInit);
	for (const TPair<int32, FTransform>& Pk : Picked) {
		for (const TSharedPtr<FJsonValue>& PrimV : G.Obj(TEXT("meshes"), Pk.Key)->GetArrayField(TEXT("primitives"))) {
			const TSharedPtr<FJsonObject> Prim = PrimV->AsObject();
			if ((int32)FGltf::Num(Prim, TEXT("mode"), 4) != 4) continue;
			const TSharedPtr<FJsonObject> At = Prim->GetObjectField(TEXT("attributes"));
			TArray<double> Pos, Nrm, Tex, Idx;
			if (!G.Read((int32)FGltf::Num(At, TEXT("POSITION"), -1), 3, Pos)) continue;
			const bool bN = At->HasField(TEXT("NORMAL")) && G.Read((int32)At->GetNumberField(TEXT("NORMAL")), 3, Nrm);
			const bool bT = At->HasField(TEXT("TEXCOORD_0")) && G.Read((int32)At->GetNumberField(TEXT("TEXCOORD_0")), 2, Tex);
			const int32 Count = Pos.Num() / 3;
			if (Prim->HasField(TEXT("indices"))) { if (!G.Read((int32)Prim->GetNumberField(TEXT("indices")), 1, Idx)) continue; }
			else { Idx.SetNum(Count); for (int32 i = 0; i < Count; i++) Idx[i] = i; }
			TArray<FVert>& Out = ByMat.FindOrAdd((int32)FGltf::Num(Prim, TEXT("material"), -1));
			for (double Iv : Idx) {
				const int32 i = (int32)Iv;
				if (i < 0 || i >= Count) continue;
				FVert V;
				V.P = Pk.Value.TransformPosition(FVector(Pos[i * 3], Pos[i * 3 + 1], Pos[i * 3 + 2]));
				V.N = bN ? Pk.Value.TransformVectorNoScale(FVector(Nrm[i * 3], Nrm[i * 3 + 1], Nrm[i * 3 + 2])).GetSafeNormal() : FVector::UpVector;
				V.UV = bT ? FVector2D(Tex[i * 2], Tex[i * 2 + 1]) : FVector2D::ZeroVector;
				Bounds += V.P;
				Out.Add(V);
			}
		}
	}
	// centred on the ground: x and z about the middle, the lowest point at y = 0
	const FVector Shift(-Bounds.GetCenter().X, -Bounds.Min.Y, -Bounds.GetCenter().Z);

	// the materials: the Prop material with this model's colour, masks (ambient occlusion, roughness, metal)
	// and normal map
	TMap<FString, UTexture2D*> Textures;
	UMaterialInterface* Parent = ATGMaterials::Get(EATGMat::Prop);
	UStaticMesh* SM = NewAsset<UStaticMesh>(PropDir, Name);
	SM->GetStaticMaterials().Reset();
	TArray<int32> MatKeys;
	ByMat.GetKeys(MatKeys);
	MatKeys.Sort();
	for (int32 k = 0; k < MatKeys.Num(); k++) {
		const int32 Mi = MatKeys[k];
		UMaterialInterface* Use = Parent;
		if (Mi >= 0) {
			const TSharedPtr<FJsonObject> Mat = G.Obj(TEXT("materials"), Mi);
			const TSharedPtr<FJsonObject>* Pbr = nullptr;
			Mat->TryGetObjectField(TEXT("pbrMetallicRoughness"), Pbr);
			UTexture2D* TC = Pbr ? ModelTexture(Spec.Source, G.ImageOf(TextureIndex(*Pbr, TEXT("baseColorTexture"))), ETex::Color, bForce, Textures) : nullptr;
			UTexture2D* TM = Pbr ? ModelTexture(Spec.Source, G.ImageOf(TextureIndex(*Pbr, TEXT("metallicRoughnessTexture"))), ETex::Masks, bForce, Textures) : nullptr;
			UTexture2D* TN = ModelTexture(Spec.Source, G.ImageOf(TextureIndex(Mat, TEXT("normalTexture"))), ETex::Normal, bForce, Textures);
			UMaterialInstanceConstant* MI = NewAsset<UMaterialInstanceConstant>(PropDir, FString::Printf(TEXT("MI_%s_%d"), Spec.Source, k));
			MI->SetParentEditorOnly(Parent);
			if (TC) MI->SetTextureParameterValueEditorOnly(FMaterialParameterInfo(TEXT("Color")), TC);
			if (TM) MI->SetTextureParameterValueEditorOnly(FMaterialParameterInfo(TEXT("Masks")), TM);
			if (TN) MI->SetTextureParameterValueEditorOnly(FMaterialParameterInfo(TEXT("Normal")), TN);
			MI->PostEditChange();
			SaveAsset(MI);
			Use = MI;
		}
		SM->GetStaticMaterials().Add(FStaticMaterial(Use, FName(*FString::Printf(TEXT("Slot%d"), k)), FName(*FString::Printf(TEXT("Slot%d"), k))));
	}

	// the mesh, in the props' axes (x, y, z -> X = x, Y = z, Z = y, centimetres: EATGAxes::World)
	FMeshDescription MD;
	FStaticMeshAttributes A(MD);
	A.Register();
	TVertexAttributesRef<FVector3f> Positions = A.GetVertexPositions();
	TVertexInstanceAttributesRef<FVector3f> Normals = A.GetVertexInstanceNormals();
	TVertexInstanceAttributesRef<FVector2f> UVs = A.GetVertexInstanceUVs();
	TPolygonGroupAttributesRef<FName> Slots = A.GetPolygonGroupMaterialSlotNames();
	auto ToUE = [&](const FVector& P) { return FVector3f((float)(P.X * Spec.ScaleX * 100), (float)(P.Z * 100), (float)(P.Y * 100)); };
	for (int32 k = 0; k < MatKeys.Num(); k++) {
		const TArray<FVert>& Vs = ByMat[MatKeys[k]];
		const FPolygonGroupID Grp = MD.CreatePolygonGroup();
		Slots[Grp] = FName(*FString::Printf(TEXT("Slot%d"), k));
		for (int32 t = 0; t + 2 < Vs.Num(); t += 3) {
			FVertexInstanceID Tri[3];
			for (int32 c = 0; c < 3; c++) {
				const FVert& V = Vs[t + c];
				const FVertexID Vx = MD.CreateVertex();
				Positions[Vx] = ToUE(V.P + Shift);
				Tri[c] = MD.CreateVertexInstance(Vx);
				Normals[Tri[c]] = FVector3f((float)V.N.X / (float)Spec.ScaleX, (float)V.N.Z, (float)V.N.Y).GetSafeNormal();
				UVs.Set(Tri[c], 0, FVector2f(V.UV));
			}
			MD.CreateTriangle(Grp, TArrayView<const FVertexInstanceID>(Tri, 3));
		}
	}
	SM->SetNumSourceModels(1);
	FStaticMeshSourceModel& Src = SM->GetSourceModel(0);
	Src.BuildSettings.bRecomputeNormals = false;
	Src.BuildSettings.bRecomputeTangents = true;
	Src.BuildSettings.bUseMikkTSpace = true;
	Src.BuildSettings.bGenerateLightmapUVs = false;
	Src.BuildSettings.bRemoveDegenerates = true;
	SM->CreateMeshDescription(0, MoveTemp(MD));
	SM->CommitMeshDescription(0);
	SM->Build(true);
	SM->PostEditChange();
	SaveAsset(SM);
	UE_LOG(LogATG, Display, TEXT("ATGImport: %s (%d materials, %s)"), *Name, MatKeys.Num(), *(Bounds.GetSize() * FVector(Spec.ScaleX, 1, 1)).ToString());
	return 1 + Textures.Num() + MatKeys.Num();
}
}

FLinearColor ATGAssets::Mean(UTexture2D* Tex) {
	FImage Img;
	if (!Tex || !Tex->Source.IsValid() || !Tex->Source.GetMipImage(Img, 0)) return FLinearColor::White;
	if (!Tex->SRGB) Img.GammaSpace = EGammaSpace::Linear;
	Img.ChangeFormat(ERawImageFormat::RGBA32F, EGammaSpace::Linear);
	FLinearColor Sum(0, 0, 0, 0);
	for (const FLinearColor& C : Img.AsRGBA32F()) Sum += C;
	return Sum / (float)FMath::Max<int64>(1, Img.GetNumPixels());
}

int32 ATGAssets::ImportAll(bool bForce) {
	if (!IFileManager::Get().DirectoryExists(*SourceDir())) {
		UE_LOG(LogATG, Display, TEXT("ATGImport: no SourceAssets folder (run node Tools/fetch_assets.mjs first)"));
		return 0;
	}
	int32 Made = ImportSurfaces(bForce);
	for (const FPropSpec& S : PropSpecs()) Made += ImportModel(S, bForce);
	return Made;
}
#endif
