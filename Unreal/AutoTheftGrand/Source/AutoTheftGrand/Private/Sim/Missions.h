// Mission engine (src/game/missions.js). C++20 coroutines preserve the scripts' sequential async flow.
// Waits, failures, cutscenes, entities and objectives are owned by one context and driven by sim frames.
#pragma once
#include "Peds.h"
#include "Pickups.h"
#include "Traffic.h"
#include <coroutine>
#include <exception>
#include <stdexcept>
#include <utility>

namespace atg {
class Missions;
class MissionContext;
struct MissionFail : std::runtime_error { using std::runtime_error::runtime_error; };
struct MissionAbort : std::runtime_error { MissionAbort() : std::runtime_error("aborted") {} };

class MissionTask {
public:
	struct promise_type {
		std::exception_ptr error;
		std::coroutine_handle<> continuation = std::noop_coroutine();
		MissionTask get_return_object() { return MissionTask(std::coroutine_handle<promise_type>::from_promise(*this)); }
		std::suspend_never initial_suspend() noexcept { return {}; }
		struct Final {
			bool await_ready() noexcept { return false; }
			std::coroutine_handle<> await_suspend(std::coroutine_handle<promise_type> h) noexcept { return h.promise().continuation; }
			void await_resume() noexcept {}
		};
		Final final_suspend() noexcept { return {}; }
		void return_void() noexcept {}
		void unhandled_exception() noexcept { error = std::current_exception(); }
	};
	using Handle = std::coroutine_handle<promise_type>;
	MissionTask() = default;
	explicit MissionTask(Handle h) : handle(h) {}
	MissionTask(MissionTask&& other) noexcept : handle(std::exchange(other.handle, {})) {}
	MissionTask& operator=(MissionTask&& other) noexcept { if (this != &other) { if (handle) handle.destroy(); handle = std::exchange(other.handle, {}); } return *this; }
	MissionTask(const MissionTask&) = delete;
	~MissionTask() { if (handle) handle.destroy(); }
	bool done() const { return !handle || handle.done(); }
	std::exception_ptr error() const { return handle ? handle.promise().error : nullptr; }
	struct Awaiter {
		Handle h;
		explicit Awaiter(Handle value) : h(value) {}
		Awaiter(Awaiter&& other) noexcept : h(std::exchange(other.h, {})) {}
		Awaiter(const Awaiter&) = delete;
		~Awaiter() { if (h) h.destroy(); }
		bool await_ready() const { return !h || h.done(); }
		void await_suspend(std::coroutine_handle<> parent) { h.promise().continuation = parent; }
		void await_resume() { if (h && h.promise().error) std::rethrow_exception(h.promise().error); }
	};
	Awaiter operator co_await() && { return Awaiter{ std::exchange(handle, {}) }; }
private:
	Handle handle;
};

struct MissionDef {
	std::string id, title, contact, log;
	bool autoStart = false, allowWanted = false, startInCar = false, noSpray = true;
	double reward = 0;
	std::vector<std::string> requiresIds;
	std::function<V3(const CityMap&)> start;
	std::function<MissionTask(MissionContext&, Game&)> run;
	std::function<void(Game&)> after;
	std::vector<std::string> chapterEnd;
};

struct RouteDriverOpts { double speed = 22, arriveR = 25; bool ignoreLights = true, flee = false, snap = true; };
class RouteDriver : public LaneDriver {
public:
	RouteDriver(Game& g, Vehicle* v, const V3& to, const RouteDriverOpts& opts = {});
	V3 dest;
	bool flee = false, arrived = false;
	double arriveR = 25;
	std::optional<Route> route;
	void update(double dt) override;
protected:
	bool chooseNext(const LanePath& cur, Exit& out) override;
};

class RaceDriver : public VehicleAI {
public:
	RaceDriver(Game& g, Vehicle* v, std::vector<V3> pts, double skillIn = 0.8) : game(g), veh(v), points(std::move(pts)), skill(skillIn) {}
	Game& game; Vehicle* veh;
	std::vector<V3> points;
	int idx = 0, lap = 0; double skill, stuck = 0, rev = 0; bool done = false;
	void update(double dt) override;
	double progress() const;
};

struct UntilOpts { double timeout = 0; bool resolveTimeout = false; std::string timeoutReason = "You ran out of time."; };
struct GoToOpts {
	double radius = NaN(), height = NaN(), y = NaN(); uint32_t color = 0xffd23f;
	bool vehicle = false, onFoot = false, slow = false, stop = false;
	Vehicle* inCar = nullptr;
	std::string text, label, inCarMsg = "You need the right vehicle.";
	std::function<bool()> condition;
};
struct MissionPedOpts : PedOpts { bool defaultScript = true, invincible = false; };
struct MissionEnemyOpts : PedOpts { bool guard = false, blip = true, arrow = true; double face = NaN(); MissionEnemyOpts() { gang = "vipers"; weapon = "pistol"; accuracy = 0.4; damageMul = 0.5; } };
struct Dialogue { std::string speaker, text; double duration = NaN(); };

class MissionContext : public std::enable_shared_from_this<MissionContext> {
public:
	MissionContext(Missions& e, const MissionDef& d);
	~MissionContext() { task = MissionTask(); }
	Missions& engine; Game& game; const MissionDef& def;
	MissionTask task;
	double t = 0;
	bool aborted = false, inCutscene = false, noSpray;
	std::vector<std::shared_ptr<Ped>> peds;
	std::vector<std::shared_ptr<Vehicle>> cars;
	std::vector<std::shared_ptr<Marker>> markers;
	std::vector<std::shared_ptr<Blip>> blips;
	std::vector<Vehicle*> lockedCars;
	std::map<std::string, Ref<Character>> speakers;
	struct AirRing { V3 c; double radius = 16, yaw = 0; };
	std::optional<AirRing> currentRing;
	std::exception_ptr pendingError;
	struct Waiter { std::function<bool(double)> predicate; std::coroutine_handle<> handle; std::exception_ptr error; bool timeout = false; };
	std::vector<std::shared_ptr<Waiter>> waiters;
	struct Until {
		MissionContext* ctx; std::shared_ptr<Waiter> waiter;
		bool await_ready() const { return (bool)ctx->pendingError; }
		void await_suspend(std::coroutine_handle<> h) { waiter->handle = h; ctx->waiters.push_back(waiter); }
		bool await_resume() { if (ctx->pendingError) std::rethrow_exception(ctx->pendingError); if (waiter->error) std::rethrow_exception(waiter->error); return !waiter->timeout; }
	};
	Until until(std::function<bool(double)> check, const UntilOpts& opts = {});
	Until until(std::function<bool()> check, const UntilOpts& opts = {});
	Until wait(double sec);
	std::function<void()> tick(std::function<void(double)> fn);
	std::function<void()> failIf(std::function<bool()> fn, const std::string& reason);
	std::function<void()> failIf(std::function<std::string()> fn);
	std::function<void()> timer(double sec, const std::string& reason = "You ran out of time.");
	void poll(double dt);
	void reject(std::exception_ptr error);
	void abort();
	Player& player() const;
	void help(const std::string& text, double dur = 6);
	void objective(const std::string& text);
	MissionTask say(const std::string& speaker, const std::string& text, double dur = NaN());
	MissionTask lines(std::vector<Dialogue> list);
	MissionTask cutscene(std::function<MissionTask()> fn);
	void shot(const V3& from, const V3& to, double fov = 50, bool snap = true);
	void twoShot(Character* a, Character* b, double side = 1, double dist = 3.2, double height = 1.55);
	void overShoulder(Character* from, Character* to, double dist = 1.4);
	void face(Character* a, Character* b);
	Ped* ped(double x, double z, const MissionPedOpts& opts = {});
	Ped* enemy(double x, double z, const MissionEnemyOpts& opts = {});
	void targetArrow(Ped* p);
	Vehicle* car(const std::string& type, double x, double z, double yaw = 0, const SpawnOpts& opts = {});
	Ped* driver(Vehicle* v, const MissionPedOpts& opts = {}, int seat = 0);
	Ped* follower(Ped* p, int slot = 0);
	Marker* marker(double x, double z, const MarkerOpts& opts = {});
	void removeMarker(Marker* m);
	std::shared_ptr<Blip> blipEntity(Character* c, uint32_t color = 0x4a90ff, const std::string& icon = "dot", bool small = false);
	std::shared_ptr<Blip> blipEntity(Vehicle* v, uint32_t color = 0x4a90ff, const std::string& icon = "dot", bool small = false);
	void unblip(const std::shared_ptr<Blip>& b);
	void gps(double x, double z);
	void gpsOff();
	MissionTask goTo(double x, double z, const GoToOpts& opts = {});
	MissionTask getIn(Vehicle* v, const std::string& text = "");
	MissionTask killAll(std::vector<Ped*> list, const std::string& text = "", const std::string& counter = "");
	MissionTask loseWanted(const std::string& text = "Lose the cops.");
	MissionTask airRing(double x, double z, double alt, double radius = 16, std::optional<V3> next = std::nullopt, const std::string& text = "");
	void wanted(int level);
	std::function<void()> keepAlive(Character* c, const std::string& reason);
	std::function<void()> keepAlive(Vehicle* v, const std::string& reason);
	void cash(double amount);
	double distTo(const V3& pos) const;
	double distTo(Character* c) const;
	double distTo(Vehicle* v) const;
	std::function<void()> driveBy(Ped* p, Character* target = nullptr, double range = 35);
	void cleanup(bool passed);
	void onCleanup(std::function<void()> fn) { cleanupFns.push_back(std::move(fn)); }
private:
	int nextFn = 0;
	std::vector<std::pair<int, std::function<void(double)>>> tickers;
	std::vector<std::pair<int, std::function<std::string()>>> fails;
	std::vector<std::function<void()>> cleanupFns;
};

class Missions : public System {
public:
	explicit Missions(Game& g);
	Game& game;
	std::vector<MissionDef> story;
	std::set<std::string> completed;
	std::shared_ptr<MissionContext> active;
	std::vector<std::shared_ptr<Marker>> contactMarkers;
	std::vector<std::string> log;
	double& maxWanted; bool& noBust;
	std::map<std::string, double>& gangDensity;
	std::function<void()> autosave;
	std::vector<const MissionDef*> available() const;
	void refreshContacts();
	bool start(const std::string& id);
	bool start(const MissionDef& def);
	void update(double dt) override;
	void failActive(const std::string& reason);
	void abortActive();
	void shutdown(); // cancel coroutines while the HUD, police and other game systems are still alive
	bool canEnterVehicle(Vehicle* v) const;
	void load(const std::set<std::string>& ids, const std::vector<std::string>& entries);
private:
	void finish();
	void pass(const std::shared_ptr<MissionContext>& ctx);
	void fail(const std::shared_ptr<MissionContext>& ctx, const std::string& reason);
};
} // namespace atg
