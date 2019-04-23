#include "glfwUtils.hpp"

// Routine for finding the monitor on which a window is currently
// located, based on a post by @Schmo on Stackoverflow (licensed under
// BY-CC-SA).
// https://stackoverflow.com/questions/21421074/how-to-create-a-full-screen-window-on-the-current-monitor-with-glfw

static int mini(int x, int y) {
  return x < y ? x : y;
}

static int maxi(int x, int y) {
  return x > y ? x : y;
}

GLFWmonitor* glfwWindowGetNearestMonitor(GLFWwindow *window) {
  int bestoverlap = 0;
  GLFWmonitor* bestmonitor = NULL;

  int wx, wy, ww, wh;
  glfwGetWindowPos(window, &wx, &wy);
  glfwGetWindowSize(window, &ww, &wh);
  int nmonitors;
  GLFWmonitor** monitors = glfwGetMonitors(&nmonitors);
  for (int i = 0; i < nmonitors; i++) {
    const GLFWvidmode* mode = glfwGetVideoMode(monitors[i]);
    int mx, my;
    glfwGetMonitorPos(monitors[i], &mx, &my);
    int mw = mode->width;
    int mh = mode->height;
    int overlap =
      maxi(0, mini(wx + ww, mx + mw) - maxi(wx, mx)) *
      maxi(0, mini(wy + wh, my + mh) - maxi(wy, my));
    if (bestoverlap < overlap) {
      bestoverlap = overlap;
      bestmonitor = monitors[i];
    }
  }
  return bestmonitor;
}
