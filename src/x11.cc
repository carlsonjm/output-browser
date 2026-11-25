#include "x11.h"

#include <X11/Xlib.h>
#include <X11/extensions/shape.h>

#include <cmath>
#include <vector>

namespace x11 {
namespace {
Display* D() {
  static Display* d = XOpenDisplay(nullptr);
  return d;
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
}  // namespace x11
