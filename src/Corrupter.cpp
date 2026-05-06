#include "plugin.hpp"
#include "corrupter_dsp/engine.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

static constexpr uint32_t kMaxBlockFrames = 256;
static constexpr float kDefaultSampleRate = 48000.f;
static constexpr float kMaxBufferSeconds = 30.f;
static constexpr int kWaveBins = 128;

static const char* kAlgoNames[] = {
	"DECIMATE", "DROPOUT", "DESTROY", "DJ FILTER", "VINYL SIM"
};

// ---------------------------------------------------------------------------
// Scale definitions (ratios, last entry is the period = 2.0 for octave scales)
// ---------------------------------------------------------------------------

struct ScaleDef {
	const char* name;
	const double* ratios;
	uint32_t num_notes;
};

static const double kChromatic[] = {
	16.0/15.0, 9.0/8.0, 6.0/5.0, 5.0/4.0, 4.0/3.0, 45.0/32.0,
	3.0/2.0, 8.0/5.0, 5.0/3.0, 9.0/5.0, 15.0/8.0, 2.0
};
static const double kMajor[] = {
	9.0/8.0, 5.0/4.0, 4.0/3.0, 3.0/2.0, 5.0/3.0, 15.0/8.0, 2.0
};
static const double kMinor[] = {
	9.0/8.0, 6.0/5.0, 4.0/3.0, 3.0/2.0, 8.0/5.0, 9.0/5.0, 2.0
};
static const double kPentatonicMaj[] = {
	9.0/8.0, 5.0/4.0, 3.0/2.0, 5.0/3.0, 2.0
};
static const double kPentatonicMin[] = {
	6.0/5.0, 4.0/3.0, 3.0/2.0, 9.0/5.0, 2.0
};
static const double kWholeTone[] = {
	9.0/8.0, 5.0/4.0, 45.0/32.0, 8.0/5.0, 9.0/5.0, 2.0
};
static const double kOctaves[] = { 2.0 };
static const double kFifths[] = { 3.0/2.0, 2.0 };

static const ScaleDef kScales[] = {
	{ "None (free)",       nullptr,         0 },
	{ "Chromatic",         kChromatic,     12 },
	{ "Major",             kMajor,          7 },
	{ "Minor",             kMinor,          7 },
	{ "Pentatonic Major",  kPentatonicMaj,  5 },
	{ "Pentatonic Minor",  kPentatonicMin,  5 },
	{ "Whole Tone",        kWholeTone,      6 },
	{ "Octaves",           kOctaves,        1 },
	{ "Fifths",            kFifths,         2 },
};
static constexpr int kNumScales = static_cast<int>(sizeof(kScales) / sizeof(kScales[0]));

static const char* kRootNames[] = {
	"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};

// ---------------------------------------------------------------------------
// Module
// ---------------------------------------------------------------------------

struct CorrupterModule : Module {

	enum ParamId {
		PARAM_TIME, PARAM_REPEATS, PARAM_MIX,
		PARAM_BEND, PARAM_BREAK, PARAM_CORRUPT,
		PARAM_BEND_CV_ATTN, PARAM_BREAK_CV_ATTN, PARAM_CORRUPT_CV_ATTN,
		PARAM_GLITCH_WINDOW,
		PARAM_BEND_ENABLE, PARAM_BREAK_ENABLE, PARAM_FREEZE_ENABLE,
		PARAM_MODE, PARAM_CORRUPT_ALGO, PARAM_CORRUPT_BANK,
		PARAM_BREAK_MICRO_MODE, PARAM_STEREO_MODE,
		NUM_PARAMS
	};

	enum InputId {
		INPUT_L, INPUT_R,
		INPUT_TIME_CV, INPUT_REPEATS_CV, INPUT_MIX_CV,
		INPUT_BEND_CV, INPUT_BREAK_CV, INPUT_CORRUPT_CV,
		INPUT_BEND_GATE, INPUT_BREAK_GATE,
		INPUT_FREEZE_GATE, INPUT_CLOCK,
		NUM_INPUTS
	};

	enum OutputId {
		OUTPUT_L, OUTPUT_R,
		NUM_OUTPUTS
	};

	enum LightId {
		LIGHT_BEND_ENABLE, LIGHT_BREAK_ENABLE, LIGHT_FREEZE_ENABLE,
		LIGHT_MODE_GREEN, LIGHT_MODE_RED,
		LIGHT_ALGO_0, LIGHT_ALGO_1, LIGHT_ALGO_2, LIGHT_ALGO_3, LIGHT_ALGO_4,
		LIGHT_CORRUPT_BANK,
		LIGHT_BREAK_MICRO,
		LIGHT_STEREO,
		LIGHT_GATE_MODE,
		LIGHT_CLOCK_SOURCE,
		NUM_LIGHTS
	};

	// DSP engine
	corrupter::Engine engine;
	void* dram = nullptr;
	bool initialised = false;

	// Persistent state (survives patch save/load)
	corrupter::PersistentState persistent;
	bool clock_internal = true;

	// Right-click menu settings (index into option lists)
	int gate_mode_index = 0;    // 0=Latching, 1=Momentary
	int scale_index = 0;        // index into kScales[]
	int scale_root = 0;         // 0=C, 1=C#, ... 11=B

	// Block accumulation buffers
	float in_l_buf[kMaxBlockFrames] = {};
	float in_r_buf[kMaxBlockFrames] = {};
	float out_l_buf[kMaxBlockFrames] = {};
	float out_r_buf[kMaxBlockFrames] = {};
	float cv_time_buf[kMaxBlockFrames] = {};
	float cv_repeats_buf[kMaxBlockFrames] = {};
	float cv_mix_buf[kMaxBlockFrames] = {};
	float cv_bend_buf[kMaxBlockFrames] = {};
	float cv_break_buf[kMaxBlockFrames] = {};
	float cv_corrupt_buf[kMaxBlockFrames] = {};
	float gate_bend_buf[kMaxBlockFrames] = {};
	float gate_break_buf[kMaxBlockFrames] = {};
	float gate_freeze_buf[kMaxBlockFrames] = {};
	float gate_clock_buf[kMaxBlockFrames] = {};
	uint32_t buf_pos = 0;
	uint32_t read_pos = 0;
	bool first_block = true;

	// Button triggers
	dsp::BooleanTrigger bendEnableTrig;
	dsp::BooleanTrigger breakEnableTrig;
	dsp::BooleanTrigger freezeEnableTrig;
	dsp::BooleanTrigger modeTrig;
	dsp::BooleanTrigger algoTrig;
	dsp::BooleanTrigger bankTrig;
	dsp::BooleanTrigger breakMicroTrig;
	dsp::BooleanTrigger stereoTrig;

	// Waveform display data (shared with widget)
	uint8_t wave_peaks[kWaveBins] = {};
	uint32_t wave_write_pos = 0;
	float wave_accum = 0.f;
	uint32_t wave_accum_count = 0;
	bool display_dirty = true;
	int current_algo = 0;

	CorrupterModule() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);

		// Large knobs (0-1)
		configParam(PARAM_TIME, 0.f, 1.f, 0.5f, "Time", "%", 0.f, 100.f);
		configParam(PARAM_REPEATS, 0.f, 1.f, 0.5f, "Repeats", "%", 0.f, 100.f);
		configParam(PARAM_MIX, 0.f, 1.f, 1.f, "Mix", "%", 0.f, 100.f);
		configParam(PARAM_BEND, 0.f, 1.f, 0.f, "Bend", "%", 0.f, 100.f);
		configParam(PARAM_BREAK, 0.f, 1.f, 0.f, "Break", "%", 0.f, 100.f);
		configParam(PARAM_CORRUPT, 0.f, 1.f, 0.f, "Corrupt", "%", 0.f, 100.f);

		// Small knobs
		configParam(PARAM_BEND_CV_ATTN, 0.f, 1.f, 1.f, "Bend CV Attn", "%", 0.f, 100.f);
		configParam(PARAM_BREAK_CV_ATTN, 0.f, 1.f, 1.f, "Break CV Attn", "%", 0.f, 100.f);
		configParam(PARAM_CORRUPT_CV_ATTN, 0.f, 1.f, 1.f, "Corrupt CV Attn", "%", 0.f, 100.f);
		configParam(PARAM_GLITCH_WINDOW, 0.f, 1.f, 0.02f, "Glitch Window", "%", 0.f, 100.f);

		// Momentary buttons
		configButton(PARAM_BEND_ENABLE, "Bend Enable");
		configButton(PARAM_BREAK_ENABLE, "Break Enable");
		configButton(PARAM_FREEZE_ENABLE, "Freeze Enable");
		configButton(PARAM_MODE, "Mode (Macro/Micro)");
		configButton(PARAM_CORRUPT_ALGO, "Corrupt Algorithm");
		configButton(PARAM_CORRUPT_BANK, "Corrupt Bank");
		configButton(PARAM_BREAK_MICRO_MODE, "Break Micro Mode");
		configButton(PARAM_STEREO_MODE, "Stereo Mode");

		// Inputs
		configInput(INPUT_L, "Audio L");
		configInput(INPUT_R, "Audio R");
		configInput(INPUT_TIME_CV, "Time CV");
		configInput(INPUT_REPEATS_CV, "Repeats CV");
		configInput(INPUT_MIX_CV, "Mix CV");
		configInput(INPUT_BEND_CV, "Bend CV");
		configInput(INPUT_BREAK_CV, "Break CV");
		configInput(INPUT_CORRUPT_CV, "Corrupt CV");
		configInput(INPUT_BEND_GATE, "Bend Gate");
		configInput(INPUT_BREAK_GATE, "Break Gate");
		configInput(INPUT_FREEZE_GATE, "Freeze Gate");
		configInput(INPUT_CLOCK, "Clock");

		// Outputs
		configOutput(OUTPUT_L, "Audio L");
		configOutput(OUTPUT_R, "Audio R");

		initEngine(kDefaultSampleRate);
	}

	~CorrupterModule() override {
		if (dram) {
			free(dram);
			dram = nullptr;
		}
	}

	void initEngine(float sampleRate) {
		if (dram) {
			free(dram);
			dram = nullptr;
		}
		initialised = false;

		corrupter::EngineConfig cfg;
		cfg.sample_rate_hz = sampleRate;
		cfg.max_supported_sample_rate_hz = 96000.f;
		cfg.max_block_frames = kMaxBlockFrames;
		cfg.max_buffer_seconds = kMaxBufferSeconds;
		cfg.random_seed = 1;

		size_t dram_bytes = corrupter::Engine::required_dram_bytes(cfg);
		dram = malloc(dram_bytes);
		if (!dram) return;

		initialised = engine.initialise(dram, dram_bytes, cfg);
		if (initialised) {
			engine.set_audio_context(sampleRate, kMaxBlockFrames);
			engine.set_persistent_state(persistent);
			applyScale();
		}

		// Reset block buffers
		buf_pos = 0;
		read_pos = 0;
		first_block = true;
	}

	void onSampleRateChange() override {
		initEngine(APP->engine->getSampleRate());
	}

	void applyScale() {
		if (!initialised) return;
		if (scale_index > 0 && scale_index < kNumScales) {
			const ScaleDef& s = kScales[scale_index];
			engine.load_scale(s.ratios, s.num_notes);
		} else {
			engine.clear_scale();
		}
		engine.set_scale_root(60 + scale_root);  // MIDI note: C4 + semitones
	}

	void processBlock() {
		if (!initialised) return;

		// Sync knobs every block
		corrupter::KnobState knobs;
		knobs.time_01 = params[PARAM_TIME].getValue();
		knobs.repeats_01 = params[PARAM_REPEATS].getValue();
		knobs.mix_01 = params[PARAM_MIX].getValue();
		knobs.bend_01 = params[PARAM_BEND].getValue();
		knobs.break_01 = params[PARAM_BREAK].getValue();
		knobs.corrupt_01 = params[PARAM_CORRUPT].getValue();
		knobs.bend_cv_attn_01 = params[PARAM_BEND_CV_ATTN].getValue();
		knobs.break_cv_attn_01 = params[PARAM_BREAK_CV_ATTN].getValue();
		knobs.corrupt_cv_attn_01 = params[PARAM_CORRUPT_CV_ATTN].getValue();
		engine.set_knobs(knobs);

		// Process button triggers — read current state from engine first,
		// toggle the relevant field, then write it back. This avoids
		// overwriting gate-toggled state every block.
		bool state_dirty = false;
		engine.get_persistent_state(&persistent);

		if (bendEnableTrig.process(params[PARAM_BEND_ENABLE].getValue() > 0.f)) {
			persistent.bend_enabled = !persistent.bend_enabled;
			state_dirty = true;
		}
		if (breakEnableTrig.process(params[PARAM_BREAK_ENABLE].getValue() > 0.f)) {
			persistent.break_enabled = !persistent.break_enabled;
			state_dirty = true;
		}
		if (freezeEnableTrig.process(params[PARAM_FREEZE_ENABLE].getValue() > 0.f)) {
			persistent.freeze_enabled = !persistent.freeze_enabled;
			state_dirty = true;
		}
		if (modeTrig.process(params[PARAM_MODE].getValue() > 0.f)) {
			persistent.macro_mode = !persistent.macro_mode;
			state_dirty = true;
		}
		if (algoTrig.process(params[PARAM_CORRUPT_ALGO].getValue() > 0.f)) {
			int next = (static_cast<int>(persistent.corrupt_algorithm) + 1) % 5;
			persistent.corrupt_algorithm = static_cast<corrupter::CorruptAlgorithm>(next);
			// Auto-derive bank: algos 0-2 = Legacy, 3-4 = Expanded
			persistent.corrupt_bank = (next >= 3)
				? corrupter::CorruptBank::kExpanded : corrupter::CorruptBank::kLegacy;
			state_dirty = true;
		}
		if (breakMicroTrig.process(params[PARAM_BREAK_MICRO_MODE].getValue() > 0.f)) {
			persistent.break_silence_mode = !persistent.break_silence_mode;
			state_dirty = true;
		}
		if (stereoTrig.process(params[PARAM_STEREO_MODE].getValue() > 0.f)) {
			persistent.unique_stereo_mode = !persistent.unique_stereo_mode;
			state_dirty = true;
		}
		// Gate mode driven by right-click menu — sync both latching fields
		bool latching = (gate_mode_index == 0);
		if (persistent.gate_latching != latching || persistent.freeze_latching != latching) {
			persistent.gate_latching = latching;
			persistent.freeze_latching = latching;
			state_dirty = true;
		}

		// Clock source: auto-detect from cable presence
		bool internal = !inputs[INPUT_CLOCK].isConnected();
		if (clock_internal != internal) {
			clock_internal = internal;
			engine.set_clock_mode_internal(clock_internal);
		}

		// Glitch window is a continuous param — always sync
		float gw = params[PARAM_GLITCH_WINDOW].getValue();
		if (std::abs(persistent.glitch_window_01 - gw) > 1e-6f) {
			persistent.glitch_window_01 = gw;
			state_dirty = true;
		}

		// Only write persistent state when a button was pressed
		if (state_dirty)
			engine.set_persistent_state(persistent);

		// Audio block
		corrupter::AudioBlock audio;
		audio.in_l = in_l_buf;
		audio.in_r = in_r_buf;
		audio.out_l = out_l_buf;
		audio.out_r = out_r_buf;
		audio.frames = buf_pos;

		// CV inputs - pass nullptr if unconnected
		corrupter::CvInputs cv;
		cv.time_v = inputs[INPUT_TIME_CV].isConnected() ? cv_time_buf : nullptr;
		cv.repeats_v = inputs[INPUT_REPEATS_CV].isConnected() ? cv_repeats_buf : nullptr;
		cv.mix_v = inputs[INPUT_MIX_CV].isConnected() ? cv_mix_buf : nullptr;
		cv.bend_v = inputs[INPUT_BEND_CV].isConnected() ? cv_bend_buf : nullptr;
		cv.break_v = inputs[INPUT_BREAK_CV].isConnected() ? cv_break_buf : nullptr;
		cv.corrupt_v = inputs[INPUT_CORRUPT_CV].isConnected() ? cv_corrupt_buf : nullptr;

		// Gate inputs
		corrupter::GateInputs gates;
		gates.bend_gate_v = inputs[INPUT_BEND_GATE].isConnected() ? gate_bend_buf : nullptr;
		gates.break_gate_v = inputs[INPUT_BREAK_GATE].isConnected() ? gate_break_buf : nullptr;
		gates.freeze_gate_v = inputs[INPUT_FREEZE_GATE].isConnected() ? gate_freeze_buf : nullptr;
		gates.clock_gate_v = inputs[INPUT_CLOCK].isConnected() ? gate_clock_buf : nullptr;

		engine.process(audio, cv, gates);

		// Read back engine state after processing (gates may have toggled enables)
		engine.get_persistent_state(&persistent);

		// Update lights from engine's actual state
		lights[LIGHT_BEND_ENABLE].setBrightness(persistent.bend_enabled ? 1.f : 0.f);
		lights[LIGHT_BREAK_ENABLE].setBrightness(persistent.break_enabled ? 1.f : 0.f);
		lights[LIGHT_FREEZE_ENABLE].setBrightness(persistent.freeze_enabled ? 1.f : 0.f);
		lights[LIGHT_MODE_GREEN].setBrightness(persistent.macro_mode ? 1.f : 0.f);
		lights[LIGHT_MODE_RED].setBrightness(persistent.macro_mode ? 0.f : 1.f);
		for (int i = 0; i < 5; i++)
			lights[LIGHT_ALGO_0 + i].setBrightness(static_cast<int>(persistent.corrupt_algorithm) == i ? 1.f : 0.f);
		lights[LIGHT_CORRUPT_BANK].setBrightness(persistent.corrupt_bank == corrupter::CorruptBank::kExpanded ? 1.f : 0.f);
		lights[LIGHT_BREAK_MICRO].setBrightness(persistent.break_silence_mode ? 1.f : 0.f);
		lights[LIGHT_STEREO].setBrightness(persistent.unique_stereo_mode ? 1.f : 0.f);
		lights[LIGHT_GATE_MODE].setBrightness(persistent.gate_latching ? 0.f : 1.f);
		lights[LIGHT_CLOCK_SOURCE].setBrightness(clock_internal ? 0.f : 1.f);
		current_algo = static_cast<int>(persistent.corrupt_algorithm);

		// Update button tooltips to reflect current state
		paramQuantities[PARAM_BEND_ENABLE]->description = persistent.bend_enabled ? "ON" : "OFF";
		paramQuantities[PARAM_BREAK_ENABLE]->description = persistent.break_enabled ? "ON" : "OFF";
		paramQuantities[PARAM_FREEZE_ENABLE]->description = persistent.freeze_enabled ? "ON" : "OFF";
		paramQuantities[PARAM_MODE]->description = persistent.macro_mode ? "Macro" : "Micro";
		paramQuantities[PARAM_CORRUPT_ALGO]->description = (current_algo >= 0 && current_algo <= 4) ? kAlgoNames[current_algo] : "";
		paramQuantities[PARAM_BREAK_MICRO_MODE]->description = persistent.break_silence_mode ? "Silence ON" : "Silence OFF";
		paramQuantities[PARAM_STEREO_MODE]->description = persistent.unique_stereo_mode ? "Stereo ON" : "Stereo OFF";

		// Update waveform display
		float sr = APP->engine->getSampleRate();
		uint32_t buf_frames = static_cast<uint32_t>(kMaxBufferSeconds * sr);
		uint32_t frames_per_bin = (buf_frames > 0) ? buf_frames / kWaveBins : 1u;
		for (uint32_t i = 0; i < buf_pos; i++) {
			float s = out_l_buf[i];
			if (s < 0.f) s = -s;
			if (s > wave_accum) wave_accum = s;
			wave_accum_count++;
			if (wave_accum_count >= frames_per_bin) {
				int peak = static_cast<int>(wave_accum * 8.f);
				if (peak > 8) peak = 8;
				wave_peaks[wave_write_pos % kWaveBins] = static_cast<uint8_t>(peak);
				wave_write_pos = (wave_write_pos + 1) % kWaveBins;
				wave_accum = 0.f;
				wave_accum_count = 0;
				display_dirty = true;
			}
		}

		read_pos = 0;
	}

	void process(const ProcessArgs& args) override {
		if (!initialised) return;

		// Write inputs to buffer
		in_l_buf[buf_pos] = inputs[INPUT_L].getVoltage() / 5.f;
		in_r_buf[buf_pos] = inputs[INPUT_R].isConnected()
			? inputs[INPUT_R].getVoltage() / 5.f
			: in_l_buf[buf_pos];

		// CV buffers (pass voltage directly)
		cv_time_buf[buf_pos] = inputs[INPUT_TIME_CV].getVoltage();
		cv_repeats_buf[buf_pos] = inputs[INPUT_REPEATS_CV].getVoltage();
		cv_mix_buf[buf_pos] = inputs[INPUT_MIX_CV].getVoltage();
		cv_bend_buf[buf_pos] = inputs[INPUT_BEND_CV].getVoltage();
		cv_break_buf[buf_pos] = inputs[INPUT_BREAK_CV].getVoltage();
		cv_corrupt_buf[buf_pos] = inputs[INPUT_CORRUPT_CV].getVoltage();

		// Gate buffers
		gate_bend_buf[buf_pos] = inputs[INPUT_BEND_GATE].getVoltage();
		gate_break_buf[buf_pos] = inputs[INPUT_BREAK_GATE].getVoltage();
		gate_freeze_buf[buf_pos] = inputs[INPUT_FREEZE_GATE].getVoltage();
		gate_clock_buf[buf_pos] = inputs[INPUT_CLOCK].getVoltage();

		// Output from previous block
		if (!first_block && read_pos < kMaxBlockFrames) {
			outputs[OUTPUT_L].setVoltage(out_l_buf[read_pos] * 5.f);
			outputs[OUTPUT_R].setVoltage(out_r_buf[read_pos] * 5.f);
		} else {
			outputs[OUTPUT_L].setVoltage(0.f);
			outputs[OUTPUT_R].setVoltage(0.f);
		}

		buf_pos++;
		read_pos++;

		// Process block when full
		if (buf_pos >= kMaxBlockFrames) {
			processBlock();
			buf_pos = 0;
			first_block = false;
		}
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "bend_enabled", json_boolean(persistent.bend_enabled));
		json_object_set_new(root, "break_enabled", json_boolean(persistent.break_enabled));
		json_object_set_new(root, "freeze_enabled", json_boolean(persistent.freeze_enabled));
		json_object_set_new(root, "macro_mode", json_boolean(persistent.macro_mode));
		json_object_set_new(root, "break_silence_mode", json_boolean(persistent.break_silence_mode));
		json_object_set_new(root, "unique_stereo_mode", json_boolean(persistent.unique_stereo_mode));
		json_object_set_new(root, "gate_latching", json_boolean(persistent.gate_latching));
		json_object_set_new(root, "freeze_latching", json_boolean(persistent.freeze_latching));
		json_object_set_new(root, "corrupt_bank", json_integer(static_cast<int>(persistent.corrupt_bank)));
		json_object_set_new(root, "corrupt_algorithm", json_integer(static_cast<int>(persistent.corrupt_algorithm)));
		json_object_set_new(root, "glitch_window_01", json_real(persistent.glitch_window_01));
		json_object_set_new(root, "gate_mode_index", json_integer(gate_mode_index));
		json_object_set_new(root, "scale_index", json_integer(scale_index));
		json_object_set_new(root, "scale_root", json_integer(scale_root));
		return root;
	}

	void dataFromJson(json_t* root) override {
		json_t* j;
		if ((j = json_object_get(root, "bend_enabled"))) persistent.bend_enabled = json_boolean_value(j);
		if ((j = json_object_get(root, "break_enabled"))) persistent.break_enabled = json_boolean_value(j);
		if ((j = json_object_get(root, "freeze_enabled"))) persistent.freeze_enabled = json_boolean_value(j);
		if ((j = json_object_get(root, "macro_mode"))) persistent.macro_mode = json_boolean_value(j);
		if ((j = json_object_get(root, "break_silence_mode"))) persistent.break_silence_mode = json_boolean_value(j);
		if ((j = json_object_get(root, "unique_stereo_mode"))) persistent.unique_stereo_mode = json_boolean_value(j);
		if ((j = json_object_get(root, "gate_latching"))) persistent.gate_latching = json_boolean_value(j);
		if ((j = json_object_get(root, "freeze_latching"))) persistent.freeze_latching = json_boolean_value(j);
		if ((j = json_object_get(root, "corrupt_bank"))) persistent.corrupt_bank = static_cast<corrupter::CorruptBank>(json_integer_value(j));
		if ((j = json_object_get(root, "corrupt_algorithm"))) persistent.corrupt_algorithm = static_cast<corrupter::CorruptAlgorithm>(json_integer_value(j));
		if ((j = json_object_get(root, "glitch_window_01"))) persistent.glitch_window_01 = static_cast<float>(json_real_value(j));
		if ((j = json_object_get(root, "gate_mode_index"))) gate_mode_index = json_integer_value(j);
		if ((j = json_object_get(root, "scale_index"))) scale_index = json_integer_value(j);
		if ((j = json_object_get(root, "scale_root"))) scale_root = json_integer_value(j);
		if (initialised) {
			engine.set_persistent_state(persistent);
			engine.set_clock_mode_internal(clock_internal);
			applyScale();
		}
	}
};

// ---------------------------------------------------------------------------
// Custom Display Widget
// ---------------------------------------------------------------------------

struct CorrupterDisplay : LedDisplay {
	CorrupterModule* module = nullptr;
	std::string fontPath;

	CorrupterDisplay() {
		fontPath = asset::system("res/fonts/DejaVuSans.ttf");
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1) {
			LedDisplay::drawLayer(args, layer);
			return;
		}

		// Load font for text rendering
		std::shared_ptr<Font> font = APP->window->loadFont(fontPath);

		if (!module) {
			// Draw placeholder in module browser
			nvgBeginPath(args.vg);
			nvgRect(args.vg, 0, 0, box.size.x, box.size.y);
			nvgFillColor(args.vg, nvgRGB(0x0a, 0x1a, 0x2a));
			nvgFill(args.vg);

			if (font) {
				nvgFontFaceId(args.vg, font->handle);
				nvgFontSize(args.vg, 10);
				nvgFillColor(args.vg, nvgRGB(0x40, 0xc0, 0xc0));
				nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
				nvgText(args.vg, box.size.x / 2, box.size.y / 2, "CORRUPTER", NULL);
			}

			LedDisplay::drawLayer(args, layer);
			return;
		}

		// Dark background
		nvgBeginPath(args.vg);
		nvgRect(args.vg, 0, 0, box.size.x, box.size.y);
		nvgFillColor(args.vg, nvgRGB(0x05, 0x10, 0x18));
		nvgFill(args.vg);

		float w = box.size.x;
		float h = box.size.y;

		// Waveform visualization
		float wave_h = h - 14.f; // leave space for text at bottom
		float mid_y = wave_h / 2.f;
		float col_w = w / kWaveBins;

		for (int b = 0; b < kWaveBins; b++) {
			int idx = (module->wave_write_pos + b) % kWaveBins;
			int peak = module->wave_peaks[idx];
			if (peak <= 0) continue;

			float amp = peak / 8.f;
			float bar_h = amp * mid_y;
			float x = b * col_w;

			// Gradient: dark teal (low) to bright cyan (high)
			int r = static_cast<int>(0x10 + amp * 0x60);
			int g = static_cast<int>(0x60 + amp * 0x9f);
			int b_col = static_cast<int>(0x80 + amp * 0x7f);

			nvgBeginPath(args.vg);
			nvgRect(args.vg, x, mid_y - bar_h, col_w, bar_h * 2.f);
			nvgFillColor(args.vg, nvgRGBA(r, g, b_col, 200));
			nvgFill(args.vg);
		}

		// Write position marker
		{
			float wp_x = static_cast<float>(module->wave_write_pos) / kWaveBins * w;
			nvgBeginPath(args.vg);
			nvgRect(args.vg, wp_x, 0, 1.5f, wave_h);
			nvgFillColor(args.vg, nvgRGBA(0xff, 0xff, 0xff, 180));
			nvgFill(args.vg);
		}

		// Bottom text: status indicators centered over button columns
		if (!font) {
			LedDisplay::drawLayer(args, layer);
			return;
		}
		float text_y = h - 3.f;
		nvgFontFaceId(args.vg, font->handle);
		nvgFontSize(args.vg, 9);
		nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_BOTTOM);

		// Column positions relative to display (matching 4-col button grid)
		float col_bnd = (17.f - 7.f) / 77.44f * w;
		float col_brk = (35.48f - 7.f) / 77.44f * w;
		float col_crp = (53.96f - 7.f) / 77.44f * w;
		float col_frz = (72.44f - 7.f) / 77.44f * w;

		// BND / BRK / FRZ — lit when enabled
		if (module->persistent.bend_enabled) {
			nvgFillColor(args.vg, nvgRGB(0x00, 0xff, 0x80));
			nvgText(args.vg, col_bnd, text_y, "BND", NULL);
		}
		if (module->persistent.break_enabled) {
			nvgFillColor(args.vg, nvgRGB(0xff, 0x80, 0x00));
			nvgText(args.vg, col_brk, text_y, "BRK", NULL);
		}
		if (module->persistent.freeze_enabled) {
			nvgFillColor(args.vg, nvgRGB(0x40, 0x80, 0xff));
			nvgText(args.vg, col_crp, text_y, "FRZ", NULL);
		}

		// Algorithm name centered on freeze column (swapped with FRZ)
		nvgFillColor(args.vg, nvgRGB(0x80, 0xe0, 0xe0));
		int algo = module->current_algo;
		if (algo >= 0 && algo <= 4)
			nvgText(args.vg, col_frz, text_y, kAlgoNames[algo], NULL);

		LedDisplay::drawLayer(args, layer);
	}
};

// ---------------------------------------------------------------------------
// Panel Label Overlay (NanoVG text replaces SVG path text)
// ---------------------------------------------------------------------------

struct CorrupterLabels : TransparentWidget {
	std::string fontPath;

	CorrupterLabels() {
		fontPath = asset::system("res/fonts/DejaVuSans.ttf");
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1) {
			TransparentWidget::drawLayer(args, layer);
			return;
		}

		std::shared_ptr<Font> font = APP->window->loadFont(fontPath);
		if (!font) {
			TransparentWidget::drawLayer(args, layer);
			return;
		}

		NVGcontext* vg = args.vg;
		nvgFontFaceId(vg, font->handle);

		float x3[3] = {25.f, 45.72f, 66.44f};
		float col[4] = {17.f, 35.48f, 53.96f, 72.44f};
		float ppmm = box.size.x / 91.44f;
		auto px = [ppmm](float mm) -> float { return mm * ppmm; };

		NVGcolor colKnob   = nvgRGB(0x90, 0xb0, 0xd0);
		NVGcolor colIO     = nvgRGB(0x90, 0xb0, 0xd0);
		NVGcolor colBottom = nvgRGB(0x70, 0x90, 0xb0);

		// --- I/O labels ---
		nvgFontSize(vg, 8);
		nvgFillColor(vg, colIO);
		nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_BOTTOM);
		nvgText(vg, px(10.f), px(12.8f), "IN L", NULL);
		nvgText(vg, px(20.f), px(12.8f), "IN R", NULL);
		nvgText(vg, px(35.f), px(12.8f), "CLOCK", NULL);
		nvgText(vg, px(71.44f), px(12.8f), "OUT L", NULL);
		nvgText(vg, px(81.44f), px(12.8f), "OUT R", NULL);

		// --- Row 1 knob labels ---
		nvgFontSize(vg, 9);
		nvgFillColor(vg, colKnob);
		nvgText(vg, px(x3[0]), px(28.f), "TIME", NULL);
		nvgText(vg, px(x3[1]), px(28.f), "REPEATS", NULL);
		nvgText(vg, px(x3[2]), px(28.f), "MIX", NULL);

		// --- Row 2 knob labels ---
		nvgText(vg, px(x3[0]), px(55.5f), "BEND", NULL);
		nvgText(vg, px(x3[1]), px(55.5f), "BREAK", NULL);
		nvgText(vg, px(x3[2]), px(55.5f), "CORRUPT", NULL);

		// --- Button labels (above buttons at y=100.5) ---
		nvgText(vg, px(col[0]), px(98.f), "BEND", NULL);
		nvgText(vg, px(col[1]), px(98.f), "BREAK", NULL);
		nvgText(vg, px(col[2]), px(98.f), "FREEZE", NULL);
		nvgText(vg, px(col[3]), px(98.f), "ALGO", NULL);

		// --- Bottom section labels ---
		nvgFontSize(vg, 8);
		nvgFillColor(vg, colBottom);
		nvgText(vg, px(col[3]), px(105.5f), "GW", NULL);
		nvgText(vg, px(x3[0]), px(115.8f), "MODE", NULL);
		nvgText(vg, px(x3[1]), px(115.8f), "SLNC", NULL);
		nvgText(vg, px(x3[2]), px(115.8f), "ST MODE", NULL);

		TransparentWidget::drawLayer(args, layer);
	}
};

// ---------------------------------------------------------------------------
// Widget
// ---------------------------------------------------------------------------

struct CorrupterWidget : ModuleWidget {
	CorrupterWidget(CorrupterModule* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Corrupter.svg")));

		// Panel text labels (rendered via NanoVG)
		{
			CorrupterLabels* labels = createWidget<CorrupterLabels>(Vec(0, 0));
			labels->box.size = box.size;
			addChild(labels);
		}

		// Screws
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		// =========================================================
		// 18 HP layout (91.44mm wide x 128.5mm tall)
		// Spread across full height for breathing room
		// =========================================================

		// X positions for 3-column and 4-column grids
		float x3[3] = {25.f, 45.72f, 66.44f};
		float col[4] = {17.f, 35.48f, 53.96f, 72.44f};

		// --- Audio I/O + Clock (top row) ---
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(10, 17)), module, CorrupterModule::INPUT_L));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(20, 17)), module, CorrupterModule::INPUT_R));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(35, 17)), module, CorrupterModule::INPUT_CLOCK));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(71.44, 17)), module, CorrupterModule::OUTPUT_L));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(81.44, 17)), module, CorrupterModule::OUTPUT_R));

		// --- Row 1: TIME / REPEATS / MIX (big knobs, +5mm for label clearance) ---
		addParam(createParamCentered<RoundBigBlackKnob>(mm2px(Vec(x3[0], 36)), module, CorrupterModule::PARAM_TIME));
		addParam(createParamCentered<RoundBigBlackKnob>(mm2px(Vec(x3[1], 36)), module, CorrupterModule::PARAM_REPEATS));
		addParam(createParamCentered<RoundBigBlackKnob>(mm2px(Vec(x3[2], 36)), module, CorrupterModule::PARAM_MIX));

		// CV inputs below row 1 knobs
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(x3[0], 47)), module, CorrupterModule::INPUT_TIME_CV));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(x3[1], 47)), module, CorrupterModule::INPUT_REPEATS_CV));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(x3[2], 47)), module, CorrupterModule::INPUT_MIX_CV));

		// --- Row 2: BEND / BREAK / CORRUPT (big knobs) ---
		addParam(createParamCentered<RoundBigBlackKnob>(mm2px(Vec(x3[0], 63.5f)), module, CorrupterModule::PARAM_BEND));
		addParam(createParamCentered<RoundBigBlackKnob>(mm2px(Vec(x3[1], 63.5f)), module, CorrupterModule::PARAM_BREAK));
		addParam(createParamCentered<RoundBigBlackKnob>(mm2px(Vec(x3[2], 63.5f)), module, CorrupterModule::PARAM_CORRUPT));

		// CV inputs + attenuators below row 2 knobs
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(20, 74.5f)), module, CorrupterModule::INPUT_BEND_CV));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(30, 74.5f)), module, CorrupterModule::PARAM_BEND_CV_ATTN));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(40.72, 74.5f)), module, CorrupterModule::INPUT_BREAK_CV));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(50.72, 74.5f)), module, CorrupterModule::PARAM_BREAK_CV_ATTN));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(61.44, 74.5f)), module, CorrupterModule::INPUT_CORRUPT_CV));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(71.44, 74.5f)), module, CorrupterModule::PARAM_CORRUPT_CV_ATTN));

		// --- LED Display (+10mm) ---
		{
			CorrupterDisplay* display = createWidget<CorrupterDisplay>(mm2px(Vec(7, 81.3)));
			display->box.size = mm2px(Vec(77.44, 13));
			display->module = module;
			addChild(display);
		}

		// --- 4 columns below display: BND / BRK / CRP / FRZ ---
		// Enable buttons (no LEDs — screen indicators show active state)
		float btn_y = 100.75f;

		addParam(createParamCentered<VCVButton>(mm2px(Vec(col[0], btn_y)), module, CorrupterModule::PARAM_BEND_ENABLE));
		addParam(createParamCentered<VCVButton>(mm2px(Vec(col[1], btn_y)), module, CorrupterModule::PARAM_BREAK_ENABLE));
		addParam(createParamCentered<VCVButton>(mm2px(Vec(col[2], btn_y)), module, CorrupterModule::PARAM_FREEZE_ENABLE));
		addParam(createParamCentered<VCVButton>(mm2px(Vec(col[3], btn_y)), module, CorrupterModule::PARAM_CORRUPT_ALGO));

		// Gate inputs + GW trimpot
		float gate_y = 108.25f;
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(col[0], gate_y)), module, CorrupterModule::INPUT_BEND_GATE));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(col[1], gate_y)), module, CorrupterModule::INPUT_BREAK_GATE));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(col[2], gate_y)), module, CorrupterModule::INPUT_FREEZE_GATE));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(col[3], gate_y)), module, CorrupterModule::PARAM_GLITCH_WINDOW));

		// --- Bottom section: settings buttons aligned under CV columns ---
		float bot_y = 118.5f;
		addParam(createParamCentered<VCVButton>(mm2px(Vec(x3[0], bot_y)), module, CorrupterModule::PARAM_MODE));
		addChild(createLightCentered<SmallLight<GreenRedLight>>(mm2px(Vec(x3[0], bot_y + 4.0f)), module, CorrupterModule::LIGHT_MODE_GREEN));

		addParam(createParamCentered<VCVButton>(mm2px(Vec(x3[1], bot_y)), module, CorrupterModule::PARAM_BREAK_MICRO_MODE));
		addChild(createLightCentered<SmallLight<GreenLight>>(mm2px(Vec(x3[1], bot_y + 4.0f)), module, CorrupterModule::LIGHT_BREAK_MICRO));

		addParam(createParamCentered<VCVButton>(mm2px(Vec(x3[2], bot_y)), module, CorrupterModule::PARAM_STEREO_MODE));
		addChild(createLightCentered<SmallLight<GreenLight>>(mm2px(Vec(x3[2], bot_y + 4.0f)), module, CorrupterModule::LIGHT_STEREO));
	}

	void appendContextMenu(Menu* menu) override {
		CorrupterModule* module = getModule<CorrupterModule>();
		if (!module) return;

		menu->addChild(new MenuSeparator);

		menu->addChild(createIndexPtrSubmenuItem("Gate Mode",
			{"Latching", "Momentary"},
			&module->gate_mode_index));

		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Bend Quantize"));

		// Scale selection
		{
			std::vector<std::string> scaleNames;
			for (int i = 0; i < kNumScales; i++)
				scaleNames.push_back(kScales[i].name);
			menu->addChild(createIndexSubmenuItem("Scale", scaleNames,
				[=]() { return module->scale_index; },
				[=](int idx) { module->scale_index = idx; module->applyScale(); }
			));
		}

		// Root note selection
		{
			std::vector<std::string> rootNames;
			for (int i = 0; i < 12; i++)
				rootNames.push_back(kRootNames[i]);
			menu->addChild(createIndexSubmenuItem("Root", rootNames,
				[=]() { return module->scale_root; },
				[=](int idx) { module->scale_root = idx; module->applyScale(); }
			));
		}
	}
};

Model* modelCorrupter = createModel<CorrupterModule, CorrupterWidget>("Corrupter");
