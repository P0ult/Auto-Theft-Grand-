// Renderable geometry for the world, built from the CityMap (ports of terrainmesh.js, roadmesh.js, city.js,
// props.js and vegetation.js). Plain C++: the Unreal side uploads these buffers to procedural mesh
// components (terrain, roads, ground, buildings) and runtime static meshes (props, vegetation, which are
// instanced). The channel layouts per material are described in ATGMaterials.cpp.
#pragma once

#include "CityMap.h"
#include "MeshBuf.h"

namespace atg {

// ------------------------------------------------------------------ terrain (terrainmesh.js)
constexpr double TERRAIN_CH = 256;
constexpr double TERRAIN_LODS[3] = { 4, 16, 64 };
int TerrainChunksX();
int TerrainChunksZ();
// chunks fully inside Los Soles are covered by the city ground
bool TerrainChunkInCity(int i, int j);
// ch0 = (r, g), ch1 = (b, farm / -urban), ch2 = (sand, forest), ch3 = (beach, 0)
MeshBuf BuildTerrainChunk(const CityMap& map, int i, int j, int lod);

// ------------------------------------------------------------------ collision primitives (collision.js)
// What the browser game's builders add to its collision world, in the same order: boxes (axis-aligned),
// oriented boxes, circles (props, trunks, islands) and decks (drivable / walkable sloped strips: bridges,
// viaducts, raised pavements).
struct ColPrim {
	enum Kind : uint8_t { Box, OBox, Circle, Deck } kind = Box;
	double minX = 0, minY = 0, minZ = 0, maxX = 0, maxY = 0, maxZ = 0; // box (minY / maxY also for oriented boxes)
	double cx = 0, cz = 0, hx = 0, hz = 0, yaw = 0;                     // oriented box
	double x = 0, z = 0, r = 0, h = NaN(), y0 = NaN();                  // circle (h: top height)
	double ax = 0, az = 0, ay = 0, bx = 0, bz = 0, by = 0, hl = 0, hr = 0; // deck: centre line a -> b, half widths left / right
	int edge = -1; bool pavement = false, skate = false;
	std::string type;
	bool soft = false, low = false, breakable = false;
	int prop = -1; // circles: index of the prop instance they belong to
};

// ------------------------------------------------------------------ roads (roadmesh.js)
// road: ch0 = (u across, v along mod 36), ch1 = (from start, from end), ch2 = (type, flags), ch3 = (half width, 0)
// concrete (decks, barriers, pillars, islands, pavements, platforms): vertex-lit layout
struct RoadChunk { MeshBuf road, conc, rails; double cx = 0, cz = 0; };
struct Sleeper { double x, y, z, yaw; };
struct RoadMeshes {
	std::map<std::pair<int, int>, RoadChunk> chunks;
	std::vector<Sleeper> sleepers;
	std::vector<ColPrim> prims; // walls, barriers, pillars, platforms, posts, islands (added as built)
	std::vector<ColPrim> decks; // deck surfaces (added after everything else)
};
RoadMeshes BuildRoadMeshes(const CityMap& map);

// ------------------------------------------------------------------ city (city.js)
// Los Soles' street plane: road-grid material (world position only)
MeshBuf BuildStreetPlane();
// ground surfaces: ch0 = pad local (u, v), ch1 = (type, sidewalk edge distance), ch2 = (pad?, half width), ch3 = (half length, 0)
MeshBuf BuildBlocks(const CityMap& map);
MeshBuf BuildLots(const CityMap& map);
MeshBuf BuildPads(const CityMap& map);
// buildings: ch0 = facade (u cells, v floors) / roof coords, ch1 = (style, seed), ch2 = (roof * 4 + ground, tint r), ch3 = (tint g, tint b)
// detail (roof clutter, fences, hedges): vertex-lit layout
struct CityChunk { MeshBuf bld, det; double cx = 0, cz = 0; };
std::map<std::pair<int, int>, CityChunk> BuildBuildings(const CityMap& map);
MeshBuf BuildPools(const CityMap& map);

// ------------------------------------------------------------------ props & vegetation (props.js, vegetation.js)
struct PropTemplate {
	MeshBuf mesh;       // vertex-lit layout
	MeshBuf leaves;     // a second part (tree leaves / palm fronds), may be empty
	bool frondMat = false;
	double r = 0, h = 0; // collision circle
	bool breakable = false;
};
std::map<std::string, PropTemplate> BuildPropTemplates();
MeshBuf ContainerGeo();
struct PropInstance { std::string type; double x, y, z, rot, scale; double phase = 0; };
std::vector<PropInstance> PlaceProps(const CityMap& map);

// (nearMesh / farMesh, not near / far: those are macros on Windows)
struct VegTemplate { MeshBuf nearMesh, nearLeaves, farMesh; bool frond = false; double r = 0, h = 0, nearD = 0, farD = 0; };
std::map<std::string, VegTemplate> BuildVegTemplates();

// collision stand-ins for props, trunks and rocks: octagonal prisms, chunked (400 m)
std::map<std::pair<int, int>, MeshBuf> BuildPropColliders(const CityMap& map, const std::vector<PropInstance>& props, const std::map<std::string, PropTemplate>& defs);

} // namespace atg
