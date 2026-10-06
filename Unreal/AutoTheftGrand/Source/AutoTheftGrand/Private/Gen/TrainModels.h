// The Sol Line's rolling stock (the models in src/entities/train.js): a diesel locomotive, passenger
// carriages and four kinds of freight wagon (box car, tank car, hopper, container flat). y = 0 at the rail
// head, +z forward. Parts reuse the car materials; EVMat::Body is paint baked into the vertex colours.
#pragma once

#include "VehicleModels.h"

namespace atg {

constexpr double LOCO_L = 17, CAR_L = 20.5, WAG_L = 14.5, TRAIN_GAP = 1.2;
constexpr int TRAIN_NCARS = 3;

struct TrainModel {
	std::vector<VPart> parts;
	Pt3 seat{ 0, 0, 0 };   // (all four seats are the cab seat)
	Pt3 doorPos{ 0, 0, 0 };
};

const TrainModel& LocoModel(uint32_t color);
const TrainModel& CarriageModel(uint32_t color);
// type: box, tank, hopper, flat
const TrainModel& WagonModel(const std::string& type, double seed);

} // namespace atg
