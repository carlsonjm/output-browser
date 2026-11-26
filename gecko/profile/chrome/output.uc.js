// Output Browser on Firefox: the frame from the approved mock-up, built from
// Firefox's own parts. Runs in each browser window (loaded by config.js).
//
// The row: back, forward, reload, the address with extensions at its right
// end, the star, the tab button with its count, the menu, and the window
// buttons. The tab strip folds down under the row when the tab button is
// pressed. The page sits in a rounded box on the window's own frame.
(function () {
  "use strict";
  const doc = document;
  const log = m => console.log("[output-browser] " + m);

  // Our start page: start.html next to this file.
  const chromeDir = Services.dirsvc.get("UChrm", Ci.nsIFile);
  const startFile = chromeDir.clone();
  startFile.append("start.html");
  const START = Services.io.newFileURI(startFile).spec;

  function onReady(fn) {
    if (window.gBrowserInit && gBrowserInit.delayedStartupFinished) { fn(); return; }
    const o = (subject) => {
      if (subject !== window) return;
      Services.obs.removeObserver(o, "browser-delayed-startup-finished");
      fn();
    };
    Services.obs.addObserver(o, "browser-delayed-startup-finished");
  }

  // ---- the row
  function buildRow() {
    const nav = doc.getElementById("nav-bar");
    const target = doc.getElementById("nav-bar-customization-target");
    if (!nav || !target) { log("no nav-bar"); return; }

    // Back, forward, reload and the address, then any extensions shown in
    // the row and the extensions button, so they sit at the address's end.
    try {
      const exts = CustomizableUI.getWidgetIdsInArea("nav-bar").filter(id => id.endsWith("-browser-action"));
      const want = ["back-button", "forward-button", "stop-reload-button", "urlbar-container",
                    ...exts, "unified-extensions-button"];
      for (const id of CustomizableUI.getWidgetIdsInArea("nav-bar")) {
        if (want.includes(id)) continue;
        try { CustomizableUI.removeWidgetFromArea(id); } catch (e) {}
      }
      want.forEach((id, i) => {
        try {
          const p = CustomizableUI.getPlacementOfWidget(id);
          if (!p || p.area !== "nav-bar") CustomizableUI.addWidgetToArea(id, "nav-bar", i);
          else if (p.position !== i) CustomizableUI.moveWidgetWithinArea(id, i);
        } catch (e) { log("place " + id + ": " + e); }
      });
    } catch (e) { log("placements: " + e); }

    const menu = doc.getElementById("PanelUI-button");

    // The star moves out of the address, to after it, as in the mock-up.
    const star = doc.getElementById("star-button-box");
    if (star && menu) {
      star.classList.add("ob-star");
      nav.insertBefore(star, menu);
    }

    // The tab button, with the number of tabs.
    if (!doc.getElementById("ob-tabs")) {
      const b = doc.createXULElement("toolbarbutton");
      b.id = "ob-tabs";
      b.className = "toolbarbutton-1 chromeclass-toolbar-additional";
      b.setAttribute("tooltiptext", "Tabs");
      const icon = doc.createElement("div");
      icon.className = "ob-stack";
      icon.append(doc.createElement("i"), doc.createElement("i"), doc.createElement("i"));
      const count = doc.createXULElement("label");
      count.id = "ob-count";
      b.append(icon, count);
      b.addEventListener("command", () => setFolded(!folded()));
      nav.insertBefore(b, menu);
    }

    // The window buttons go at the end of the row (or the start, if the
    // desktop puts them on the left; see userChrome.css).
    let box = nav.querySelector(":scope > .titlebar-buttonbox-container");
    if (!box) {
      box = doc.querySelector("#TabsToolbar .titlebar-buttonbox-container");
      if (box) nav.appendChild(box);
    }
    if (box) box.classList.add("ob-wins");
    log("row ready");
  }

  // An extension shown in the row later still goes at the address's end.
  function keepExtensionsInPlace() {
    CustomizableUI.addListener({
      onWidgetAdded(id, area) {
        if (area !== "nav-bar" || !id.endsWith("-browser-action")) return;
        const u = CustomizableUI.getPlacementOfWidget("unified-extensions-button");
        const p = CustomizableUI.getPlacementOfWidget(id);
        if (u && p && u.area === "nav-bar" && p.position > u.position)
          CustomizableUI.moveWidgetWithinArea(id, u.position);
      },
    });
  }

  // ---- the tab strip
  const toolbox = () => doc.getElementById("navigator-toolbox");
  const folded = () => toolbox().hasAttribute("ob-folded");
  function setFolded(on) {
    toolbox().toggleAttribute("ob-folded", on);
    doc.getElementById("ob-tabs")?.toggleAttribute("checked", on);
  }
  function updateCount() {
    const n = gBrowser.visibleTabs ? gBrowser.visibleTabs.length : gBrowser.tabs.length;
    const c = doc.getElementById("ob-count");
    if (c) c.setAttribute("value", String(n));
  }

  // ---- the menu: only the rows the mock-up has, in its groups.
  function trimMenu() {
    const hide = [
      "appMenu-fxa-status2", "appMenu-fxa-separator", "appMenu-new-window-button2",
      "appMenu-passwords-button", "appMenu-extensions-themes-button", "appMenu-print-button",
      "appMenu-save-file-button", "appMenu-translate-button", "appMenu-more-button2",
      "appMenu-report-broken-site-button", "appMenu-help-button2", "appMenu-quit-button2",
      "appMenu-restoreSession", "appMenu-profiles-button", "appMenu-proton-update-banner",
    ];
    const view = doc.getElementById("appMenu-mainView") ||
                 doc.querySelector("#appMenu-popup panelview, template#appMenu-viewCache");
    const apply = () => {
      for (const id of hide) doc.getElementById(id)?.setAttribute("hidden", "true");
      const rename = { "appMenu-settings-button": "Browser settings",
                       "appMenu-bookmarks-button": "Bookmarks and pinned sites" };
      for (const [id, label] of Object.entries(rename))
        doc.getElementById(id)?.setAttribute("label", label);
    };
    // The menu is built the first time it opens.
    doc.getElementById("appMenu-popup")?.addEventListener("popupshowing", apply);
    PanelUI?.mainView?.addEventListener?.("ViewShowing", apply);
    if (view) apply();
  }

  // ---- start page and search
  async function startPageAndSearch() {
    try {
      const { AboutNewTab } = ChromeUtils.importESModule("resource:///modules/AboutNewTab.sys.mjs");
      AboutNewTab.newTabURL = START;
    } catch (e) { log("new tab page: " + e); }
    // Our start page shows an empty address, as a new tab should.
    if (window.gInitialPages && !gInitialPages.includes(START)) gInitialPages.push(START);
    try {
      await Services.search.init();
      const ddg = (await Services.search.getVisibleEngines()).find(e => /duckduckgo/i.test(e.name));
      if (ddg && (await Services.search.getDefault())?.name !== ddg.name)
        await Services.search.setDefault(ddg, Ci.nsISearchService.CHANGE_REASON_USER);
    } catch (e) { log("search engine: " + e); }
  }

  onReady(() => {
    try { buildRow(); } catch (e) { log("row: " + e); }
    try { keepExtensionsInPlace(); } catch (e) { log("extensions: " + e); }
    try { trimMenu(); } catch (e) { log("menu: " + e); }
    startPageAndSearch();
    updateCount();
    for (const ev of ["TabOpen", "TabClose", "TabShow", "TabHide"])
      gBrowser.tabContainer.addEventListener(ev, () => setTimeout(updateCount, 0));
    // Ctrl+Shift+Space also folds the strip down and up.
    window.addEventListener("keydown", e => {
      if (e.ctrlKey && e.shiftKey && e.code === "Space") { setFolded(!folded()); e.preventDefault(); }
    }, true);
    doc.documentElement.setAttribute("output-browser", "true");
    log("ready");
  });
})();
