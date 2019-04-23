#define NANOVG_GL2
#include "window.hpp"
#include "Milkrack.hpp"
#include "widgets.hpp"
#include "dsp/digital.hpp"
#include "nanovg_gl.h"
#include "deps/projectm/src/libprojectM/projectM.hpp"
#include "deps/projectm/src/libprojectM/wipemalloc.h"
#include "glfwUtils.hpp"

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

  MilkrackModule* module;

  // Owned by the render thread
  GLFWwindow* window;
  projectM* pm = nullptr;
  bool dirtySize = false;

  // Owned by the main thread
  std::shared_ptr<Font> font;
  std::thread renderThread;

  // Shared between threads
  Status status = Status::NOT_INITIALIZED; // guarded by pm_m
  static const int kPresetIDRandom = -1; // Switch to a random preset
  static const int kPresetIDKeep = -2; // Keep the current preset
  int requestPresetID = kPresetIDKeep; // guarded by pm_m // Indicates to the render thread that it should switch to the specified preset
  bool requestToggleAutoplay = false; // guarded by pm_m
  std::mutex pm_m;

  ProjectMWidget(std::string presetURL) : s(initSettings(presetURL)),
					  window(createWindow()),
					  renderThread([this](){ this->renderLoop(); }) {
  }

  static ProjectMWidget* create(Vec pos, std::string presetURL) {
    ProjectMWidget* p = new ProjectMWidget(presetURL);
    p->box.pos = pos;
    return p;
  }

  ~ProjectMWidget() {
    {
      // Request that the render thread terminates the renderLoop
      std::lock_guard <std::mutex> l(pm_m);
      if (status != Status::FAILED) {
	status = Status::PLEASE_EXIT;
      }
    }
    // Wait for renderLoop to terminate before releasing resources
    renderThread.join();
  }

  void step() override {
    {
      std::lock_guard<std::mutex> l(pm_m);
      if (status != Status::RENDERING) return;
    }
    dirty = true;
    if (module->full) {
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

  unsigned int activePreset() const {
    unsigned int presetIdx;
    if (!pm) return 0;
    pm->selectedPresetIndex(presetIdx);
    return presetIdx;
  }

  std::string activePresetName() const {
    unsigned int presetIdx;
    if (!pm) return "";
    if (pm->selectedPresetIndex(presetIdx)) {
     return pm->getPresetName(presetIdx);
    }
    return "";
  }

  void draw(NVGcontext* vg) override {
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

  void toggleAutoplay() {
    std::lock_guard<std::mutex> l(pm_m);
    requestToggleAutoplay = true;
  }

  void renderSetAutoplay(bool enable) {
    if (!pm) return;
    pm->setPresetLock(!enable);
  }

  bool isAutoplayEnabled() const {
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

  static void logGLFWError(int errcode, const char* errmsg) {
    rack::loggerLog(WARN_LEVEL, "Milkrack/" __FILE__, 0, "GLFW error %d: %s", errcode, errmsg);
  }

  GLFWwindow* createWindow() {
    glfwSetErrorCallback(logGLFWError);
    logContextInfo("gWindow", rack::gWindow);
    glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    glfwWindowHint(GLFW_MAXIMIZED, GLFW_FALSE);
    GLFWwindow* c = glfwCreateWindow(x, y, "", NULL, NULL);

    if (!c) {
      rack::loggerLog(DEBUG_LEVEL, "Milkrack/" __FILE__, __LINE__, "Milkrack renderLoop could not create a context, bailing.");
    }

    glfwSetWindowUserPointer(c, reinterpret_cast<void*>(this));
    glfwSetFramebufferSizeCallback(c, framebufferSizeCallback);
    glfwSetWindowCloseCallback(c, [](GLFWwindow* w) { glfwIconifyWindow(w); });
    glfwSetKeyCallback(c, keyCallback);
    glfwSetWindowTitle(c, u8"Milkrack");
    return c;
  }

  static void framebufferSizeCallback(GLFWwindow* win, int x, int y) {
    ProjectMWidget* widget = reinterpret_cast<ProjectMWidget*>(glfwGetWindowUserPointer(win));
    widget->dirtySize = true;
  }

  int last_xpos, last_ypos, last_width, last_height;
  static void keyCallback(GLFWwindow* win, int key, int scancode, int action, int mods) {
    ProjectMWidget* widget = reinterpret_cast<ProjectMWidget*>(glfwGetWindowUserPointer(win));
    if (action != GLFW_PRESS) return;
    switch (key) {
    case GLFW_KEY_F:
    case GLFW_KEY_F4:
    case GLFW_KEY_ENTER:
      {
	const GLFWmonitor* current_monitor = glfwGetWindowMonitor(win);
	if (!current_monitor) {
	  GLFWmonitor* best_monitor = glfwWindowGetNearestMonitor(win);
	  const GLFWvidmode* mode = glfwGetVideoMode(best_monitor);
	  glfwGetWindowPos(win, &widget->last_xpos, &widget->last_ypos);
	  glfwGetWindowSize(win, &widget->last_width, &widget->last_height);
	  glfwSetWindowMonitor(win, best_monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
	} else {
	  glfwSetWindowMonitor(win, nullptr, widget->last_xpos, widget->last_ypos, widget->last_width, widget->last_height, GLFW_DONT_CARE);
	}

      }
      break;
    case GLFW_KEY_ESCAPE:
    case GLFW_KEY_Q:
      {
	const GLFWmonitor* monitor = glfwGetWindowMonitor(win);
	if (!monitor) {
	  glfwIconifyWindow(win);
	} else {
	  glfwSetWindowMonitor(win, nullptr, widget->last_xpos, widget->last_ypos, widget->last_width, widget->last_height, GLFW_DONT_CARE);
	}
      }
      break;
    case GLFW_KEY_R:
      widget->requestPresetID = kPresetIDRandom;
      break;
    default:
      break;
    }
  }

  // Runs in renderThread. This function is responsible for the whole
  // lifetime of the rendering GL context and the projectM instance.
  void renderLoop() {
    if (!window) {
      std::lock_guard<std::mutex> l(pm_m);
      status = Status::FAILED;
      return;
    }
    glfwMakeContextCurrent(window);
    logContextInfo("Milkrack window", window);

    // Initialize projectM
    pm = new projectM(s);

    status = Status::RENDERING;

    renderSetAutoplay(false);
    renderLoopNextPreset();

    while (true) {
      {
	{
	  std::lock_guard<std::mutex> l(pm_m);
	  // Did the main thread request that we exit?
	  if (status == Status::PLEASE_EXIT) {
	    break;
	  }
	}

	// Resize?
	if (dirtySize) {
	  int x, y;
	  glfwGetFramebufferSize(window, &x, &y);
	  pm->projectM_resetGL(x, y);
	  dirtySize = false;
	}

	{
	  std::lock_guard<std::mutex> l(pm_m);
	  // Did the main thread request an autoplay toggle?
	  if (requestToggleAutoplay) {
	    renderSetAutoplay(!isAutoplayEnabled());
	    requestToggleAutoplay = false;
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
	}

	pm->renderFrame();
	glfwSwapBuffers(window);
      }
      usleep(1000000/fps);
    }

    delete pm;
    glfwDestroyWindow(window);
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
    w->toggleAutoplay();
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
    setPanel(SVG::load(assetPlugin(plugin, "res/MilkrackSeparateWindow.svg")));

    addInput(Port::create<PJ301MPort>(Vec(15, 60), Port::INPUT, module, MilkrackModule::LEFT_INPUT));
    addInput(Port::create<PJ301MPort>(Vec(15, 90), Port::INPUT, module, MilkrackModule::RIGHT_INPUT));

    addParam(ParamWidget::create<TL1105>(Vec(19, 150), module, MilkrackModule::NEXT_PRESET_PARAM, 0.0, 1.0, 0.0));

    std::shared_ptr<Font> font = Font::load(assetPlugin(plugin, "res/fonts/LiberationSans/LiberationSans-Regular.ttf"));
    w = ProjectMWidget::create(Vec(15, 120), assetPlugin(plugin, "presets/presets_projectM/"));
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
