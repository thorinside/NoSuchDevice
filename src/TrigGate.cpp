#include "plugin.hpp"


struct TrigGate : Module {
	enum ParamId {
		PARAMS_LEN
	};
	enum InputId {
		START_INPUT,
		STOP_INPUT,
		INPUTS_LEN
	};
	enum OutputId {
		GATE_OUTPUT,
		OUTPUTS_LEN
	};
	enum LightId {
		START_LIGHT,
		STOP_LIGHT,
		GATE_LIGHT,
		LIGHTS_LEN
	};

	bool gateOn = false;

	TrigGate() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configInput(START_INPUT, "Start");
		configInput(STOP_INPUT, "Stop");
		configOutput(GATE_OUTPUT, "Gate");
	}

	void process(const ProcessArgs& args) override {
		float startTrigger = inputs[START_INPUT].getVoltage();
        float stopTrigger = inputs[STOP_INPUT].getVoltage();
        if (startTrigger >= 1.0f && !gateOn) {
            gateOn = true;
            outputs[GATE_OUTPUT].setVoltage(10.0f);
        }
        else if (stopTrigger >= 1.0f && gateOn) {
            gateOn = false;
            outputs[GATE_OUTPUT].setVoltage(0.0f);
        }

		lights[START_LIGHT].setSmoothBrightness(startTrigger / 10.0f, args.sampleTime);
        lights[STOP_LIGHT].setSmoothBrightness(stopTrigger / 10.0f, args.sampleTime);
        lights[GATE_LIGHT].setSmoothBrightness(gateOn ? 1.0f : 0.0f, args.sampleTime);
	}
};


struct TrigGateWidget : ModuleWidget {
	TrigGateWidget(TrigGate* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/TrigGate.svg")));

		addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(5.08, 24.672)), module, TrigGate::START_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(5.08, 37.353)), module, TrigGate::STOP_INPUT));

		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(5.08, 109.232)), module, TrigGate::GATE_OUTPUT));

		addChild(createLightCentered<MediumLight<BlueLight>>(mm2px(Vec(5.08, 18.525)), module, TrigGate::START_LIGHT));
		addChild(createLightCentered<MediumLight<BlueLight>>(mm2px(Vec(5.08, 31.095)), module, TrigGate::STOP_LIGHT));
		addChild(createLightCentered<MediumLight<BlueLight>>(mm2px(Vec(5.08, 101.862)), module, TrigGate::GATE_LIGHT));
	}
};


Model* modelTrigGate = createModel<TrigGate, TrigGateWidget>("TrigGate");