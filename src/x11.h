// Small X11 helpers for the embedded page and menu windows. Kept apart from
// the CEF headers, which clash with Xlib's macros.
#pragma once

#include <string>

namespace x11 {
// Moves and sizes a child window, in pixels relative to its parent.
void Place(unsigned long w, int x, int y, int width, int height);
// Maps (and raises) or unmaps a window.
void Show(unsigned long w, bool show);
// Gives the window round corners of |r| pixels, or clears the shape when r <= 0.
void Round(unsigned long w, int width, int height, int r);
// Makes sure |w| is a child of |parent|, reparenting it if it is not.
// Returns true when it had to be moved.
bool EnsureParent(unsigned long w, unsigned long parent);
// One line about a window: position, size, map state and parent, for the log.
std::string Describe(unsigned long w);
}  // namespace x11
