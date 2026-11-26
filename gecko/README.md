# Output Browser on Firefox (test)

The approved design built on Firefox (Gecko) instead of CEF, to compare the
two. Nothing is compiled: a private copy of the installed Firefox loads our
frame from its own profile.

- `profile/chrome/output.uc.js` arranges Firefox's own parts into our row:
  back, forward, reload, the address with extensions at its end, the star,
  the tab button with its count, the menu and the window buttons. The tab
  strip folds down under the row from the tab button.
- `profile/chrome/userChrome.css` is the look: Box in a box, the colours and
  sizes of the mock-up.
- `profile/chrome/start.html` is the start page. On Firefox it keeps its own
  bookmarks.
- `autoconfig/` is Firefox's settings file that loads `output.uc.js` into
  each window. It goes into the private copy only.

Set up and run (Linux, with Firefox installed):

    bash gecko/install.sh            # into ~/Scratch/output-gecko
    ~/Scratch/output-gecko/run.sh
