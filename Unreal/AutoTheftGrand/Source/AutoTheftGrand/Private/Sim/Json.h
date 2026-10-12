// A small JSON value: what the save file (save.js) and the multiplayer state (net.js) need. Parsing never
// throws (bad text gives a null value); dump() writes compact JSON with numbers in their shortest form, the way
// JSON.stringify does, so a browser and this port read each other's messages.
#pragma once

#include <string>
#include <utility>
#include <vector>

namespace atg {

struct Json {
	enum Type { Null, Bool, Num, Str, Arr, Obj } type = Null;
	bool b = false;
	double n = 0;
	std::string s;
	std::vector<Json> a;
	std::vector<std::pair<std::string, Json>> o;

	Json() = default;
	static Json Boolean(bool v) { Json j; j.type = Bool; j.b = v; return j; }
	static Json Number(double v) { Json j; j.type = Num; j.n = v; return j; }
	static Json String(std::string v) { Json j; j.type = Str; j.s = std::move(v); return j; }
	static Json Array() { Json j; j.type = Arr; return j; }
	static Json Object() { Json j; j.type = Obj; return j; }

	const Json* get(const std::string& key) const { for (const auto& kv : o) if (kv.first == key) return &kv.second; return nullptr; }
	const Json* at(size_t i) const { return type == Arr && i < a.size() ? &a[i] : nullptr; }
	bool has(const std::string& key) const { return get(key) != nullptr; }
	// (an object's key, replaced if it is there already)
	Json& set(const std::string& key, Json v);
	Json& push(Json v) { type = Arr; a.push_back(std::move(v)); return a.back(); }

	bool isNum() const { return type == Num; }
	bool isStr() const { return type == Str; }
	bool isArr() const { return type == Arr; }
	bool isObj() const { return type == Obj; }
	// Number.isFinite / Number.isInteger
	bool finite() const;
	bool integer() const;

	static Json parse(const std::string& text);
	std::string dump() const;
};

// field readers that tolerate a missing or wrong-typed value (someone else's data)
double JNum(const Json* v, double alt = 0);
double JNumAt(const Json& o, const char* key, double alt = 0);
std::string JStrAt(const Json& o, const char* key);
bool JTruthy(const Json* v); // (the JavaScript sense: 0, "", null, false and missing are false)

} // namespace atg
