// Animal models (port of the catalogues and buildParts in src/entities/animals.js): the species and breeds, and
// each breed's low-poly body, head, legs, tail and wings in its coat's colours. The Animal class (the rig and the
// movement) is not here. Identical to the JavaScript: Tools/dumpanimals.mjs against Tools/animalstest.cpp.
#pragma once

#include "MeshBuf.h"
#include <map>
#include <string>
#include <vector>

namespace atg {

struct AnimalSpecies {
	std::string name;
	bool quad = false;
	bool bird = false;
	bool hop = false;
	double len = 0, h = 0, w = 0, bh = 0;
	double head = 0, snout = 0, neck = 0;
	std::string ears;
	std::string tail;
	double walk = 0, run = 0, fly = 0, fear = 0, hp = 0;
	bool antlers = false;
	bool horns = false;
};

const std::map<std::string, AnimalSpecies>& AnimalSpeciesTable();

struct AnimalBreed {
	std::string species;
	std::string name;
	uint32_t coat[4] = { 0xffffff, 0xffffff, 0xffffff, 0xffffff };
	double scale = 1;
	double price = -1;
	std::string ears;
	std::string tail;
	double legK = NAN;
	double snoutK = NAN;
	bool saddle = false;
	bool fluffy = false;
	uint32_t stripes = 0xffffffff;
	uint32_t patches = 0xffffffff;
};

const std::map<std::string, AnimalBreed>& AnimalBreeds();
const std::vector<std::string>& PetBreeds();
const std::vector<std::string>& AnimalBreedOrder(); // keys in JS insertion order

struct AnimalParts {
	MeshBuf body;
	MeshBuf wing;      // birds only
	MeshBuf head;
	MeshBuf frontLeg;
	MeshBuf hindLeg;
	MeshBuf tail;
	bool bird = false;
	double k = 1;
	double wingY = 0, wingX = 0;
	double headPos[3] = { 0, 0, 0 };
	double hipY = 0;
	double legX = 0, legZf = 0, legZh = 0;
	double tailPos[3] = { 0, 0, 0 };
};

const AnimalParts& BuildAnimalParts(const std::string& breedKey);

} // namespace atg