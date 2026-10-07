// A small work-alike of the Web Audio API, enough for src/game/audio.js to port line by line: an audio context
// rendering stereo in 128-frame quanta, gain, biquad, oscillator, buffer source, panner and compressor nodes,
// and audio params with the same automation rules (setValueAtTime, linear and exponential ramps,
// setTargetAtTime, cancelScheduledValues) that other nodes can modulate. Plain C++: the Unreal side only asks
// it for samples (Game/ATGAudio).
//
// Where it differs from a browser: oscillators are band-limited with PolyBLEP rather than wavetables; the
// panner is the spec's equal-power model (audio.js asks for HRTF); and the convolution reverb is replaced by
// an algorithmic one (Freeverb) with the same decay time, since a 2.2 s convolution is too heavy to run here.
#pragma once

#include <cstdint>
#include <deque>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace atg {
namespace wa {

constexpr int SR = 48000;      // sample rate
constexpr int QUANTUM = 128;   // frames per render quantum
constexpr double FOREVER = std::numeric_limits<double>::infinity();

class Context;
class Node;

// an AudioParam: an intrinsic value, scheduled automation and (a-rate) inputs from other nodes
class Param {
public:
	Param(Context& ctx, Node* owner, double v) : owner(owner), ctx(ctx), segV(v), cur(v) {}
	Node* owner;
	double value() const { return cur; }
	void setValue(double v); // (.value = v: setValueAtTime(v, currentTime))
	Param& setValueAtTime(double v, double t);
	Param& linearRampToValueAtTime(double v, double t);
	Param& exponentialRampToValueAtTime(double v, double t);
	Param& setTargetAtTime(double v, double t, double tc);
	Param& cancelScheduledValues(double t);
	// the values for the current quantum (automation plus the inputs' first channel)
	void process(float* out);
	std::vector<Node*> inputs;
private:
	friend class Context;
	enum Kind { Set, Linear, Exp, Target };
	struct Event { Kind kind; double v, t, tc; };
	Context& ctx;
	std::deque<Event> events;
	// the segment in force: a constant, or a setTarget approach, from (segT, segV)
	bool segTarget = false;
	double segT = 0, segV, segGoal = 0, segTc = 0;
	double cur;
	double segAt(double t) const;
	double at(double t);
	void insert(const Event& e);
};

class Node {
public:
	explicit Node(Context& ctx);
	virtual ~Node() = default;
	Context& ctx;
	// connect this node's output into a node (returns it, for chaining) or into a param
	Node* connect(Node* dst);
	void connect(Param& p);
	void disconnect();
	// pull this quantum's output (computes it once per quantum)
	void pull();
	float out[2][QUANTUM];
	bool mono = true;        // (out[1] equals out[0])
	double expires = FOREVER; // (the context removes the node once its time has passed: one-shot sounds)
protected:
	friend class Context;
	std::vector<Node*> inputs;
	std::vector<Node*> outNodes;
	std::vector<Param*> outParams;
	int64_t lastQuantum = -1;
	float in[2][QUANTUM];
	bool inMono = true;
	void mixInputs();
	virtual void process() = 0;
};

class GainNode : public Node {
public:
	explicit GainNode(Context& ctx);
	Param gain;
protected:
	void process() override;
};

class BiquadFilterNode : public Node {
public:
	enum Type { Lowpass, Highpass, Bandpass };
	explicit BiquadFilterNode(Context& ctx);
	Type type = Lowpass;
	Param frequency, Q;
protected:
	void process() override;
private:
	double x1[2] = {}, x2[2] = {}, y1[2] = {}, y2[2] = {};
	double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, lastF = -1, lastQ = -1; int lastType = -1;
};

class OscillatorNode : public Node {
public:
	enum Type { Sine, Square, Sawtooth, Triangle };
	explicit OscillatorNode(Context& ctx);
	Type type = Sine;
	Param frequency;
	void start(double t = 0) { startT = t; }
	void stop(double t) { stopT = t; }
protected:
	void process() override;
private:
	double startT = FOREVER, stopT = FOREVER, phase = 0;
};

// mono sample data (the noise buffers)
struct Buffer { std::vector<float> data; };

class BufferSourceNode : public Node {
public:
	explicit BufferSourceNode(Context& ctx);
	std::shared_ptr<const Buffer> buffer;
	bool loop = false;
	void start(double t = 0, double offset = 0) { startT = t; startOffset = offset; }
	void stop(double t) { stopT = t; }
protected:
	void process() override;
private:
	double startT = FOREVER, stopT = FOREVER, startOffset = 0;
	int64_t pos = -1;
};

class PannerNode : public Node {
public:
	explicit PannerNode(Context& ctx);
	double x = 0, y = 0, z = 0;
	double refDistance = 1, maxDistance = 10000, rolloffFactor = 1;
protected:
	void process() override;
};

// stands in for the ConvolverNode with audio.js's 2.2 s impulse: a stereo Freeverb with that decay
class ReverbNode : public Node {
public:
	explicit ReverbNode(Context& ctx);
protected:
	void process() override;
private:
	struct Comb { std::vector<float> buf; int i = 0; float store = 0; };
	struct Allpass { std::vector<float> buf; int i = 0; };
	Comb combs[2][8];
	Allpass alls[2][4];
};

class DynamicsCompressorNode : public Node {
public:
	explicit DynamicsCompressorNode(Context& ctx);
	double threshold = -24, knee = 30, ratio = 12, attack = 0.003, release = 0.25;
protected:
	void process() override;
private:
	double envelope = 0, curveK = 0, yKneeDb = 0;
};

class DestinationNode : public Node {
public:
	explicit DestinationNode(Context& ctx) : Node(ctx) {}
protected:
	void process() override;
};

class Context {
public:
	Context();
	~Context();
	double currentTime() const { return (double)frame / SR; }
	int64_t quantum() const { return frame / QUANTUM; }
	DestinationNode* destination;
	// the listener: position, forward and up
	double lx = 0, ly = 0, lz = 0, fx = 0, fy = 0, fz = -1, ux = 0, uy = 1, uz = 0;

	GainNode* createGain();
	BiquadFilterNode* createBiquadFilter();
	OscillatorNode* createOscillator();
	BufferSourceNode* createBufferSource();
	PannerNode* createPanner();
	ReverbNode* createReverb();
	DynamicsCompressorNode* createDynamicsCompressor();

	// render stereo frames (interleaved L R), a whole number of quanta is rendered internally
	void render(float* interleaved, int frames);
	size_t nodeCount() const { return nodes.size(); }

	// the latest stop time scheduled since this was last reset (audio.js: how long a one-shot's panner lives)
	double scheduledEnd = 0;
	void noteEnd(double t) { if (t > scheduledEnd) scheduledEnd = t; }

private:
	friend class Node;
	friend class Param;
	int64_t frame = 0;
	std::vector<std::unique_ptr<Node>> nodes;
	float pending[2][QUANTUM]; int pendingAt = QUANTUM;
	template <typename T> T* make() { auto n = std::make_unique<T>(*this); T* p = n.get(); nodes.push_back(std::move(n)); return p; }
	void collect();
};

} // namespace wa
} // namespace atg
