#include "plugin.hpp"


struct DivTrig : Module {
	enum ParamId {
		PROB_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		TRIG_INPUT,
		RESET_INPUT,
		INPUTS_LEN
	};
	enum OutputId {
		_1X_OUTPUT,
		_2X_OUTPUT,
		_3X_OUTPUT,
		_4X_OUTPUT,
		_5X_OUTPUT,
		_6X_OUTPUT,
		_7X_OUTPUT,
		_8X_OUTPUT,
		OUTPUTS_LEN
	};
	enum LightId {
		TRIG_LIGHT,
		LIGHTS_LEN
	};

    std::vector<int> counters = {1, 2, 3, 4, 5, 6, 7, 8}; // Divisors for each output
	std::vector<dsp::PulseGenerator> pulseGenerators = {
		dsp::PulseGenerator(),
		dsp::PulseGenerator(),
		dsp::PulseGenerator(),
		dsp::PulseGenerator(),
		dsp::PulseGenerator(),
		dsp::PulseGenerator(),
		dsp::PulseGenerator(),
		dsp::PulseGenerator()
	};

	dsp::SchmittTrigger trigger;
	bool triggerState, prevTriggerState;

	dsp::SchmittTrigger resetTrigger;
	bool resetTriggerState, prevResetTriggerState;

	DivTrig() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam(PROB_PARAM, 0.f, 1.f, 0.5f, "Probability");
		configInput(TRIG_INPUT, "Trig");
		configInput(RESET_INPUT, "Reset");
		configOutput(_1X_OUTPUT, "1x");
		configOutput(_2X_OUTPUT, "2x");
		configOutput(_3X_OUTPUT, "3x");
		configOutput(_4X_OUTPUT, "4x");
		configOutput(_5X_OUTPUT, "5x");
		configOutput(_6X_OUTPUT, "6x");
		configOutput(_7X_OUTPUT, "7x");
		configOutput(_8X_OUTPUT, "8x");
	}

	void process(const ProcessArgs& args) override {
		float resetInput = inputs[RESET_INPUT].getVoltage();

		resetTrigger.process(rescale(resetInput, 0.1f, 2.0f, 0.f, 1.f));
		prevResetTriggerState = resetTriggerState;
		resetTriggerState = resetTrigger.isHigh();

		if (!prevResetTriggerState && resetTriggerState) {
			onReset();
			return;
		}


		float input = inputs[TRIG_INPUT].getVoltage();
		float prob = params[PROB_PARAM].getValue();

		trigger.process(rescale(input, 0.1f, 2.0f, 0.f, 1.f));
		prevTriggerState = triggerState;
		triggerState = trigger.isHigh();

		if (!prevTriggerState && triggerState) {
			for (int i = 0; i < (int)counters.size(); i++) {
				counters[i] = counters[i] - 1;
				if (counters[i] == 0) {
					counters[i] = i + 1;
					if (random::uniform() < prob) {
						pulseGenerators[_1X_OUTPUT + i].trigger();
					}
				}
			}
		}

		for (int i = 0; i < (int)counters.size(); i++) {
			float out = pulseGenerators[_1X_OUTPUT + i].process(args.sampleTime);
			outputs[_1X_OUTPUT + i].setVoltage(10.0f * out);
		}
	}

	void onReset() override {
        counters = {1, 2, 3, 4, 5, 6, 7, 8}; // Reset counters

		for (int i = 0; i < (int)counters.size(); i++) {
			pulseGenerators[_1X_OUTPUT + i].reset();
			outputs[_1X_OUTPUT + i].setVoltage(0.0f);
		}
    }

    void onRandomize() override {
        params[PROB_PARAM].setValue(random::uniform());
	}
};


struct DivTrigWidget : ModuleWidget {
	DivTrigWidget(DivTrig* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/DivTrig.svg")));

		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(5.08, 35.984)), module, DivTrig::PROB_PARAM));

		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(5.08, 24.672)), module, DivTrig::TRIG_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(5.191, 110.117)), module, DivTrig::RESET_INPUT));

		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(5.191, 47.497)), module, DivTrig::_1X_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(5.191, 55.093)), module, DivTrig::_2X_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(5.191, 62.689)), module, DivTrig::_3X_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(5.191, 70.284)), module, DivTrig::_4X_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(5.191, 77.88)), module, DivTrig::_5X_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(5.191, 85.476)), module, DivTrig::_6X_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(5.191, 93.071)), module, DivTrig::_7X_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(5.191, 100.667)), module, DivTrig::_8X_OUTPUT));

		addChild(createLightCentered<MediumLight<RedLight>>(mm2px(Vec(5.08, 18.525)), module, DivTrig::TRIG_LIGHT));
	}
};


Model* modelDivTrig = createModel<DivTrig, DivTrigWidget>("DivTrig");