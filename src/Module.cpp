#include <GL/gl.h>
#define NANOVG_GL2
#include "Milkrack.hpp"
#include "widgets.hpp"
#include "dsp/digital.hpp"
#include "nanovg_gl.h"
#include "deps/projectm/src/libprojectM/projectM.hpp"
#include "deps/projectm/src/libprojectM/wipemalloc.h"

#include <iostream>

struct MilkrackModule : Module {
  enum ParamIds {
    NEXT_PRESET_PARAM,
    NUM_PARAMS
  };
  enum InputIds {
    LEFT_INPUT, RIGHT_INPUT,
    NUM_INPUTS
  };
  enum OutputIds {
    NUM_OUTPUTS
  };
  enum LightIds {
    NUM_LIGHTS
  };

  MilkrackModule() : Module(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS) {}
  void step() override;

  int i = 0;
  bool full = false;
  bool nextPreset = false;
  SchmittTrigger nextPresetTrig;
  float pcm_data[512];
};


void MilkrackModule::step() {
  pcm_data[i++] = inputs[LEFT_INPUT].value;
  pcm_data[i++] = inputs[RIGHT_INPUT].value;
  if (i >= 512) {
    i = 0;
    full = true;
  }
  if (nextPresetTrig.process(params[NEXT_PRESET_PARAM].value)) {
    nextPreset = true;
  }
}


struct ProjectMWidget : FramebufferWidget {
  projectM* pm;
  unsigned int texture;
  MilkrackModule* module;
  float x = 360.0;
  float y = 360.0;
  bool debug = true;
  std::shared_ptr<Font> font;

  ProjectMWidget() {
    projectM::Settings s;
    s.presetURL = "/home/korfuri/Code/Rack-SDK/plugins/Milkrack/src/deps/projectm/presets/presets_projectM/";
    pm = new projectM(s);
    texture = pm->initRenderToTexture();
    nextPreset();
  }

  ~ProjectMWidget() {
    //delete projectM; // TODO
  }

  int stepi = 0;

  void step() override {
    dirty = true;
    if (module->full) {
      pm->pcm()->addPCMfloat_2ch(module->pcm_data, 512);
      module->full = false;
    }
    if (module->nextPreset) {
      module->nextPreset = false;
      nextPreset();
    }
  }

  std::string activePreset() {
    unsigned int presetIdx;
    if (pm->selectedPresetIndex(presetIdx)) {
     return pm->getPresetName(presetIdx);
    }
    return "";
  }

  void draw(NVGcontext* vg) override {
    pm->renderFrame();

    int img = nvglCreateImageFromHandleGL2(vg, texture, x, y, 0);
    NVGpaint imgPaint = nvgImagePattern(vg, 0, 0, x, y, 0.0f, img, 1.0f);

    nvgBeginPath(vg);
    nvgRect(vg, 0, 0, x, y);
    nvgFillPaint(vg, imgPaint);
    nvgFill(vg);
    nvgClosePath(vg);

    if (debug) {
      nvgSave(vg);
      nvgScissor(vg, 0, 0, x, y);
      nvgBeginPath(vg);
      nvgFillColor(vg, nvgRGB(0x06, 0xbd, 0x01));
      nvgFontSize(vg, 14);
      nvgFontFaceId(vg, font->handle);
      nvgTextAlign(vg, NVG_ALIGN_BOTTOM);
      nvgText(vg, 10, 20, activePreset().c_str(), nullptr);
      nvgFill(vg);
      nvgClosePath(vg);
      nvgRestore(vg);
    }
  }

  void nextPreset() {
    unsigned int n = pm->getPlaylistSize();
    if (n) {
      pm->selectPreset(rand() % n);
    }
  }
};

struct MilkrackModuleWidget : ModuleWidget {
  MilkrackModuleWidget(MilkrackModule *module) : ModuleWidget(module) {
    setPanel(SVG::load(assetPlugin(plugin, "res/MilkrackModule.svg")));

    addInput(Port::create<PJ301MPort>(Vec(15, 60), Port::INPUT, module, MilkrackModule::LEFT_INPUT));
    addInput(Port::create<PJ301MPort>(Vec(15, 90), Port::INPUT, module, MilkrackModule::RIGHT_INPUT));

    addParam(ParamWidget::create<TL1105>(Vec(19, 150), module, MilkrackModule::NEXT_PRESET_PARAM, 0.0, 1.0, 0.0));

    std::shared_ptr<Font> font = Font::load(assetPlugin(plugin, "res/fonts/LiberationSans/LiberationSans-Regular.ttf"));
    ProjectMWidget* w = Widget::create<ProjectMWidget>(Vec(50, 10));
    w->module = module;
    w->font = font;
    addChild(w);
  }
};


// Specify the Module and ModuleWidget subclass, human-readable
// author name for categorization per plugin, module slug (should never
// change), human-readable module name, and any number of tags
// (found in `include/tags.hpp`) separated by commas.
Model *modelMilkrackModule = Model::create<MilkrackModule, MilkrackModuleWidget>("Milkrack", "Milkrack", "Milkrack - Old Skool Winamp visuals in yo rack", VISUAL_TAG);
