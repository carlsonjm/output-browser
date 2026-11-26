// Output Browser on Firefox. (Firefox skips this file's first line.)
// Loads output.uc.js from the profile's chrome folder into every browser
// window, as soon as the window's document exists.
try {
  const { classes: Cc, interfaces: Ci, utils: Cu } = Components;
  const svc = (c, i) => Cc[c].getService(i);
  const obs = svc("@mozilla.org/observer-service;1", Ci.nsIObserverService);
  obs.addObserver({
    observe(win) {
      win.addEventListener("DOMContentLoaded", () => {
        if (win.location.href !== "chrome://browser/content/browser.xhtml") return;
        try {
          // Firefox only loads code into its windows from its own kinds of
          // address, so the profile's chrome folder is given one:
          // resource://outputbrowser/.
          const dir = svc("@mozilla.org/file/directory_service;1", Ci.nsIProperties)
                        .get("UChrm", Ci.nsIFile);
          const io = svc("@mozilla.org/network/io-service;1", Ci.nsIIOService);
          const res = io.getProtocolHandler("resource").QueryInterface(Ci.nsIResProtocolHandler);
          if (!res.hasSubstitution("outputbrowser"))
            res.setSubstitution("outputbrowser", io.newFileURI(dir));
          const loader = svc("@mozilla.org/moz/jssubscript-loader;1", Ci.mozIJSSubScriptLoader);
          loader.loadSubScript("resource://outputbrowser/output.uc.js", win);
        } catch (e) { Cu.reportError(e); }
      }, { once: true });
    }
  }, "chrome-document-global-created");
} catch (e) { Components.utils.reportError(e); }
