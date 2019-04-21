#if defined(__APPLE__)
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif
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

  ProjectMWidget(std::string presetURL) {
    projectM::Settings s;
    s.presetURL = presetURL;
    pm = new projectM(s);
    texture = pm->initRenderToTexture();
    setAutoplay(false);
    nextPreset();
  }

  static ProjectMWidget* create(Vec pos, std::string presetURL) {
    ProjectMWidget* p = new ProjectMWidget(presetURL);
    p->box.pos = pos;
    return p;
  }

  ~ProjectMWidget() {
    delete pm;
  }

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

  unsigned int activePreset() const {
    unsigned int presetIdx;
    pm->selectedPresetIndex(presetIdx);
    return presetIdx;
  }

  std::string activePresetName() const {
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
      nvgText(vg, 10, 20, activePresetName().c_str(), nullptr);
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

  void setPreset(unsigned int i) {
    unsigned int n = pm->getPlaylistSize();
    if (n && i < n) {
      pm->selectPreset(i);
    }
  }

  void setAutoplay(bool enable) {
    pm->setPresetLock(!enable);
  }

  bool isAutoplayEnabled() const {
    return !pm->isPresetLocked();
  }

  std::list<std::pair<unsigned int, std::string> > listPresets() {
    std::list<std::pair<unsigned int, std::string> > presets;
    unsigned int n = pm->getPlaylistSize();
    if (!n) {
      return presets;
    }
    for (unsigned int i = 0; i < n; ++i){
      presets.push_back(std::make_pair(i, std::string(pm->getPresetName(i))));
    }
    return presets;
  }
};


struct SetPresetMenuItem : MenuItem {
  ProjectMWidget* w;
  unsigned int presetId;

  void onAction(EventAction& e) override {
    w->setPreset(presetId);
  }

  void step() override {
    rightText = (w->activePreset() == presetId) ? "<<" : "";
    MenuItem::step();
  }

  static SetPresetMenuItem* construct(std::string label, unsigned int i, ProjectMWidget* w) {
    SetPresetMenuItem* m = new SetPresetMenuItem;
    m->w = w;
    m->presetId = i;
    m->text = label;
    return m;
  }
};

struct ToggleAutoplayMenuItem : MenuItem {
  ProjectMWidget* w;

  void onAction(EventAction& e) override {
    w->setAutoplay(!w->isAutoplayEnabled());
  }

  void step() override {
    rightText = (w->isAutoplayEnabled() ? "yes" : "no");
    MenuItem::step();
  }

  static ToggleAutoplayMenuItem* construct(std::string label, ProjectMWidget* w) {
    ToggleAutoplayMenuItem* m = new ToggleAutoplayMenuItem;
    m->w = w;
    m->text = label;
    return m;
  }
};


struct MilkrackModuleWidget : ModuleWidget {
  ProjectMWidget* w;

  MilkrackModuleWidget(MilkrackModule *module) : ModuleWidget(module) {
    setPanel(SVG::load(assetPlugin(plugin, "res/MilkrackModule.svg")));

    addInput(Port::create<PJ301MPort>(Vec(15, 60), Port::INPUT, module, MilkrackModule::LEFT_INPUT));
    addInput(Port::create<PJ301MPort>(Vec(15, 90), Port::INPUT, module, MilkrackModule::RIGHT_INPUT));

    addParam(ParamWidget::create<TL1105>(Vec(19, 150), module, MilkrackModule::NEXT_PRESET_PARAM, 0.0, 1.0, 0.0));

    std::shared_ptr<Font> font = Font::load(assetPlugin(plugin, "res/fonts/LiberationSans/LiberationSans-Regular.ttf"));
    w = ProjectMWidget::create(Vec(50, 10), assetPlugin(plugin, "presets/presets_projectM/"));
    w->module = module;
    w->font = font;
    addChild(w);
  }

  void randomize() override {
    w->nextPreset();
  }

  void appendContextMenu(Menu* menu) override {
    MilkrackModule* m = dynamic_cast<MilkrackModule*>(module);
    assert(m);

    menu->addChild(construct<MenuLabel>());
    menu->addChild(construct<MenuLabel>(&MenuLabel::text, "Options"));
    menu->addChild(ToggleAutoplayMenuItem::construct("Cycle through presets", w));

    menu->addChild(construct<MenuLabel>());
    menu->addChild(construct<MenuLabel>(&MenuLabel::text, "Preset"));
    auto presets = w->listPresets();
    for (auto p : presets) {
      menu->addChild(SetPresetMenuItem::construct(p.second, p.first, w));
    }
  }
};


// Specify the Module and ModuleWidget subclass, human-readable
// author name for categorization per plugin, module slug (should never
// change), human-readable module name, and any number of tags
// (found in `include/tags.hpp`) separated by commas.
Model *modelMilkrackModule = Model::create<MilkrackModule, MilkrackModuleWidget>("Milkrack", "Milkrack", "Milkrack - Old Skool Winamp visuals in yo rack", VISUAL_TAG);
