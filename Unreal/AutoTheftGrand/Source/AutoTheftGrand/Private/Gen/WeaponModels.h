// Procedural weapon models (port of weaponGeometry in src/game/weapondefs.js). Model space: barrel along +z,
// top along +y, origin at the grip. Vertex colours in the vertex-lit layout; empty for fists.
#pragma once

#include "MeshBuf.h"

namespace atg {
const MeshBuf& WeaponGeometry(const std::string& id);
}
