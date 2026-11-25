#include "x11.h"

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <unistd.h>

#include <algorithm>
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

bool Size(unsigned long w, int* width, int* height) {
  if (!D() || !w) return false;
  XWindowAttributes a{};
  if (!XGetWindowAttributes(D(), w, &a)) return false;
  *width = a.width;
  *height = a.height;
  return true;
}

bool EnsureParent(unsigned long w, unsigned long parent) {
  if (!D() || !w || !parent) return false;
  XSync(D(), False);
  if (ParentOf(w) == parent) return false;
  XReparentWindow(D(), w, parent, 0, 0);
  XSync(D(), False);
  return true;
}

std::vector<unsigned long> StrayToplevels(const std::vector<unsigned long>& known) {
  std::vector<unsigned long> out;
  Display* d = D();
  if (!d) return out;
  const Atom pid_atom = XInternAtom(d, "_NET_WM_PID", False);
  ::Window root = 0, parent = 0, *kids = nullptr;
  unsigned int n = 0;
  if (!XQueryTree(d, DefaultRootWindow(d), &root, &parent, &kids, &n)) return out;
  for (unsigned int i = 0; i < n; ++i) {
    const ::Window w = kids[i];
    if (std::find(known.begin(), known.end(), w) != known.end()) continue;
    XWindowAttributes a;
    if (!XGetWindowAttributes(d, w, &a) || a.override_redirect || a.map_state != IsViewable) continue;
    ::Window owner = 0;
    if (XGetTransientForHint(d, w, &owner) && owner) continue;
    Atom type = 0;
    int format = 0;
    unsigned long count = 0, after = 0;
    unsigned char* data = nullptr;
    bool ours = false;
    if (XGetWindowProperty(d, w, pid_atom, 0, 1, False, XA_CARDINAL, &type, &format, &count,
                           &after, &data) == Success && data) {
      ours = count == 1 && format == 32 && *reinterpret_cast<unsigned long*>(data) ==
                                               static_cast<unsigned long>(getpid());
      XFree(data);
    }
    if (ours) out.push_back(w);
  }
  if (kids) XFree(kids);
  return out;
}

void MakeDialog(unsigned long w, unsigned long owner) {
  Display* d = D();
  if (!d) return;
  XSetTransientForHint(d, w, owner);
  const Atom type = XInternAtom(d, "_NET_WM_WINDOW_TYPE", False);
  const Atom dialog = XInternAtom(d, "_NET_WM_WINDOW_TYPE_DIALOG", False);
  XChangeProperty(d, w, type, XA_ATOM, 32, PropModeReplace,
                  reinterpret_cast<const unsigned char*>(&dialog), 1);
  XWindowAttributes ow, dw;
  if (XGetWindowAttributes(d, owner, &ow) && XGetWindowAttributes(d, w, &dw)) {
    int ox = 0, oy = 0;
    ::Window child = 0;
    XTranslateCoordinates(d, owner, DefaultRootWindow(d), 0, 0, &ox, &oy, &child);
    const int x = ox + (ow.width - dw.width) / 2, y = oy + std::max(0, (ow.height - dw.height) / 3);
    XMoveWindow(d, w, x, y);
  }
  XFlush(d);
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
