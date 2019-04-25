#define NANOVG_GL2
#include "window.hpp"

#include "Renderer.hpp"
#include "GLFW/glfw3.h"
#include "deps/projectm/src/libprojectM/projectM.hpp"
#include "glfwUtils.hpp"
#include "util/common.hpp"
#include <thread>
#include <mutex>

void ProjectMRenderer::init(projectM::Settings const& s) {
  window = createWindow();
  renderThread = std::thread([this, s](){ this->renderLoop(s); });
}

ProjectMRenderer::~ProjectMRenderer() {
  // Request that the render thread terminates the renderLoop
  setStatus(Status::PLEASE_EXIT);
  // Wait for renderLoop to terminate before releasing resources
  renderThread.join();
  // Destroy the window in the main thread, because it's not legal
  // to do so in other threads.
  glfwDestroyWindow(window);
}

void ProjectMRenderer::addPCMData(float* data, unsigned int nsamples) {
  std::lock_guard<std::mutex> l(pm_m);
  if (!pm) return;
  pm->pcm()->addPCMfloat_2ch(data, nsamples);
}

// Requests that projectM changes the preset at the next opportunity
void ProjectMRenderer::requestPresetID(int id) {
  std::lock_guard<std::mutex> l(flags_m);
  requestedPresetID = id;
}

// Requests that projectM changes the autoplay status
void ProjectMRenderer::requestToggleAutoplay() {
  std::lock_guard<std::mutex> l(flags_m);
  requestedToggleAutoplay = true;
}

// True if projectM is autoplaying presets
bool ProjectMRenderer::isAutoplayEnabled() const {
  std::lock_guard<std::mutex> l(pm_m);
  if (!pm) return false;
  return !pm->isPresetLocked();
}

// ID of the current preset in projectM's list
unsigned int ProjectMRenderer::activePreset() const {
  unsigned int presetIdx;
  std::lock_guard<std::mutex> l(pm_m);
  if (!pm) return 0;
  pm->selectedPresetIndex(presetIdx);
  return presetIdx;
}

// Name of the preset projectM is currently displaying
std::string ProjectMRenderer::activePresetName() const {
  unsigned int presetIdx;
  std::unique_lock<std::mutex> l(pm_m);
  if (!pm) return "";
  if (pm->selectedPresetIndex(presetIdx)) {
    l.unlock();
    return pm->getPresetName(presetIdx);
  }
  return "";
}

// Returns a list of all presets currently loaded by projectM
std::list<std::pair<unsigned int, std::string> > ProjectMRenderer::listPresets() const {
  std::list<std::pair<unsigned int, std::string> > presets;
  unsigned int n;
  {
    std::lock_guard<std::mutex> l(pm_m);
    if (!pm) return presets;
    n = pm->getPlaylistSize();
  }
  if (!n) {
    return presets;
  }
  for (unsigned int i = 0; i < n; ++i){
    std::string s;
    {
      std::lock_guard<std::mutex> l(pm_m);
      s = pm->getPresetName(i);
    }
    presets.push_back(std::make_pair(i, std::string(s)));
  }
  return presets;
}

bool ProjectMRenderer::isRendering() const {
  return getStatus() == Status::RENDERING;
}


int ProjectMRenderer::getClearRequestedPresetID() {
  std::lock_guard<std::mutex> l(flags_m);
  int r = requestedPresetID;
  requestedPresetID = kPresetIDKeep;
  return r;
}

bool ProjectMRenderer::getClearRequestedToggleAutoplay() {
  std::lock_guard<std::mutex> l(flags_m);
  bool r = requestedToggleAutoplay;;
  requestedToggleAutoplay = false;
  return r;
}

ProjectMRenderer::Status ProjectMRenderer::getStatus() const {
  std::lock_guard<std::mutex> l(flags_m);
  return status;
}

void ProjectMRenderer::setStatus(Status s) {
  std::lock_guard<std::mutex> l(flags_m);
  status = s;
}

void ProjectMRenderer::renderSetAutoplay(bool enable) {
  std::lock_guard<std::mutex> l(pm_m);
  pm->setPresetLock(!enable);
}

// Switch to the next preset. This should be called only from the
// render thread.
void ProjectMRenderer::renderLoopNextPreset() {
  std::lock_guard<std::mutex> l(pm_m);
  unsigned int n = pm->getPlaylistSize();
  if (n) {
    pm->selectPreset(rand() % n);
  }
}

// Switch to the indicated preset. This should be called only from
// the render thread.
void ProjectMRenderer::renderLoopSetPreset(unsigned int i) {
  std::lock_guard<std::mutex> l(pm_m);
  unsigned int n = pm->getPlaylistSize();
  if (n && i < n) {
    pm->selectPreset(i);
  }
}

void ProjectMRenderer::renderLoop(projectM::Settings s) {
  if (!window) {
    setStatus(Status::FAILED);
    return;
  }
  glfwMakeContextCurrent(window);
  logContextInfo("Milkrack window", window);
  
  // Initialize projectM
  {
    std::lock_guard<std::mutex> l(pm_m);
    pm = new projectM(s);
    extraProjectMInitialization();
  }
  
  setStatus(Status::RENDERING);
  renderSetAutoplay(false);
  renderLoopNextPreset();
  
  while (true) {
    {
      // Did the main thread request that we exit?
      if (getStatus() == Status::PLEASE_EXIT) {
	break;
      }
      
      // Resize?
      if (dirtySize) {
	int x, y;
	glfwGetFramebufferSize(window, &x, &y);
	pm->projectM_resetGL(x, y);
	dirtySize = false;
      }
      
      {
	// Did the main thread request an autoplay toggle?
	if (getClearRequestedToggleAutoplay()) {
	  renderSetAutoplay(!isAutoplayEnabled());
	}
	
	// Did the main thread request that we change the preset?
	int rpid = getClearRequestedPresetID();
	if (rpid != kPresetIDKeep) {
	  if (rpid == kPresetIDRandom) {
	    renderLoopNextPreset();
	  } else {
	    renderLoopSetPreset(rpid);
	  }
	}
      }
      
      {
	std::lock_guard<std::mutex> l(pm_m);
	pm->renderFrame();
      }
      glfwSwapBuffers(window);
    }
    usleep(1000000/60); // TODO fps
  }
  
  delete pm;
  glFinish(); // Finish any pending OpenGL operations
  setStatus(Status::EXITING);
}

void ProjectMRenderer::logContextInfo(std::string name, GLFWwindow* w) const {
  int major = glfwGetWindowAttrib(w, GLFW_CONTEXT_VERSION_MAJOR);
  int minor = glfwGetWindowAttrib(w, GLFW_CONTEXT_VERSION_MINOR);
  int revision = glfwGetWindowAttrib(w, GLFW_CONTEXT_REVISION);
  int api = glfwGetWindowAttrib(w, GLFW_CLIENT_API);
  rack::loggerLog(rack::DEBUG_LEVEL, "Milkrack/" __FILE__, __LINE__, "%s context using API %d version %d.%d.%d, profile %d", name.c_str(), api, major, minor, revision);
}

void ProjectMRenderer::logGLFWError(int errcode, const char* errmsg) {
  rack::loggerLog(rack::WARN_LEVEL, "Milkrack/" __FILE__, 0, "GLFW error %d: %s", errcode, errmsg);
}

GLFWwindow* WindowedRenderer::createWindow() {
  glfwSetErrorCallback(logGLFWError);
  glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);
  glfwWindowHint(GLFW_MAXIMIZED, GLFW_FALSE);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
  GLFWwindow* c = glfwCreateWindow(360, 360, "", NULL, NULL);  
  if (!c) {
    rack::loggerLog(rack::DEBUG_LEVEL, "Milkrack/" __FILE__, __LINE__, "Milkrack renderLoop could not create a context, bailing.");
    return nullptr;
  }
  glfwSetWindowUserPointer(c, reinterpret_cast<void*>(this));
  glfwSetFramebufferSizeCallback(c, framebufferSizeCallback);
  glfwSetWindowCloseCallback(c, [](GLFWwindow* w) { glfwIconifyWindow(w); });
  glfwSetKeyCallback(c, keyCallback);
  glfwSetWindowTitle(c, u8"Milkrack");
  return c;
}

void WindowedRenderer::framebufferSizeCallback(GLFWwindow* win, int x, int y) {
  WindowedRenderer* r = reinterpret_cast<WindowedRenderer*>(glfwGetWindowUserPointer(win));
  r->dirtySize = true;
}


void WindowedRenderer::keyCallback(GLFWwindow* win, int key, int scancode, int action, int mods) {
  WindowedRenderer* r = reinterpret_cast<WindowedRenderer*>(glfwGetWindowUserPointer(win));
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
	glfwGetWindowPos(win, &r->last_xpos, &r->last_ypos);
	glfwGetWindowSize(win, &r->last_width, &r->last_height);
	glfwSetWindowMonitor(win, best_monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
      } else {
	glfwSetWindowMonitor(win, nullptr, r->last_xpos, r->last_ypos, r->last_width, r->last_height, GLFW_DONT_CARE);
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
	glfwSetWindowMonitor(win, nullptr, r->last_xpos, r->last_ypos, r->last_width, r->last_height, GLFW_DONT_CARE);
      }
    }
    break;
  case GLFW_KEY_R:
    r->requestPresetID(kPresetIDRandom);
    break;
  default:
    break;
  }
}

GLFWwindow* TextureRenderer::createWindow() {
  glfwSetErrorCallback(logGLFWError);
  logContextInfo("gWindow", rack::gWindow);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
  glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
  GLFWwindow* c = glfwCreateWindow(360, 360, "", NULL, rack::gWindow);
  if (!c) {
    rack::loggerLog(rack::DEBUG_LEVEL, "Milkrack/" __FILE__, __LINE__, "Milkrack renderLoop could not create a context, bailing.");
    return nullptr;
  }
  logContextInfo("Milkrack context", c);
  return c;
}


void TextureRenderer::extraProjectMInitialization() {
  texture = pm->initRenderToTexture();
}

int TextureRenderer::getTextureID() const {
  return texture;
}
