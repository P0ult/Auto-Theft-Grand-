#include "Json.h"
#include <charconv>
#include <cstdio>
#include <cmath>
#include <cstdlib>

namespace atg {

Json& Json::set(const std::string& key, Json v) {
	type = Obj;
	for (auto& kv : o) if (kv.first == key) { kv.second = std::move(v); return kv.second; }
	o.push_back({ key, std::move(v) });
	return o.back().second;
}

bool Json::finite() const { return type == Num && std::isfinite(n); }
bool Json::integer() const { return finite() && n == std::floor(n); }

// ------------------------------------------------------------------ reading
namespace {

void Utf8(std::string& out, unsigned cp) {
	if (cp < 0x80) out += (char)cp;
	else if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 0x3F)); }
	else if (cp < 0x10000) { out += (char)(0xE0 | (cp >> 12)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
	else { out += (char)(0xF0 | (cp >> 18)); out += (char)(0x80 | ((cp >> 12) & 0x3F)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
}

struct Reader {
	const char* p;
	int depth = 0;
	struct Bad {};
	Json value() {
		ws();
		if (++depth > 64) throw Bad{};
		Json v;
		const char c = *p;
		if (c == '{') v = obj();
		else if (c == '[') v = arr();
		else if (c == '"') v = str();
		else if (c == 't') { word("true"); v = Json::Boolean(true); }
		else if (c == 'f') { word("false"); v = Json::Boolean(false); }
		else if (c == 'n') { word("null"); }
		else v = num();
		depth--;
		return v;
	}
	void ws() { while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++; }
	void word(const char* w) { while (*w) { if (*p != *w) throw Bad{}; p++; w++; } }
	unsigned hex4() {
		unsigned v = 0;
		for (int i = 0; i < 4; i++) {
			const char c = *p++;
			v <<= 4;
			if (c >= '0' && c <= '9') v |= c - '0'; else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10; else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10; else throw Bad{};
		}
		return v;
	}
	Json str() {
		Json v = Json::String("");
		if (*p != '"') throw Bad{};
		p++;
		while (*p && *p != '"') {
			if (*p != '\\') { v.s += *p++; continue; }
			p++;
			const char e = *p++;
			switch (e) {
			case 'n': v.s += '\n'; break; case 't': v.s += '\t'; break; case 'r': v.s += '\r'; break;
			case 'b': v.s += '\b'; break; case 'f': v.s += '\f'; break;
			case 'u': {
				unsigned cp = hex4();
				if (cp >= 0xD800 && cp < 0xDC00 && p[0] == '\\' && p[1] == 'u') { p += 2; const unsigned lo = hex4(); if (lo >= 0xDC00 && lo < 0xE000) cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00); }
				Utf8(v.s, cp);
				break;
			}
			case 0: throw Bad{};
			default: v.s += e;
			}
		}
		if (*p != '"') throw Bad{};
		p++;
		return v;
	}
	Json num() {
		char* end = nullptr;
		const double d = std::strtod(p, &end);
		if (end == p) throw Bad{};
		p = end;
		return Json::Number(d);
	}
	Json arr() {
		Json v = Json::Array();
		p++; ws();
		if (*p == ']') { p++; return v; }
		for (;;) {
			v.a.push_back(value());
			ws();
			if (*p == ',') { p++; continue; }
			if (*p == ']') { p++; return v; }
			throw Bad{};
		}
	}
	Json obj() {
		Json v = Json::Object();
		p++; ws();
		if (*p == '}') { p++; return v; }
		for (;;) {
			ws();
			Json k = str();
			ws();
			if (*p != ':') throw Bad{};
			p++;
			v.o.push_back({ k.s, value() });
			ws();
			if (*p == ',') { p++; continue; }
			if (*p == '}') { p++; return v; }
			throw Bad{};
		}
	}
};

void Dump(const Json& v, std::string& s) {
	switch (v.type) {
	case Json::Null: s += "null"; break;
	case Json::Bool: s += v.b ? "true" : "false"; break;
	case Json::Num: {
		if (!std::isfinite(v.n)) { s += "null"; break; }
		if (v.n == 0) { s += '0'; break; } // (and -0, as JSON.stringify writes it)
		char b[40];
		const auto r = std::to_chars(b, b + sizeof b, v.n);
		s.append(b, r.ptr);
		break;
	}
	case Json::Str:
		s += '"';
		for (const char c : v.s) {
			switch (c) {
			case '"': s += "\\\""; break;
			case '\\': s += "\\\\"; break;
			case '\n': s += "\\n"; break;
			case '\r': s += "\\r"; break;
			case '\t': s += "\\t"; break;
			case '\b': s += "\\b"; break;
			case '\f': s += "\\f"; break;
			default:
				if ((unsigned char)c < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", (unsigned)(unsigned char)c); s += b; }
				else s += c;
			}
		}
		s += '"';
		break;
	case Json::Arr:
		s += '[';
		for (size_t i = 0; i < v.a.size(); i++) { if (i) s += ','; Dump(v.a[i], s); }
		s += ']';
		break;
	case Json::Obj:
		s += '{';
		for (size_t i = 0; i < v.o.size(); i++) { if (i) s += ','; Dump(Json::String(v.o[i].first), s); s += ':'; Dump(v.o[i].second, s); }
		s += '}';
		break;
	}
}

} // namespace

Json Json::parse(const std::string& text) {
	Reader r{ text.c_str() };
	try { return r.value(); } catch (const Reader::Bad&) { return Json(); }
}

std::string Json::dump() const { std::string text; Dump(*this, text); return text; }

double JNum(const Json* v, double alt) { return v && v->type == Json::Num && std::isfinite(v->n) ? v->n : alt; }
double JNumAt(const Json& o, const char* key, double alt) { return JNum(o.get(key), alt); }
std::string JStrAt(const Json& o, const char* key) { const Json* v = o.get(key); return v && v->type == Json::Str ? v->s : std::string(); }
bool JTruthy(const Json* v) {
	if (!v) return false;
	switch (v->type) {
	case Json::Null: return false;
	case Json::Bool: return v->b;
	case Json::Num: return v->n != 0 && !std::isnan(v->n);
	case Json::Str: return !v->s.empty();
	default: return true;
	}
}

} // namespace atg
