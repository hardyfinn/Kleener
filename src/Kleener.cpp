// Kleener: a 16-bit loop sequencer inspired by the Klee sequencer.
//
// How it works, in musical terms:
// - 16 BIT switches hold an on/off pattern. LOAD copies the switches into the
//   loop (the "register"); the loop then turns one step on each CLOCK.
// - The 8 sliders are pitch weights. PITCH A is the sum of the sliders whose
//   bit is on in the first 8 bits of the loop window, so as the pattern
//   turns past the sliders the melody changes. PITCH B does the same with
//   the next 8 bits of the loop (or, from the right-click menu, the
//   switched-off bits), giving a related second line.
// - Each step has a BUS switch (1, 2 or 3). When a step whose bit is on
//   plays, its bus gate fires. ANY fires on every step whose bit is on.
// - MUTATE is the chance that a bit flips as it leaves the playhead, so the
//   pattern slowly rewrites itself. FLIP forces the next bit to flip.
#include "plugin.hpp"

// ---- LAYOUT (same numbers in res/make_panel.py) ----
static const float COL_X0 = 16.7f, COL_DX = 17.0f;
static const float ROW_Y[2] = {25.0f, 45.0f};
static const float BTN_DX = -3.2f, SW_DX = 3.6f, LIGHT_DY = 5.8f;
static const float SLIDER_Y = 70.5f;
static const float KNOB_Y = 95.5f;
static const float X_LEN = 12.0f, X_LENCV = 23.5f, X_MUT = 39.0f, X_MUTCV = 50.5f, X_RANGE = 66.0f,
                   X_RANGECV = 77.5f, X_GLIDE = 92.0f, X_SCALE = 106.0f, X_LOAD = 141.0f;
static const float SCALE_LX = 113.0f, SCALE_Y0 = 86.6f, SCALE_DY = 2.7f;
static const float JACK_Y = 115.6f;
static const float X_CLOCK = 11.0f, X_RESET = 25.0f, X_LOADIN = 39.0f, X_FLIP = 53.0f;
static const float X_A = 71.0f, X_B = 84.0f, X_BUS1 = 100.0f, X_BUS2 = 113.0f, X_BUS3 = 126.0f, X_ANY = 141.0f;

static const int NUM_BITS = 16;
static const int NUM_SLIDERS = 8;
static const int NUM_SCALES = 6;

// Semitones in each scale (root C, 0 V). OFF and CHROM are handled apart.
static const std::vector<int> SCALE_NOTES[NUM_SCALES] = {
	{},
	{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11},
	{0, 2, 3, 5, 7, 8, 10},   // natural minor
	{0, 2, 3, 5, 7, 9, 10},   // dorian
	{0, 1, 3, 5, 7, 8, 10},   // phrygian
	{0, 3, 5, 7, 10},         // minor pentatonic
};

// Default slider positions, in semitones (at RANGE 100 %)
static const float SLIDER_DEFAULT[NUM_SLIDERS] = {3, 7, 2, 5, 12, 10, 5, 0};
static const bool PATTERN_DEFAULT[NUM_BITS] = {1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 1, 0, 0, 1, 0, 0};

struct Kleener : Module {
	enum ParamIds {
		ENUMS(BIT_PARAM, NUM_BITS),
		ENUMS(BUS_PARAM, NUM_BITS),
		ENUMS(SLIDER_PARAM, NUM_SLIDERS),
		LENGTH_PARAM,
		MUTATE_PARAM,
		RANGE_PARAM,
		GLIDE_PARAM,
		SCALE_PARAM,
		LOAD_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		CLOCK_INPUT,
		RESET_INPUT,
		LOAD_INPUT,
		FLIP_INPUT,
		LENGTH_INPUT,
		MUTATE_INPUT,
		RANGE_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		A_OUTPUT,
		B_OUTPUT,
		ENUMS(BUS_OUTPUT, 3),
		ANY_OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		ENUMS(BIT_LIGHT, NUM_BITS),       // the latch button's own light (switch state)
		ENUMS(REG_LIGHT, NUM_BITS),       // what is in the loop now
		ENUMS(HEAD_LIGHT, NUM_BITS),      // playhead
		ENUMS(SCALE_LIGHT, NUM_SCALES),
		LOAD_LIGHT,
		NUM_LIGHTS
	};

	bool reg[NUM_BITS] = {};     // the loop
	bool lastSwitch[NUM_BITS] = {};
	int head = -1;               // -1 = just reset: the next clock plays step 1
	bool flipPending = false;
	float slewA = 0.f, slewB = 0.f;

	// right-click options
	bool liveSwitches = true;    // switches write straight into the loop
	bool resetReloads = true;    // RESET also loads the switches
	bool tieGates = false;       // hold a bus gate across neighbouring steps on the same bus
	int bMode = 0;               // 0 = next 8 bits, 1 = switched-off bits of the window

	dsp::SchmittTrigger clockTrigger, resetTrigger, loadTrigger, loadButton, flipTrigger;
	dsp::PulseGenerator resetBlank, loadFlash;
	dsp::ClockDivider lightDivider;

	Kleener() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		for (int i = 0; i < NUM_BITS; i++) {
			configSwitch(BIT_PARAM + i, 0.f, 1.f, PATTERN_DEFAULT[i] ? 1.f : 0.f, string::f("Bit %d", i + 1), {"Off", "On"});
			configSwitch(BUS_PARAM + i, 0.f, 2.f, (float) (i % 3), string::f("Step %d bus", i + 1), {"Bus 1", "Bus 2", "Bus 3"});
		}
		for (int i = 0; i < NUM_SLIDERS; i++)
			configParam(SLIDER_PARAM + i, 0.f, 1.f, SLIDER_DEFAULT[i] / 12.f, string::f("Pitch weight %d", i + 1), " semitones (at RANGE 100%)", 0.f, 12.f);
		configParam(LENGTH_PARAM, 2.f, 16.f, 16.f, "Loop length", " steps");
		getParamQuantity(LENGTH_PARAM)->snapEnabled = true;
		configParam(MUTATE_PARAM, 0.f, 1.f, 0.f, "Mutate (chance a bit flips as it passes)", "%", 0.f, 100.f);
		configParam(RANGE_PARAM, 0.f, 2.f, 1.f, "Range (scales all sliders)", "%", 0.f, 100.f);
		configParam(GLIDE_PARAM, 0.f, 1.f, 0.f, "Glide");
		configSwitch(SCALE_PARAM, 0.f, NUM_SCALES - 1, 2.f, "Scale (root C)", {"Off (unquantised)", "Chromatic", "Minor", "Dorian", "Phrygian", "Minor pentatonic"});
		getParamQuantity(SCALE_PARAM)->snapEnabled = true;
		configButton(LOAD_PARAM, "Load the switches into the loop");
		for (int p : {LENGTH_PARAM, MUTATE_PARAM, RANGE_PARAM, GLIDE_PARAM, SCALE_PARAM})
			getParamQuantity(p)->randomizeEnabled = false;

		configInput(CLOCK_INPUT, "Clock");
		configInput(RESET_INPUT, "Reset (back to step 1, reloads the switches)");
		configInput(LOAD_INPUT, "Load trigger");
		configInput(FLIP_INPUT, "Flip trigger (the next bit to pass flips)");
		configInput(LENGTH_INPUT, "Length CV (+1 V = +1.5 steps)");
		configInput(MUTATE_INPUT, "Mutate CV (+1 V = +10 %)");
		configInput(RANGE_INPUT, "Range CV (+1 V = +10 %)");
		configOutput(A_OUTPUT, "Pitch A (V/OCT)");
		configOutput(B_OUTPUT, "Pitch B (V/OCT)");
		for (int k = 0; k < 3; k++)
			configOutput(BUS_OUTPUT + k, string::f("Bus %d gate", k + 1));
		configOutput(ANY_OUTPUT, "Any bit gate");
		for (int i = 0; i < NUM_BITS; i++) {
			configLight(REG_LIGHT + i, string::f("Step %d in the loop", i + 1));
			configLight(HEAD_LIGHT + i, string::f("Step %d playing", i + 1));
		}

		lightDivider.setDivision(64);
		loadSwitches();
	}

	void loadSwitches() {
		for (int i = 0; i < NUM_BITS; i++) {
			reg[i] = params[BIT_PARAM + i].getValue() > 0.5f;
			lastSwitch[i] = reg[i];
		}
	}

	int length() {
		float l = params[LENGTH_PARAM].getValue() + inputs[LENGTH_INPUT].getVoltage() * 1.5f;
		return clamp((int) std::round(l), 2, NUM_BITS);
	}

	float mutateAmount() {
		return clamp(params[MUTATE_PARAM].getValue() + inputs[MUTATE_INPUT].getVoltage() * 0.1f, 0.f, 1.f);
	}

	static float quantise(float v, int scale) {
		if (scale <= 0)
			return v;
		const std::vector<int>& notes = SCALE_NOTES[scale];
		float semis = v * 12.f;
		int oct = (int) std::floor(semis / 12.f);
		float within = semis - oct * 12.f;
		float best = 0.f, bestDist = 1e9f;
		// look at the scale notes in this octave and the root of the next one
		for (int n : notes) {
			float d = std::fabs(within - n);
			if (d < bestDist) { bestDist = d; best = n; }
		}
		if (std::fabs(within - 12.f) < bestDist)
			best = 12.f;
		return (oct * 12.f + best) / 12.f;
	}

	void onReset() override {
		head = -1;
		loadSwitches();
		slewA = slewB = 0.f;
	}

	void onRandomize() override {
		Module::onRandomize();
		head = -1;
		loadSwitches();
	}

	void process(const ProcessArgs& args) override {
		int len = length();

		// switches: when one changes by hand, write it into the loop straight away
		for (int i = 0; i < NUM_BITS; i++) {
			bool s = params[BIT_PARAM + i].getValue() > 0.5f;
			if (s != lastSwitch[i]) {
				lastSwitch[i] = s;
				if (liveSwitches)
					reg[i] = s;
			}
		}

		if (resetTrigger.process(inputs[RESET_INPUT].getVoltage(), 0.1f, 1.f)) {
			head = -1;
			if (resetReloads)
				loadSwitches();
			resetBlank.trigger(1e-3f);
		}
		bool blanked = resetBlank.process(args.sampleTime);

		bool lb = loadButton.process(params[LOAD_PARAM].getValue());
		bool lt = loadTrigger.process(inputs[LOAD_INPUT].getVoltage(), 0.1f, 1.f);
		if (lb || lt) {
			loadSwitches();
			loadFlash.trigger(0.15f);
		}

		if (flipTrigger.process(inputs[FLIP_INPUT].getVoltage(), 0.1f, 1.f))
			flipPending = true;

		if (clockTrigger.process(inputs[CLOCK_INPUT].getVoltage(), 0.1f, 1.f) && !blanked) {
			// the bit that just played may flip as it leaves the playhead
			if (head >= 0) {
				bool flip = flipPending;
				float m = mutateAmount();
				if (m > 0.f && random::uniform() < m)
					flip = true;
				if (flip)
					reg[head] = !reg[head];
				flipPending = false;
			}
			head++;
			if (head >= len)
				head = 0;
		}
		if (head >= len)
			head = 0;   // LENGTH turned down below the playhead

		int h = head < 0 ? 0 : head;

		// ---- pitch: sum the slider weights of the bits that are on
		float range = clamp(params[RANGE_PARAM].getValue() + inputs[RANGE_INPUT].getVoltage() * 0.1f, 0.f, 2.f);
		float a = 0.f, b = 0.f;
		for (int i = 0; i < NUM_SLIDERS; i++) {
			float w = params[SLIDER_PARAM + i].getValue();
			if (reg[(h + i) % len])
				a += w;
			bool bBit = (bMode == 0) ? reg[(h + NUM_SLIDERS + i) % len] : !reg[(h + i) % len];
			if (bBit)
				b += w;
		}
		int scale = (int) std::round(params[SCALE_PARAM].getValue());
		a = quantise(clamp(a * range, -10.f, 10.f), scale);
		b = quantise(clamp(b * range, -10.f, 10.f), scale);

		float g = params[GLIDE_PARAM].getValue();
		if (g <= 0.001f) {
			slewA = a;
			slewB = b;
		}
		else {
			float tau = g * g * 0.5f;   // up to half a second
			float k = 1.f - std::exp(-args.sampleTime / tau);
			slewA += (a - slewA) * k;
			slewB += (b - slewB) * k;
		}
		outputs[A_OUTPUT].setVoltage(slewA);
		outputs[B_OUTPUT].setVoltage(slewB);

		// ---- gates: follow the clock while it is high (or held, with ties)
		bool clockHigh = inputs[CLOCK_INPUT].getVoltage() >= 1.f;
		bool playing = head >= 0;
		bool bit = playing && reg[h];
		int bus = (int) std::round(params[BUS_PARAM + h].getValue());
		int nxt = (h + 1) % len;
		bool nextBit = reg[nxt];
		int nextBus = (int) std::round(params[BUS_PARAM + nxt].getValue());
		bool busGate = bit && (clockHigh || (tieGates && nextBit && nextBus == bus));
		bool anyGate = bit && (clockHigh || (tieGates && nextBit));
		for (int k = 0; k < 3; k++)
			outputs[BUS_OUTPUT + k].setVoltage(busGate && bus == k ? 10.f : 0.f);
		outputs[ANY_OUTPUT].setVoltage(anyGate ? 10.f : 0.f);

		// ---- lights
		bool flash = loadFlash.process(args.sampleTime);
		if (lightDivider.process()) {
			float dt = args.sampleTime * lightDivider.getDivision();
			for (int i = 0; i < NUM_BITS; i++) {
				float r = (i < len) ? (reg[i] ? 1.f : 0.06f) : 0.f;
				lights[REG_LIGHT + i].setBrightnessSmooth(r, dt, 30.f);
				lights[HEAD_LIGHT + i].setBrightnessSmooth(playing && i == h ? 1.f : 0.f, dt, 30.f);
			}
			for (int k = 0; k < NUM_SCALES; k++)
				lights[SCALE_LIGHT + k].setBrightness(k == scale ? 1.f : 0.f);
			lights[LOAD_LIGHT].setBrightnessSmooth(flash || params[LOAD_PARAM].getValue() > 0.f ? 1.f : 0.f, dt, 30.f);
		}
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_t* r = json_array();
		for (int i = 0; i < NUM_BITS; i++)
			json_array_append_new(r, json_boolean(reg[i]));
		json_object_set_new(root, "loop", r);
		json_object_set_new(root, "head", json_integer(head));
		json_object_set_new(root, "liveSwitches", json_boolean(liveSwitches));
		json_object_set_new(root, "resetReloads", json_boolean(resetReloads));
		json_object_set_new(root, "tieGates", json_boolean(tieGates));
		json_object_set_new(root, "bMode", json_integer(bMode));
		return root;
	}

	void dataFromJson(json_t* root) override {
		json_t* j;
		// switches are restored before this runs, so remember them as "not changed"
		for (int i = 0; i < NUM_BITS; i++)
			lastSwitch[i] = params[BIT_PARAM + i].getValue() > 0.5f;
		if ((j = json_object_get(root, "loop"))) {
			for (int i = 0; i < NUM_BITS && i < (int) json_array_size(j); i++)
				reg[i] = json_boolean_value(json_array_get(j, i));
		}
		if ((j = json_object_get(root, "head"))) head = clamp((int) json_integer_value(j), -1, NUM_BITS - 1);
		if ((j = json_object_get(root, "liveSwitches"))) liveSwitches = json_boolean_value(j);
		if ((j = json_object_get(root, "resetReloads"))) resetReloads = json_boolean_value(j);
		if ((j = json_object_get(root, "tieGates"))) tieGates = json_boolean_value(j);
		if ((j = json_object_get(root, "bMode"))) bMode = clamp((int) json_integer_value(j), 0, 1);
	}
};

struct KleenerWidget : ModuleWidget {
	KleenerWidget(Kleener* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Kleener.svg")));

		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		for (int i = 0; i < NUM_BITS; i++) {
			float x = COL_X0 + (i % 8) * COL_DX;
			float y = ROW_Y[i / 8];
			addParam(createLightParamCentered<VCVLightLatch<MediumSimpleLight<WhiteLight>>>(mm2px(Vec(x + BTN_DX, y)), module, Kleener::BIT_PARAM + i, Kleener::BIT_LIGHT + i));
			addParam(createParamCentered<CKSSThree>(mm2px(Vec(x + SW_DX, y)), module, Kleener::BUS_PARAM + i));
			addChild(createLightCentered<SmallLight<YellowLight>>(mm2px(Vec(x + BTN_DX, y + LIGHT_DY)), module, Kleener::REG_LIGHT + i));
			addChild(createLightCentered<TinyLight<WhiteLight>>(mm2px(Vec(x + SW_DX, y + LIGHT_DY)), module, Kleener::HEAD_LIGHT + i));
		}
		for (int i = 0; i < NUM_SLIDERS; i++)
			addParam(createParamCentered<VCVSlider>(mm2px(Vec(COL_X0 + i * COL_DX, SLIDER_Y)), module, Kleener::SLIDER_PARAM + i));

		addParam(createParamCentered<RoundBlackSnapKnob>(mm2px(Vec(X_LEN, KNOB_Y)), module, Kleener::LENGTH_PARAM));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(X_LENCV, KNOB_Y)), module, Kleener::LENGTH_INPUT));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(X_MUT, KNOB_Y)), module, Kleener::MUTATE_PARAM));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(X_MUTCV, KNOB_Y)), module, Kleener::MUTATE_INPUT));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(X_RANGE, KNOB_Y)), module, Kleener::RANGE_PARAM));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(X_RANGECV, KNOB_Y)), module, Kleener::RANGE_INPUT));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(X_GLIDE, KNOB_Y)), module, Kleener::GLIDE_PARAM));
		addParam(createParamCentered<RoundBlackSnapKnob>(mm2px(Vec(X_SCALE, KNOB_Y)), module, Kleener::SCALE_PARAM));
		for (int k = 0; k < NUM_SCALES; k++)
			addChild(createLightCentered<SmallLight<YellowLight>>(mm2px(Vec(SCALE_LX, SCALE_Y0 + k * SCALE_DY)), module, Kleener::SCALE_LIGHT + k));
		addParam(createLightParamCentered<VCVLightBezel<YellowLight>>(mm2px(Vec(X_LOAD, KNOB_Y)), module, Kleener::LOAD_PARAM, Kleener::LOAD_LIGHT));

		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(X_CLOCK, JACK_Y)), module, Kleener::CLOCK_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(X_RESET, JACK_Y)), module, Kleener::RESET_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(X_LOADIN, JACK_Y)), module, Kleener::LOAD_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(X_FLIP, JACK_Y)), module, Kleener::FLIP_INPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(X_A, JACK_Y)), module, Kleener::A_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(X_B, JACK_Y)), module, Kleener::B_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(X_BUS1, JACK_Y)), module, Kleener::BUS_OUTPUT + 0));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(X_BUS2, JACK_Y)), module, Kleener::BUS_OUTPUT + 1));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(X_BUS3, JACK_Y)), module, Kleener::BUS_OUTPUT + 2));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(X_ANY, JACK_Y)), module, Kleener::ANY_OUTPUT));
	}

	void appendContextMenu(Menu* menu) override {
		Kleener* module = dynamic_cast<Kleener*>(this->module);
		if (!module)
			return;
		menu->addChild(new MenuSeparator);
		menu->addChild(createIndexPtrSubmenuItem("Pitch B reads",
			{"The next 8 bits of the loop", "The switched-off bits (opposite of A)"}, &module->bMode));
		menu->addChild(createBoolPtrMenuItem("Tie gates across neighbouring steps on the same bus", "", &module->tieGates));
		menu->addChild(createBoolPtrMenuItem("Switches change the loop straight away", "", &module->liveSwitches));
		menu->addChild(createBoolPtrMenuItem("RESET also loads the switches", "", &module->resetReloads));
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuItem("Copy the loop onto the switches", "", [=]() {
			for (int i = 0; i < NUM_BITS; i++) {
				module->params[Kleener::BIT_PARAM + i].setValue(module->reg[i] ? 1.f : 0.f);
				module->lastSwitch[i] = module->reg[i];
			}
		}));
		menu->addChild(createMenuItem("Random switches", "", [=]() {
			for (int i = 0; i < NUM_BITS; i++)
				module->params[Kleener::BIT_PARAM + i].setValue(random::uniform() < 0.5f ? 1.f : 0.f);
		}));
		menu->addChild(createMenuItem("Random bus switches", "", [=]() {
			for (int i = 0; i < NUM_BITS; i++)
				module->params[Kleener::BUS_PARAM + i].setValue((float) (random::u32() % 3));
		}));
	}
};

Model* modelKleener = createModel<Kleener, KleenerWidget>("Kleener");
