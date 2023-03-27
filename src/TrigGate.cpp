#include "plugin.hpp"


struct TrigGate : Module {
	enum ParamId {
		PARAMS_LEN
	};
	enum InputId {
		START_INPUT_INPUT,
		STOP_INPUT_INPUT,
		INPUTS_LEN
	};
	enum OutputId {
		GATE_OUTPUT_OUTPUT,
		OUTPUTS_LEN
	};
	enum LightId {
		START_LIGHT_LIGHT,
		STOP_LIGHT_LIGHT,
		GATE_LIGHT_LIGHT,
		LIGHTS_LEN
	};

	bool gateOn = false;

	TrigGate() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configInput(START_INPUT_INPUT, "Start");
		configInput(STOP_INPUT_INPUT, "Stop");
		configOutput(GATE_OUTPUT_OUTPUT, "Gate");
	}

	void process(const ProcessArgs& args) override {
		float startTrigger = inputs[START_INPUT_INPUT].getVoltage();
        float stopTrigger = inputs[STOP_INPUT_INPUT].getVoltage();
        if (startTrigger >= 1.0f && !gateOn) {
            gateOn = true;
            outputs[GATE_OUTPUT_OUTPUT].setVoltage(10.0f);
        }
        else if (stopTrigger >= 1.0f && gateOn) {
            gateOn = false;
            outputs[GATE_OUTPUT_OUTPUT].setVoltage(0.0f);
        }

		lights[START_LIGHT_LIGHT].setSmoothBrightness(startTrigger / 10.0f, args.sampleTime);
        lights[STOP_LIGHT_LIGHT].setSmoothBrightness(stopTrigger / 10.0f, args.sampleTime);
        lights[GATE_LIGHT_LIGHT].setSmoothBrightness(gateOn ? 1.0f : 0.0f, args.sampleTime);
	}
};


struct TrigGateWidget : ModuleWidget {
	TrigGateWidget(TrigGate* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/TrigGate.svg")));

		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(5.08, 20.968)), module, TrigGate::START_INPUT_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(5.08, 33.648)), module, TrigGate::STOP_INPUT_INPUT));

		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(5.08, 109.232)), module, TrigGate::GATE_OUTPUT_OUTPUT));

		addChild(createLightCentered<MediumLight<BlueLight>>(mm2px(Vec(5.08, 15.350359)), module, TrigGate::START_LIGHT_LIGHT));
		addChild(createLightCentered<MediumLight<BlueLight>>(mm2px(Vec(5.08, 27.919857)), module, TrigGate::STOP_LIGHT_LIGHT));
		addChild(createLightCentered<MediumLight<BlueLight>>(mm2px(Vec(5.08, 102.39134)), module, TrigGate::GATE_LIGHT_LIGHT));
	}
};


Model* modelTrigGate = createModel<TrigGate, TrigGateWidget>("TrigGate");