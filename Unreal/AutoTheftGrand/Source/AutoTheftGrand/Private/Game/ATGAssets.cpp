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

// which downloaded model stands in for which kinds of prop. Name: the asset (SM_Name); File: under SourceAssets
// (glTF or OBJ); Nodes: the glTF parts that make it (Poly Haven files often hold a clean and a worn copy side by
// side, or a kit of parts; none: all of it); ScaleX: stretched across (the bench is shortened to the game's 1.9 m);
// Fit: scaled to this height in metres (0: the file's metres); bCentre: centred on its middle (else the file's
// origin, a palm's trunk); bCutout: holes from the colour's alpha, two-sided (leaves). OBJ files carry no
// materials here: Tex names the colour, normal (OpenGL) and specular images beside them.
struct FPropSpec {
	const TCHAR* Name;
	TArray<const TCHAR*> Types;
	const TCHAR* File;
	TArray<const TCHAR*> Nodes;
	double ScaleX = 1, Fit = 0;
	bool bCentre = true, bCutout = false;
	const TCHAR* Tex[3] = { nullptr, nullptr, nullptr };
};
FPropSpec Ph(const TCHAR* Name, std::initializer_list<const TCHAR*> Types, std::initializer_list<const TCHAR*> Nodes = {}, double ScaleX = 1) {
	FPropSpec S;
	S.Name = Name; S.Types = Types; S.Nodes = Nodes; S.ScaleX = ScaleX;
	S.File = nullptr;
	return S;
}
FPropSpec Palm(const TCHAR* Name, std::initializer_list<const TCHAR*> Types, double Fit) {
	FPropSpec S;
	S.Name = Name; S.Types = Types; S.File = TEXT("opengameart/palm_tree_v2/palm_tree.obj");
	S.Fit = Fit; S.bCentre = false; S.bCutout = true;
	S.Tex[0] = TEXT("diffus.tga"); S.Tex[1] = TEXT("normal.tga"); S.Tex[2] = TEXT("specular.tga");
	return S;
}
const TArray<FPropSpec>& PropSpecs() {
	static const TArray<FPropSpec> Specs = {
		Ph(TEXT("fire_hydrant"), { TEXT("hydrant") }, { TEXT("fire_hydrant"), TEXT("fire_hydrant_cap_01"), TEXT("fire_hydrant_cap_02"), TEXT("fire_hydrant_cap_03"), TEXT("fire_hydrant_chain") }),
		Ph(TEXT("metal_trash_can"), { TEXT("trashcan") }, { TEXT("metal_trash_can"), TEXT("metal_trash_can_handle_left"), TEXT("metal_trash_can_handle_right") }),
		Ph(TEXT("modular_street_seating"), { TEXT("bench") }, { TEXT("crossbar"), TEXT("legs_double"), TEXT("legs_single"), TEXT("suspended_support_01"), TEXT("back_support_r"),
			TEXT("back_support_l"), TEXT("arm_rest_01"), TEXT("arm_rest_02"), TEXT("seat"), TEXT("seat_back") }, 0.78),
		Ph(TEXT("concrete_road_barrier"), {}),
		// the street dressing (ATGDressing)
		Ph(TEXT("utility_box_01"), { TEXT("utilitybox0") }),
		Ph(TEXT("utility_box_02"), { TEXT("utilitybox1") }),
		Ph(TEXT("planter_box_01"), { TEXT("planter0") }),
		Ph(TEXT("planter_box_02"), { TEXT("planter1") }),
		Ph(TEXT("Barrel_01"), { TEXT("barrel0") }),
		Ph(TEXT("Barrel_02"), { TEXT("barrel1") }),
		Ph(TEXT("barrel_03"), { TEXT("barrel2") }),
		Ph(TEXT("wooden_crate_01"), { TEXT("crate0") }),
		Ph(TEXT("wooden_crate_02"), { TEXT("crate1") }),
		Ph(TEXT("cardboard_box_01"), { TEXT("cardboard") }),
		Ph(TEXT("trashbag"), { TEXT("trashbag") }),
		// the palms: the city's three (9, 10.3 and 11.6 m trunks, PalmGeos) and the countryside's
		Palm(TEXT("palm_tree_0"), { TEXT("palm0"), TEXT("palm") }, 10.5),
		Palm(TEXT("palm_tree_1"), { TEXT("palm1") }, 11.8),
		Palm(TEXT("palm_tree_2"), { TEXT("palm2") }, 13.1),
	};
	return Specs;
}
// a Poly Haven model lives at polyhaven/<name>/<name>.gltf
FString SpecFile(const FPropSpec& S) { return S.File ? FString(S.File) : FString::Printf(TEXT("polyhaven/%s/%s.gltf"), S.Name, S.Name); }
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
		if (!S.Types.ContainsByPredicate([&](const TCHAR* T) { return Type == T; })) continue;
		return LoadObject<UStaticMesh>(nullptr, *ObjectPath(PropDir, FString(TEXT("SM_")) + S.Name), nullptr, LOAD_NoWarn | LOAD_Quiet);
	}
	return nullptr;
}

TMap<FString, FVector2D> ATGAssets::DressingSizes(const TArray<FString>& Kinds) {
	TMap<FString, FVector2D> Out;
	for (const FString& K : Kinds) {
		const UStaticMesh* M = PropMesh(K);
		if (!M) continue;
		// (a circle that covers most of the footprint, in metres)
		const FBox B = M->GetBoundingBox();
		Out.Add(K, FVector2D(FMath::Max(B.GetExtent().X, B.GetExtent().Y) * 0.009, B.GetSize().Z * 0.01));
	}
	return Out;
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
	if (Kind == ETex::Masks && FPaths::GetBaseFilename(File).Contains(TEXT("specular"))) {
		// an old-style specular map: occlusion white, roughness from the shine, no metal
		UTexture2D* T = nullptr;
		if (!bForce && Exists(TexDir, Name)) T = LoadObject<UTexture2D>(nullptr, *ObjectPath(TexDir, Name));
		FImage Spec;
		if (!T && LoadImage(File, Spec, true)) {
			for (FColor& C : Spec.AsBGRA8()) C = FColor(255, (uint8)(255 - C.G * 0.6f), 0, 255);
			T = SaveTexture(Name, Spec, ETex::Masks, false);
		}
		Done.Add(File, T);
		return T;
	}
	UTexture2D* T = nullptr;
	if (!bForce && Exists(TexDir, Name)) T = LoadObject<UTexture2D>(nullptr, *ObjectPath(TexDir, Name));
	FImage Img;
	if (!T && LoadImage(File, Img, Kind != ETex::Color)) T = SaveTexture(Name, Img, Kind, Kind == ETex::Normal);
	Done.Add(File, T);
	return T;
}

// what a model file gives: its triangles per material, and per material its colour, masks (occlusion, roughness,
// metal) and normal (OpenGL) images; positions in the file's axes (y up), normals unit length
struct FVert { FVector P, N; FVector2D UV; };
struct FModel { TMap<int32, TArray<FVert>> ByMat; TMap<int32, TArray<FString>> Images; };

bool ReadGltf(const FPropSpec& Spec, const FString& File, FModel& Out) {
	FGltf G;
	if (!G.Load(File)) return false;
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
			const int32 Mi = (int32)FGltf::Num(Prim, TEXT("material"), -1);
			TArray<FVert>& Vs = Out.ByMat.FindOrAdd(Mi);
			for (double Iv : Idx) {
				const int32 i = (int32)Iv;
				if (i < 0 || i >= Count) continue;
				FVert V;
				V.P = Pk.Value.TransformPosition(FVector(Pos[i * 3], Pos[i * 3 + 1], Pos[i * 3 + 2]));
				V.N = bN ? Pk.Value.TransformVectorNoScale(FVector(Nrm[i * 3], Nrm[i * 3 + 1], Nrm[i * 3 + 2])).GetSafeNormal() : FVector::UpVector;
				V.UV = bT ? FVector2D(Tex[i * 2], Tex[i * 2 + 1]) : FVector2D::ZeroVector;
				Vs.Add(V);
			}
			if (Mi >= 0 && !Out.Images.Contains(Mi)) {
				const TSharedPtr<FJsonObject> Mat = G.Obj(TEXT("materials"), Mi);
				const TSharedPtr<FJsonObject>* Pbr = nullptr;
				Mat->TryGetObjectField(TEXT("pbrMetallicRoughness"), Pbr);
				Out.Images.Add(Mi, {
					Pbr ? G.ImageOf(TextureIndex(*Pbr, TEXT("baseColorTexture"))) : FString(),
					Pbr ? G.ImageOf(TextureIndex(*Pbr, TEXT("metallicRoughnessTexture"))) : FString(),
					G.ImageOf(TextureIndex(Mat, TEXT("normalTexture"))) });
			}
		}
	}
	return Out.ByMat.Num() > 0;
}

// a Wavefront OBJ: one material, its images named by the spec (the v of its uvs counts up from the bottom)
bool ReadObj(const FPropSpec& Spec, const FString& File, FModel& Out) {
	TArray<FString> Lines;
	if (!FFileHelper::LoadFileToStringArray(Lines, *File)) return false;
	TArray<FVector> P, N;
	TArray<FVector2D> T;
	TArray<FVert>& Vs = Out.ByMat.FindOrAdd(0);
	for (const FString& Line : Lines) {
		TArray<FString> W;
		Line.ParseIntoArrayWS(W);
		if (W.Num() < 2) continue;
		if (W[0] == TEXT("v") && W.Num() >= 4) P.Add(FVector(FCString::Atod(*W[1]), FCString::Atod(*W[2]), FCString::Atod(*W[3])));
		else if (W[0] == TEXT("vn") && W.Num() >= 4) N.Add(FVector(FCString::Atod(*W[1]), FCString::Atod(*W[2]), FCString::Atod(*W[3])).GetSafeNormal());
		else if (W[0] == TEXT("vt") && W.Num() >= 3) T.Add(FVector2D(FCString::Atod(*W[1]), 1.0 - FCString::Atod(*W[2])));
		else if (W[0] == TEXT("f") && W.Num() >= 4) {
			// v/vt/vn corners (1-based, negative counts back), fanned into triangles
			auto Corner = [&](const FString& C) {
				TArray<FString> Ix;
				C.ParseIntoArray(Ix, TEXT("/"), false);
				auto At = [&](int32 k, int32 Num) { if (!Ix.IsValidIndex(k) || Ix[k].IsEmpty()) return -1; const int32 i = FCString::Atoi(*Ix[k]); return i < 0 ? Num + i : i - 1; };
				FVert V;
				const int32 pi = At(0, P.Num()), ti = At(1, T.Num()), ni = At(2, N.Num());
				V.P = P.IsValidIndex(pi) ? P[pi] : FVector::ZeroVector;
				V.UV = T.IsValidIndex(ti) ? T[ti] : FVector2D::ZeroVector;
				V.N = N.IsValidIndex(ni) ? N[ni] : FVector::UpVector;
				return V;
			};
			const FVert A = Corner(W[1]);
			for (int32 k = 2; k + 1 < W.Num(); k++) { Vs.Add(A); Vs.Add(Corner(W[k])); Vs.Add(Corner(W[k + 1])); }
		}
	}
	const FString Dir = FPaths::GetPath(File);
	auto Img = [&](int32 k) { return Spec.Tex[k] ? Dir / Spec.Tex[k] : FString(); };
	Out.Images.Add(0, { Img(0), Img(2), Img(1) });
	return Vs.Num() > 0;
}

int32 ImportModel(const FPropSpec& Spec, bool bForce) {
	const FString Name = FString(TEXT("SM_")) + Spec.Name;
	if (!bForce && Exists(PropDir, Name)) return 0;
	const FString File = SourceDir() / SpecFile(Spec);
	if (!FPaths::FileExists(File)) { UE_LOG(LogATG, Warning, TEXT("ATGImport: %s is not in SourceAssets (run node Tools/fetch_assets.mjs)"), *SpecFile(Spec)); return 0; }
	FModel Mdl;
	if (!(File.EndsWith(TEXT(".obj")) ? ReadObj(Spec, File, Mdl) : ReadGltf(Spec, File, Mdl))) { UE_LOG(LogATG, Warning, TEXT("ATGImport: could not read %s"), *File); return 0; }

	// on the ground: the lowest point at y = 0, x and z about the middle (or the file's origin), scaled to fit
	FBox Bounds(ForceInit);
	for (const auto& M : Mdl.ByMat) for (const FVert& V : M.Value) Bounds += V.P;
	const double Scale = Spec.Fit > 0 ? Spec.Fit / FMath::Max(Bounds.GetSize().Y, 1e-6) : 1.0;
	const FVector Shift = Spec.bCentre ? FVector(-Bounds.GetCenter().X, -Bounds.Min.Y, -Bounds.GetCenter().Z) : FVector(0, -Bounds.Min.Y, 0);

	// the materials: the Prop material (PropCutout for leaves) with this model's images, its textures named
	// after the folder they came from (the palms share theirs)
	const FString Folder = FPaths::GetCleanFilename(FPaths::GetPath(File));
	TMap<FString, UTexture2D*> Textures;
	UMaterialInterface* Parent = ATGMaterials::Get(Spec.bCutout ? EATGMat::PropCutout : EATGMat::Prop);
	UStaticMesh* SM = NewAsset<UStaticMesh>(PropDir, Name);
	SM->GetStaticMaterials().Reset();
	TArray<int32> MatKeys;
	Mdl.ByMat.GetKeys(MatKeys);
	MatKeys.Sort();
	for (int32 k = 0; k < MatKeys.Num(); k++) {
		UMaterialInterface* Use = Parent;
		if (const TArray<FString>* Im = Mdl.Images.Find(MatKeys[k])) {
			UTexture2D* TC = ModelTexture(Folder, (*Im)[0], ETex::Color, bForce, Textures);
			UTexture2D* TM = ModelTexture(Folder, (*Im)[1], ETex::Masks, bForce, Textures);
			UTexture2D* TN = ModelTexture(Folder, (*Im)[2], ETex::Normal, bForce, Textures);
			UMaterialInstanceConstant* MI = NewAsset<UMaterialInstanceConstant>(PropDir, FString::Printf(TEXT("MI_%s_%d"), Spec.Name, k));
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
	auto ToUE = [&](const FVector& P) { const FVector Q = (P + Shift) * Scale * 100.0; return FVector3f((float)(Q.X * Spec.ScaleX), (float)Q.Z, (float)Q.Y); };
	for (int32 k = 0; k < MatKeys.Num(); k++) {
		const TArray<FVert>& Vs = Mdl.ByMat[MatKeys[k]];
		const FPolygonGroupID Grp = MD.CreatePolygonGroup();
		Slots[Grp] = FName(*FString::Printf(TEXT("Slot%d"), k));
		for (int32 t = 0; t + 2 < Vs.Num(); t += 3) {
			FVertexInstanceID Tri[3];
			for (int32 c = 0; c < 3; c++) {
				const FVert& V = Vs[t + c];
				const FVertexID Vx = MD.CreateVertex();
				Positions[Vx] = ToUE(V.P);
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
	UE_LOG(LogATG, Display, TEXT("ATGImport: %s (%d materials, %s m)"), *Name, MatKeys.Num(), *(Bounds.GetSize() * Scale * FVector(Spec.ScaleX, 1, 1)).ToString());
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
