// Small X11 helpers for the embedded page and menu windows. Kept apart from
// the CEF headers, which clash with Xlib's macros.
#pragma once

namespace x11 {
// Moves and sizes a child window, in pixels relative to its parent.
void Place(unsigned long w, int x, int y, int width, int height);
// Maps (and raises) or unmaps a window.
void Show(unsigned long w, bool show);
// Gives the window round corners of |r| pixels, or clears the shape when r <= 0.
void Round(unsigned long w, int width, int height, int r);
}  // namespace x11
