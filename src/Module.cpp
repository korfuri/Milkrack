#define NANOVG_GL2
#include "window.hpp"
#include "Milkrack.hpp"
#include "widgets.hpp"
#include "dsp/digital.hpp"
#include "nanovg_gl.h"
#include "deps/projectm/src/libprojectM/projectM.hpp"
#include "deps/projectm/src/libprojectM/wipemalloc.h"

#include <thread>

static const unsigned int kSampleWindow = 512;

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

  unsigned int i = 0;
  bool full = false;
  bool nextPreset = false;
  SchmittTrigger nextPresetTrig;
  float pcm_data[kSampleWindow];
};


void MilkrackModule::step() {
  pcm_data[i++] = inputs[LEFT_INPUT].value;
  if (inputs[RIGHT_INPUT].active)
    pcm_data[i++] = inputs[RIGHT_INPUT].value;
  else
    pcm_data[i++] = inputs[LEFT_INPUT].value;
  if (i >= kSampleWindow) {
    i = 0;
    full = true;
  }
  if (nextPresetTrig.process(params[NEXT_PRESET_PARAM].value)) {
    nextPreset = true;
  }
}

struct ProjectMWidget : FramebufferWidget {
  enum Status {
    NOT_INITIALIZED,
    SETTINGS_SET,
    RENDERING,
    FAILED,
    PLEASE_EXIT,
    EXITING
  };

  const float x = 360.0;
  const float y = 360.0;
  const int fps = 60;
  const bool debug = true;
  const projectM::Settings s;

  // Owned by the UI thread (step(), draw())
  MilkrackModule* module;
  std::thread renderThread;
  std::shared_ptr<Font> font;

  // Shared between threads
  projectM* pm; // guarded by pm_m;
  unsigned int texture; // guarded by pm_m;
  Status status = Status::NOT_INITIALIZED; // guarded by pm_m
  std::condition_variable pm_cv; // guarded by pm_m;
  static const int kPresetIDRandom = -1; // Switch to a random preset
  static const int kPresetIDKeep = -2; // Keep the current preset
  int requestPresetID = kPresetIDKeep; // guarded by pm_m; // Indicates to the render thread that it should switch to the specified preset
  std::mutex pm_m;

  ProjectMWidget(std::string presetURL) : s(initSettings(presetURL)), renderThread([this](){ this->renderLoop(); }) {
    // Block until renderLoop() finished its initialization
    std::unique_lock<std::mutex> l(pm_m);
    while (status != Status::RENDERING && status != Status::FAILED) {
      pm_cv.wait(l);
    }
  }

  static ProjectMWidget* create(Vec pos, std::string presetURL) {
    ProjectMWidget* p = new ProjectMWidget(presetURL);
    p->box.pos = pos;
    return p;
  }

  ~ProjectMWidget() {
    {
      std::lock_guard <std::mutex> l(pm_m);
      if (status != Status::FAILED) {
	status = Status::PLEASE_EXIT;
      }
    }
    renderThread.join();
  }

  void step() override {
    dirty = true;
    if (module->full) {
      std::lock_guard<std::mutex> l(pm_m);
      if (!pm) return;
      pm->pcm()->addPCMfloat_2ch(module->pcm_data, kSampleWindow);
      module->full = false;
    }
    // If the module requests that we change the preset at random
    // (i.e. the random button was clicked), tell the render thread to
    // do so on the next pass.
    if (module->nextPreset) {
      module->nextPreset = false;
      std::lock_guard<std::mutex> l(pm_m);
      requestPresetID = kPresetIDRandom;
    }
  }

  unsigned int activePreset() {
    std::lock_guard<std::mutex> l(pm_m);
    unsigned int presetIdx;
    if (!pm) return 0;
    pm->selectedPresetIndex(presetIdx);
    return presetIdx;
  }

  std::string activePresetName() {
    std::lock_guard<std::mutex> l(pm_m);
    unsigned int presetIdx;
    if (!pm) return "";
    if (pm->selectedPresetIndex(presetIdx)) {
     return pm->getPresetName(presetIdx);
    }
    return "";
  }

  void draw(NVGcontext* vg) override {
    {
      std::lock_guard<std::mutex> l(pm_m);
      if (status == Status::RENDERING) {
	nvgBeginPath(vg);
	nvgRect(vg, 0, 0, x, y);
	int img = nvglCreateImageFromHandleGL2(vg, texture, x, y, 0); // TODO can we memoize this?
	NVGpaint imgPaint = nvgImagePattern(vg, 0, 0, x, y, 0.0f, img, 1.0f); // TODO can we memoize that?
	nvgFillPaint(vg, imgPaint);
	nvgFill(vg);
	nvgClosePath(vg);
      }
    }

    nvgSave(vg);
    nvgScissor(vg, 0, 0, x, y);
    nvgBeginPath(vg);
    nvgFillColor(vg, nvgRGB(0x06, 0xbd, 0x01));
    nvgFontSize(vg, 14);
    nvgFontFaceId(vg, font->handle);
    nvgTextAlign(vg, NVG_ALIGN_BOTTOM);
    nvgText(vg, 10, 20, activePresetName().c_str(), nullptr);
    if (status == Status::FAILED) {
      nvgText(vg, 10, 100, "Unable to initialize rendering. See log for details.", nullptr);
    }
    nvgFill(vg);
    nvgClosePath(vg);
    nvgRestore(vg);
  }

  void setAutoplay(bool enable) {
    std::lock_guard<std::mutex> l(pm_m);
    if (!pm) return;
    pm->setPresetLock(!enable);
  }

  bool isAutoplayEnabled() {
    std::lock_guard<std::mutex> l(pm_m);
    if (!pm) return false;
    return !pm->isPresetLocked();
  }

  std::list<std::pair<unsigned int, std::string> > listPresets() {
    std::list<std::pair<unsigned int, std::string> > presets;
    std::lock_guard<std::mutex> l(pm_m);
    if (!pm) return presets;
    unsigned int n = pm->getPlaylistSize();
    if (!n) {
      return presets;
    }
    for (unsigned int i = 0; i < n; ++i){
      presets.push_back(std::make_pair(i, std::string(pm->getPresetName(i))));
    }
    return presets;
  }

  void randomize() {
    // Tell the render thread to switch to another preset on the next
    // pass.
    std::lock_guard<std::mutex> l(pm_m);
    requestPresetID = kPresetIDRandom;
  }

    void logContextInfo(std::string name, GLFWwindow* w) const {
    int major = glfwGetWindowAttrib(w, GLFW_CONTEXT_VERSION_MAJOR);
    int minor = glfwGetWindowAttrib(w, GLFW_CONTEXT_VERSION_MINOR);
    int revision = glfwGetWindowAttrib(w, GLFW_CONTEXT_REVISION);
    int api = glfwGetWindowAttrib(w, GLFW_CLIENT_API);
    rack::loggerLog(DEBUG_LEVEL, "Milkrack/" __FILE__, __LINE__, "%s context using API %d version %d.%d.%d, profile %d", name.c_str(), api, major, minor, revision);
  }

  // Runs in renderThread. This function is responsible for the whole
  // lifetime of the rendering GL context and the projectM instance.
  void renderLoop() {
    logContextInfo("gWindow", rack::gWindow);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_MAXIMIZED, GLFW_TRUE);
    GLFWwindow* offscreen_context = glfwCreateWindow(x, y, "", NULL, rack::gWindow);
    if (!offscreen_context) {
      rack::loggerLog(DEBUG_LEVEL, "Milkrack/" __FILE__, __LINE__, "Milkrack renderLoop could not create a context, bailing.");
      std::lock_guard<std::mutex> l(pm_m);
      status = Status::FAILED;
      pm = nullptr;
      pm_cv.notify_all(); // Tell the main thread it can continue
      return;
    }
    glfwMakeContextCurrent(offscreen_context);
    logContextInfo("offscreen_context", offscreen_context);

    // Initialize projectM rendering to texture
    {
      std::lock_guard<std::mutex> l(pm_m);
      pm = new projectM(s);
      texture = pm->initRenderToTexture();
      status = Status::RENDERING;
      pm_cv.notify_all(); // Tell the main thread it can continue
    }

    setAutoplay(false);
    renderLoopNextPreset();

    while (true) {
      {
	std::lock_guard<std::mutex> l(pm_m);
	// Did the main thread request that we exit?
	if (status == Status::PLEASE_EXIT) {
	  break;
	}
	// Did the main thread request that we change the preset?
	if (requestPresetID != kPresetIDKeep) {
	  if (requestPresetID == kPresetIDRandom) {
	    renderLoopNextPreset();
	  } else {
	    renderLoopSetPreset(requestPresetID);
	  }
	  requestPresetID = kPresetIDKeep;
	}
	pm->renderFrame();
      }
      usleep(1000000/fps);
    }

    delete pm;
    glfwDestroyWindow(offscreen_context);
    status = Status::EXITING;
  }

  // Builds a Settings object to initialize the member one in the
  // object.  This is needed because we must initialize this->settings
  // before starting this->renderThread, and that has to be done in
  // ProjectMWidget's ctor init list.
  projectM::Settings initSettings(std::string presetURL) const {
    projectM::Settings s;
    s.presetURL = presetURL;
    s.windowWidth = x;
    s.windowHeight = y;
    return s;
  }

    // Switch to the next preset. This should be called only from the
  // render thread. pm_m must be held when calling this.
  void renderLoopNextPreset() {
    unsigned int n = pm->getPlaylistSize();
    if (n) {
      pm->selectPreset(rand() % n);
    }
  }

  // Switch to the indicated preset. This should be called only from
  // the render thread. pm_m must be held when calling this.
  void renderLoopSetPreset(unsigned int i) {
    unsigned int n = pm->getPlaylistSize();
    if (n && i < n) {
      pm->selectPreset(i);
    }
  }
};


struct SetPresetMenuItem : MenuItem {
  ProjectMWidget* w;
  unsigned int presetId;

  void onAction(EventAction& e) override {
    w->requestPresetID = presetId;
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
