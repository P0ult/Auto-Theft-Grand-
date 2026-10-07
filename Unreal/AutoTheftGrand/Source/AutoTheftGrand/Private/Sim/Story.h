// Story scripts (src/game/story.js). Chapters are registered in browser order as they are ported.
#pragma once
#include "Missions.h"

namespace atg {
std::vector<MissionDef> BuildStory();
Appearance StoryLook(const std::string& key);
MissionPedOpts StoryCast(const std::string& key, bool invincible = false);
V3 StoryLandmark(const Game& game, const std::string& key, double dx = 0, double dz = 0);
const P3& StoryPoint(const Game& game, const std::string& key, const std::string& point);
void AddChapterTwo(std::vector<MissionDef>& story);
void AddChapterThree(std::vector<MissionDef>& story);
void AddChapterFour(std::vector<MissionDef>& story);
void AddChapterFive(std::vector<MissionDef>& story);
MissionTask StoryCountdown(MissionContext& m);
struct StoryRoadSpot { double x, z, yaw; };
StoryRoadSpot StoryRoadNear(Game& game, double x, double z, int lane = 1);
void StoryTeleport(Game& game, double x, double z, double yaw = 0, double y = NaN());
MissionTask FadeTeleport(MissionContext& m, double x, double z, double yaw, double y = NaN());
Appearance GangLook(const std::string& gang);
std::vector<V3> StoryRing(double x, double z, int n, double radius, double phase = 0);
std::vector<Ped*> StoryCrew(MissionContext& m, const std::string& gang, const std::vector<V3>& pts, const std::vector<std::string>& weapons, const MissionEnemyOpts& opts = {});
std::function<void()> AggroWhenNear(MissionContext& m, const std::vector<Ped*>& list, double range = 30);
std::function<void()> CrewSupport(MissionContext& m, std::vector<Ped*> crew, std::function<std::vector<Ped*>()> enemies);
struct ChaseCar { Vehicle* v; Ped* driver; std::vector<Ped*> guns; };
ChaseCar StoryChaseCar(MissionContext& m, const std::string& type, double x, double z, double yaw, const std::string& gang, int shooters = 1, std::function<Appearance()> look = nullptr);
} // namespace atg
