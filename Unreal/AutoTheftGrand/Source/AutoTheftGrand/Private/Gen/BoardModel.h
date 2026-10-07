// The skateboard's model (port of buildBoardModel in src/entities/skateboard.js): the deck with its nose and
// tail kicked up and a little concave, grip tape on top, the painted graphic underneath, the ply edge, two
// trucks and four wheels. The deck is its own frame, raised 0.065 m, so it can flip under the rider.
// Coordinates are the board's own: x left, y up, z forward. Identical to the JavaScript:
// Tools/dumpboard.mjs against Tools/boardtest.cpp.
#pragma once

#include "MeshBuf.h"
#include "VehicleDefs.h"

namespace atg {

struct BoardModel {
	MeshBuf paint;    // the graphic underneath (bodyMaterial(color))
	MeshBuf trim;     // grip tape, the ply edge, the trucks (M.trim)
	MeshBuf wheel;    // one wheel with its hub, turning about x (its own urethane colour)
	double deckY = 0.065;
	struct Wheel { double x, y, z; bool front; };
	std::vector<Wheel> wheels; // (the wheels' pivots in the deck's frame)
};

// the top of the grip tape above the ground (where the rider's feet go)
constexpr double BOARD_DECK_Y = 0.105;

const BoardModel& BuildBoardModel(const VehicleDef& def);

} // namespace atg
