# Output Browser

Output Browser is a web browser for the Shuffle desktop, built on the
[Chromium Embedded Framework](https://github.com/chromiumembedded/cef) (CEF).
The browser draws its own window, top row, tab strip and extension pop-overs;
Chromium draws the web pages.

## How it is put together

- `src/main.cc` is the browser. Each web page is a CEF Chrome-style browser
  with Chrome's own toolbar switched off.
- `ui/bar.html` is the top row and the tab strip that folds down under it.
  `ui/menu.html` is the menu. Both are drawn in small Alloy-style browsers and
  send their commands to `main.cc` as console lines that start with `ob:`.
- `ui/start.html` is the page a new tab opens on.

CEF allows one Chrome-style browser per window, and only while the window
holds no other browser. So each tab is created in a hidden window of its own,
and the tab that is showing is moved into the main window.

## Building the browser

```
CEF=/path/to/cef_binary_... bash tools/build.sh out
out/output-browser
```

`CEF` is a CEF binary distribution (the "minimal" one is enough). The browser
keeps its profile in `~/.local/share/output-browser`; `--profile-dir=` changes
that, `--url=` opens a page instead of the start page, and `--square-corners`
leaves the page corners square.

## Building CEF

`tools/build-cef.sh` builds CEF from source with Widevine and the H.264/AAC
media formats switched on. It needs about 100 GB of free disk, `git`,
`python3` and `curl`, and takes several hours.

```
bash tools/build-cef.sh 8078
```

The argument is the Chromium branch: the third part of a Chromium version
(8078 for 156.0.8078.109).

## Licence

GPL-2.0-or-later; see `LICENSE`. CEF and Chromium carry their own licences.
