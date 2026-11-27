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

    // Extensions shown in the row go to the tab strip's right end first, so
    // the clean-up below doesn't unpin them. Then the row keeps back, forward,
    // reload and the address, and nothing else Firefox put there.
    try {
      const isExt = id => id.endsWith("-browser-action");
      for (const id of CustomizableUI.getWidgetIdsInArea("nav-bar").filter(isExt))
        CustomizableUI.addWidgetToArea(id, "TabsToolbar");
      const want = ["back-button", "forward-button", "stop-reload-button", "urlbar-container"];
      for (const id of CustomizableUI.getWidgetIdsInArea("nav-bar")) {
        if (want.includes(id) || id === "unified-extensions-button") continue;
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
    placePuzzle();

    const menu = doc.getElementById("PanelUI-button");

    // The star moves out of the address, to after it, as in the mock-up.
    // Firefox may add it to the address a moment later, so look again.
    const moveStar = (tries) => {
      const btn = doc.getElementById("star-button");
      const star = doc.getElementById("star-button-box") || btn?.parentNode;
      if (star && star.parentNode !== nav) {
        star.classList.add("ob-star");
        star.removeAttribute("hidden");
        nav.insertBefore(star, menu);
        log("star moved");
      } else if (!star && tries > 0) {
        setTimeout(() => moveStar(tries - 1), 500);
      } else if (!star) {
        log("no star found");
      }
    };
    moveStar(10);

    // The tab button: the logo, at the left end of the row.
    if (!doc.getElementById("ob-tabs")) {
      const b = doc.createXULElement("toolbarbutton");
      b.id = "ob-tabs";
      b.className = "toolbarbutton-1 chromeclass-toolbar-additional";
      b.setAttribute("tooltiptext", "Tabs");
      const NS = "http://www.w3.org/2000/svg";
      const el = (tag, attrs, parent) => {
        const e = doc.createElementNS(NS, tag);
        for (const k in attrs) e.setAttribute(k, attrs[k]);
        if (parent) parent.appendChild(e);
        return e;
      };
      // At rest: the blue card low left, the red card high right; where they
      // overlap, a pale shape with the O's opening in it.
      const svg = el("svg", { viewBox: "0 0 24 24", class: "ob-logo" });
      const defs = el("defs", {}, svg);
      const clip = el("clipPath", { id: "ob-clip-blue" }, defs);
      const BLUE = { x: 1, y: 9.5, width: 14, height: 11, rx: 3.5 };
      const RED = { x: 8, y: 4, width: 14, height: 11, rx: 3.5 };
      el("rect", BLUE, clip);
      el("rect", RED, el("g", { class: "ob-r" }, svg));
      const blue = el("g", { class: "ob-b" }, svg);
      // Stacked, a ring in the window's colour keeps an even gap between the
      // front card's edge and the card behind it.
      el("rect", Object.assign({ class: "ob-halo" }, BLUE), blue);
      el("rect", BLUE, blue);
      const mid = el("g", { class: "ob-mid" }, svg);
      el("rect", Object.assign({}, RED, { fill: "#f3bde9", "clip-path": "url(#ob-clip-blue)" }), mid);
      el("circle", { cx: 11.5, cy: 12.25, r: 2.1, fill: "#1c1c1c" }, mid);
      // Folded out: the cards stack and the front one holds the count.
      const n = el("text", { class: "ob-n", id: "ob-count", x: 8, y: 15, "text-anchor": "middle",
                             "dominant-baseline": "central" }, svg);
      n.textContent = "1";
      b.append(svg);
      b.addEventListener("command", () => setFolded(!folded()));
      nav.insertBefore(b, nav.firstChild);
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

  // The puzzle (every extension) goes last in the tab strip, after the
  // extensions shown there.
  function placePuzzle() {
    const puzzle = doc.getElementById("unified-extensions-button");
    const strip = doc.getElementById("TabsToolbar");
    if (!puzzle || !strip) { log("no puzzle or strip"); return; }
    if (puzzle.parentNode !== strip || strip.lastElementChild !== puzzle) strip.appendChild(puzzle);
    puzzle.removeAttribute("hidden");
    // Firefox opens the add-ons page instead of the list when every extension
    // is shown outside it. The puzzle always opens the list, as in the mock-up.
    const ue = window.gUnifiedExtensions;
    if (ue && typeof ue.hasExtensionsInPanel === "function" && !ue.obAlwaysPanel) {
      ue.obAlwaysPanel = true;
      ue.hasExtensionsInPanel = () => true;
      log("puzzle always opens the list");
    } else if (!ue || typeof ue.hasExtensionsInPanel !== "function") {
      log("puzzle: no hasExtensionsInPanel (" + (ue ? Object.keys(Object.getPrototypeOf(ue) || ue).join(",") : "none") + ")");
    }
    log("extensions in the strip: " +
        CustomizableUI.getWidgetIdsInArea("TabsToolbar").filter(id => id.endsWith("-browser-action")).length);
  }

  // For the self-check: where things in the strip really are.
  const box = e => { const r = e.getBoundingClientRect(); return Math.round(r.x) + "," + Math.round(r.y) + " " + Math.round(r.width) + "x" + Math.round(r.height); };
  function describeStrip() {
    const tabs = [...doc.querySelectorAll(".tabbrowser-tab")].map(t =>
      (t.closing || t.hasAttribute("closing") ? "closing:" : "") + (t.hasAttribute("fadein") ? "" : "nofadein:") +
      box(t) + (t.style.maxWidth ? " mw=" + t.style.maxWidth : "") + " cmw=" + getComputedStyle(t).maxWidth);
    const plus = doc.getElementById("tabs-newtab-button");
    const spacer = doc.querySelector(".closing-tabs-spacer");
    return "tabs [" + tabs.join(" | ") + "] plus " + (plus ? box(plus) : "none") +
           (spacer ? " spacer " + spacer.style.width : "");
  }
  function describeExtension() {
    const item = doc.querySelector("#TabsToolbar .unified-extensions-item");
    if (!item) { log("extension layers: none in the strip"); return; }
    const out = [];
    const walk = (e, d) => {
      if (d > 4) return;
      const cs = getComputedStyle(e);
      if (cs.display !== "none")
        out.push(e.localName + (e.className ? "." + String(e.className).split(" ").join(".") : "") + " " + box(e) +
                 " pad " + cs.padding + " mar " + cs.margin + " pos " + cs.position);
      for (const c of e.children) walk(c, d + 1);
    };
    walk(item, 0);
    log("extension layers: " + out.join(" || "));
  }
  window.addEventListener("keydown", e => {
    if (e.ctrlKey && e.shiftKey && e.code === "KeyL") { describeExtension(); log("strip: " + describeStrip()); e.preventDefault(); }
  }, true);

  // An extension shown later (a new install, or Show in the list) goes to the
  // strip too, never the address.
  function keepExtensionsInPlace() {
    CustomizableUI.addListener({
      onWidgetAdded(id, area) {
        if (area !== "nav-bar" || !id.endsWith("-browser-action")) return;
        setTimeout(() => {
          try { CustomizableUI.addWidgetToArea(id, "TabsToolbar"); placePuzzle(); }
          catch (e) { log("move " + id + ": " + e); }
        }, 0);
      },
    });
  }

  // The address's placeholder, as in the mock-up.
  // Firefox's translations set it again whenever the search engine changes,
  // so ours is put back each time.
  function placeholder() {
    const input = doc.getElementById("urlbar-input") || gURLBar?.inputField;
    if (!input) return;
    const TEXT = "Search or type an address";
    const put = () => {
      if (input.hasAttribute("data-l10n-id")) input.removeAttribute("data-l10n-id");
      if (input.getAttribute("placeholder") !== TEXT) input.setAttribute("placeholder", TEXT);
    };
    put();
    new MutationObserver(put).observe(input, { attributes: true, attributeFilter: ["placeholder", "data-l10n-id"] });
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
    if (c) { c.textContent = n > 99 ? "99+" : String(n); c.style.fontSize = n > 9 ? "6.5px" : ""; }
    doc.getElementById("ob-tabs")?.setAttribute("tooltiptext", n === 1 ? "1 tab" : n + " tabs");
  }

  // ---- the menu: only the rows the mock-up has, in its groups:
  // open something new, read the page, find things again, settings.
  const MENU = [
    [/new-tab/, "New tab"],
    [/private-window/, "New private window"],
    "-",
    [/find/, "Find on page"],
    [/zoom/, null],
    "-",
    [/history/, "History"],
    [/bookmarks/, "Bookmarks and pinned sites"],
    [/downloads/, "Downloads"],
    "-",
    [/settings|preferences/, "Browser settings"],
  ];
  let menuLogged = false, menuShownLogged = false;
  function shapeMenu() {
    const view = doc.getElementById("appMenu-mainView");
    const body = view?.querySelector(".panel-subview-body") || view;
    if (!body) return;
    const items = [...body.children];
    if (!menuLogged) { menuLogged = true; log("menu rows: " + items.map(e => e.id || e.localName).join(" ")); }
    const used = new Set();
    let last = null, group = false;
    for (const row of MENU) {
      if (row === "-") { group = true; continue; }
      const el = items.find(e => !used.has(e) && e.id && row[0].test(e.id) &&
                                 e.localName !== "toolbarseparator");
      if (!el) continue;
      if (row[1]) { el.removeAttribute("data-l10n-id"); el.setAttribute("label", row[1]); }
      el.toggleAttribute("ob-group", group && last !== null);
      group = false;
      used.add(el);
      el.removeAttribute("hidden");
      body.insertBefore(el, last ? last.nextSibling : body.firstChild);
      last = el;
    }
    for (const e of items) if (!used.has(e)) e.setAttribute("hidden", "true");
    // The account and sync banner, wherever it is.
    for (const e of (view?.querySelectorAll("[id*='fxa'], [id*='sync'], .panel-banner-item") || []))
      if (!used.has(e)) e.setAttribute("hidden", "true");
    // For the self-check: anything still drawn in the menu, and its lines.
    if (!menuShownLogged) {
      menuShownLogged = true;
      setTimeout(() => {
        const shown = [...view.querySelectorAll("*")].filter(e => e.getBoundingClientRect().height > 0 &&
          e.parentNode === body);
        log("menu shown: " + shown.map(e => (e.id || e.localName) + (e.hasAttribute("ob-group") ? "^" : "")).join(" "));
      }, 400);
    }
  }
  function trimMenu() {
    doc.addEventListener("ViewShowing", e => { if (e.target.id === "appMenu-mainView") shapeMenu(); }, true);
    doc.addEventListener("popupshowing", e => { if (e.target.id === "appMenu-popup") shapeMenu(); }, true);
  }

  // ---- a tab's hold menu: touch and hold a tab, or right-click it. Only the
  // rows for organising the tab, with Add to group first, as in the mock-up.
  const TAB_MENU = [
    /Group/i,
    "-",
    /pinTab|unpinTab/, /moveTabOptions|moveToNewWindow|openTabInWindow/, /duplicateTab/,
    "-",
    /reloadTab$/, /toggleMuteTab/,
    "-",
    /closeTab$/, /closeTabOptions/,
  ];
  let tabMenuLogged = false;
  function shapeTabMenu(menu) {
    const items = [...menu.children];
    if (!tabMenuLogged) { tabMenuLogged = true; log("tab menu rows: " + items.map(e => e.id || e.localName).join(" ")); }
    const used = new Set();
    let last = null, sepNext = false;
    for (const row of TAB_MENU) {
      if (row === "-") { sepNext = last !== null; continue; }
      for (const el of items.filter(e => !used.has(e) && e.id && row.test(e.id) && e.localName !== "menuseparator")) {
        if (sepNext) {
          const sep = items.find(e => e.localName === "menuseparator" && !used.has(e));
          if (sep) { used.add(sep); sep.removeAttribute("hidden"); menu.insertBefore(sep, last.nextSibling); last = sep; }
          sepNext = false;
        }
        used.add(el);
        menu.insertBefore(el, last ? last.nextSibling : menu.firstChild);
        last = el;
      }
    }
    // Firefox shows or hides each row for the tab at hand; ours only hides the
    // rows that aren't in the list.
    for (const e of items) if (!used.has(e)) e.setAttribute("hidden", "true");
  }
  function holdTabs() {
    const menu = doc.getElementById("tabContextMenu");
    if (menu) menu.addEventListener("popupshowing", e => { if (e.target === menu) setTimeout(() => shapeTabMenu(menu), 0); });
    // By touch (pointer events, which reach the strip whether or not the
    // system sends touch events): hold a tab for its menu, or pull it down
    // out of the strip to make it its own window, as the mouse does.
    const strip = gBrowser.tabContainer;
    let hold = null;
    strip.addEventListener("pointerdown", e => {
      if (e.pointerType !== "touch") return;
      const tab = e.target.closest?.(".tabbrowser-tab");
      if (!tab) return;
      hold = { tab, x: e.screenX, y: e.screenY, id: e.pointerId, moved: false,
               bottom: doc.getElementById("TabsToolbar").getBoundingClientRect().bottom,
               timer: setTimeout(() => {
                 if (!hold || hold.moved) return;
                 openHoldMenu(hold.tab, hold.x, hold.y, "pointer");
                 hold = null;
               }, 500) };
    }, true);
    window.addEventListener("pointermove", e => {
      if (!hold || e.pointerId !== hold.id) return;
      const dx = e.screenX - hold.x, dy = e.screenY - hold.y;
      if (Math.hypot(dx, dy) > 10) { hold.moved = true; clearTimeout(hold.timer); }
      // Pulled down past the strip's bottom edge by a finger's width.
      if (e.clientY > hold.bottom + 40 && dy > 40) {
        const tab = hold.tab; hold = null;
        pullOut(tab, "pointer");
      }
    }, true);
    const end = e => { if (hold && e.pointerId === hold.id) { clearTimeout(hold.timer); hold = null; } };
    window.addEventListener("pointerup", end, true);
    window.addEventListener("pointercancel", end, true);

    // The touch-event path that J confirmed opens the menu, kept alongside;
    // whichever fires first opens it, once.
    let timer = null, start = null;
    strip.addEventListener("touchstart", e => {
      const tab = e.target.closest?.(".tabbrowser-tab");
      if (!tab || e.touches.length !== 1) return;
      const t = e.touches[0];
      start = { x: t.screenX, y: t.screenY, tab, bottom: doc.getElementById("TabsToolbar").getBoundingClientRect().bottom };
      timer = setTimeout(() => { timer = null; openHoldMenu(tab, start.x, start.y, "touch"); }, 500);
    }, { passive: true });
    window.addEventListener("touchmove", e => {
      const t = e.touches?.[0];
      if (!start?.tab || !t) return;
      if (t.clientY > start.bottom + 40 && t.screenY - start.y > 40) {
        const tab = start.tab; start.tab = null;
        pullOut(tab, "touch");
      }
    }, { passive: true, capture: true });
    window.addEventListener("touchend", () => { if (start) start.tab = null; }, { passive: true, capture: true });
    const cancel = e => {
      if (!timer) return;
      const t = e.touches?.[0];
      if (e.type === "touchmove" && t && Math.hypot(t.screenX - start.x, t.screenY - start.y) < 10) return;
      clearTimeout(timer); timer = null;
    };
    for (const ev of ["touchmove", "touchend", "touchcancel"]) strip.addEventListener(ev, cancel, { passive: true });

    let pulledAt = 0;
    function pullOut(tab, how) {
      if (Date.now() - pulledAt < 800 || !tab.isConnected) return;
      pulledAt = Date.now();
      log("touch: tab pulled out (" + how + ")");
      if (gBrowser.visibleTabs.length > 1) gBrowser.replaceTabWithWindow(tab);
    }
    let openedAt = 0;
    function openHoldMenu(tab, x, y, how) {
      if (Date.now() - openedAt < 800) return;
      openedAt = Date.now();
      TabContextMenu.contextTab = tab;
      menu?.openPopupAtScreen(x, y, true);
      log("touch: hold menu (" + how + ")");
    }
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
      let search = Services.search;
      for (const url of ["moz-src:///toolkit/components/search/SearchService.sys.mjs",
                         "resource://gre/modules/SearchService.sys.mjs"]) {
        if (search) break;
        try {
          const m = ChromeUtils.importESModule(url);
          search = m.SearchService?.prototype ? null : m.SearchService;
        } catch (e) {}
      }
      if (!search) { log("search engine: left to the policy file"); return; }
      await search.init();
      const ddg = (await search.getVisibleEngines()).find(e => /duckduckgo/i.test(e.name));
      if (ddg && (await search.getDefault())?.name !== ddg.name)
        await search.setDefault(ddg, Ci.nsISearchService.CHANGE_REASON_USER);
      log("search engine: " + (await search.getDefault())?.name);
    } catch (e) { log("search engine: " + e); }
  }

  onReady(() => {
    try { buildRow(); } catch (e) { log("row: " + e); }
    try { keepExtensionsInPlace(); } catch (e) { log("extensions: " + e); }
    try { trimMenu(); } catch (e) { log("menu: " + e); }
    try { holdTabs(); } catch (e) { log("tab menu: " + e); }
    startPageAndSearch().then(() => setTimeout(placeholder, 300));
    placeholder();
    // For the self-check: say when the menu opens and closes.
    doc.addEventListener("popupshown", e => {
      if (e.target.id === "appMenu-popup" || e.target.id === "unified-extensions-panel") {
        const r = e.target.getBoundingClientRect();
        log(e.target.id + " shown at " + Math.round(r.x) + "," + Math.round(r.y) + " size " +
            Math.round(r.width) + "x" + Math.round(r.height));
      }
    }, true);
    doc.addEventListener("popuphidden", e => {
      if (e.target.id === "appMenu-popup" || e.target.id === "unified-extensions-panel")
        log(e.target.id + " hidden");
    }, true);
    updateCount();
    for (const ev of ["TabOpen", "TabClose", "TabShow", "TabHide"])
      gBrowser.tabContainer.addEventListener(ev, () => setTimeout(updateCount, 0));
    // Firefox holds tab widths after a close until the pointer leaves the
    // strip, which leaves + stranded; let the strip settle straight away. If a
    // closing tab is still there a moment later, finish its close.
    gBrowser.tabContainer.addEventListener("TabClose", () => setTimeout(() => {
      const tc = gBrowser.tabContainer;
      try { (tc._unlockTabSizing || tc.unlockTabSizing)?.call(tc, { instant: true }); } catch (e) {}
      setTimeout(() => {
        const stuck = [...doc.querySelectorAll(".tabbrowser-tab")].filter(t =>
          t.closing || t.hasAttribute("closing") || !t.hasAttribute("fadein"));
        for (const t of stuck)
          t.dispatchEvent(new TransitionEvent("transitionend", { propertyName: "max-width", bubbles: true }));
        log("after close: " + describeStrip() + (stuck.length ? " (finished " + stuck.length + " stuck close)" : ""));
      }, 450);
    }, 0));
    // Groups sit at the left of the strip, after any pinned tabs; loose tabs
    // follow them. A loose tab that lands ahead of a group moves to the end,
    // keeping the loose tabs in their order.
    let ordering = false;
    const groupsFirst = () => {
      if (ordering) return;
      const tabs = [...gBrowser.tabs].filter(t => !t.pinned && !t.closing);
      let last = -1;
      tabs.forEach((t, i) => { if (t.group) last = i; });
      const ahead = tabs.slice(0, last).filter(t => !t.group);
      if (!ahead.length) return;
      // Move every loose tab, in order, so the ones already behind the groups
      // keep their place in the line too.
      const loose = tabs.filter(t => !t.group);
      ordering = true;
      try { for (const t of loose) gBrowser.moveTabToEnd(t); }
      catch (e) { log("groups first: " + e); }
      finally { ordering = false; }
      log("groups first: moved " + loose.length + " loose tab(s) after the groups");
    };
    for (const ev of ["TabOpen", "TabMove", "TabGrouped", "TabUngrouped", "TabGroupCreate", "TabGroupExpand"])
      gBrowser.tabContainer.addEventListener(ev, () => setTimeout(groupsFirst, 0));
    setTimeout(groupsFirst, 500);
    // Every window's title is the page's own title, with no "Mozilla
    // Firefox" after it (new windows were getting it).
    try {
      const de = doc.documentElement;
      for (const [k, v] of [["data-title-default", "Output Browser"], ["data-title-private", "Output Browser"],
                            ["data-content-title-default", "CONTENTTITLE"], ["data-content-title-private", "CONTENTTITLE"]])
        if (de.hasAttribute(k)) de.setAttribute(k, v);
      gBrowser.updateTitlebar?.();
    } catch (e) { log("title: " + e); }
    // Ctrl+Shift+Space also folds the strip down and up.
    window.addEventListener("keydown", e => {
      if (e.ctrlKey && e.shiftKey && e.code === "Space") { setFolded(!folded()); e.preventDefault(); }
    }, true);
    doc.documentElement.setAttribute("output-browser", "true");
    log("ready");
  });
})();
