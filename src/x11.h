// Small X11 helpers for the embedded page and menu windows. Kept apart from
// the CEF headers, which clash with Xlib's macros.
#pragma once

#include <string>
#include <vector>

namespace x11 {
// Moves and sizes a child window, in pixels relative to its parent.
void Place(unsigned long w, int x, int y, int width, int height);
// Maps (and raises) or unmaps a window.
void Show(unsigned long w, bool show);
// Gives the window round corners of |r| pixels, or clears the shape when r <= 0.
void Round(unsigned long w, int width, int height, int r);
// The size of a window in pixels. Returns false if it cannot be read.
bool Size(unsigned long w, int* width, int* height);
// Makes sure |w| is a child of |parent|, reparenting it if it is not.
// Returns true when it had to be moved.
bool EnsureParent(unsigned long w, unsigned long parent);
// Shown top-level windows of this process that have no owner and are not
// in |known|, such as the confirmation Chrome opens before installing an
// extension. Menus and tooltips are left out.
std::vector<unsigned long> StrayToplevels(const std::vector<unsigned long>& known);
// Makes |w| a dialog belonging to |owner|, centred over it.
void MakeDialog(unsigned long w, unsigned long owner);
// One line about a window: position, size, map state and parent, for the log.
std::string Describe(unsigned long w);
}  // namespace x11
