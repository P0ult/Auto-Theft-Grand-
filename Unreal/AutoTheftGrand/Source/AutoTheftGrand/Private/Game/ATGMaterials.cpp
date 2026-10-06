// See ATGMaterials.h. The HLSL below mirrors the GLSL in the browser game line for line where it can; the
// custom expressions return float4 (colour + roughness, or emissive / normal + a spare) which is split with
// component masks. No screen-space derivatives are used (they don't exist in ray-tracing shaders): the
// anti-aliasing widths come from the pixel's depth instead.
#include "Game/ATGMaterials.h"
#include "AutoTheftGrand.h"
#include "Gen/RoadLayout.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialParameterCollection.h"
#include "UObject/WeakObjectPtrTemplates.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "Materials/MaterialExpressionCameraVectorWS.h"
#include "Materials/MaterialExpressionCollectionParameter.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionLocalPosition.h"
#include "Materials/MaterialExpressionPerInstanceCustomData.h"
#include "Materials/MaterialExpressionPixelDepth.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionTime.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionVertexNormalWS.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#endif

namespace {
// bump when the generated materials change: new assets are made under new names
constexpr int32 GMatVersion = 3;
const TCHAR* GMatNames[] = { TEXT("Terrain"), TEXT("Road"), TEXT("Street"), TEXT("Ground"), TEXT("Building"), TEXT("VertexLit"), TEXT("Frond"), TEXT("Water"), TEXT("Standard"), TEXT("Glass") };

FString AssetName(const TCHAR* Base) { return FString::Printf(TEXT("M_ATG_%s_%d"), Base, GMatVersion); }
FString MpcName() { return FString::Printf(TEXT("MPC_ATG_%d"), GMatVersion); }
FString ObjectPath(const FString& Name) { return FString::Printf(TEXT("/Game/ATG/Materials/%s.%s"), *Name, *Name); }

// (weak pointers: statics must not own UObjects; the assets are rooted instead)
TWeakObjectPtr<UMaterialInterface> GCache[(int32)EATGMat::Count];
TWeakObjectPtr<UMaterialParameterCollection> GMpc;
template <class T> T* Keep(T* Obj) { if (Obj && !Obj->IsRooted()) Obj->AddToRoot(); return Obj; }
}

// ------------------------------------------------------------------ HLSL
namespace ATGHlsl {
// shared noise (NOISE_GLSL) and helpers, as methods of a local struct (custom code can't declare functions)
static const TCHAR* NoiseBegin = TEXT(R"HLSL(
struct ATGN {
	float h21(float2 p) { float3 p3 = frac(float3(p.x, p.y, p.x) * 0.1031); p3 += dot(p3, p3.yzx + 33.33); return frac((p3.x + p3.y) * p3.z); }
	float vn2(float2 p) { float2 i = floor(p); float2 f = frac(p); float2 u = f * f * (3.0 - 2.0 * f);
		return lerp(lerp(h21(i), h21(i + float2(1.0, 0.0)), u.x), lerp(h21(i + float2(0.0, 1.0)), h21(i + float2(1.0, 1.0)), u.x), u.y); }
	float fbm3(float2 p) { return vn2(p) * 0.5 + vn2(p * 2.1 + 3.1) * 0.3 + vn2(p * 4.3 + 7.7) * 0.2; }
	float gmod(float x, float y) { return x - y * floor(x / y); }
	float stripe(float x, float c, float w, float aa) { return 1.0 - smoothstep(w - aa, w + aa, abs(x - c)); }
	float rectM(float2 f, float2 lo, float2 hi, float aa) { float2 a = smoothstep(lo - aa, lo + aa, f) * (1.0 - smoothstep(hi - aa, hi + aa, f)); return a.x * a.y; }
	float bandG(float x, float c, float w) { return step(abs(x - c), w); }
)HLSL");
static const TCHAR* NoiseEnd = TEXT(R"HLSL(
};
ATGN N;
)HLSL");

// ---- terrain (terrainmesh.js)
static const TCHAR* TerrainFields = TEXT(R"HLSL(
	float3 fields(float2 wp, float n) {
		float a = 0.33; float c = cos(a), s = sin(a);
		float2 q = float2(c * wp.x + s * wp.y, -s * wp.x + c * wp.y);
		float2 size = float2(130.0, 85.0);
		float2 cell = floor(q / size);
		float2 f = frac(q / size) * size;
		float hh = h21(cell * 1.37 + 3.1);
		float edge = min(min(f.x, size.x - f.x), min(f.y, size.y - f.y));
		float rows = 0.5 + 0.5 * sin((hh > 0.5 ? f.x : f.y) * 6.2831 / 1.7);
		float3 col;
		if (hh < 0.28) col = lerp(float3(0.62, 0.5, 0.22), float3(0.78, 0.65, 0.3), rows * 0.6 + n * 0.3);
		else if (hh < 0.5) col = lerp(float3(0.14, 0.26, 0.07), float3(0.24, 0.38, 0.1), rows);
		else if (hh < 0.66) col = lerp(float3(0.3, 0.21, 0.14), float3(0.4, 0.3, 0.2), rows);
		else if (hh < 0.82) col = lerp(float3(0.34, 0.42, 0.16), float3(0.28, 0.34, 0.12), n);
		else col = lerp(float3(0.5, 0.52, 0.2), float3(0.18, 0.4, 0.12), rows * 0.8);
		col = lerp(float3(0.42, 0.34, 0.24), col, smoothstep(1.2, 3.0, edge));
		return col * (0.88 + 0.24 * n);
	}
)HLSL");
static const TCHAR* TerrainBody = TEXT(R"HLSL(
float3 W = LP / 100.0;
float2 wp = W.xy;
float h = W.z;
float n = N.fbm3(wp * 0.05);
float n2 = N.vn2(wp * 0.9);
float n3 = N.fbm3(wp * 0.35);
float farm = max(uv1.y, 0.0), urb = saturate(-uv1.y), sand = uv2.x, forest = uv2.y, beach = uv3.x;
float3 base = float3(uv0.x, uv0.y, uv1.x);
float3 grass = lerp(float3(0.26, 0.3, 0.11), float3(0.46, 0.39, 0.18), smoothstep(0.35, 0.7, n)) * (0.8 + 0.35 * n3) * (0.9 + 0.2 * n2);
grass = lerp(grass, base, 0.35);
float3 col = grass;
float3 ff = lerp(float3(0.16, 0.2, 0.09), float3(0.24, 0.2, 0.12), smoothstep(0.4, 0.7, n3)) * (0.85 + 0.3 * n2);
col = lerp(col, ff, forest);
float rip = sin(wp.x * 0.9 + N.vn2(wp * 0.05) * 9.0) * 0.5 + 0.5;
float3 ds = lerp(float3(0.5, 0.34, 0.2), float3(0.6, 0.44, 0.28), smoothstep(0.3, 0.7, n)) * (0.92 + 0.08 * rip) * (0.9 + 0.15 * n3);
ds *= 1.0 - smoothstep(0.82, 0.9, N.vn2(wp * 3.0)) * 0.3;
col = lerp(col, ds, sand);
if (farm > 0.01) col = lerp(col, N.fields(wp, n3), smoothstep(0.1, 0.6, farm));
float3 bs = lerp(float3(0.66, 0.56, 0.4), float3(0.5, 0.42, 0.3), smoothstep(1.2, 0.2, h));
col = lerp(col, bs * (0.92 + 0.12 * n3), beach);
if (urb > 0.01) {
	float2 sl = frac(wp / float2(3.1, 2.3));
	float joint = max(smoothstep(0.035, 0.0, min(sl.x, 1.0 - sl.x)), smoothstep(0.05, 0.0, min(sl.y, 1.0 - sl.y)));
	float3 pv = lerp(float3(0.3, 0.295, 0.28), float3(0.38, 0.37, 0.345), smoothstep(0.3, 0.7, n3)) * (0.88 + 0.2 * n2);
	pv = lerp(pv, float3(0.2, 0.195, 0.185), joint * 0.5);
	pv = lerp(pv, float3(0.33, 0.36, 0.2), smoothstep(0.62, 0.8, n) * 0.4);
	col = lerp(col, pv, urb);
}
float3 wn = normalize(float3(NRM.x, NRM.z, NRM.y));
float steep = smoothstep(0.78, 0.55, wn.y);
float strata = 0.5 + 0.5 * sin(h * 1.3 + n * 4.0);
float3 rock = lerp(float3(0.38, 0.35, 0.31), float3(0.62, 0.36, 0.22) * (0.8 + 0.3 * strata), sand);
rock *= 0.72 + 0.45 * n3;
col = lerp(col, rock, steep);
col = lerp(col, float3(0.5, 0.48, 0.45) * (0.8 + 0.3 * n3), smoothstep(470.0, 620.0, h) * (1.0 - steep) * 0.7);
float rough = lerp(0.96, 0.9, sand);
return float4(col, rough);
)HLSL");

// ---- road network (roadmesh.js ROADNET_EXT)
static const TCHAR* RoadBody = TEXT(R"HLSL(
float3 W = LP / 100.0;
float2 wp = W.xy;
float type = floor(uv2.x + 0.5);
float u = uv0.x, v = uv0.y;
float wMax = uv3.x;
float n = N.fbm3(wp * 0.25);
float fine = N.vn2(wp * 6.0);
float aa = PD / 100.0 * 0.0011 + 0.01;
float3 col;
float rough = 0.9;
float paintW = 0.0, paintY = 0.0;
if (type > 6.5 && type < 7.5) {
	float stone = N.vn2(wp * 9.0) * 0.6 + N.vn2(wp * 23.0) * 0.4;
	col = lerp(float3(0.27, 0.26, 0.24), float3(0.42, 0.4, 0.37), stone) * (0.8 + 0.3 * n);
	col = lerp(col, col * float3(0.55, 0.5, 0.45), smoothstep(0.95, 0.55, abs(u)) * 0.6);
	col = lerp(col, float3(0.36, 0.3, 0.22) * (0.8 + 0.3 * n), smoothstep(wMax - 1.2, wMax, abs(u)) * 0.7);
	rough = 1.0;
} else if (type > 4.5 && type < 5.5) {
	col = float3(0.42, 0.33, 0.22) * (0.75 + 0.35 * n) * (0.85 + 0.25 * fine);
	float rut = N.stripe(abs(u), 1.0, 0.35, 0.3) * 0.25;
	col *= 1.0 - rut;
	col = lerp(col, float3(0.33, 0.38, 0.2) * (0.8 + 0.4 * n), smoothstep(0.55, 0.0, abs(u)) * 0.5 * smoothstep(0.4, 0.7, N.vn2(wp * 0.7)));
	float edgeFade = smoothstep(wMax - 0.9, wMax, abs(u));
	col = lerp(col, float3(0.4, 0.36, 0.26), edgeFade * 0.6);
	rough = 1.0;
} else {
	col = float3(0.09, 0.09, 0.095) * (0.72 + 0.45 * n) * (0.9 + 0.2 * fine);
	if (type > 5.5 && type < 6.5) {
		col *= 0.97;
	} else if (type > 7.5) {
		paintY += N.stripe(u, 0.15, 0.07, aa) + N.stripe(u, -0.15, 0.07, aa);
		paintW += N.stripe(abs(u), 3.55, 0.07, aa) * step(0.55, frac(v / 9.0));
		paintW += N.stripe(abs(u), 6.9, 0.08, aa);
		float track = N.stripe(abs(u), 1.1, 0.35, 0.4) + N.stripe(abs(u), 2.7, 0.35, 0.4) + N.stripe(abs(u), 4.4, 0.35, 0.4) + N.stripe(abs(u), 6.0, 0.35, 0.4);
		col *= 1.0 - track * 0.08;
	} else if (type < 1.5) {
		paintY += N.stripe(u, -3.72, 0.08, aa);
		paintW += N.stripe(u, 3.72, 0.08, aa);
		paintW += N.stripe(u, 0.0, 0.07, aa) * step(0.55, frac(v / 12.0));
		float track = N.stripe(abs(u), 1.05, 0.35, 0.4) + N.stripe(abs(u), 2.65, 0.35, 0.4);
		col *= 1.0 - track * 0.1;
		col = lerp(col, float3(0.2, 0.2, 0.2) * (0.8 + 0.3 * n), step(3.9, abs(u)) * 0.5);
	} else if (type < 2.5) {
		paintY += N.stripe(u, -2.05, 0.08, aa);
		paintW += N.stripe(u, 2.05, 0.08, aa);
	} else if (type < 3.5) {
		paintY += N.stripe(u, 0.16, 0.07, aa) + N.stripe(u, -0.16, 0.07, aa);
		paintW += N.stripe(abs(u), 3.78, 0.08, aa);
		col = lerp(col, float3(0.14, 0.13, 0.12) * (0.8 + 0.4 * n), step(3.95, abs(u)) * 0.6);
	} else {
		paintY += N.stripe(u, 0.0, 0.08, aa) * step(0.5, frac(v / 9.0));
		paintW += N.stripe(abs(u), 3.45, 0.06, aa) * 0.7;
	}
	float flags = uv2.y;
	float vs = uv1.x, fromEnd = uv1.y;
	float atStart = N.gmod(flags, 2.0), atEnd = step(1.5, N.gmod(flags, 4.0));
	float uMax = min(wMax - 0.5, 7.0);
	paintW += atEnd * step(0.3, u) * step(u, uMax) * step(0.4, fromEnd) * step(fromEnd, 0.95);
	paintW += atStart * step(-uMax, u) * step(u, -0.3) * step(0.4, vs) * step(vs, 0.95);
	float zA = step(3.5, N.gmod(flags, 8.0)), zB = step(7.5, N.gmod(flags, 16.0));
	float zeb = step(0.5, frac(u / 1.3 + 0.25)) * step(abs(u), wMax - 0.6);
	paintW += zeb * (zA * step(1.0, vs) * step(vs, 4.2) + zB * step(1.0, fromEnd) * step(fromEnd, 4.2));
	paintW += zB * step(0.3, u) * step(u, uMax) * step(4.9, fromEnd) * step(fromEnd, 5.4);
	paintW += zA * step(-uMax, u) * step(u, -0.3) * step(4.9, vs) * step(vs, 5.4);
	float wear = smoothstep(0.25, 0.7, N.vn2(wp * 1.3 + 5.0)) * 0.55 + 0.45;
	paintW *= wear; paintY *= wear;
	float patchN = smoothstep(0.62, 0.66, N.vn2(wp * 0.08 + 13.0));
	col = lerp(col, col * 0.78, patchN);
	float crack = smoothstep(0.02, 0.0, abs(N.vn2(wp * 0.7) - 0.5)) * 0.6;
	col *= 1.0 - crack * 0.5;
	col = lerp(col, float3(0.75, 0.62, 0.18), saturate(paintY));
	col = lerp(col, float3(0.8, 0.8, 0.78), saturate(paintW));
	rough = lerp(0.92, 0.6, saturate(paintW + paintY));
}
return float4(col, rough);
)HLSL");

// ---- Los Soles' street grid (shaders.js ROAD_EXT): %XS%, %ZS%, %RB% are filled in from the generator
static const TCHAR* StreetBody = TEXT(R"HLSL(
const float XS_[%NX%] = { %XS% };
const float ZS_[%NZ%] = { %ZS% };
const float2 RBC[%NRB%] = { %RB% };
const float HR = 10.0;
float3 W = LP / 100.0;
float2 wp = W.xy;
float dxm = 1e5, dzm = 1e5;
for (int i = 0; i < %NX%; i++) { float d = wp.x - XS_[i]; if (abs(d) < abs(dxm)) dxm = d; }
for (int j = 0; j < %NZ%; j++) { float d = wp.y - ZS_[j]; if (abs(d) < abs(dzm)) dzm = d; }
float ax = abs(dxm), az = abs(dzm);
float rbD = 1e5; float2 rbC = float2(0.0, 0.0);
for (int k = 0; k < %NRB%; k++) { float dd = length(wp - RBC[k]); if (dd < rbD) { rbD = dd; rbC = RBC[k]; } }
bool nearRb = rbD < 26.0;
float n = N.fbm3(wp * 0.25);
float fine = N.vn2(wp * 6.0);
float3 col = float3(0.085, 0.085, 0.09) * (0.72 + 0.45 * n) * (0.9 + 0.2 * fine);
float aa = PD / 100.0 * 0.0009 + 0.01;
float paintY = 0.0, paintW = 0.0;
bool inNS = ax < HR, inEW = az < HR;
float lat = 0.0, lon = 0.0, fromInt = 0.0, side = 0.0;
if (inEW && !inNS) { lat = dzm; lon = wp.x; fromInt = ax - HR; side = -lat * dxm; }
else if (inNS && !inEW) { lat = dxm; lon = wp.y; fromInt = az - HR; side = lat * dzm; }
if ((inEW && !inNS) || (inNS && !inEW)) {
	float wear = smoothstep(0.25, 0.7, N.vn2(wp * 1.3 + 5.0)) * 0.55 + 0.45;
	float track = N.stripe(abs(lat), 1.1, 0.35, 0.4) + N.stripe(abs(lat), 2.7, 0.35, 0.4) + N.stripe(abs(lat), 4.6, 0.35, 0.4) + N.stripe(abs(lat), 6.2, 0.35, 0.4);
	col *= 1.0 - track * 0.12;
	paintY += (N.stripe(lat, 0.16, 0.07, aa) + N.stripe(lat, -0.16, 0.07, aa)) * step(0.0, fromInt);
	float dash = step(0.45, frac(lon / 7.0)) * step(4.0, fromInt);
	paintW += N.stripe(abs(lat), 3.65, 0.07, aa) * dash;
	paintW += N.stripe(abs(lat), 7.25, 0.06, aa) * step(6.0, fromInt) * 0.8;
	float cw = step(0.6, fromInt) * (1.0 - step(4.4, fromInt)) * step(abs(lat), HR - 0.6);
	paintW += cw * step(0.5, frac(lat / 1.2 + 0.25));
	paintW += step(4.9, fromInt) * (1.0 - step(5.5, fromInt)) * step(0.0, side) * step(0.3, abs(lat)) * step(abs(lat), 7.1) * (nearRb ? step(0.5, frac(lat / 0.9)) : 1.0);
	paintW *= wear; paintY *= wear;
	float oil = smoothstep(0.55, 0.9, N.vn2(wp * 0.9)) * (1.0 - smoothstep(6.0, 22.0, fromInt));
	col *= 1.0 - oil * 0.35;
} else if (inNS && inEW) {
	if (nearRb) {
		paintW += N.stripe(rbD, 6.6, 0.12, aa) + N.stripe(rbD, 9.6, 0.1, aa) * step(0.5, frac(atan2(wp.y - rbC.y, wp.x - rbC.x) * 3.0));
		col *= 0.97;
	} else {
		float mh = 1.0 - smoothstep(0.55, 0.6, length(float2(dxm, dzm) - float2(2.5, -3.0)));
		col = lerp(col, float3(0.06, 0.06, 0.06), mh);
	}
	col *= 0.95 + 0.1 * N.vn2(wp * 3.0);
}
float patchN = smoothstep(0.62, 0.66, N.vn2(wp * 0.08 + 13.0));
col = lerp(col, col * 0.75, patchN);
float crack = smoothstep(0.02, 0.0, abs(N.vn2(wp * 0.7) - 0.5)) * 0.6;
col *= 1.0 - crack * 0.5;
col = lerp(col, float3(0.75, 0.62, 0.18), saturate(paintY));
col = lerp(col, float3(0.78, 0.78, 0.76), saturate(paintW));
float rough = lerp(0.92, 0.6, saturate(paintY + paintW));
return float4(col, rough);
)HLSL");

// ---- blocks, lots and pads (shaders.js GROUND_EXT)
static const TCHAR* GroundBody = TEXT(R"HLSL(
float3 W = LP / 100.0;
float2 wp = W.xy;
float type = floor(uv1.x + 0.5);
float edge = uv1.y;
bool pad = uv2.x > 0.5;
float hw = uv2.y, hl = uv3.x;
float n = N.fbm3(wp * 0.3);
float3 col = float3(0.5, 0.5, 0.5);
float rough = 0.9;
float3 gn = normalize(float3(NRM.x, NRM.z, NRM.y));
if (pad && type > 8.5) {
	float u = uv0.x, v = uv0.y;
	float fromEnd = hl - abs(v);
	float3 asph = float3(0.1, 0.1, 0.105) * (0.75 + 0.35 * n) * (0.9 + 0.2 * N.vn2(wp * 5.0));
	if (type < 9.5) {
		col = asph;
		float paint = 0.0;
		paint += N.bandG(abs(u), hw - 1.2, 0.4);
		paint += step(abs(u), 0.45) * step(0.5, frac(v / 60.0)) * step(60.0, fromEnd);
		paint += step(6.0, fromEnd) * step(fromEnd, 38.0) * step(abs(u), hw - 3.0) * step(0.5, frac((u + hw) / 3.4));
		paint += step(150.0, fromEnd) * step(fromEnd, 195.0) * step(4.0, abs(u)) * step(abs(u), 9.5);
		paint += step(fromEnd, 1.5) * step(abs(u), hw - 1.0);
		col = lerp(col, float3(0.82, 0.82, 0.8), saturate(paint) * (0.55 + 0.45 * smoothstep(0.2, 0.6, N.vn2(wp * 1.3))));
		col *= 1.0 - smoothstep(0.6, 0.9, N.vn2(float2(u * 0.4, v * 0.02))) * 0.25 * step(abs(u), 6.0);
		rough = 0.9;
	} else if (type < 10.5) {
		col = asph * 1.05;
		float y = step(abs(u), 0.18) + N.bandG(abs(u), hw - 0.8, 0.12) + N.bandG(abs(u), hw - 1.2, 0.12);
		col = lerp(col, float3(0.8, 0.62, 0.12), saturate(y));
		rough = 0.9;
	} else if (type < 11.5) {
		float2 g = frac(wp / 7.5);
		float slab = N.vn2(floor(wp / 7.5) * 1.37);
		col = float3(0.38, 0.375, 0.36) * (0.9 + 0.1 * n) * (0.94 + 0.1 * slab) * (1.0 - saturate(step(g.x, 0.01) + step(g.y, 0.01)) * 0.35);
		col *= 1.0 - smoothstep(0.66, 0.9, N.vn2(wp * 0.35)) * 0.16;
		rough = 0.85;
	} else if (type < 12.5) {
		col = float3(0.34, 0.35, 0.33) * (0.9 + 0.12 * n);
		float r = length(float2(u, v));
		float ring = N.bandG(r, hw * 0.78, 0.35);
		float H = (step(abs(u + 2.2), 0.45) + step(abs(u - 2.2), 0.45)) * step(abs(v), 3.2) + step(abs(u), 2.2) * step(abs(v), 0.45);
		col = lerp(col, float3(0.85, 0.7, 0.12), ring);
		col = lerp(col, float3(0.9, 0.9, 0.9), saturate(H));
		rough = 0.8;
	} else {
		col = float3(0.4, 0.34, 0.24) * (0.75 + 0.4 * n) * (0.9 + 0.2 * N.vn2(wp * 3.0));
		rough = 1.0;
	}
} else if (edge < 4.0) {
	float2 g = frac(wp / 1.6);
	float joint = step(g.x, 0.025) + step(g.y, 0.025);
	col = float3(0.54, 0.53, 0.5) * (0.85 + 0.25 * n) * (1.0 - saturate(joint) * 0.25);
	col = lerp(col, float3(0.66, 0.65, 0.62), step(edge, 0.3));
	col *= 1.0 - smoothstep(0.6, 0.85, N.vn2(wp * 0.6)) * 0.2;
	rough = 0.85;
} else if (type < 0.5) {
	col = float3(0.5, 0.49, 0.47) * (0.8 + 0.3 * n); rough = 0.85;
} else if (type < 1.5) {
	float dry = smoothstep(0.35, 0.75, N.fbm3(wp * 0.05));
	col = lerp(float3(0.12, 0.2, 0.06), float3(0.3, 0.27, 0.12), dry) * (0.75 + 0.5 * N.vn2(wp * 2.5)) * (0.85 + 0.3 * n);
	rough = 0.95;
} else if (type < 2.5) {
	col = float3(0.32, 0.25, 0.17) * (0.7 + 0.5 * n) * (0.9 + 0.2 * N.vn2(wp * 5.0)); rough = 1.0;
} else if (type < 3.5) {
	col = float3(0.1, 0.1, 0.105) * (0.75 + 0.4 * n);
	float pl = step(frac(wp.x / 3.0), 0.04) * step(frac(wp.y / 12.0), 0.45);
	col = lerp(col, float3(0.7, 0.7, 0.7), pl * 0.7);
	rough = 0.9;
} else if (type < 4.5) {
	float2 c = floor(wp / 2.0);
	float chk = N.gmod(c.x + c.y, 2.0);
	col = lerp(float3(0.62, 0.58, 0.52), float3(0.5, 0.46, 0.42), chk) * (0.9 + 0.15 * n);
	float2 g = frac(wp / 2.0);
	col *= 1.0 - (step(g.x, 0.02) + step(g.y, 0.02)) * 0.2;
	rough = 0.7;
} else if (type < 5.5) {
	col = float3(0.6, 0.59, 0.56) * (0.85 + 0.2 * n); rough = 0.8;
} else if (type < 6.5) {
	col = float3(0.16, 0.3, 0.42) * (0.9 + 0.15 * n); rough = 0.6;
} else if (type < 7.5) {
	col = float3(0.55, 0.47, 0.36) * (0.8 + 0.4 * N.vn2(wp * 8.0)); rough = 1.0;
} else {
	float rip = sin(wp.x * 1.3 + N.vn2(wp * 0.2) * 6.0) * 0.5 + 0.5;
	col = float3(0.76, 0.66, 0.48) * (0.85 + 0.1 * rip + 0.1 * n); rough = 1.0;
}
if (gn.y < 0.5) { col = float3(0.6, 0.59, 0.56) * (0.85 + 0.2 * n); rough = 0.8; }
return float4(col, rough);
)HLSL");

// ---- buildings (shaders.js BUILDING_EXT), %OUT% picks the output
static const TCHAR* BuildingRoom = TEXT(R"HLSL(
	float3 roomInterior(float2 f, float3 V, float3 Nn, float rnd, float lit) {
		float3 T = normalize(cross(float3(0.0, 1.0, 0.0), Nn));
		float3 d = float3(dot(V, T), V.y, dot(V, -Nn));
		d.z = max(d.z, 0.05);
		float tx = d.x > 0.0 ? (1.0 - f.x) / max(d.x, 1e-4) : f.x / max(-d.x, 1e-4);
		float ty = d.y > 0.0 ? (1.0 - f.y) / max(d.y, 1e-4) : f.y / max(-d.y, 1e-4);
		float tz = 0.9 / d.z;
		float t = min(min(tx, ty), tz);
		float3 h = float3(f, 0.0) + d * t;
		float3 wallC = lerp(float3(0.62, 0.55, 0.45), float3(0.45, 0.5, 0.56), rnd);
		wallC = lerp(wallC, float3(0.7, 0.4, 0.35), step(0.8, frac(rnd * 7.3)));
		float3 c;
		if (t == tz) {
			c = wallC;
			c *= 1.0 - 0.55 * step(h.y, 0.32) * step(0.15 + rnd * 0.2, h.x) * step(h.x, 0.6 + rnd * 0.3);
			c = lerp(c, float3(0.2, 0.3, 0.5), step(0.55, h.y) * step(h.y, 0.75) * step(0.35, h.x) * step(h.x, 0.6) * step(0.5, rnd));
		} else if (t == ty) {
			c = d.y > 0.0 ? float3(0.9, 0.88, 0.84) : float3(0.32, 0.24, 0.17) * (0.8 + 0.4 * step(0.5, frac(h.x * 4.0)));
		} else {
			c = wallC * 0.78;
		}
		c *= lerp(0.55, 1.0, h.y) * (lit > 0.5 ? 1.0 : 0.35);
		return c;
	}
)HLSL");
static const TCHAR* BuildingBody = TEXT(R"HLSL(
float style = floor(uv1.x + 0.5);
float seed = uv1.y;
float rg = floor(uv2.x + 0.5);
float roof = floor(rg / 4.0 + 0.01);
float groundF = rg - roof * 4.0;
float3 tint = float3(uv2.y, uv3.x, uv3.y);
float3 W = LP / 100.0;
float3 wpos = float3(W.x, W.z, W.y);
float3 Vu = -CAM;
float3 V = normalize(float3(Vu.x, Vu.z, Vu.y));
float3 NJ = normalize(float3(NRM.x, NRM.z, NRM.y));
float3 col = tint;
float rough = 0.85, metal = 0.0;
float3 emit = float3(0.0, 0.0, 0.0);
float nightK = smoothstep(0.1, 0.7, Night);
if (roof > 1.5) {
	float row = frac(uv0.y * 3.2);
	float colT = frac(uv0.x * 3.0 + step(0.5, frac(uv0.y * 1.6)) * 0.5);
	float3 c = lerp(float3(0.55, 0.22, 0.12), float3(0.42, 0.3, 0.26), step(0.6, frac(seed * 13.0)));
	c *= 0.75 + 0.35 * smoothstep(0.0, 0.9, row) * (0.8 + 0.2 * smoothstep(0.0, 0.3, colT) * smoothstep(1.0, 0.7, colT));
	c *= 0.85 + 0.25 * N.vn2(wpos.xz * 1.7);
	col = c; rough = 0.75;
} else if (roof > 0.5) {
	float n = N.fbm3(wpos.xz * 0.9);
	col = lerp(float3(0.34, 0.33, 0.32), float3(0.42, 0.4, 0.37), step(0.5, seed)) * (0.75 + 0.4 * n);
	rough = 0.95;
} else {
	float2 uv = uv0;
	float2 cell = floor(uv);
	float2 f = frac(uv);
	float rnd = N.h21(cell + seed * 137.1);
	float rnd2 = N.h21(cell.yx * 1.7 + seed * 71.3);
	float fw = PD / 100.0 * 0.00115 / 3.2 / max(abs(dot(V, NJ)), 0.25);
	float aa = clamp(fw * 0.75, 0.002, 0.5);
	float farK = smoothstep(0.35, 0.95, fw);
	bool groundFloor = groundF > 0.5 && cell.y < 0.5;
	float3 wall = tint;
	float win = 0.0;
	float3 glass = float3(0.05, 0.07, 0.09);
	float litProb = 0.35;
	float3 lightCol = lerp(float3(1.0, 0.72, 0.42), float3(0.85, 0.92, 1.0), step(0.7, rnd2));
	float frame = 0.0;
	float shopSign = 0.0;
	float glassMetal = 0.55;
	if (style < 0.5) {
		wall = tint * float3(0.62, 0.61, 0.6) * (0.9 + 0.1 * N.vn2(uv * 3.0));
		win = N.rectM(f, float2(-0.1, 0.3), float2(1.1, 0.88), aa);
		frame = (1.0 - smoothstep(0.012, 0.012 + aa, min(f.x, 1.0 - f.x))) * win;
		glass = float3(0.07, 0.1, 0.13); litProb = 0.45; lightCol = lerp(lightCol, float3(0.9, 0.95, 1.0), 0.6);
	} else if (style < 1.5) {
		wall = tint * 0.35;
		win = N.rectM(f, float2(0.025, 0.2), float2(0.975, 0.985), aa);
		glass = tint * float3(0.12, 0.16, 0.2);
		glassMetal = 0.85;
		litProb = 0.5; lightCol = float3(0.85, 0.93, 1.0);
	} else if (style < 2.5) {
		float2 b = float2(uv.x * 12.8 + step(0.5, frac(uv.y * 22.5)) * 0.5, uv.y * 45.0);
		float2 bf = frac(b);
		float mortar = max(1.0 - smoothstep(0.0, 0.08 + fw * 12.0, bf.y), 1.0 - smoothstep(0.0, 0.04 + fw * 12.0, bf.x));
		mortar *= 1.0 - farK;
		float3 brick = tint * float3(0.58, 0.3, 0.22) * (0.85 + 0.3 * N.h21(floor(b)));
		wall = lerp(brick, float3(0.55, 0.52, 0.48), mortar * 0.8);
		win = N.rectM(f, float2(0.26, 0.24), float2(0.74, 0.8), aa);
		frame = N.rectM(f, float2(0.22, 0.2), float2(0.78, 0.84), aa) - win;
		litProb = 0.38;
	} else if (style < 3.5) {
		wall = tint * float3(0.82, 0.78, 0.7) * (0.9 + 0.15 * N.fbm3(uv * 2.0));
		float has = step(0.22, rnd);
		win = N.rectM(f, float2(0.3, 0.34), float2(0.7, 0.8), aa) * has;
		frame = N.rectM(f, float2(0.26, 0.3), float2(0.74, 0.84), aa) * has - win;
		litProb = 0.33;
	} else if (style < 4.5) {
		float rib = sin(uv.x * 6.2831 * 14.0);
		wall = tint * float3(0.6, 0.62, 0.64) * (0.85 + 0.12 * rib * (1.0 - farK)) * (0.85 + 0.25 * N.vn2(uv * float2(1.0, 0.5)));
		float hasW = step(0.55, rnd);
		win = N.rectM(f, float2(0.1, 0.72), float2(0.9, 0.9), aa) * hasW;
		litProb = 0.25; lightCol = float3(0.9, 1.0, 0.9);
		if (groundFloor && rnd2 > 0.55) {
			float door = N.rectM(f, float2(0.08, 0.0), float2(0.92, 0.72), aa);
			wall = lerp(wall, float3(0.45, 0.46, 0.44) * (0.8 + 0.2 * step(0.5, frac(f.y * 18.0))), door);
		}
	} else if (style < 5.5) {
		wall = tint * float3(0.78, 0.72, 0.64) * (0.9 + 0.15 * N.fbm3(uv * 2.0));
		win = N.rectM(f, float2(0.25, 0.3), float2(0.75, 0.8), aa) * step(0.15, rnd);
		frame = N.rectM(f, float2(0.22, 0.27), float2(0.78, 0.83), aa) * step(0.15, rnd) - win;
		litProb = 0.3;
	} else if (style < 6.5) {
		float pil = 1.0 - N.rectM(f, float2(0.14, -0.1), float2(0.86, 1.1), aa);
		wall = lerp(tint * float3(0.75, 0.7, 0.62), tint * float3(0.88, 0.84, 0.76), pil);
		win = N.rectM(f, float2(0.22, 0.14), float2(0.78, 0.88), aa);
		frame = step(abs(f.x - 0.5), 0.015) * win;
		litProb = 0.42;
	} else {
		wall = tint * float3(0.9, 0.9, 0.88);
		win = N.rectM(f, float2(0.08, 0.12), float2(0.92, 0.86), aa);
		float rail = step(frac(f.x * 14.0), 0.18) * step(f.y, 0.4) * step(0.12, f.y) + step(abs(f.y - 0.4), 0.015);
		frame = saturate(rail * win);
		glass = float3(0.08, 0.12, 0.14);
		litProb = 0.3;
	}
	if (groundFloor) {
		float isShop = (style > 4.5 && style < 5.5) ? 1.0 : step(0.45, rnd2);
		if ((style > 3.5 && style < 4.5) || groundF > 1.5) isShop = 0.0;
		if (isShop > 0.5) {
			win = N.rectM(f, float2(0.04, 0.05), float2(0.96, 0.72), aa);
			frame = N.rectM(f, float2(0.02, 0.03), float2(0.98, 0.74), aa) - win;
			shopSign = N.rectM(f, float2(-0.1, 0.78), float2(1.1, 0.97), aa);
			litProb = 0.85;
			lightCol = float3(1.0, 0.85, 0.6);
		} else if (rnd < 0.18 && style > 1.5) {
			float door = N.rectM(f, float2(0.32, 0.0), float2(0.68, 0.72), aa);
			wall = lerp(wall, float3(0.2, 0.13, 0.08), door);
			win *= 1.0 - door;
		}
	}
	float slab = (1.0 - smoothstep(0.0, 0.02 + aa, f.y)) * (1.0 - farK) * step(style, 2.5) * step(0.5, style);
	wall *= 1.0 - slab * 0.25;
	win = lerp(win, 0.4, farK);
	frame = lerp(frame, 0.0, farK);
	float lit = step(N.h21(cell * 1.31 + seed * 19.7 + 0.5), litProb * (0.25 + 0.4 * Night));
	float3 interior = N.roomInterior(f, V, NJ, rnd, lit);
	interior = lerp(interior, float3(0.5, 0.5, 0.5), farK);
	float3 winCol = glass + interior * 0.18;
	col = lerp(wall, winCol, win);
	col = lerp(col, float3(0.85, 0.84, 0.8) * (style < 2.5 && style > 1.5 ? 0.9 : 1.0), saturate(frame));
	float3 signCol = 0.5 + 0.5 * cos(6.2831 * (N.h21(float2(seed * 91.0, cell.y)) + float3(0.0, 0.33, 0.67)));
	float glyph = step(0.45, N.h21(floor(float2(uv.x * 7.0, f.y * 5.0)) + seed * 3.0)) * N.rectM(f, float2(-0.1, 0.81), float2(1.1, 0.94), aa);
	col = lerp(col, signCol * 0.4 + glyph * 0.2, shopSign);
	rough = lerp(0.88, 0.06, win);
	metal = lerp(0.0, glassMetal, win);
	emit = interior * lightCol * lit * win * (nightK * 1.25 + (shopSign > 0.0 || litProb > 0.8 ? 0.08 : 0.0));
	emit += signCol * (0.25 + glyph * 1.4) * shopSign * (nightK * 3.5 + 0.05);
	emit *= 1.0 + step(0.93, rnd2) * 0.4 * sin(Time * 13.0 + rnd * 40.0) * nightK;
}
%OUT%
)HLSL");

// ---- vertex-lit props, vegetation, concrete, vehicles, people (+ PROP_EXT signals)
static const TCHAR* VertexLitBody = TEXT(R"HLSL(
float3 col = float3(uv1.x, uv1.y, uv2.x) * Tint.rgb * float3(TR, TG, TB);
float rough = uv3.y > 0.001 ? uv3.y : 0.8;
float sig = floor(uv3.x + 0.5);
float3 emit = col * uv2.y * (0.02 + Lights * 1.6);
if (sig < -0.5) {
	float3 W = LP / 100.0;
	float2 wp = float2(W.x, W.y) + W.z * 0.7;
	float n = N.fbm3(wp * 0.6);
	col *= 0.78 + 0.32 * n;
	col *= 1.0 - smoothstep(0.55, 0.8, N.vn2(float2(W.x + W.y, W.z * 3.0) * 0.3)) * 0.25;
}
if (sig > 0.5) {
	float c = N.gmod(Time + Phase, 34.0);
	bool nsG = c < 13.0, nsY = c >= 13.0 && c < 16.0;
	bool ewG = c >= 17.0 && c < 30.0, ewY = c >= 30.0 && c < 33.0;
	float on = 0.0;
	if (sig < 1.5) on = (!nsG && !nsY) ? 1.0 : 0.0;
	else if (sig < 2.5) on = nsY ? 1.0 : 0.0;
	else if (sig < 3.5) on = nsG ? 1.0 : 0.0;
	else if (sig < 4.5) on = (!ewG && !ewY) ? 1.0 : 0.0;
	else if (sig < 5.5) on = ewY ? 1.0 : 0.0;
	else on = ewG ? 1.0 : 0.0;
	emit += col * on * 6.0;
	col *= 0.15 + on * 0.2;
}
%OUT%
)HLSL");

// ---- palm fronds: a leaflet mask drawn from the card's uvs (the browser game used a painted texture)
static const TCHAR* FrondBody = TEXT(R"HLSL(
float3 col = float3(uv1.x, uv1.y, uv2.x);
float u = uv0.x, v = uv0.y;
float d = abs(u - 0.5) * 2.0;
float width = 1.0 - v * 0.55;
float leaf = step(0.38, frac(v * 15.0 + d * 1.8));
float rib = step(d, 0.07);
float mask = d < width ? max(leaf * step(0.08, v), rib) : 0.0;
col *= 0.8 + 0.3 * (1.0 - d);
return float4(col, mask);
)HLSL");

// ---- sea, lake and pools (city.js _water)
static const TCHAR* WaterBody = TEXT(R"HLSL(
float3 W = LP / 100.0;
float2 wp = W.xy;
float pool = uv0.y;
float depth = pool > 0.5 ? 2.0 : uv0.x;
float3 deep = pool > 0.5 ? float3(0.02, 0.25, 0.32) : float3(0.01, 0.06, 0.08);
float3 shallow = pool > 0.5 ? float3(0.1, 0.55, 0.6) : float3(0.05, 0.28, 0.27);
float3 col = lerp(shallow, deep, smoothstep(0.0, 6.0, depth));
float foam = 0.0;
if (pool < 0.5) {
	float shore = 1.0 - smoothstep(0.0, 1.4, depth);
	float wave = smoothstep(0.75, 1.0, sin(depth * 5.0 - Time * 1.6 + N.vn2(wp * 0.15) * 4.0));
	foam = saturate(shore * (0.55 + 0.45 * wave) * smoothstep(0.35, 0.65, N.vn2(wp * 0.9 + Time * 0.2)) + shore * shore * 0.6);
	col = lerp(col, float3(0.85, 0.88, 0.9), foam);
}
float2 p = wp * (pool > 0.5 ? 2.0 : 1.0);
float t = Time;
float2 g = float2(0.0, 0.0);
g += float2(0.6, 0.8) * cos(dot(p, float2(0.6, 0.8)) * 0.35 + t * 1.3) * 0.35;
g += float2(-0.7, 0.7) * cos(dot(p, float2(-0.7, 0.7)) * 0.62 + t * 1.7) * 0.2;
g += float2(0.2, -1.0) * cos(dot(p, float2(0.2, -1.0)) * 1.3 + t * 2.3) * 0.12;
g += float2(0.9, 0.3) * cos(dot(p, float2(0.9, 0.3)) * 2.7 + t * 3.1) * 0.07;
g += (float2(N.vn2(p * 1.9 + t * 0.6), N.vn2(p * 1.9 - t * 0.5 + 4.0)) - 0.5) * 0.25;
float3 wn = normalize(float3(-g.x * 0.35, 1.0, -g.y * 0.35));
wn = normalize(lerp(wn, float3(0.0, 1.0, 0.0), foam * 0.8 + smoothstep(200.0, 900.0, PD / 100.0) * 0.6));
%OUT%
)HLSL");

FString Wrap(const TCHAR* Extra, const TCHAR* Body) { return FString(NoiseBegin) + Extra + NoiseEnd + Body; }
FString Out(const FString& Code, const TCHAR* Ret) { return Code.Replace(TEXT("%OUT%"), Ret); }

FString StreetCode() {
	FString Xs, Zs, Rb;
	for (size_t i = 0; i < atg::XS.size(); i++) Xs += FString::Printf(TEXT("%s%.1f"), i ? TEXT(", ") : TEXT(""), atg::XS[i]);
	for (size_t j = 0; j < atg::ZS.size(); j++) Zs += FString::Printf(TEXT("%s%.1f"), j ? TEXT(", ") : TEXT(""), atg::ZS[j]);
	for (size_t k = 0; k < atg::CITY_ROUNDABOUTS.size(); k++) Rb += FString::Printf(TEXT("%sfloat2(%.1f, %.1f)"), k ? TEXT(", ") : TEXT(""), atg::XS[atg::CITY_ROUNDABOUTS[k].first], atg::ZS[atg::CITY_ROUNDABOUTS[k].second]);
	FString Code = Wrap(TEXT(""), StreetBody);
	Code = Code.Replace(TEXT("%NX%"), *FString::FromInt((int32)atg::XS.size())).Replace(TEXT("%NZ%"), *FString::FromInt((int32)atg::ZS.size())).Replace(TEXT("%NRB%"), *FString::FromInt((int32)atg::CITY_ROUNDABOUTS.size()));
	return Code.Replace(TEXT("%XS%"), *Xs).Replace(TEXT("%ZS%"), *Zs).Replace(TEXT("%RB%"), *Rb);
}
} // namespace ATGHlsl

// ------------------------------------------------------------------ editor: building the assets
#if WITH_EDITOR
namespace {
struct FMatBuilder {
	UMaterial* M;
	explicit FMatBuilder(UMaterial* InM) : M(InM) {}
	int32 Col = -1400;

	template <class T> T* New() {
		T* E = NewObject<T>(M);
		E->Material = M;
		E->MaterialExpressionEditorX = Col;
		E->MaterialExpressionEditorY = M->GetExpressionCollection().Expressions.Num() * 60;
		M->GetExpressionCollection().AddExpression(E);
		return E;
	}
	UMaterialExpression* UV(int32 Index) { auto* E = New<UMaterialExpressionTextureCoordinate>(); E->CoordinateIndex = Index; return E; }
	UMaterialExpression* LocalPos() { return New<UMaterialExpressionLocalPosition>(); }
	UMaterialExpression* Normal() { return New<UMaterialExpressionVertexNormalWS>(); }
	UMaterialExpression* Camera() { return New<UMaterialExpressionCameraVectorWS>(); }
	UMaterialExpression* Depth() { return New<UMaterialExpressionPixelDepth>(); }
	UMaterialExpression* Time() { return New<UMaterialExpressionTime>(); }
	UMaterialExpression* Mpc(UMaterialParameterCollection* C, const TCHAR* Name) {
		auto* E = New<UMaterialExpressionCollectionParameter>();
		E->Collection = C; E->ParameterName = FName(Name); E->ParameterId = C->GetParameterId(FName(Name));
		return E;
	}
	UMaterialExpression* InstanceData(uint32 Index, float Default) {
		auto* E = New<UMaterialExpressionPerInstanceCustomData>();
		E->DataIndex = Index; E->ConstDefaultValue = Default;
		return E;
	}
	UMaterialExpression* VecParam(const TCHAR* Name, const FLinearColor& Default) {
		auto* E = New<UMaterialExpressionVectorParameter>();
		E->ParameterName = FName(Name); E->DefaultValue = Default;
		return E;
	}
	UMaterialExpressionCustom* Custom(const FString& Code, ECustomMaterialOutputType Type, const TArray<TPair<FString, UMaterialExpression*>>& Ins, const TCHAR* Desc) {
		Col = -600;
		auto* C = New<UMaterialExpressionCustom>();
		C->Code = Code; C->OutputType = Type; C->Description = Desc;
		C->Inputs.Reset();
		for (const auto& P : Ins) { FCustomInput In; In.InputName = FName(*P.Key); In.Input.Connect(0, P.Value); C->Inputs.Add(In); }
		Col = -1400;
		return C;
	}
	UMaterialExpression* Mask(UMaterialExpression* Src, bool R, bool G, bool B, bool A) {
		Col = -250;
		auto* E = New<UMaterialExpressionComponentMask>();
		E->Input.Connect(0, Src);
		E->R = R; E->G = G; E->B = B; E->A = A;
		Col = -1400;
		return E;
	}
	UMaterialEditorOnlyData* Out() { return M->GetEditorOnlyData(); }
};

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

UMaterialParameterCollection* MakeMpc() {
	const FString Name = MpcName();
	UPackage* Pkg = CreatePackage(*(TEXT("/Game/ATG/Materials/") + Name));
	UMaterialParameterCollection* C = NewObject<UMaterialParameterCollection>(Pkg, FName(*Name), RF_Public | RF_Standalone);
	auto Add = [C](const TCHAR* P, float V) { FCollectionScalarParameter S; S.ParameterName = FName(P); S.DefaultValue = V; C->ScalarParameters.Add(S); };
	Add(TEXT("Night"), 0.f);
	Add(TEXT("StreetLights"), 0.f);
	Add(TEXT("EmissiveBoost"), 3.f);
	Add(TEXT("SimTime"), 0.f); // (the simulation's shader clock: the traffic lights and the traffic agree)
	C->PostEditChange();
	SaveAsset(C);
	return C;
}

UMaterial* MakeMaterial(EATGMat Which, UMaterialParameterCollection* C) {
	const FString Name = AssetName(GMatNames[(int32)Which]);
	UPackage* Pkg = CreatePackage(*(TEXT("/Game/ATG/Materials/") + Name));
	UMaterial* M = NewObject<UMaterial>(Pkg, FName(*Name), RF_Public | RF_Standalone | RF_Transactional);
	FMatBuilder B(M);
	M->MaterialDomain = MD_Surface;
	M->BlendMode = BLEND_Opaque;
	M->SetShadingModel(MSM_DefaultLit);
	using namespace ATGHlsl;
	auto Split = [&](UMaterialExpression* E) {
		B.Out()->BaseColor.Connect(0, B.Mask(E, true, true, true, false));
		B.Out()->Roughness.Connect(0, B.Mask(E, false, false, false, true));
	};
	auto UVs = [&](TArray<TPair<FString, UMaterialExpression*>>& Ins) { for (int32 i = 0; i < 4; i++) Ins.Add({ FString::Printf(TEXT("uv%d"), i), B.UV(i) }); };
	switch (Which) {
	case EATGMat::Terrain: {
		TArray<TPair<FString, UMaterialExpression*>> Ins; UVs(Ins);
		Ins.Add({ TEXT("LP"), B.LocalPos() }); Ins.Add({ TEXT("NRM"), B.Normal() });
		Split(B.Custom(Wrap(TerrainFields, TerrainBody), CMOT_Float4, Ins, TEXT("ATG terrain")));
		break;
	}
	case EATGMat::Road: {
		TArray<TPair<FString, UMaterialExpression*>> Ins; UVs(Ins);
		Ins.Add({ TEXT("LP"), B.LocalPos() }); Ins.Add({ TEXT("PD"), B.Depth() });
		Split(B.Custom(Wrap(TEXT(""), RoadBody), CMOT_Float4, Ins, TEXT("ATG road network")));
		break;
	}
	case EATGMat::Street: {
		TArray<TPair<FString, UMaterialExpression*>> Ins;
		Ins.Add({ TEXT("LP"), B.LocalPos() }); Ins.Add({ TEXT("PD"), B.Depth() });
		Split(B.Custom(StreetCode(), CMOT_Float4, Ins, TEXT("ATG city streets")));
		break;
	}
	case EATGMat::Ground: {
		TArray<TPair<FString, UMaterialExpression*>> Ins; UVs(Ins);
		Ins.Add({ TEXT("LP"), B.LocalPos() }); Ins.Add({ TEXT("NRM"), B.Normal() });
		Split(B.Custom(Wrap(TEXT(""), GroundBody), CMOT_Float4, Ins, TEXT("ATG ground")));
		break;
	}
	case EATGMat::Building: {
		UMaterialExpression* Lp = B.LocalPos(); UMaterialExpression* Nrm = B.Normal(); UMaterialExpression* Cam = B.Camera(); UMaterialExpression* Pd = B.Depth();
		UMaterialExpression* Night = B.Mpc(C, TEXT("Night")); UMaterialExpression* Boost = B.Mpc(C, TEXT("EmissiveBoost")); UMaterialExpression* T = B.Time();
		UMaterialExpression* U[4] = { B.UV(0), B.UV(1), B.UV(2), B.UV(3) };
		auto Ins = [&](bool WithBoost) {
			TArray<TPair<FString, UMaterialExpression*>> I;
			for (int32 i = 0; i < 4; i++) I.Add({ FString::Printf(TEXT("uv%d"), i), U[i] });
			I.Add({ TEXT("LP"), Lp }); I.Add({ TEXT("NRM"), Nrm }); I.Add({ TEXT("CAM"), Cam }); I.Add({ TEXT("PD"), Pd }); I.Add({ TEXT("Night"), Night }); I.Add({ TEXT("Time"), T });
			if (WithBoost) I.Add({ TEXT("Boost"), Boost });
			return I;
		};
		const FString Code = Wrap(BuildingRoom, BuildingBody);
		UMaterialExpressionCustom* A = B.Custom(Out(Code, TEXT("return float4(col, rough);")), CMOT_Float4, Ins(false), TEXT("ATG building surface"));
		UMaterialExpressionCustom* E = B.Custom(Out(Code, TEXT("return float4(emit * Boost, metal);")), CMOT_Float4, Ins(true), TEXT("ATG building light"));
		Split(A);
		B.Out()->EmissiveColor.Connect(0, B.Mask(E, true, true, true, false));
		B.Out()->Metallic.Connect(0, B.Mask(E, false, false, false, true));
		break;
	}
	case EATGMat::VertexLit: {
		M->SetUsageByFlag(MATUSAGE_InstancedStaticMeshes, true);
		UMaterialExpression* U[4] = { B.UV(0), B.UV(1), B.UV(2), B.UV(3) };
		UMaterialExpression* Lp = B.LocalPos(); UMaterialExpression* T = B.Mpc(C, TEXT("SimTime"));
		UMaterialExpression* Tint = B.VecParam(TEXT("Tint"), FLinearColor::White);
		UMaterialExpression* Ph = B.InstanceData(0, 0.f); UMaterialExpression* Tr = B.InstanceData(1, 1.f); UMaterialExpression* Tg = B.InstanceData(2, 1.f); UMaterialExpression* Tb = B.InstanceData(3, 1.f);
		UMaterialExpression* Lights = B.Mpc(C, TEXT("StreetLights")); UMaterialExpression* Boost = B.Mpc(C, TEXT("EmissiveBoost"));
		auto Ins = [&](bool WithBoost) {
			TArray<TPair<FString, UMaterialExpression*>> I;
			for (int32 i = 1; i < 4; i++) I.Add({ FString::Printf(TEXT("uv%d"), i), U[i] });
			I.Add({ TEXT("LP"), Lp }); I.Add({ TEXT("Time"), T }); I.Add({ TEXT("Tint"), Tint });
			I.Add({ TEXT("Phase"), Ph }); I.Add({ TEXT("TR"), Tr }); I.Add({ TEXT("TG"), Tg }); I.Add({ TEXT("TB"), Tb }); I.Add({ TEXT("Lights"), Lights });
			if (WithBoost) I.Add({ TEXT("Boost"), Boost });
			return I;
		};
		const FString Code = Wrap(TEXT(""), VertexLitBody);
		Split(B.Custom(Out(Code, TEXT("return float4(col, rough);")), CMOT_Float4, Ins(false), TEXT("ATG vertex-lit surface")));
		UMaterialExpressionCustom* E = B.Custom(Out(Code, TEXT("return float4(emit * Boost, 0.0);")), CMOT_Float4, Ins(true), TEXT("ATG vertex-lit light"));
		B.Out()->EmissiveColor.Connect(0, B.Mask(E, true, true, true, false));
		break;
	}
	case EATGMat::Frond: {
		M->SetUsageByFlag(MATUSAGE_InstancedStaticMeshes, true);
		M->BlendMode = BLEND_Masked;
		M->TwoSided = true;
		M->OpacityMaskClipValue = 0.5f;
		TArray<TPair<FString, UMaterialExpression*>> Ins;
		for (int32 i = 0; i < 3; i++) Ins.Add({ FString::Printf(TEXT("uv%d"), i), B.UV(i) });
		UMaterialExpressionCustom* F = B.Custom(Wrap(TEXT(""), FrondBody), CMOT_Float4, Ins, TEXT("ATG palm frond"));
		B.Out()->BaseColor.Connect(0, B.Mask(F, true, true, true, false));
		B.Out()->OpacityMask.Connect(0, B.Mask(F, false, false, false, true));
		break;
	}
	case EATGMat::Water: {
		M->bTangentSpaceNormal = false;
		UMaterialExpression* Lp = B.LocalPos(); UMaterialExpression* U0 = B.UV(0); UMaterialExpression* T = B.Time(); UMaterialExpression* Pd = B.Depth();
		auto Ins = [&]() { TArray<TPair<FString, UMaterialExpression*>> I; I.Add({ TEXT("uv0"), U0 }); I.Add({ TEXT("LP"), Lp }); I.Add({ TEXT("Time"), T }); I.Add({ TEXT("PD"), Pd }); return I; };
		const FString Code = Wrap(TEXT(""), WaterBody);
		Split(B.Custom(Out(Code, TEXT("return float4(col, lerp(0.04, 0.6, foam));")), CMOT_Float4, Ins(), TEXT("ATG water surface")));
		UMaterialExpressionCustom* Nw = B.Custom(Out(Code, TEXT("return float4(wn.x, wn.z, wn.y, 0.0);")), CMOT_Float4, Ins(), TEXT("ATG water normal"));
		B.Out()->Normal.Connect(0, B.Mask(Nw, true, true, true, false));
		break;
	}
	case EATGMat::Standard:
	case EATGMat::Glass: {
		const bool bGlass = Which == EATGMat::Glass;
		M->SetUsageByFlag(MATUSAGE_InstancedStaticMeshes, true);
		M->SetUsageByFlag(MATUSAGE_SkeletalMesh, true);
		if (bGlass) { M->BlendMode = BLEND_Translucent; M->TranslucencyLightingMode = TLM_SurfacePerPixelLighting; }
		UMaterialExpression* U1 = B.UV(1); UMaterialExpression* U2 = B.UV(2);
		UMaterialExpression* Col = B.VecParam(TEXT("Color"), FLinearColor::White);
		UMaterialExpression* Em = B.VecParam(TEXT("Emissive"), FLinearColor::Black);
		UMaterialExpression* Sf = B.VecParam(TEXT("Surface"), FLinearColor(0.5f, 0.f, 1.f, 0.f));
		UMaterialExpression* Boost = B.Mpc(C, TEXT("EmissiveBoost"));
		auto Ins = [&]() { TArray<TPair<FString, UMaterialExpression*>> I; I.Add({ TEXT("uv1"), U1 }); I.Add({ TEXT("uv2"), U2 }); I.Add({ TEXT("Color"), Col }); I.Add({ TEXT("Emissive"), Em }); I.Add({ TEXT("Surface"), Sf }); I.Add({ TEXT("Boost"), Boost }); return I; };
		UMaterialExpressionCustom* A = B.Custom(TEXT("float3 vc = float3(uv1.x, uv1.y, uv2.x); return float4(vc * Color.rgb, Surface.r);"), CMOT_Float4, Ins(), TEXT("ATG standard colour"));
		UMaterialExpressionCustom* E = B.Custom(TEXT("return float4(Emissive.rgb * Boost, Surface.g);"), CMOT_Float4, Ins(), TEXT("ATG standard light"));
		UMaterialExpressionCustom* O = B.Custom(TEXT("return float4(Surface.b, 0, 0, 0);"), CMOT_Float4, Ins(), TEXT("ATG standard opacity"));
		Split(A);
		B.Out()->EmissiveColor.Connect(0, B.Mask(E, true, true, true, false));
		B.Out()->Metallic.Connect(0, B.Mask(E, false, false, false, true));
		if (bGlass) B.Out()->Opacity.Connect(0, B.Mask(O, true, false, false, false));
		break;
	}
	default: break;
	}
	M->PreEditChange(nullptr);
	M->PostEditChange();
	SaveAsset(M);
	return M;
}
} // namespace

void ATGMaterials::EnsureAssets() {
	UMaterialParameterCollection* C = LoadObject<UMaterialParameterCollection>(nullptr, *ObjectPath(MpcName()), nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (!C) { UE_LOG(LogATG, Log, TEXT("Creating %s"), *MpcName()); C = MakeMpc(); }
	GMpc = Keep(C);
	for (int32 i = 0; i < (int32)EATGMat::Count; i++) {
		const FString Name = AssetName(GMatNames[i]);
		UMaterialInterface* M = LoadObject<UMaterialInterface>(nullptr, *ObjectPath(Name), nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (!M) { UE_LOG(LogATG, Log, TEXT("Creating %s"), *Name); M = MakeMaterial((EATGMat)i, C); }
		GCache[i] = Keep(M);
	}
}
#endif

// ------------------------------------------------------------------ runtime
UMaterialParameterCollection* ATGMaterials::Collection() {
	if (!GMpc.IsValid()) {
		UMaterialParameterCollection* C = LoadObject<UMaterialParameterCollection>(nullptr, *ObjectPath(MpcName()), nullptr, LOAD_NoWarn | LOAD_Quiet);
#if WITH_EDITOR
		if (!C) { EnsureAssets(); return GMpc.Get(); }
#endif
		GMpc = Keep(C);
	}
	return GMpc.Get();
}

UMaterialInterface* ATGMaterials::Get(EATGMat Which) {
	const int32 i = (int32)Which;
	if (!GCache[i].IsValid()) {
		UMaterialInterface* M = LoadObject<UMaterialInterface>(nullptr, *ObjectPath(AssetName(GMatNames[i])), nullptr, LOAD_NoWarn | LOAD_Quiet);
#if WITH_EDITOR
		if (!M) { EnsureAssets(); M = GCache[i].Get(); }
#endif
		if (!M) {
			UE_LOG(LogATG, Warning, TEXT("Material %s is missing (open the project in the editor once to generate it); using the default material"), *AssetName(GMatNames[i]));
			M = UMaterial::GetDefaultMaterial(MD_Surface);
		}
		GCache[i] = Keep(M);
	}
	return GCache[i].Get();
}
