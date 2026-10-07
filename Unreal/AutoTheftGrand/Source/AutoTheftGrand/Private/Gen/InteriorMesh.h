// A walk-in shop's room (port of buildInteriorMesh in src/world/interiors.js): the tiled floor, walls with their
// dado band, the ceiling and its light panels, the door frame, the counter and till, and each shop's dressing
// (shelves of stock, the Gun Barn's racks, Big Bun's kitchen and tables, the kennels and fish tanks, the bar's
// bottle wall and pool table, the cafe's tables, the liquor store's aisles and fridges). In world coordinates.
// What the meshes can't hold is listed for the renderer: glass boxes, weapons on display, and the posters and
// menu boards (drawn on canvases in the browser). Identical to the JavaScript: Tools/dumpinteriors.mjs against
// Tools/interiorstest.cpp.
#pragma once

#include "CityMap.h"
#include "MeshBuf.h"

namespace atg {

struct InteriorMesh {
	MeshBuf solid;  // vertex coloured, lit (roughness 0.78)
	MeshBuf glow;   // vertex coloured, unlit: light panels, the till's screen, tanks, bottles, fridge fronts
	struct Glass { double x, y, z, sx, sy, sz; };                 // (clear boxes: the gun case, the kennel fronts, the pastry case)
	std::vector<Glass> glass;
	struct Weapon { std::string id; double x, y, z, yaw, roll; };  // (a weapon model, turned by yaw then rolled)
	std::vector<Weapon> weapons;
	// a poster or menu board: which picture, its centre, size and the way it faces (a yaw: it faces its local +z);
	// lit: a poster on the wall (lit) or a board that shines on its own
	struct Panel { std::string picture; double x, y, z, width, height, yaw; bool lit; };
	std::vector<Panel> panels;
};

InteriorMesh BuildInteriorMesh(const InteriorShell& it);

} // namespace atg
