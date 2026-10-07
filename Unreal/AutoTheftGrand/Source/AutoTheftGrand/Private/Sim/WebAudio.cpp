#include "WebAudio.h"
#include <algorithm>
#include <cmath>

namespace atg {
namespace wa {

namespace {
const double WaPi = 3.14159265358979323846;
double WaPolyBlep(double t, double dt) {
	if (dt <= 0) return 0;
	if (t < dt) { t /= dt; return t + t - t * t - 1; }
	if (t > 1 - dt) { t = (t - 1) / dt; return t * t + t + t + 1; }
	return 0;
}
}

// ------------------------------------------------------------------ AudioParam
void Param::insert(const Event& e) {
	auto it = events.begin();
	while (it != events.end() && it->t <= e.t) ++it;
	events.insert(it, e);
}
void Param::setValue(double v) { setValueAtTime(v, ctx.currentTime()); }
Param& Param::setValueAtTime(double v, double t) { if (std::isfinite(v) && std::isfinite(t)) insert({ Set, v, t, 0 }); return *this; }
Param& Param::linearRampToValueAtTime(double v, double t) { if (std::isfinite(v) && std::isfinite(t)) insert({ Linear, v, t, 0 }); return *this; }
Param& Param::exponentialRampToValueAtTime(double v, double t) { if (std::isfinite(v) && std::isfinite(t)) insert({ Exp, v, t, 0 }); return *this; }
Param& Param::setTargetAtTime(double v, double t, double tc) { if (std::isfinite(v) && std::isfinite(t) && std::isfinite(tc)) insert({ Target, v, t, tc }); return *this; }
Param& Param::cancelScheduledValues(double t) {
	while (!events.empty() && events.back().t >= t) events.pop_back();
	return *this;
}

double Param::segAt(double t) const {
	if (!segTarget) return segV;
	if (segTc <= 0) return segGoal;
	return segGoal + (segV - segGoal) * std::exp(-(t - segT) / segTc);
}

// the automation's value at t (t only moves forward): events take effect in order; a ramp runs from the
// previous event's time and value to its own
double Param::at(double t) {
	while (!events.empty()) {
		const Event& e = events.front();
		if (e.kind == Linear || e.kind == Exp) {
			if (t >= e.t) { segTarget = false; segT = e.t; segV = e.v; events.pop_front(); continue; }
			const double v0 = segAt(segT), span = e.t - segT;
			const double f = span > 0 ? (t - segT) / span : 1;
			if (e.kind == Linear) return v0 + (e.v - v0) * f;
			if (v0 == 0 || (v0 > 0) != (e.v > 0)) return v0;
			return v0 * std::pow(e.v / v0, f);
		}
		if (t < e.t) break;
		const double start = segAt(e.t);
		if (e.kind == Set) { segTarget = false; segV = e.v; }
		else { segTarget = true; segV = start; segGoal = e.v; segTc = e.tc; }
		segT = e.t;
		events.pop_front();
	}
	return segAt(t);
}

void Param::process(float* out) {
	const double t0 = ctx.currentTime();
	for (int i = 0; i < QUANTUM; i++) out[i] = (float)at(t0 + (double)i / SR);
	for (Node* n : inputs) {
		n->pull();
		for (int i = 0; i < QUANTUM; i++) out[i] += n->out[0][i];
	}
	cur = out[QUANTUM - 1];
}

// ------------------------------------------------------------------ nodes
Node::Node(Context& c) : ctx(c) {
	for (int ch = 0; ch < 2; ch++) for (int i = 0; i < QUANTUM; i++) out[ch][i] = in[ch][i] = 0;
}

Node* Node::connect(Node* dst) {
	dst->inputs.push_back(this);
	outNodes.push_back(dst);
	return dst;
}
void Node::connect(Param& p) {
	p.inputs.push_back(this);
	outParams.push_back(&p);
}
void Node::disconnect() {
	for (Node* d : outNodes) d->inputs.erase(std::remove(d->inputs.begin(), d->inputs.end(), this), d->inputs.end());
	for (Param* p : outParams) p->inputs.erase(std::remove(p->inputs.begin(), p->inputs.end(), this), p->inputs.end());
	outNodes.clear(); outParams.clear();
}

void Node::mixInputs() {
	inMono = true;
	for (int i = 0; i < QUANTUM; i++) in[0][i] = in[1][i] = 0;
	for (Node* n : inputs) {
		n->pull();
		if (!n->mono) inMono = false;
		for (int i = 0; i < QUANTUM; i++) { in[0][i] += n->out[0][i]; in[1][i] += n->out[n->mono ? 0 : 1][i]; }
	}
}

void Node::pull() {
	if (lastQuantum == ctx.quantum()) return;
	lastQuantum = ctx.quantum();
	mixInputs();
	process();
}

GainNode::GainNode(Context& c) : Node(c), gain(c, this, 1) {}
void GainNode::process() {
	float g[QUANTUM];
	gain.process(g);
	mono = inMono;
	for (int i = 0; i < QUANTUM; i++) { out[0][i] = in[0][i] * g[i]; out[1][i] = in[1][i] * g[i]; }
}

BiquadFilterNode::BiquadFilterNode(Context& c) : Node(c), frequency(c, this, 350), Q(c, this, 1) {}
void BiquadFilterNode::process() {
	float fr[QUANTUM], qq[QUANTUM];
	frequency.process(fr); Q.process(qq);
	mono = inMono;
	const int chans = mono ? 1 : 2;
	const double nyq = SR / 2.0;
	for (int i = 0; i < QUANTUM; i++) {
		if (fr[i] != lastF || qq[i] != lastQ || type != lastType) {
			lastF = fr[i]; lastQ = qq[i]; lastType = type;
			// the Audio EQ cookbook as the Web Audio spec uses it (lowpass / highpass Q in dB)
			const double f = std::min(std::max((double)fr[i], 1.0), nyq * 0.999);
			const double w0 = 2 * WaPi * f / SR, cw = std::cos(w0), sw = std::sin(w0);
			double a0, alpha;
			if (type == Bandpass) {
				alpha = sw / (2 * std::max(1e-4, (double)qq[i]));
				b0 = alpha; b1 = 0; b2 = -alpha;
			} else {
				alpha = sw / (2 * std::pow(10.0, qq[i] / 20.0));
				if (type == Lowpass) { b0 = (1 - cw) / 2; b1 = 1 - cw; b2 = b0; }
				else { b0 = (1 + cw) / 2; b1 = -(1 + cw); b2 = b0; }
			}
			a0 = 1 + alpha;
			b0 /= a0; b1 /= a0; b2 /= a0; a1 = -2 * cw / a0; a2 = (1 - alpha) / a0;
		}
		for (int ch = 0; ch < chans; ch++) {
			const double x = in[ch][i];
			const double y = b0 * x + b1 * x1[ch] + b2 * x2[ch] - a1 * y1[ch] - a2 * y2[ch];
			x2[ch] = x1[ch]; x1[ch] = x; y2[ch] = y1[ch]; y1[ch] = std::abs(y) < 1e-25 ? 0 : y;
			out[ch][i] = (float)y;
		}
		if (mono) out[1][i] = out[0][i];
	}
}

OscillatorNode::OscillatorNode(Context& c) : Node(c), frequency(c, this, 440) {}
void OscillatorNode::process() {
	float fr[QUANTUM];
	frequency.process(fr);
	mono = true;
	const double t0 = ctx.currentTime();
	for (int i = 0; i < QUANTUM; i++) {
		const double t = t0 + (double)i / SR;
		float v = 0;
		if (t >= startT && t < stopT) {
			const double dt = std::min(0.5, std::abs((double)fr[i]) / SR);
			const double p = phase;
			switch (type) {
			case Sine: v = (float)std::sin(2 * WaPi * p); break;
			case Sawtooth: v = (float)(2 * p - 1 - WaPolyBlep(p, dt)); break;
			case Square: v = (float)((p < 0.5 ? 1 : -1) + WaPolyBlep(p, dt) - WaPolyBlep(std::fmod(p + 0.5, 1.0), dt)); break;
			case Triangle: v = (float)(1 - 4 * std::abs(p - 0.5)); break;
			}
			phase += fr[i] / SR;
			phase -= std::floor(phase);
		}
		out[0][i] = out[1][i] = v;
	}
}

BufferSourceNode::BufferSourceNode(Context& c) : Node(c) {}
void BufferSourceNode::process() {
	mono = true;
	const double t0 = ctx.currentTime();
	const int64_t len = buffer ? (int64_t)buffer->data.size() : 0;
	for (int i = 0; i < QUANTUM; i++) {
		const double t = t0 + (double)i / SR;
		float v = 0;
		if (len > 0 && t >= startT && t < stopT) {
			if (pos < 0) { pos = (int64_t)std::llround(startOffset * SR); if (loop) pos %= len; }
			if (pos < len) {
				v = buffer->data[(size_t)pos];
				pos++;
				if (loop && pos >= len) pos = 0;
			}
		}
		out[0][i] = out[1][i] = v;
	}
}

PannerNode::PannerNode(Context& c) : Node(c) {}
void PannerNode::process() {
	mono = false;
	// distance: the inverse model
	double sx = x - ctx.lx, sy = y - ctx.ly, sz = z - ctx.lz;
	const double d = std::sqrt(sx * sx + sy * sy + sz * sz);
	const double dg = refDistance / (refDistance + rolloffFactor * (std::max(d, refDistance) - refDistance));
	// azimuth (the spec's), then equal-power panning of a mono input
	double gl = std::cos(WaPi / 4), gr = std::sin(WaPi / 4);
	if (d > 1e-6) {
		sx /= d; sy /= d; sz /= d;
		// the listener's right = forward x up, and up re-orthogonalised
		double rx = ctx.fy * ctx.uz - ctx.fz * ctx.uy, ry = ctx.fz * ctx.ux - ctx.fx * ctx.uz, rz = ctx.fx * ctx.uy - ctx.fy * ctx.ux;
		const double rl = std::sqrt(rx * rx + ry * ry + rz * rz);
		if (rl > 1e-9) {
			rx /= rl; ry /= rl; rz /= rl;
			const double upx = ry * ctx.fz - rz * ctx.fy, upy = rz * ctx.fx - rx * ctx.fz, upz = rx * ctx.fy - ry * ctx.fx;
			const double up = sx * upx + sy * upy + sz * upz;
			double px = sx - up * upx, py = sy - up * upy, pz = sz - up * upz;
			const double pl = std::sqrt(px * px + py * py + pz * pz);
			if (pl > 1e-9) {
				px /= pl; py /= pl; pz /= pl;
				double az = std::acos(std::max(-1.0, std::min(1.0, px * rx + py * ry + pz * rz))) * 180 / WaPi;
				if (px * ctx.fx + py * ctx.fy + pz * ctx.fz < 0) az = 360 - az;
				az = (az >= 0 && az <= 270) ? 90 - az : 450 - az;
				if (az < -90) az = -180 - az; else if (az > 90) az = 180 - az;
				const double xk = (az + 90) / 180;
				gl = std::cos(xk * WaPi / 2); gr = std::sin(xk * WaPi / 2);
			}
		}
	}
	for (int i = 0; i < QUANTUM; i++) {
		const float s = inMono ? in[0][i] : (in[0][i] + in[1][i]) * 0.5f;
		out[0][i] = (float)(s * gl * dg); out[1][i] = (float)(s * gr * dg);
	}
}

ReverbNode::ReverbNode(Context& c) : Node(c) {
	static const int CombT[8] = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 }, AllT[4] = { 556, 441, 341, 225 };
	for (int ch = 0; ch < 2; ch++) {
		for (int k = 0; k < 8; k++) combs[ch][k].buf.assign((size_t)((CombT[k] + ch * 23) * SR / 44100), 0.f);
		for (int k = 0; k < 4; k++) alls[ch][k].buf.assign((size_t)((AllT[k] + ch * 23) * SR / 44100), 0.f);
	}
}
void ReverbNode::process() {
	mono = false;
	// a decay to -60 dB in about 2 s, like the impulse's (1 - t)^2.5 over 2.2 s
	const float feedback = 0.912f, damp = 0.2f, inGain = 0.015f, wet = 3.0f;
	for (int i = 0; i < QUANTUM; i++) {
		const float x = (in[0][i] + in[1][i]) * inGain;
		for (int ch = 0; ch < 2; ch++) {
			float acc = 0;
			for (Comb& c : combs[ch]) {
				const float y = c.buf[(size_t)c.i];
				c.store = y * (1 - damp) + c.store * damp;
				c.buf[(size_t)c.i] = x + c.store * feedback;
				if (++c.i >= (int)c.buf.size()) c.i = 0;
				acc += y;
			}
			for (Allpass& a : alls[ch]) {
				const float b = a.buf[(size_t)a.i];
				const float y = -acc + b;
				a.buf[(size_t)a.i] = acc + b * 0.5f;
				if (++a.i >= (int)a.buf.size()) a.i = 0;
				acc = y;
			}
			out[ch][i] = acc * wet;
		}
	}
}

DynamicsCompressorNode::DynamicsCompressorNode(Context& c) : Node(c) {}
void DynamicsCompressorNode::process() {
	mono = inMono;
	// the static curve as Chromium builds it (DynamicsCompressorKernel): linear up to the threshold, an
	// exponential knee whose sharpness k makes the slope 1 / ratio at threshold + knee, then that ratio; and
	// the make-up gain (1 / curve(0 dBFS))^0.6
	const double lt = std::pow(10.0, threshold / 20);
	auto kneeCurve = [&](double x, double k) { return x < lt ? x : lt + (1 - std::exp(-k * (x - lt))) / k; };
	const double kneeDb = threshold + knee, kneeLin = std::pow(10.0, kneeDb / 20);
	auto dB = [](double v) { return 20 * std::log10(std::max(v, 1e-12)); };
	auto slopeAt = [&](double x, double k) {
		if (x < lt) return 1.0;
		const double x2 = x * 1.001;
		return (dB(kneeCurve(x2, k)) - dB(kneeCurve(x, k))) / (dB(x2) - dB(x));
	};
	if (curveK <= 0) {
		double minK = 0.1, maxK = 10000, k = 5;
		for (int i = 0; i < 15; i++) { if (slopeAt(kneeLin, k) < 1 / ratio) maxK = k; else minK = k; k = std::sqrt(minK * maxK); }
		curveK = k;
		yKneeDb = dB(kneeCurve(kneeLin, k));
	}
	auto saturate = [&](double x) {
		if (x < kneeLin) return kneeCurve(x, curveK);
		return std::pow(10.0, (yKneeDb + (dB(x) - kneeDb) / ratio) / 20);
	};
	const double makeup = std::pow(1 / saturate(1), 0.6);
	const double ka = std::exp(-1.0 / (attack * SR)), kr = std::exp(-1.0 / (release * SR));
	for (int i = 0; i < QUANTUM; i++) {
		const double peak = std::max(std::abs(in[0][i]), std::abs(in[1][i]));
		envelope = peak > envelope ? peak + (envelope - peak) * ka : peak + (envelope - peak) * kr;
		const double g = (envelope > lt ? saturate(envelope) / envelope : 1) * makeup;
		out[0][i] = (float)(in[0][i] * g); out[1][i] = (float)(in[1][i] * g);
	}
}

void DestinationNode::process() {
	mono = inMono;
	for (int i = 0; i < QUANTUM; i++) { out[0][i] = in[0][i]; out[1][i] = in[1][i]; }
}

// ------------------------------------------------------------------ the context
Context::Context() {
	for (int ch = 0; ch < 2; ch++) for (int i = 0; i < QUANTUM; i++) pending[ch][i] = 0;
	destination = make<DestinationNode>();
}
Context::~Context() = default;

GainNode* Context::createGain() { return make<GainNode>(); }
BiquadFilterNode* Context::createBiquadFilter() { return make<BiquadFilterNode>(); }
OscillatorNode* Context::createOscillator() { return make<OscillatorNode>(); }
BufferSourceNode* Context::createBufferSource() { return make<BufferSourceNode>(); }
PannerNode* Context::createPanner() { return make<PannerNode>(); }
ReverbNode* Context::createReverb() { return make<ReverbNode>(); }
DynamicsCompressorNode* Context::createDynamicsCompressor() { return make<DynamicsCompressorNode>(); }

void Context::render(float* outp, int frames) {
	int done = 0;
	while (done < frames) {
		if (pendingAt >= QUANTUM) {
			destination->pull();
			for (int i = 0; i < QUANTUM; i++) {
				pending[0][i] = destination->out[0][i];
				pending[1][i] = destination->out[destination->mono ? 0 : 1][i];
			}
			frame += QUANTUM;
			pendingAt = 0;
			if (quantum() % 8 == 0) collect();
		}
		const int n = std::min(frames - done, QUANTUM - pendingAt);
		for (int i = 0; i < n; i++) {
			const float l = pending[0][pendingAt + i], r = pending[1][pendingAt + i];
			outp[(done + i) * 2] = std::isfinite(l) ? std::max(-1.f, std::min(1.f, l)) : 0.f;
			outp[(done + i) * 2 + 1] = std::isfinite(r) ? std::max(-1.f, std::min(1.f, r)) : 0.f;
		}
		pendingAt += n;
		done += n;
	}
}

// drop the nodes whose time is up (finished one-shot sounds), unhooking them from everything else
void Context::collect() {
	const double now = currentTime();
	bool any = false;
	for (const auto& n : nodes) if (n->expires < now) { any = true; break; }
	if (!any) return;
	std::vector<Node*> dead;
	for (const auto& n : nodes) if (n->expires < now) dead.push_back(n.get());
	auto isDead = [&](Node* n) { return std::find(dead.begin(), dead.end(), n) != dead.end(); };
	for (Node* d : dead) {
		d->disconnect();
		for (Node* src : d->inputs) src->outNodes.erase(std::remove(src->outNodes.begin(), src->outNodes.end(), d), src->outNodes.end());
		d->inputs.clear();
	}
	// params of dead nodes that live nodes modulate
	for (const auto& n : nodes) {
		if (isDead(n.get())) continue;
		auto& ps = n->outParams;
		ps.erase(std::remove_if(ps.begin(), ps.end(), [&](Param* p) { return isDead(p->owner); }), ps.end());
	}
	nodes.erase(std::remove_if(nodes.begin(), nodes.end(), [&](const std::unique_ptr<Node>& n) { return isDead(n.get()); }), nodes.end());
}

} // namespace wa
} // namespace atg
