#include "x11.h"

#include <X11/Xlib.h>
#include <X11/extensions/shape.h>

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace x11 {
namespace {
std::string g_last_error;
int OnError(Display* d, XErrorEvent* e) {
  char text[256] = {};
  XGetErrorText(d, e->error_code, text, sizeof text);
  g_last_error = std::string(text) + " (request " + std::to_string(e->request_code) + ")";
  fprintf(stderr, "[output-browser] X error: %s\n", g_last_error.c_str());
  return 0;
}
Display* D() {
  static Display* d = [] {
    Display* x = XOpenDisplay(nullptr);
    fprintf(stderr, "[output-browser] X display %s: %s\n", XDisplayName(nullptr),
            x ? "open" : "FAILED TO OPEN");
    if (x) XSetErrorHandler(OnError);
    return x;
  }();
  return d;
}
unsigned long ParentOf(unsigned long w) {
  ::Window root = 0, parent = 0, *kids = nullptr;
  unsigned int n = 0;
  if (!XQueryTree(D(), w, &root, &parent, &kids, &n)) return 0;
  if (kids) XFree(kids);
  return parent;
}
}  // namespace

void Place(unsigned long w, int x, int y, int width, int height) {
  if (!D() || !w || width <= 0 || height <= 0) return;
  XWindowChanges c{};
  c.x = x;
  c.y = y;
  c.width = width;
  c.height = height;
  XConfigureWindow(D(), w, CWX | CWY | CWWidth | CWHeight, &c);
  XFlush(D());
}

void Show(unsigned long w, bool show) {
  if (!D() || !w) return;
  if (show) XMapRaised(D(), w);
  else XUnmapWindow(D(), w);
  XFlush(D());
}

void Round(unsigned long w, int width, int height, int r) {
  if (!D() || !w) return;
  if (r <= 0 || width < 2 * r || height < 2 * r) {
    XShapeCombineMask(D(), w, ShapeBounding, 0, 0, None, ShapeSet);
    XFlush(D());
    return;
  }
  std::vector<XRectangle> rects;
  for (int y = 0; y < r; ++y) {
    const double dy = r - y - 0.5;
    const int in = static_cast<int>(std::lround(r - std::sqrt(r * r - dy * dy)));
    const short wd = static_cast<short>(width - 2 * in);
    rects.push_back({static_cast<short>(in), static_cast<short>(y), static_cast<unsigned short>(wd), 1});
    rects.push_back({static_cast<short>(in), static_cast<short>(height - 1 - y),
                     static_cast<unsigned short>(wd), 1});
  }
  rects.push_back({0, static_cast<short>(r), static_cast<unsigned short>(width),
                   static_cast<unsigned short>(height - 2 * r)});
  XShapeCombineRectangles(D(), w, ShapeBounding, 0, 0, rects.data(),
                          static_cast<int>(rects.size()), ShapeSet, Unsorted);
  XFlush(D());
}

bool EnsureParent(unsigned long w, unsigned long parent) {
  if (!D() || !w || !parent) return false;
  XSync(D(), False);
  if (ParentOf(w) == parent) return false;
  XReparentWindow(D(), w, parent, 0, 0);
  XSync(D(), False);
  return true;
}

std::string Describe(unsigned long w) {
  if (!D()) return "no X display";
  if (!w) return "no window";
  XSync(D(), False);
  XWindowAttributes a{};
  if (!XGetWindowAttributes(D(), w, &a)) return "window " + std::to_string(w) + " not readable";
  const char* map = a.map_state == IsViewable ? "viewable"
                    : a.map_state == IsUnviewable ? "mapped, parent unmapped" : "unmapped";
  return "window " + std::to_string(w) + " at " + std::to_string(a.x) + "," + std::to_string(a.y) +
         " size " + std::to_string(a.width) + "x" + std::to_string(a.height) + ", " + map +
         ", parent " + std::to_string(ParentOf(w)) + ", root " + std::to_string(a.root);
}
}  // namespace x11
