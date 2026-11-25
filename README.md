# Output Browser

Output Browser is a web browser for the Shuffle desktop, built on the
[Chromium Embedded Framework](https://github.com/chromiumembedded/cef) (CEF).
The browser draws its own window, top row, tab strip and extension pop-overs;
Chromium draws the web pages.

The repository is at an early stage: it holds the CEF build tool and no
browser yet.

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
