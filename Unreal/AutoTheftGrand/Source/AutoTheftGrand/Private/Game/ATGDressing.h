// Street dressing the browser game doesn't have: downloaded models (ATGAssets) set beside the generated props,
// with collision circles of their own (so the simulation treats them as props: people and cars bump into them,
// the light ones can be knocked over). They are added after the generator, at the end of the prop list, so the
// generated props keep their indices, and from a fixed seed, so every run gets the same street. Plain C++.
#pragma once

#include <map>
#include <string>
#include <utility>
#include <vector>

namespace atg { class CityMap; struct PropInstance; struct PropTemplate; }

namespace ATGDressing {
	// the kinds that can be placed: utilitybox0/1, planter0/1, barrel0/1/2, crate0/1, cardboard, trashbag
	const std::vector<std::string>& Kinds();
	// Have: the kinds that were imported, with their collision radius and height (metres). Adds templates for them
	// to Defs and instances to Props; returns how many it placed.
	int Place(const atg::CityMap& Map, std::vector<atg::PropInstance>& Props, std::map<std::string, atg::PropTemplate>& Defs,
		const std::map<std::string, std::pair<double, double>>& Have);
}
