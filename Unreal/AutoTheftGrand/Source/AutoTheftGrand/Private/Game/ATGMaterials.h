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

// FxAlpha / FxAdd: the effects (ATGEffects): particles, decals, skid marks and tracers, alpha blended or additive
// Unlit: vertex colour x Color, shining on its own (three.js MeshBasicMaterial); Picture: a texture (the parameter
// "Picture"): Mode.x its lit colour, Mode.y how bright it shines on its own, Mode.z its roughness (0: 0.9) (posters
// (1, 0), menu boards and shop signs (0, k), billboards (1, k))
// Rain: effects.js Rain, streaks falling round the camera (uv0 = box x, z; uv1 = box y, seed; uv2 = end, side)
// Wood: the pier's planks (landmarks.js pierwood, from the world position); Cutout: a picture with holes (its red
// channel the mask), two-sided, lit (Color x the picture) and glowing (Mode.y): the Vistawood letters
// Prop: a textured model (ATGAssets): the texture parameters Color, Masks (occlusion, roughness, metal) and Normal
// Terrain, Road, Street, Ground and Building have a second form (its asset name ends in T) that draws the
// downloaded surface textures under the game's own colours and markings; it is used when they were imported
enum class EATGMat : uint8 { Terrain, Road, Street, Ground, Building, VertexLit, Frond, Water, Standard, Glass, FxAlpha, FxAdd, Unlit, Picture, Rain, Wood, Cutout, Beam, DeathBlur, Human, DeathGrade, Prop, Count };

namespace ATGMaterials {
	// the material (falls back to the engine default if the generated one is missing)
	UMaterialInterface* Get(EATGMat Which);
	UMaterialParameterCollection* Collection();
#if WITH_EDITOR
	// build (and save) any that are missing; called once the editor has started
	void EnsureAssets();
#endif
}
