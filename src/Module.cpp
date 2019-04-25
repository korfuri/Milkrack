#define NANOVG_GL2
#include "window.hpp"
#include "Milkrack.hpp"
#include "widgets.hpp"
#include "dsp/digital.hpp"
#include "nanovg_gl.h"
#include "deps/projectm/src/libprojectM/projectM.hpp"
#include "Renderer.hpp"

#include <thread>

static const unsigned int kSampleWindow = 512;

struct MilkrackModule : Module {
  enum ParamIds {
    NEXT_PRESET_PARAM,
    NUM_PARAMS
  };
  enum InputIds {
    LEFT_INPUT, RIGHT_INPUT,
    NEXT_PRESET_INPUT,
    NUM_INPUTS
  };
  enum OutputIds {
    NUM_OUTPUTS
  };
  enum LightIds {
    NUM_LIGHTS
  };

  MilkrackModule() : Module(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS) {}

  unsigned int i = 0;
  bool full = false;
  bool nextPreset = false;
  SchmittTrigger nextPresetTrig;
  float pcm_data[kSampleWindow];

  void step() override {
    pcm_data[i++] = inputs[LEFT_INPUT].value;
    if (inputs[RIGHT_INPUT].active)
      pcm_data[i++] = inputs[RIGHT_INPUT].value;
    else
      pcm_data[i++] = inputs[LEFT_INPUT].value;
    if (i >= kSampleWindow) {
      i = 0;
      full = true;
    }
    if (nextPresetTrig.process(params[NEXT_PRESET_PARAM].value + inputs[NEXT_PRESET_INPUT].value)) {
      nextPreset = true;
    }
  }
};


struct BaseProjectMWidget : FramebufferWidget {
  const int fps = 60;
  const bool debug = true;
  const projectM::Settings s;

  MilkrackModule* module;

  std::shared_ptr<Font> font;

  BaseProjectMWidget() {}
  virtual ~BaseProjectMWidget() {}

  void init(std::string presetURL) {
    getRenderer()->init(initSettings(presetURL));
  }

  template<typename T>
  static BaseProjectMWidget* create(Vec pos, std::string presetURL) {
    BaseProjectMWidget* p = new T;
    p->box.pos = pos;
    p->init(presetURL);
    return p;
  }

  virtual ProjectMRenderer* getRenderer() = 0;

  void step() override {
    dirty = true;
    if (module->full) {
      getRenderer()->addPCMData(module->pcm_data, kSampleWindow);
      module->full = false;
    }
    // If the module requests that we change the preset at random
    // (i.e. the random button was clicked), tell the render thread to
    // do so on the next pass.
    if (module->nextPreset) {
      module->nextPreset = false;
      getRenderer()->requestPresetID(kPresetIDRandom);
    }
  }

  void randomize() {
    // Tell the render thread to switch to another preset on the next
    // pass.
    getRenderer()->requestPresetID(kPresetIDRandom);
  }

  // Builds a Settings object to initialize the member one in the
  // object.  This is needed because we must initialize this->settings
  // before starting this->renderThread, and that has to be done in
  // ProjectMWidget's ctor init list.
  projectM::Settings initSettings(std::string presetURL) const {
    projectM::Settings s;
    s.presetURL = presetURL;
    s.windowWidth = 360;
    s.windowHeight = 360;
    return s;
  }
};

struct WindowedProjectMWidget : BaseProjectMWidget {
  WindowedRenderer* renderer;

  WindowedProjectMWidget() : renderer(new WindowedRenderer) {}

  ~WindowedProjectMWidget() { delete renderer; }

  ProjectMRenderer* getRenderer() override { return renderer; }

  void draw(NVGcontext* vg) override {
    nvgSave(vg);
    nvgBeginPath(vg);
    nvgFillColor(vg, nvgRGB(0x06, 0xbd, 0x01));
    nvgFontSize(vg, 14);
    nvgFontFaceId(vg, font->handle);
    nvgTextAlign(vg, NVG_ALIGN_BOTTOM);
    nvgScissor(vg, 5, 5, 20, 330);
    nvgRotate(vg, M_PI/2);
    if (!getRenderer()->isRendering()) {
      nvgText(vg, 5, -7, "Unable to initialize rendering. See log for details.", nullptr);
    } else {
      nvgText(vg, 5, -7, getRenderer()->activePresetName().c_str(), nullptr);
    }
    nvgFill(vg);
    nvgClosePath(vg);
    nvgRestore(vg);
  }
};

struct EmbeddedProjectMWidget : BaseProjectMWidget {
  static const int x = 360;
  static const int y = 360;

  TextureRenderer* renderer;

  EmbeddedProjectMWidget() : renderer(new TextureRenderer) {}

  ~EmbeddedProjectMWidget() { delete renderer; }

  ProjectMRenderer* getRenderer() override { return renderer; }

  void draw(NVGcontext* vg) override {
    int img = nvglCreateImageFromHandleGL2(vg, renderer->getTextureID(), x, y, 0);
    NVGpaint imgPaint = nvgImagePattern(vg, 0, 0, x, y, 0.0f, img, 1.0f);

    nvgBeginPath(vg);
    nvgRect(vg, 0, 0, x, y);
    nvgFillPaint(vg, imgPaint);
    nvgFill(vg);
    nvgClosePath(vg);

    nvgSave(vg);
    nvgScissor(vg, 0, 0, x, y);
    nvgBeginPath(vg);
    nvgFillColor(vg, nvgRGB(0x06, 0xbd, 0x01));
    nvgFontSize(vg, 14);
    nvgFontFaceId(vg, font->handle);
    nvgTextAlign(vg, NVG_ALIGN_BOTTOM);
    nvgText(vg, 10, 20, getRenderer()->activePresetName().c_str(), nullptr);
    nvgFill(vg);
    nvgClosePath(vg);
    nvgRestore(vg);
  }
};


struct SetPresetMenuItem : MenuItem {
  BaseProjectMWidget* w;
  unsigned int presetId;

  void onAction(EventAction& e) override {
    w->getRenderer()->requestPresetID(presetId);
  }

  void step() override {
    rightText = (w->getRenderer()->activePreset() == presetId) ? "<<" : "";
    MenuItem::step();
  }

  static SetPresetMenuItem* construct(std::string label, unsigned int i, BaseProjectMWidget* w) {
    SetPresetMenuItem* m = new SetPresetMenuItem;
    m->w = w;
    m->presetId = i;
    m->text = label;
    return m;
  }
};

struct ToggleAutoplayMenuItem : MenuItem {
  BaseProjectMWidget* w;

  void onAction(EventAction& e) override {
    w->getRenderer()->requestToggleAutoplay();
  }

  void step() override {
    rightText = (w->getRenderer()->isAutoplayEnabled() ? "yes" : "no");
    MenuItem::step();
  }

  static ToggleAutoplayMenuItem* construct(std::string label, BaseProjectMWidget* w) {
    ToggleAutoplayMenuItem* m = new ToggleAutoplayMenuItem;
    m->w = w;
    m->text = label;
    return m;
  }
};


struct BaseMilkrackModuleWidget : ModuleWidget {
  BaseProjectMWidget* w;

  using ModuleWidget::ModuleWidget;

  void randomize() override {
    w->randomize();
  }

  void appendContextMenu(Menu* menu) override {
    MilkrackModule* m = dynamic_cast<MilkrackModule*>(module);
    assert(m);

    menu->addChild(construct<MenuLabel>());
    menu->addChild(construct<MenuLabel>(&MenuLabel::text, "Options"));
    menu->addChild(ToggleAutoplayMenuItem::construct("Cycle through presets", w));

    menu->addChild(construct<MenuLabel>());
    menu->addChild(construct<MenuLabel>(&MenuLabel::text, "Preset"));
    auto presets = w->getRenderer()->listPresets();
    for (auto p : presets) {
      menu->addChild(SetPresetMenuItem::construct(p.second, p.first, w));
    }
  }
};

struct MilkrackModuleWidget : BaseMilkrackModuleWidget {
  MilkrackModuleWidget(MilkrackModule* module) : BaseMilkrackModuleWidget(module) {
    setPanel(SVG::load(assetPlugin(plugin, "res/MilkrackSeparateWindow.svg")));

    addInput(Port::create<PJ301MPort>(Vec(15, 60), Port::INPUT, module, MilkrackModule::LEFT_INPUT));
    addInput(Port::create<PJ301MPort>(Vec(15, 90), Port::INPUT, module, MilkrackModule::RIGHT_INPUT));

    addParam(ParamWidget::create<TL1105>(Vec(19, 150), module, MilkrackModule::NEXT_PRESET_PARAM, 0.0, 1.0, 0.0));
    addInput(Port::create<PJ301MPort>(Vec(15, 170), Port::INPUT, module, MilkrackModule::NEXT_PRESET_INPUT));

    std::shared_ptr<Font> font = Font::load(assetPlugin(plugin, "res/fonts/LiberationSans/LiberationSans-Regular.ttf"));
    w = BaseProjectMWidget::create<WindowedProjectMWidget>(Vec(50, 20), assetPlugin(plugin, "presets_projectM/"));
    w->module = module;
    w->font = font;
    addChild(w);
  }
};


struct EmbeddedMilkrackModuleWidget : BaseMilkrackModuleWidget {
  EmbeddedMilkrackModuleWidget(MilkrackModule* module) : BaseMilkrackModuleWidget(module) {
    setPanel(SVG::load(assetPlugin(plugin, "res/MilkrackModule.svg")));

    addInput(Port::create<PJ301MPort>(Vec(15, 60), Port::INPUT, module, MilkrackModule::LEFT_INPUT));
    addInput(Port::create<PJ301MPort>(Vec(15, 90), Port::INPUT, module, MilkrackModule::RIGHT_INPUT));

    addParam(ParamWidget::create<TL1105>(Vec(19, 150), module, MilkrackModule::NEXT_PRESET_PARAM, 0.0, 1.0, 0.0));
    addInput(Port::create<PJ301MPort>(Vec(15, 170), Port::INPUT, module, MilkrackModule::NEXT_PRESET_INPUT));

    std::shared_ptr<Font> font = Font::load(assetPlugin(plugin, "res/fonts/LiberationSans/LiberationSans-Regular.ttf"));
    w = BaseProjectMWidget::create<EmbeddedProjectMWidget>(Vec(50, 10), assetPlugin(plugin, "presets_projectM/"));
    w->module = module;
    w->font = font;
    addChild(w);
  }
};

Model *modelWindowedMilkrackModule = Model::create<MilkrackModule, MilkrackModuleWidget>("Milkrack", "Milkrack Windowed", "Milkrack - Windowed", VISUAL_TAG);
Model *modelEmbeddedMilkrackModule = Model::create<MilkrackModule, EmbeddedMilkrackModuleWidget>("Milkrack", "Milkrack Embedded", "Milkrack - Embedded", VISUAL_TAG);
