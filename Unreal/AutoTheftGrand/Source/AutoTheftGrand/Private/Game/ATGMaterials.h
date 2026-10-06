// The game's materials. They are ports of the browser game's GLSL (src/world/shaders.js, terrainmesh.js,
// roadmesh.js, city.js) as HLSL custom expressions, built in C++ the first time the editor starts (and saved
// under /Game/ATG/Materials, so they are cooked with the game). At runtime they are loaded by name.
//
// Every generated mesh is uploaded with four UV channels whose meaning depends on the material:
//   Terrain:    uv0 = (r, g), uv1 = (b, farm / -urban), uv2 = (sand, forest), uv3 = (beach, -)
//   Road:       uv0 = (u across, v along mod 36), uv1 = (from start, from end), uv2 = (type, flags), uv3 = (half width, -)
//   Street:     (world position only: Los Soles' grid of streets)
//   Ground:     uv0 = pad local (u, v), uv1 = (type, sidewalk edge distance), uv2 = (pad?, half width), uv3 = (half length, -)
//   Building:   uv0 = facade (u cells, v floors), uv1 = (style, seed), uv2 = (roof * 4 + ground, tint r), uv3 = (tint g, tint b)
//   VertexLit:  uv1 = (r, g), uv2 = (b, glow), uv3 = (signal / -1 concrete, roughness); per-instance data 0 = signal phase,
//               1..3 = tint; a "Tint" vector parameter for per-object colours (car paint, clothes)
//   Frond:      uv0 = (across, along) for the leaflet mask, uv1 / uv2 colour
//   Water:      uv0 = (depth, pool?)
//   Standard:   uv1 = (r, g), uv2.x = b; vector parameters Color (times the vertex colour), Emissive and Surface
//               (roughness, metalness, opacity): three.js's MeshStandardMaterial (vehicles, people, weapons)
//   Glass:      the same, translucent
// The procedural mesh components sit at the world origin, so the materials read world position through the
// Local Position node (no large-world-coordinate types in the custom code).
#pragma once

#include "CoreMinimal.h"

class UMaterialInterface;
class UMaterialParameterCollection;

enum class EATGMat : uint8 { Terrain, Road, Street, Ground, Building, VertexLit, Frond, Water, Standard, Glass, Count };

namespace ATGMaterials {
	// the material (falls back to the engine default if the generated one is missing)
	UMaterialInterface* Get(EATGMat Which);
	UMaterialParameterCollection* Collection();
#if WITH_EDITOR
	// build (and save) any that are missing; called once the editor has started
	void EnsureAssets();
#endif
}
