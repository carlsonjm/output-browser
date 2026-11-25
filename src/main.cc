// Output Browser.
//
// Pages are CEF Chrome-style browsers, so they behave as Chrome does. The row,
// the tab strip, the menu and the start page are our own HTML.
//
// A CEF window can hold only one Chrome-style page, and a page cannot move to
// another window. So the main window holds only our row, and each tab is a
// Chrome-style browser embedded in the main window as a child window (X11).
// The tab that is showing sits over the page box; the others are hidden.
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstdio>
#include <deque>
#include <fstream>
#include <functional>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include "include/base/cef_compiler_specific.h"
#include "include/cef_app.h"
#include "include/cef_browser.h"
#include "include/cef_command_ids.h"
#include "include/cef_command_line.h"
#include "include/cef_parser.h"
#include "include/cef_request_context.h"
#include "include/cef_task.h"
#include "include/views/cef_box_layout.h"
#include "include/views/cef_browser_view.h"
#include "include/views/cef_browser_view_delegate.h"
#include "include/views/cef_display.h"
#include "include/views/cef_panel.h"
#include "include/views/cef_window.h"
#include "include/views/cef_window_delegate.h"
#include "x11.h"

namespace {

constexpr cef_color_t kChrome = 0xFF1C1C1C;  // window chrome
constexpr cef_color_t kMenu = 0xFF1A1A1A;    // menu surface
constexpr int kNavH = 44;    // the row
constexpr int kStripH = 48;  // the tab strip, 40 plus its 8 below
constexpr int kInset = 8;    // page box margin: 0 8 8
constexpr int kRadius = 10;  // page box corners
constexpr int kMenuW = 294, kMenuH = 400;  // until the menu reports its size

std::string g_ui;  // folder holding bar.html, menu.html and start.html
int g_browsers = 0;
int g_windows = 0;

std::string UiUrl(const std::string& f) { return "file://" + g_ui + "/" + f; }
std::string StartUrl() { return UiUrl("start.html"); }
bool IsStart(const std::string& u) { return u.rfind(StartUrl(), 0) == 0; }
bool Flag(const char* name) { return CefCommandLine::GetGlobalCommandLine()->HasSwitch(name); }

// Which window buttons go on which side, from the desktop's own setting
// (KWin's title bar buttons, which Shuffle's Settings changes). Only
// minimise (I), maximise (A) and close (X) are kept, in the desktop's order.
struct ButtonSides { std::string left = "", right = "IAX"; };
ButtonSides ReadButtonSides() {
  ButtonSides s;
  // Shuffle's defaults: menu on the left, minimise, maximise, close on the right.
  const char* xdg = getenv("XDG_CONFIG_HOME");
  const char* home = getenv("HOME");
  const std::string path = xdg && *xdg ? std::string(xdg) + "/kwinrc"
                                       : std::string(home ? home : ".") + "/.config/kwinrc";
  std::ifstream f(path);
  std::string line, group;
  std::string left = "M", right = "IAX";
  while (std::getline(f, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (!line.empty() && line[0] == '[') { group = line; continue; }
    if (group != "[org.kde.kdecoration2]") continue;
    if (line.rfind("ButtonsOnLeft=", 0) == 0) left = line.substr(14);
    if (line.rfind("ButtonsOnRight=", 0) == 0) right = line.substr(15);
  }
  auto keep = [](const std::string& in) {
    std::string o;
    for (char c : in) if (c == 'I' || c == 'A' || c == 'X') o += c;
    return o;
  };
  s.left = keep(left);
  s.right = keep(right);
  if (s.left.empty() && s.right.empty()) s.right = "IAX";
  return s;
}

void Log(const std::string& s) {
  fprintf(stderr, "[output-browser] %s\n", s.c_str());
  fflush(stderr);
}

void MaybeQuit() {
  if (g_browsers == 0 && g_windows == 0) CefQuitMessageLoop();
}

class FnTask : public CefTask {
 public:
  explicit FnTask(std::function<void()> f) : f_(std::move(f)) {}
  void Execute() override { f_(); }
 private:
  std::function<void()> f_;
  IMPLEMENT_REFCOUNTING(FnTask);
};
// Runs |f| on the UI thread after the current callback returns.
void Post(std::function<void()> f, int64_t delay_ms = 0) {
  if (delay_ms > 0)
    CefPostDelayedTask(TID_UI, new FnTask(std::move(f)), delay_ms);
  else
    CefPostTask(TID_UI, new FnTask(std::move(f)));
}

std::string Q(const std::string& s) {
  std::string o = "\"";
  for (unsigned char c : s) {
    switch (c) {
      case '"': o += "\\\""; break;
      case '\\': o += "\\\\"; break;
      case '\n': o += "\\n"; break;
      case '\r': o += "\\r"; break;
      case '\t': o += "\\t"; break;
      case '<': o += "\\u003c"; break;
      default:
        if (c < 0x20) { char b[8]; snprintf(b, sizeof b, "\\u%04x", c); o += b; }
        else o += static_cast<char>(c);
    }
  }
  return o + "\"";
}
const char* B(bool b) { return b ? "true" : "false"; }

int64_t NowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch()).count();
}

// ---------------------------------------------------------------- files

std::string g_root;  // the profile folder, ~/.local/share/output-browser

std::string ReadFile(const std::string& p) {
  std::ifstream f(p, std::ios::binary);
  if (!f) return "";
  return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}
CefRefPtr<CefDictionaryValue> ReadJsonDict(const std::string& p) {
  const std::string s = ReadFile(p);
  if (s.empty()) return nullptr;
  auto v = CefParseJSON(s, JSON_PARSER_ALLOW_TRAILING_COMMAS);
  return v && v->GetType() == VTYPE_DICTIONARY ? v->GetDictionary() : nullptr;
}
CefRefPtr<CefDictionaryValue> Dict(CefRefPtr<CefDictionaryValue> d, const std::string& k) {
  return d && d->GetType(k) == VTYPE_DICTIONARY ? d->GetDictionary(k) : nullptr;
}
std::string Str(CefRefPtr<CefDictionaryValue> d, const std::string& k) {
  return d && d->GetType(k) == VTYPE_STRING ? d->GetString(k).ToString() : "";
}
std::string Lower(std::string s) {
  for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

// ---------------------------------------------------------------- extensions
//
// Extensions are Chrome's own: installed from the Chrome Web Store into the
// profile, and run by Chrome. We only read what is installed, to show each
// one in the address bar and open its pop-over.

struct Ext { std::string id, name, popup, icon; };

// An extension's name can be a message from its translations.
std::string Localize(const std::string& dir, CefRefPtr<CefDictionaryValue> man,
                     const std::string& s) {
  if (s.rfind("__MSG_", 0) != 0 || s.size() < 9) return s;
  const std::string key = Lower(s.substr(6, s.size() - 8));
  for (const std::string& loc : {std::string("en_US"), std::string("en"), Str(man, "default_locale")}) {
    if (loc.empty()) continue;
    auto m = ReadJsonDict(dir + "/_locales/" + loc + "/messages.json");
    if (!m) continue;
    CefDictionaryValue::KeyList keys;
    m->GetKeys(keys);
    for (auto& k : keys) {
      const std::string ks = k.ToString();
      if (Lower(ks) != key) continue;
      const std::string msg = Str(Dict(m, ks), "message");
      if (!msg.empty()) return msg;
    }
  }
  return s.substr(6, s.size() - 8);
}

// The icon closest to 32 px, from a path or a {size: path} list.
std::string PickIcon(CefRefPtr<CefValue> v) {
  if (!v) return "";
  if (v->GetType() == VTYPE_STRING) return v->GetString().ToString();
  if (v->GetType() != VTYPE_DICTIONARY) return "";
  auto d = v->GetDictionary();
  CefDictionaryValue::KeyList keys;
  d->GetKeys(keys);
  std::string best;
  int best_size = 0;
  for (auto& k : keys) {
    const int size = atoi(k.ToString().c_str());
    const std::string path = Str(d, k.ToString());
    if (path.empty()) continue;
    const bool better = best.empty() ||
                        (size >= 32 && (best_size < 32 || size < best_size)) ||
                        (size < 32 && best_size < 32 && size > best_size);
    if (better) { best = path; best_size = size; }
  }
  return best;
}

bool Disabled(CefRefPtr<CefDictionaryValue> s) {
  if (s->GetType("disable_reasons") == VTYPE_LIST && s->GetList("disable_reasons")->GetSize() > 0)
    return true;
  if (s->GetType("disable_reasons") == VTYPE_INT && s->GetInt("disable_reasons") != 0) return true;
  if (s->GetType("state") == VTYPE_INT && s->GetInt("state") == 0) return true;
  return false;
}

std::vector<Ext> ScanExtensions() {
  std::vector<Ext> out;
  const std::string prof = g_root + "/default";
  for (const char* file : {"/Secure Preferences", "/Preferences"}) {
    auto settings = Dict(Dict(ReadJsonDict(prof + file), "extensions"), "settings");
    if (!settings) continue;
    CefDictionaryValue::KeyList ids;
    settings->GetKeys(ids);
    for (auto& idv : ids) {
      const std::string id = idv.ToString();
      if (std::any_of(out.begin(), out.end(), [&id](const Ext& e) { return e.id == id; })) continue;
      auto st = Dict(settings, id);
      if (!st || Disabled(st)) continue;
      const int loc = st->GetType("location") == VTYPE_INT ? st->GetInt("location") : 0;
      if (loc == 5 || loc == 10) continue;  // built into the browser
      const std::string path = Str(st, "path");
      if (path.empty()) continue;
      const std::string dir = path[0] == '/' ? path : prof + "/Extensions/" + path;
      auto man = ReadJsonDict(dir + "/manifest.json");
      if (!man || man->HasKey("theme") || man->HasKey("app")) continue;
      Ext e;
      e.id = id;
      e.name = Localize(dir, man, Str(man, "name"));
      auto act = Dict(man, "action");
      if (!act) act = Dict(man, "browser_action");
      if (!act) act = Dict(man, "page_action");
      e.popup = Str(act, "default_popup");
      while (!e.popup.empty() && e.popup[0] == '/') e.popup.erase(0, 1);
      std::string icon = act ? PickIcon(act->GetValue("default_icon")) : "";
      if (icon.empty()) icon = PickIcon(man->GetValue("icons"));
      while (!icon.empty() && icon[0] == '/') icon.erase(0, 1);
      if (!icon.empty()) e.icon = "file://" + dir + "/" + icon;
      out.push_back(e);
    }
  }
  std::sort(out.begin(), out.end(),
            [](const Ext& a, const Ext& b) { return Lower(a.name) < Lower(b.name); });
  return out;
}

// ---------------------------------------------------------------- our own settings
//
// Kept in output-browser.json in the profile folder: which extensions are
// shown in the address bar (up to two) and the bookmarks on the start page.

struct Bookmark { std::string u, t; };
struct Store {
  std::vector<std::string> shown;
  bool shown_set = false;  // until the person picks, the first two are shown
  std::vector<Bookmark> bookmarks;
} g_store;

std::string StorePath() { return g_root + "/output-browser.json"; }

void LoadStore() {
  auto d = ReadJsonDict(StorePath());
  bool have_bookmarks = false;
  if (d && d->GetType("shown") == VTYPE_LIST) {
    g_store.shown_set = true;
    auto l = d->GetList("shown");
    for (size_t i = 0; i < l->GetSize(); ++i)
      if (l->GetType(i) == VTYPE_STRING) g_store.shown.push_back(l->GetString(i).ToString());
  }
  if (d && d->GetType("bookmarks") == VTYPE_LIST) {
    have_bookmarks = true;
    auto l = d->GetList("bookmarks");
    for (size_t i = 0; i < l->GetSize(); ++i) {
      if (l->GetType(i) != VTYPE_DICTIONARY) continue;
      auto e = l->GetDictionary(i);
      if (!Str(e, "u").empty()) g_store.bookmarks.push_back({Str(e, "u"), Str(e, "t")});
    }
  }
  if (!have_bookmarks)
    g_store.bookmarks = {{"https://mail.google.com/", "Mail"},
                         {"https://www.youtube.com/", "YouTube"},
                         {"https://weather.com/", "Weather"},
                         {"https://www.netflix.com/", "Netflix"},
                         {"https://docs.google.com/", "Docs"}};
}

std::string BookmarksJson() {
  std::string s = "[";
  for (size_t i = 0; i < g_store.bookmarks.size(); ++i)
    s += std::string(i ? "," : "") + "{\"u\":" + Q(g_store.bookmarks[i].u) +
         ",\"t\":" + Q(g_store.bookmarks[i].t) + "}";
  return s + "]";
}

void SaveStore() {
  std::string s = "{";
  if (g_store.shown_set) {
    s += "\"shown\":[";
    for (size_t i = 0; i < g_store.shown.size(); ++i) s += (i ? "," : "") + Q(g_store.shown[i]);
    s += "],";
  }
  s += "\"bookmarks\":" + BookmarksJson() + "}\n";
  const std::string tmp = StorePath() + ".new";
  {
    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
    if (!f) return;
    f << s;
  }
  rename(tmp.c_str(), StorePath().c_str());
}

bool Bookmarked(const std::string& u) {
  return std::any_of(g_store.bookmarks.begin(), g_store.bookmarks.end(),
                     [&u](const Bookmark& b) { return b.u == u; });
}

// The extensions shown in the address bar: the person's picks, else the first two.
std::vector<std::string> ShownIds(const std::vector<Ext>& exts) {
  std::vector<std::string> v;
  if (g_store.shown_set) {
    for (auto& id : g_store.shown)
      if (std::any_of(exts.begin(), exts.end(), [&id](const Ext& e) { return e.id == id; }))
        v.push_back(id);
  } else {
    for (auto& e : exts) v.push_back(e.id);
  }
  if (v.size() > 2) v.resize(2);
  return v;
}

std::string ExtsJson(const std::vector<Ext>& exts) {
  const auto shown = ShownIds(exts);
  std::string s = "[";
  for (size_t i = 0; i < exts.size(); ++i) {
    const auto& e = exts[i];
    const bool on = std::find(shown.begin(), shown.end(), e.id) != shown.end();
    s += std::string(i ? "," : "") + "{\"id\":" + Q(e.id) + ",\"n\":" + Q(e.name) +
         ",\"i\":" + Q(e.icon) + ",\"p\":" + B(!e.popup.empty()) + ",\"s\":" + B(on) + "}";
  }
  return s + "]";
}

std::string HostName(const std::string& url);

// A short name for a bookmark: the page title up to its first separator.
std::string ShortTitle(const std::string& title, const std::string& url) {
  std::string t = title;
  for (const char* sep : {" - ", " | ", " \xE2\x80\x93 ", " \xE2\x80\x94 ", " \xC2\xB7 "}) {
    auto p = t.find(sep);
    if (p != std::string::npos && p > 0) t = t.substr(0, p);
  }
  if (!t.empty() && t != url) return t;
  return HostName(url);
}

std::string HostName(const std::string& url) {
  CefURLParts parts;
  std::string host = CefParseURL(url, parts) ? CefString(&parts.host).ToString() : url;
  if (host.rfind("www.", 0) == 0) host = host.substr(4);
  return host;
}

class UiDelegate : public CefBrowserViewDelegate {
 public:
  int height = kNavH;
  cef_runtime_style_t GetBrowserRuntimeStyle() override { return CEF_RUNTIME_STYLE_ALLOY; }
  CefSize GetPreferredSize(CefRefPtr<CefView>) override { return CefSize(0, height); }
 private:
  IMPLEMENT_REFCOUNTING(UiDelegate);
};

class Win;

struct Tab {
  int id = 0;
  CefRefPtr<CefBrowser> browser;  // set once created
  std::string url, title, fav;
  bool loading = false, back = false, fwd = false, closing = false;
  std::string first_url;  // loaded once the tab's window is in place
  unsigned long xid() const { return browser ? browser->GetHost()->GetWindowHandle() : 0; }
};

class PageClient : public CefClient, public CefLifeSpanHandler,
                   public CefDisplayHandler, public CefLoadHandler,
                   public CefKeyboardHandler, public CefDownloadHandler,
                   public CefFocusHandler {
 public:
  explicit PageClient(Win* w) : win_(w) {}
  void Detach() { win_ = nullptr; }
  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
  CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }
  CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }
  CefRefPtr<CefKeyboardHandler> GetKeyboardHandler() override { return this; }
  CefRefPtr<CefDownloadHandler> GetDownloadHandler() override { return this; }
  CefRefPtr<CefFocusHandler> GetFocusHandler() override { return this; }

  void OnGotFocus(CefRefPtr<CefBrowser> b) override;
  bool OnConsoleMessage(CefRefPtr<CefBrowser> b, cef_log_severity_t,
                        const CefString& message, const CefString& source,
                        int) override;
  void OnAfterCreated(CefRefPtr<CefBrowser> b) override;
  void OnBeforeClose(CefRefPtr<CefBrowser> b) override;
  bool OnBeforePopup(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, int,
                     const CefString& target_url, const CefString&,
                     WindowOpenDisposition disposition, bool,
                     const CefPopupFeatures&, CefWindowInfo&,
                     CefRefPtr<CefClient>&, CefBrowserSettings&,
                     CefRefPtr<CefDictionaryValue>&, bool*) override;
  void OnAddressChange(CefRefPtr<CefBrowser> b, CefRefPtr<CefFrame> f,
                       const CefString& url) override;
  void OnTitleChange(CefRefPtr<CefBrowser> b, const CefString& title) override;
  void OnFaviconURLChange(CefRefPtr<CefBrowser> b,
                          const std::vector<CefString>& urls) override;
  void OnFullscreenModeChange(CefRefPtr<CefBrowser> b, bool fs) override;
  void OnLoadingStateChange(CefRefPtr<CefBrowser> b, bool loading, bool back,
                            bool fwd) override;
  bool OnPreKeyEvent(CefRefPtr<CefBrowser> b, const CefKeyEvent& e,
                     CefEventHandle, bool*) override;
  bool OnBeforeDownload(CefRefPtr<CefBrowser>, CefRefPtr<CefDownloadItem>,
                        const CefString&,
                        CefRefPtr<CefBeforeDownloadCallback> cb) override {
    cb->Continue("", false);  // the usual Downloads folder
    return true;
  }

 private:
  Win* win_;
  IMPLEMENT_REFCOUNTING(PageClient);
};

// Client for our own HTML: the row and strip, and the menu. Only these
// browsers may send commands, by console lines that start with "ob:".
class UiClient : public CefClient, public CefLifeSpanHandler,
                 public CefDisplayHandler, public CefKeyboardHandler,
                 public CefDragHandler {
 public:
  explicit UiClient(Win* w) : win_(w) {}
  void Detach() { win_ = nullptr; }
  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
  CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }
  CefRefPtr<CefKeyboardHandler> GetKeyboardHandler() override { return this; }
  CefRefPtr<CefDragHandler> GetDragHandler() override { return this; }
  void OnAfterCreated(CefRefPtr<CefBrowser> b) override;
  void OnBeforeClose(CefRefPtr<CefBrowser> b) override;
  void OnDraggableRegionsChanged(CefRefPtr<CefBrowser> b, CefRefPtr<CefFrame>,
                                 const std::vector<CefDraggableRegion>& regions) override;
  bool OnConsoleMessage(CefRefPtr<CefBrowser>, cef_log_severity_t,
                        const CefString& message, const CefString& source,
                        int) override;
  bool OnPreKeyEvent(CefRefPtr<CefBrowser>, const CefKeyEvent& e,
                     CefEventHandle, bool*) override;
 private:
  Win* win_;
  IMPLEMENT_REFCOUNTING(UiClient);
};

// Client for an extension's pop-over: a Chrome-style page laid over the top
// of the page box, under the address bar, like a menu.
class PopClient : public CefClient, public CefLifeSpanHandler,
                  public CefDisplayHandler, public CefLoadHandler,
                  public CefKeyboardHandler {
 public:
  explicit PopClient(Win* w) : win_(w) {}
  void Detach() { win_ = nullptr; }
  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
  CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }
  CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }
  CefRefPtr<CefKeyboardHandler> GetKeyboardHandler() override { return this; }
  void OnAfterCreated(CefRefPtr<CefBrowser> b) override;
  void OnBeforeClose(CefRefPtr<CefBrowser> b) override;
  bool OnBeforePopup(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, int,
                     const CefString& target_url, const CefString&,
                     WindowOpenDisposition disposition, bool,
                     const CefPopupFeatures&, CefWindowInfo&,
                     CefRefPtr<CefClient>&, CefBrowserSettings&,
                     CefRefPtr<CefDictionaryValue>&, bool*) override;
  bool OnConsoleMessage(CefRefPtr<CefBrowser> b, cef_log_severity_t,
                        const CefString& message, const CefString&, int) override;
  void OnLoadingStateChange(CefRefPtr<CefBrowser> b, bool loading, bool, bool) override;
  bool OnPreKeyEvent(CefRefPtr<CefBrowser>, const CefKeyEvent& e,
                     CefEventHandle, bool*) override;
 private:
  Win* win_;
  IMPLEMENT_REFCOUNTING(PopClient);
};

// Measures the pop-over's page and reports its size, as Chrome sizes
// extension pop-overs to their content.
const char kMeasure[] =
    "(()=>{if(window.__obm)return;window.__obm=1;const r=()=>{const b=document.body,"
    "d=document.documentElement;if(!b)return;const cs=getComputedStyle(b);"
    "const w=Math.ceil(b.getBoundingClientRect().width+parseFloat(cs.marginLeft||0)+"
    "parseFloat(cs.marginRight||0));const h=Math.ceil(Math.max(d.scrollHeight,b.scrollHeight));"
    "console.log('obpop:'+w+'x'+h)};if(document.body)new ResizeObserver(r).observe(document.body);"
    "r()})()";

std::vector<Win*> g_wins;

class Win : public CefWindowDelegate {
 public:
  Win(bool priv, std::string first) : private_(priv), first_(std::move(first)) {
    page_client_ = new PageClient(this);
    ui_client_ = new UiClient(this);
    pop_client_ = new PopClient(this);
    ui_delegate_ = new UiDelegate;
    if (private_) {
      CefRequestContextSettings s;  // no cache path: kept in memory only
      context_ = CefRequestContext::CreateContext(s, nullptr);
    }
  }

  static void OpenWindow(bool priv, const std::string& url) {
    CefWindow::CreateTopLevelWindow(new Win(priv, url));
  }

  // ---- window

  // The main window holds only our own (Alloy-style) browser for the row.
  cef_runtime_style_t GetWindowRuntimeStyle() override { return CEF_RUNTIME_STYLE_ALLOY; }

  // No desktop title bar: the window buttons are in our own row, and the
  // row's empty space moves the window. --system-title-bar brings it back.
  bool IsFrameless(CefRefPtr<CefWindow>) override { return !Flag("system-title-bar"); }

  // The window's own name, so the desktop knows it is Output Browser (and
  // matches it to output-browser.desktop) instead of guessing from its title.
  bool GetLinuxWindowProperties(CefRefPtr<CefWindow>,
                                CefLinuxWindowProperties& p) override {
    CefString(&p.wm_class_class) = "output-browser";
    CefString(&p.wm_class_name) = "output-browser";
    CefString(&p.wayland_app_id) = "output-browser";
    CefString(&p.wm_role_name) = "browser";
    return true;
  }

  void DragRegions(CefRefPtr<CefBrowser> b, const std::vector<CefDraggableRegion>& r) {
    if (window_ && ui_view_ && ui_view_->GetBrowser() && ui_view_->GetBrowser()->IsSame(b))
      window_->SetDraggableRegions(r);
  }

  void OnWindowCreated(CefRefPtr<CefWindow> w) override {
    window_ = w;
    ++g_windows;
    g_wins.push_back(this);
    w->SetTitle(private_ ? "Output Browser (private)" : "Output Browser");
    w->SetBackgroundColor(kChrome);
    CefBoxLayoutSettings v;
    v.horizontal = false;
    auto layout = w->SetToBoxLayout(v);

    CefBrowserSettings ui;
    ui.background_color = kChrome;
    ui_view_ = CefBrowserView::CreateBrowserView(ui_client_, UiUrl("bar.html"), ui,
                                                 nullptr, nullptr, ui_delegate_);
    w->AddChildView(ui_view_);

    // The page box area. Tabs are laid over it as child windows.
    content_ = CefPanel::CreatePanel(this);
    content_->SetBackgroundColor(kChrome);
    w->AddChildView(content_);
    layout->SetFlexForView(content_, 1);

    w->Show();
    CefRefPtr<Win> self(this);
    const std::string first = first_;
    // The first tab waits until the new window is on screen at its real size.
    // A tab embedded while the window is still being mapped never paints.
    Post([self, first] { self->NewTab(first, true); }, 400);
    if (!private_) ScanTick();
  }

  void OnWindowDestroyed(CefRefPtr<CefWindow>) override {
    page_client_->Detach();
    ui_client_->Detach();
    pop_client_->Detach();
    g_wins.erase(std::remove(g_wins.begin(), g_wins.end(), this), g_wins.end());
    pop_ = nullptr;
    ui_view_ = nullptr;
    content_ = nullptr;
    window_ = nullptr;
    menu_ = nullptr;
    --g_windows;
    MaybeQuit();
  }

  CefSize GetPreferredSize(CefRefPtr<CefView> v) override {
    return v && v->AsPanel() && v->AsPanel()->AsWindow() ? CefSize(1280, 860) : CefSize();
  }

  // Closing the window closes every tab first, then our own browsers.
  bool CanClose(CefRefPtr<CefWindow>) override {
    closing_all_ = true;
    CloseMenu();
    ClosePop();
    for (auto& t : tabs_) {
      if (!t->closing && t->browser) {
        t->closing = true;
        t->browser->GetHost()->CloseBrowser(false);
      }
    }
    pending_.clear();
    if (!tabs_.empty()) return false;
    if (menu_) menu_->GetHost()->CloseBrowser(true);
    auto b = ui_view_ ? ui_view_->GetBrowser() : nullptr;
    return b ? b->GetHost()->TryCloseBrowser() : true;
  }

  void OnWindowBoundsChanged(CefRefPtr<CefWindow>, const CefRect&) override {
    PlaceActive();
    if (window_ && LooksMaximized() != maximized_) {
      maximized_ = LooksMaximized();
      PushState();
    }
    if (menu_open_) CloseMenu();
    ClosePop();
  }

  void OnLayoutChanged(CefRefPtr<CefView> v, const CefRect&) override {
    if (!content_ || !v || !v->IsSame(content_)) return;
    PlaceActive();
    if (menu_open_) CloseMenu();
    ClosePop();
  }

  // ---- tabs

  void NewTab(const std::string& url, bool activate) {
    if (!window_ || closing_all_) return;
    const unsigned long parent = window_->GetWindowHandle();
    if (!parent) {
      Log("main window has no X11 window handle; is the browser running on X11?");
      return;
    }
    auto t = std::make_unique<Tab>();
    t->id = ++next_id_;
    t->url = url;
    t->first_url = url;
    t->title = IsStart(url) ? "New tab" : url;
    pending_.push_back({t->id, activate});
    const int id = t->id;
    // New tabs go after the one showing, as Chrome places them.
    auto pos = tabs_.end();
    if (Tab* a = Find(active_)) {
      pos = std::find_if(tabs_.begin(), tabs_.end(), [a](auto& x) { return x.get() == a; }) + 1;
      if (pos > tabs_.end()) pos = tabs_.end();
    }
    tabs_.insert(pos, std::move(t));

    CefWindowInfo info;
    const CefRect r = PageRectPx();
    info.SetAsChild(parent, r);
    info.runtime_style = CEF_RUNTIME_STYLE_CHROME;
    // The tab starts blank and loads its page only once its window is placed:
    // a page whose first paint happens before that never shows.
    CefBrowserHost::CreateBrowser(info, page_client_, "about:blank", CefBrowserSettings(),
                                  nullptr, context_);
    Log("new tab " + std::to_string(id) + " " + url);
    PushState();
  }

  // Called when a tab's browser exists.
  void TabCreated(CefRefPtr<CefBrowser> b) {
    if (pending_.empty()) return;
    auto [id, activate] = pending_.front();
    pending_.pop_front();
    Tab* t = Find(id);
    if (!t) { b->GetHost()->CloseBrowser(true); return; }
    t->browser = b;
    Log("tab " + std::to_string(id) + " created: " + x11::Describe(t->xid()));
    Log("main window: " + x11::Describe(window_ ? window_->GetWindowHandle() : 0) +
        ", scale " + std::to_string(Scale()));
    if (activate || !Find(active_)) Activate(id);
    else x11::Show(t->xid(), false);
    CefRefPtr<Win> self(this);
    Post([self, id] {
      Tab* tt = self->Find(id);
      if (!tt || !tt->browser || tt->first_url.empty()) return;
      self->PlaceActive();
      const std::string u = tt->first_url;
      tt->first_url.clear();
      tt->browser->GetMainFrame()->LoadURL(u);
      Log("tab " + std::to_string(id) + " loads " + u);
    }, 100);
    PushState();
  }

  void Activate(int id) {
    Tab* t = Find(id);
    if (!t || t->closing || !t->browser || !window_) return;
    Tab* old = Find(active_);
    active_ = id;
    PlaceActive();
    x11::Show(t->xid(), true);
    if (old && old != t) x11::Show(old->xid(), false);
    Log("showing tab " + std::to_string(id) + ": " + x11::Describe(t->xid()));
    // Place again once the window has settled, in case its size was not final.
    CefRefPtr<Win> self(this);
    Post([self, id] {
      if (self->active_ != id) return;
      self->PlaceActive();
      if (Tab* tt = self->Find(id)) Log("tab " + std::to_string(id) + " now: " + x11::Describe(tt->xid()));
    }, 300);
    ClosePop();
    t->browser->GetHost()->SetFocus(true);
    if (menu_open_) CloseMenu();
    PushState();
  }

  void CloseTab(int id) {
    Tab* t = Find(id);
    if (!t || t->closing) return;
    t->closing = true;
    if (active_ == id) ActivateNeighbour(id);
    if (t->browser) t->browser->GetHost()->CloseBrowser(false);
    PushState();
  }

  // ---- from the page client

  Tab* TabFor(CefRefPtr<CefBrowser> b) {
    for (auto& t : tabs_)
      if (t->browser && t->browser->IsSame(b)) return t.get();
    return nullptr;
  }

  void PageClosed(CefRefPtr<CefBrowser> b) {
    Tab* t = TabFor(b);
    if (!t) return;
    const int id = t->id;
    if (active_ == id) ActivateNeighbour(id);
    tabs_.erase(std::remove_if(tabs_.begin(), tabs_.end(), [id](auto& x) { return x->id == id; }),
                tabs_.end());
    PushState();
    if (tabs_.empty() && pending_.empty() && window_) {
      // The last tab went: the window goes too, as in Chrome.
      CefRefPtr<CefWindow> w = window_;
      Post([w] { if (!w->IsClosed()) w->Close(); });
    }
  }

  void Fullscreen(CefRefPtr<CefBrowser> b, bool fs) {
    Tab* t = TabFor(b);
    if (!t || t->id != active_ || !window_) return;
    fullscreen_ = fs;
    CloseMenu();
    ui_delegate_->height = UiHeight();
    ui_view_->InvalidateLayout();
    if (window_->IsFullscreen() != fs) window_->SetFullscreen(fs);
    window_->Layout();
    PlaceActive();
  }

  void PushState() {
    if (!window_) return;
    Tab* a = Find(active_);
    std::string s = "{\"tabs\":[";
    bool first = true;
    for (auto& t : tabs_) {
      if (t->closing) continue;
      if (!first) s += ",";
      first = false;
      s += "{\"id\":" + std::to_string(t->id) + ",\"t\":" + Q(t->title) +
           ",\"f\":" + Q(t->fav) + ",\"cur\":" + B(t->id == active_) + "}";
    }
    s += "]";
    if (a) {
      s += ",\"url\":" + Q(a->url) + ",\"start\":" + B(IsStart(a->url)) +
           ",\"back\":" + B(a->back) + ",\"fwd\":" + B(a->fwd) +
           ",\"loading\":" + B(a->loading);
      std::string title = (a->title.empty() ? a->url : a->title) + " – Output Browser";
      if (private_) title += " (private)";
      window_->SetTitle(title);
    }
    if (a)
      s += ",\"marked\":" + std::string(B(Bookmarked(a->url))) +
           ",\"markable\":" + B(!IsStart(a->url) && !a->url.empty());
    s += ",\"exts\":" + (private_ ? std::string("[]") : ExtsJson(exts_)) +
         ",\"pop\":" + Q(pop_id_) + ",\"extmenu\":" + B(menu_open_ && menu_mode_ == "ext");
    s += ",\"fold\":" + std::string(B(fold_)) + ",\"menu\":" + B(menu_open_ && menu_mode_ == "main") +
         ",\"private\":" + B(private_) + ",\"max\":" + B(maximized_);
    if (IsFrameless(window_)) s += ",\"wl\":" + Q(sides_.left) + ",\"wr\":" + Q(sides_.right);
    s += "}";
    Js(ui_view_ ? ui_view_->GetBrowser() : nullptr, "window.ob&&ob.set(" + s + ")");
  }

  // ---- our own HTML

  void UiCreated(CefRefPtr<CefBrowser> b) {
    if (ui_view_ && ui_view_->GetBrowser() && ui_view_->GetBrowser()->IsSame(b)) return;
    menu_ = b;  // the other UI browser is the menu
    if (!menu_open_) x11::Show(b->GetHost()->GetWindowHandle(), false);
    else PlaceMenu();
  }

  void UiClosed(CefRefPtr<CefBrowser> b) {
    if (menu_ && menu_->IsSame(b)) menu_ = nullptr;
  }

  void Command(CefRefPtr<CefDictionaryValue> d) {
    if (!window_) return;
    const std::string c = d->GetString("c").ToString();
    CefRefPtr<CefBrowser> b;
    if (Tab* a = Find(active_)) b = a->browser;

    // Any other click in our row or menu closes an open pop-over.
    if (c != "ready" && c != "menuready" && c != "menusize" && c != "menuclose" &&
        c != "extpop" && c != "extshow")
      ClosePop();
    if (c == "ready") { sides_ = ReadButtonSides(); PushState(); return; }
    if (c == "dismiss") return;
    if (c == "star") { ToggleStar(); return; }
    if (c == "extshow") { ShowExt(d->GetString("id").ToString(), d->GetBool("on")); return; }
    if (c == "extpop") {
      const std::string id = d->GetString("id").ToString();
      if (id == pop_id_) { ClosePop(); return; }
      const double r = d->HasKey("r") ? d->GetDouble("r") : menu_r_;
      OpenPop(id, r);
      return;
    }
    if (c == "extmenu") {
      menu_r_ = d->GetDouble("r");
      menu_open_ && menu_mode_ == "ext" ? CloseMenu() : OpenMenu("ext");
      return;
    }
    if (c == "winmin") { window_->Minimize(); return; }
    if (c == "winmax") { ToggleMax(); return; }
    if (c == "winclose") { window_->Close(); return; }
    if (c == "menuready") { MenuState(); return; }
    if (c == "menusize") {
      menu_w_ = d->GetInt("w");
      menu_h_ = d->GetInt("h");
      if (menu_open_) PlaceMenu();
      return;
    }
    if (c == "menu") { menu_open_ && menu_mode_ == "main" ? CloseMenu() : OpenMenu("main"); return; }
    if (c == "menuclose") { CloseMenu(); return; }
    if (c == "fold") { SetFold(!fold_); return; }
    if (c == "tab") { Activate(d->GetInt("id")); return; }
    if (c == "close") { CloseTab(d->GetInt("id")); return; }
    if (c == "focuspage") { if (b) b->GetHost()->SetFocus(true); return; }
    if (c == "zoomin" || c == "zoomout") {
      if (b) b->GetHost()->Zoom(c == "zoomin" ? CEF_ZOOM_COMMAND_IN : CEF_ZOOM_COMMAND_OUT);
      CefRefPtr<Win> self(this);
      Post([self] { self->MenuState(); }, 120);
      return;
    }

    // Everything below closes the menu first.
    CloseMenu();
    if (c == "newtab") NewTab(StartUrl(), true);
    else if (c == "newwindow") OpenWindow(false, StartUrl());
    else if (c == "private") OpenWindow(true, StartUrl());
    else if (c == "history") NewTab("chrome://history", true);
    else if (c == "bookmarks") NewTab("chrome://bookmarks", true);
    else if (c == "downloads") NewTab("chrome://downloads", true);
    else if (c == "settings") NewTab("chrome://settings", true);
    else if (c == "extmanage") NewTab("chrome://extensions", true);
    else if (c == "extget") NewTab("https://chromewebstore.google.com/category/extensions", true);
    else if (!b) return;
    else if (c == "back") b->GoBack();
    else if (c == "fwd") b->GoForward();
    else if (c == "reload") b->Reload();
    else if (c == "stop") b->StopLoad();
    else if (c == "go") {
      const std::string u = d->GetString("url").ToString();
      if (!u.empty()) b->GetMainFrame()->LoadURL(u);
      b->GetHost()->SetFocus(true);
    } else if (c == "find") {
#ifdef IDC_FIND
      b->GetHost()->ExecuteChromeCommand(IDC_FIND, CEF_WOD_CURRENT_TAB);
#endif
    }
  }

  // Keys we own. Returns true when handled.
  bool Key(const CefKeyEvent& e) {
    if (e.type != KEYEVENT_RAWKEYDOWN || !window_) return false;
    const bool ctrl = e.modifiers & EVENTFLAG_CONTROL_DOWN;
    const bool shift = e.modifiers & EVENTFLAG_SHIFT_DOWN;
    const bool alt = e.modifiers & EVENTFLAG_ALT_DOWN;
    const int k = e.windows_key_code;
    CefRefPtr<Win> self(this);
    auto run = [self](std::function<void(Win*)> f) {
      Post([self, f] { f(self.get()); });
      return true;
    };
    if (k == 0x1B && menu_open_) return run([](Win* w) { w->CloseMenu(); });
    if (k == 0x1B && pop_) return run([](Win* w) { w->ClosePop(); });
    if (alt && !ctrl && (k == 0x25 || k == 0x27))  // Alt+Left, Alt+Right
      return run([k](Win* w) {
        if (Tab* a = w->Find(w->active_))
          if (a->browser) k == 0x25 ? a->browser->GoBack() : a->browser->GoForward();
      });
    if (!ctrl || alt) return false;
    if (k == 'T' && !shift) return run([](Win* w) { w->NewTab(StartUrl(), true); });
    if (k == 'T' && shift) return true;  // reopen closed tab: not built yet
    if (k == 'N') return run([shift](Win* w) { OpenWindow(shift, StartUrl()); });
    if (k == 'W' && !shift) return run([](Win* w) { w->CloseTab(w->active_); });
    if (k == 'L') return run([](Win* w) { w->FocusAddress(); });
    if (k == 'H' && !shift) return run([](Win* w) { w->NewTab("chrome://history", true); });
    if (k == 'J' && !shift) return run([](Win* w) { w->NewTab("chrome://downloads", true); });
    if (k == 'D' && !shift) return run([](Win* w) { w->ToggleStar(); });
    if (k == 0x09) return run([shift](Win* w) { w->Cycle(shift ? -1 : 1); });
    if (k >= '1' && k <= '9' && !shift) {
      const int n = k - '1';
      return run([n](Win* w) {
        std::vector<int> open = w->OpenIds();
        if (open.empty()) return;
        w->Activate(n == 8 || n >= static_cast<int>(open.size()) ? open.back() : open[n]);
      });
    }
    return false;
  }

  // ---- bookmarks and the start page

  void ToggleStar() {
    Tab* a = Find(active_);
    if (!a || a->url.empty() || IsStart(a->url)) return;
    auto& v = g_store.bookmarks;
    if (Bookmarked(a->url))
      v.erase(std::remove_if(v.begin(), v.end(), [a](const Bookmark& b) { return b.u == a->url; }),
              v.end());
    else
      v.push_back({a->url, ShortTitle(a->title, a->url)});
    SaveStore();
    Broadcast();
  }

  // Commands from the start page, which is ours but runs in a tab.
  void StartCommand(CefRefPtr<CefBrowser> b, CefRefPtr<CefDictionaryValue> d) {
    Tab* t = TabFor(b);
    if (!t) return;
    const std::string c = d->GetString("c").ToString();
    const std::string u = d->GetString("u").ToString();
    auto& v = g_store.bookmarks;
    if (c == "startready") { PushStart(t); return; }
    if (c == "bmadd" && !u.empty() && !Bookmarked(u)) {
      v.push_back({u, HostName(u)});
    } else if (c == "bmdel") {
      v.erase(std::remove_if(v.begin(), v.end(), [&u](const Bookmark& x) { return x.u == u; }),
              v.end());
    } else {
      return;
    }
    SaveStore();
    Broadcast();
  }

  void PushStart(Tab* t) {
    if (t && t->browser && IsStart(t->url))
      Js(t->browser, "window.ob&&ob.set({\"bm\":" + BookmarksJson() + "})");
  }

  // Bookmarks and shown extensions are shared by every window.
  static void Broadcast() {
    for (Win* w : g_wins) {
      w->PushState();
      for (auto& t : w->tabs_) w->PushStart(t.get());
      if (w->menu_open_ && w->menu_mode_ == "ext") w->MenuState();
    }
  }

  // ---- extensions

  // Extensions are read again every few seconds, so one installed from the
  // Chrome Web Store, or removed in Manage extensions, shows up by itself.
  void ScanTick() {
    if (!window_) return;
    std::vector<Ext> e = ScanExtensions();
    std::string sig;
    for (auto& x : e) sig += x.id + "\t" + x.name + "\t" + x.popup + "\t" + x.icon + "\n";
    if (sig != exts_sig_) {
      exts_sig_ = sig;
      exts_ = std::move(e);
      PushState();
      if (menu_open_ && menu_mode_ == "ext") MenuState();
      if (!pop_id_.empty() && !FindExt(pop_id_)) ClosePop();
    }
    CefRefPtr<Win> self(this);
    Post([self] { self->ScanTick(); }, 4000);
  }

  const Ext* FindExt(const std::string& id) {
    for (auto& e : exts_)
      if (e.id == id) return &e;
    return nullptr;
  }

  // Show or hide an extension in the address bar. Up to two are shown; a
  // third pushes out the one shown longest.
  void ShowExt(const std::string& id, bool on) {
    if (!FindExt(id)) return;
    std::vector<std::string> v = ShownIds(exts_);
    v.erase(std::remove(v.begin(), v.end(), id), v.end());
    if (on) v.push_back(id);
    while (v.size() > 2) v.erase(v.begin());
    g_store.shown = v;
    g_store.shown_set = true;
    SaveStore();
    Broadcast();
  }

  // An extension's pop-over is its own Chrome-style page, laid over the top of
  // the page box under the address bar, as Chrome lays it under its toolbar.
  void OpenPop(const std::string& id, double right) {
    const Ext* e = FindExt(id);
    if (!e || e->popup.empty() || !window_ || private_) return;
    if (pop_pending_) return;  // one is on its way
    ClosePop();
    CloseMenu(false);
    pop_id_ = id;
    pop_r_ = right;
    pop_w_ = kPopW;
    pop_h_ = kPopH;
    pop_pending_ = true;
    pop_opened_at_ = NowMs();
    CefWindowInfo info;
    info.SetAsChild(window_->GetWindowHandle(), PopRectPx());
    info.runtime_style = CEF_RUNTIME_STYLE_CHROME;
    CefBrowserHost::CreateBrowser(info, pop_client_, "chrome-extension://" + id + "/" + e->popup,
                                  CefBrowserSettings(), nullptr, context_);
    Log("pop-over for " + e->name);
    PushState();
  }

  void PopCreated(CefRefPtr<CefBrowser> b) {
    pop_pending_ = false;
    if (pop_id_.empty() || !window_) { b->GetHost()->CloseBrowser(true); return; }
    pop_ = b;
    PlacePop();
    // Keyboard focus stays in our row until the pop-over is clicked, so a
    // click on the page (which takes focus) closes it.
    if (ui_view_ && ui_view_->GetBrowser()) ui_view_->GetBrowser()->GetHost()->SetFocus(true);
  }

  void PopClosed(CefRefPtr<CefBrowser> b) {
    if (pop_ && pop_->IsSame(b)) {
      pop_ = nullptr;
      pop_id_.clear();
      PushState();
    }
  }

  void PopSize(CefRefPtr<CefBrowser> b, int w, int h) {
    if (!pop_ || !pop_->IsSame(b)) return;
    w = std::clamp(w, 200, 800);
    h = std::clamp(h, 60, 600);
    if (w == pop_w_ && h == pop_h_) return;
    pop_w_ = w;
    pop_h_ = h;
    PlacePop();
  }

  void ClosePop() {
    if (pop_id_.empty() && !pop_) return;
    if (pop_) pop_->GetHost()->CloseBrowser(true);
    pop_ = nullptr;
    pop_id_.clear();
    PushState();
  }

  void PageFocused(CefRefPtr<CefBrowser> b) {
    if (TabFor(b) && !pop_id_.empty() && NowMs() - pop_opened_at_ > 400) ClosePop();
  }

 private:
  static constexpr int kPopW = 380, kPopH = 520;

  CefRect PopRectPx() {
    const float s = Scale();
    const int ww = ui_view_ ? ui_view_->GetBounds().width : 1280;
    int x = static_cast<int>(std::lround(pop_r_)) - pop_w_;
    x = std::max(8, std::min(x, ww - pop_w_ - 8));
    return CefRect(static_cast<int>(std::lround(x * s)),
                   static_cast<int>(std::lround((UiHeight() + 4) * s)),
                   static_cast<int>(std::lround(pop_w_ * s)),
                   static_cast<int>(std::lround(pop_h_ * s)));
  }

  void PlacePop() {
    if (!pop_ || !window_) return;
    const unsigned long x = pop_->GetHost()->GetWindowHandle();
    const CefRect r = PopRectPx();
    x11::EnsureParent(x, window_->GetWindowHandle());
    x11::Place(x, r.x, r.y, r.width, r.height);
    x11::Show(x, true);
    pop_->GetHost()->NotifyScreenInfoChanged();
    pop_->GetHost()->WasResized();
    x11::Round(x, r.width, r.height, static_cast<int>(std::lround(kRadius * Scale())));
  }

  Tab* Find(int id) {
    for (auto& t : tabs_)
      if (t->id == id) return t.get();
    return nullptr;
  }

  std::vector<int> OpenIds() {
    std::vector<int> v;
    for (auto& t : tabs_)
      if (!t->closing && t->browser) v.push_back(t->id);
    return v;
  }

  void ActivateNeighbour(int id) {
    std::vector<int> all;
    for (auto& t : tabs_) all.push_back(t->id);
    auto it = std::find(all.begin(), all.end(), id);
    active_ = 0;
    if (it == all.end()) return;
    auto ok = [this](int x) { Tab* t = Find(x); return t && !t->closing && t->browser; };
    for (auto r = it + 1; r != all.end(); ++r)
      if (ok(*r)) { Activate(*r); return; }
    for (auto l = it; l != all.begin();) {
      --l;
      if (ok(*l)) { Activate(*l); return; }
    }
  }

  void Cycle(int dir) {
    std::vector<int> open = OpenIds();
    if (open.size() < 2) return;
    auto it = std::find(open.begin(), open.end(), active_);
    int i = it == open.end() ? 0 : static_cast<int>(it - open.begin());
    i = (i + dir + static_cast<int>(open.size())) % static_cast<int>(open.size());
    Activate(open[i]);
  }

  // True when the window fills the screen's work area, whether or not the
  // desktop calls it maximised. A desktop can hand the full-screen size back
  // as the window's normal size, which leaves nothing smaller to return to.
  bool FillsWorkArea() {
    auto d = window_ ? window_->GetDisplay() : nullptr;
    if (!d) return false;
    const CefRect a = d->GetWorkArea(), b = window_->GetBounds();
    return b.width >= a.width - 4 && b.height >= a.height - 4;
  }
  bool LooksMaximized() { return window_ && (window_->IsMaximized() || FillsWorkArea()); }

  // A comfortable size in the middle of the screen, for when there is no
  // smaller size to restore to.
  void RestoreToDefaultSize() {
    auto d = window_ ? window_->GetDisplay() : nullptr;
    if (!d) return;
    const CefRect a = d->GetWorkArea();
    const int w = std::min(1280, a.width * 3 / 4), h = std::min(860, a.height * 4 / 5);
    window_->SetBounds(CefRect(a.x + (a.width - w) / 2, a.y + (a.height - h) / 2, w, h));
  }

  void ToggleMax() {
    if (window_->IsMaximized()) {
      window_->Restore();
      // If the size it restores to is the whole screen again, pick one.
      CefRefPtr<Win> self = this;
      Post([self] { if (self->window_ && self->FillsWorkArea() && !self->window_->IsMaximized())
                      self->RestoreToDefaultSize(); }, 250);
    } else if (FillsWorkArea()) {
      RestoreToDefaultSize();
    } else {
      window_->Maximize();
    }
  }

  float Scale() {
    auto d = window_ ? window_->GetDisplay() : nullptr;
    return d ? d->GetDeviceScaleFactor() : 1.0f;
  }

  int UiHeight() const { return fullscreen_ ? 0 : kNavH + (fold_ ? kStripH : 0); }

  // The page box in pixels, relative to the main window: the window less the
  // row (and strip) above and the 8 px margin on the other sides. Worked out
  // from the main window's own size, which is always current.
  CefRect PageRectPx() {
    const float s = Scale();
    int ww = 0, wh = 0;
    if (!window_ || !x11::Size(window_->GetWindowHandle(), &ww, &wh) || ww < 2 || wh < 2) {
      const CefRect b = window_ ? window_->GetClientAreaBoundsInScreen() : CefRect(0, 0, 1280, 860);
      ww = static_cast<int>(std::lround(b.width * s));
      wh = static_cast<int>(std::lround(b.height * s));
    }
    const int i = static_cast<int>(std::lround((fullscreen_ ? 0 : kInset) * s));
    const int top = static_cast<int>(std::lround(UiHeight() * s));
    return CefRect(i, top, std::max(ww - 2 * i, 1), std::max(wh - top - i, 1));
  }

  void PlaceActive() {
    Tab* t = Find(active_);
    if (!t || !t->browser) return;
    const CefRect r = PageRectPx();
    if (x11::EnsureParent(t->xid(), window_->GetWindowHandle()))
      Log("tab " + std::to_string(t->id) + " was not inside the main window; moved it in");
    x11::Place(t->xid(), r.x, r.y, r.width, r.height);
    Log("place tab " + std::to_string(t->id) + " at " + std::to_string(r.x) + "," +
        std::to_string(r.y) + " size " + std::to_string(r.width) + "x" + std::to_string(r.height));
    auto host = t->browser->GetHost();
    host->NotifyScreenInfoChanged();
    host->WasResized();
    const int radius = Flag("square-corners") || fullscreen_
                           ? 0 : static_cast<int>(std::lround(kRadius * Scale()));
    x11::Round(t->xid(), r.width, r.height, radius);
  }

  void SetFold(bool open) {
    fold_ = open;
    ui_delegate_->height = UiHeight();
    ui_view_->InvalidateLayout();
    window_->Layout();
    PlaceActive();
    PushState();
  }

  CefRect MenuRectPx() {
    const float s = Scale();
    const int w = menu_w_ > 0 ? menu_w_ : kMenuW, h = menu_h_ > 0 ? menu_h_ : kMenuH;
    const int ww = ui_view_->GetBounds().width;
    // The main menu sits under ⋯ at the right; the extensions list under its
    // button in the address bar.
    int x = ww - 10 - w;
    if (menu_mode_ == "ext") x = std::max(8, std::min(static_cast<int>(std::lround(menu_r_)) + 6 - w, ww - 8 - w));
    return CefRect(static_cast<int>(std::lround(x * s)),
                   static_cast<int>(std::lround((UiHeight() + 2) * s)),
                   static_cast<int>(std::lround(w * s)), static_cast<int>(std::lround(h * s)));
  }

  void PlaceMenu() {
    if (!menu_) return;
    const unsigned long x = menu_->GetHost()->GetWindowHandle();
    const CefRect r = MenuRectPx();
    x11::EnsureParent(x, window_->GetWindowHandle());
    x11::Place(x, r.x, r.y, r.width, r.height);
    x11::Show(x, true);
    menu_->GetHost()->SetFocus(true);
  }

  void OpenMenu(const std::string& mode) {
    // A click on ⋯ while the menu is open first closes it (it loses focus),
    // so a second open within a moment is that same click.
    if (NowMs() - menu_closed_at_ < 300 && mode == menu_closed_mode_) return;
    ClosePop();
    if (menu_mode_ != mode) { menu_w_ = 0; menu_h_ = 0; }
    menu_mode_ = mode;
    menu_open_ = true;
    if (!menu_) {
      if (!menu_requested_) {
        menu_requested_ = true;
        CefWindowInfo info;
        info.SetAsChild(window_->GetWindowHandle(), MenuRectPx());
        info.runtime_style = CEF_RUNTIME_STYLE_ALLOY;
        CefBrowserSettings ms;
        ms.background_color = kMenu;
        CefBrowserHost::CreateBrowser(info, ui_client_, UiUrl("menu.html"), ms, nullptr,
                                      nullptr);
      }
    } else {
      MenuState();  // the menu reports its new size, and is placed then
      PlaceMenu();
    }
    PushState();
  }

  void CloseMenu(bool refocus = true) {
    if (!menu_open_) return;
    menu_open_ = false;
    menu_closed_at_ = NowMs();
    menu_closed_mode_ = menu_mode_;
    if (menu_) x11::Show(menu_->GetHost()->GetWindowHandle(), false);
    if (refocus)
      if (Tab* a = Find(active_))
        if (a->browser) a->browser->GetHost()->SetFocus(true);
    PushState();
  }

  void MenuState() {
    int pct = 100;
    if (Tab* a = Find(active_))
      if (a->browser)
        pct = static_cast<int>(std::lround(std::pow(1.2, a->browser->GetHost()->GetZoomLevel()) * 100));
    Js(menu_, "window.ob&&ob.set({\"zoom\":" + std::to_string(pct) + ",\"mode\":" + Q(menu_mode_) +
                  ",\"exts\":" + ExtsJson(exts_) + "})");
  }

  void FocusAddress() {
    if (!ui_view_) return;
    ui_view_->RequestFocus();
    Js(ui_view_->GetBrowser(), "window.ob&&ob.focusAddress()");
  }

  static void Js(CefRefPtr<CefBrowser> b, const std::string& code) {
    if (b) b->GetMainFrame()->ExecuteJavaScript(code, "", 0);
  }

  bool private_;
  std::string first_;
  CefRefPtr<CefRequestContext> context_;
  CefRefPtr<PageClient> page_client_;
  CefRefPtr<UiClient> ui_client_;
  CefRefPtr<PopClient> pop_client_;
  CefRefPtr<UiDelegate> ui_delegate_;
  CefRefPtr<CefWindow> window_;
  CefRefPtr<CefBrowserView> ui_view_;
  CefRefPtr<CefBrowser> menu_;
  CefRefPtr<CefPanel> content_;
  std::vector<std::unique_ptr<Tab>> tabs_;
  struct Pending { int id; bool activate; };
  std::deque<Pending> pending_;
  int next_id_ = 0, active_ = 0;
  int menu_w_ = 0, menu_h_ = 0;
  int64_t menu_closed_at_ = 0;
  bool fold_ = false, menu_open_ = false, menu_requested_ = false;
  bool fullscreen_ = false, closing_all_ = false, maximized_ = false;
  ButtonSides sides_;
  std::string menu_mode_ = "main", menu_closed_mode_;
  double menu_r_ = 0;  // right edge of the extensions button, for its list
  std::vector<Ext> exts_;
  std::string exts_sig_;
  CefRefPtr<CefBrowser> pop_;
  std::string pop_id_;
  double pop_r_ = 0;
  int pop_w_ = kPopW, pop_h_ = kPopH;
  bool pop_pending_ = false;
  int64_t pop_opened_at_ = 0;
  IMPLEMENT_REFCOUNTING(Win);
};

// ------------------------------------------------------- client callbacks

void PageClient::OnAfterCreated(CefRefPtr<CefBrowser> b) {
  ++g_browsers;
  if (win_ && !b->IsPopup()) win_->TabCreated(b);
}
void PageClient::OnBeforeClose(CefRefPtr<CefBrowser> b) {
  if (win_) win_->PageClosed(b);
  --g_browsers;
  MaybeQuit();
}
bool PageClient::OnBeforePopup(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, int,
                               const CefString& target_url, const CefString&,
                               WindowOpenDisposition d, bool, const CefPopupFeatures&,
                               CefWindowInfo&, CefRefPtr<CefClient>&,
                               CefBrowserSettings&, CefRefPtr<CefDictionaryValue>&,
                               bool*) {
  // Links that open a tab or window become our tabs. Real pop-ups (sign-in
  // windows and the like) open as CEF makes them, so their opener still works.
  if (!win_) return false;
  const std::string url = target_url.ToString();
  CefRefPtr<Win> w(win_);
  switch (d) {
    case CEF_WOD_NEW_FOREGROUND_TAB:
    case CEF_WOD_NEW_BACKGROUND_TAB:
    case CEF_WOD_NEW_WINDOW:
    case CEF_WOD_SINGLETON_TAB:
    case CEF_WOD_SWITCH_TO_TAB: {
      const bool fg = d != CEF_WOD_NEW_BACKGROUND_TAB;
      Post([w, url, fg] { w->NewTab(url, fg); });
      return true;
    }
    case CEF_WOD_OFF_THE_RECORD:
      Post([url] { Win::OpenWindow(true, url); });
      return true;
    default:
      return false;
  }
}
void PageClient::OnAddressChange(CefRefPtr<CefBrowser> b, CefRefPtr<CefFrame> f,
                                 const CefString& url) {
  if (!win_ || !f->IsMain()) return;
  if (Tab* t = win_->TabFor(b)) {
    if (!t->first_url.empty()) return;  // still the blank starting page
    t->url = url.ToString();
    win_->PushState();
  }
}
void PageClient::OnTitleChange(CefRefPtr<CefBrowser> b, const CefString& title) {
  if (!win_) return;
  if (Tab* t = win_->TabFor(b)) {
    if (!t->first_url.empty()) return;
    t->title = title.ToString();
    win_->PushState();
  }
}
void PageClient::OnFaviconURLChange(CefRefPtr<CefBrowser> b,
                                    const std::vector<CefString>& urls) {
  if (!win_) return;
  if (Tab* t = win_->TabFor(b)) {
    t->fav = urls.empty() ? "" : urls[0].ToString();
    win_->PushState();
  }
}
void PageClient::OnFullscreenModeChange(CefRefPtr<CefBrowser> b, bool fs) {
  if (!win_) return;
  CefRefPtr<Win> w(win_);
  Post([w, b, fs] { w->Fullscreen(b, fs); });
}
void PageClient::OnLoadingStateChange(CefRefPtr<CefBrowser> b, bool loading,
                                      bool back, bool fwd) {
  if (!win_) return;
  if (Tab* t = win_->TabFor(b)) {
    t->loading = loading; t->back = back; t->fwd = fwd;
    win_->PushState();
    if (!loading) win_->PushStart(t);
  }
}
bool PageClient::OnPreKeyEvent(CefRefPtr<CefBrowser> b, const CefKeyEvent& e,
                               CefEventHandle, bool*) {
  return win_ && win_->TabFor(b) ? win_->Key(e) : false;
}

void PageClient::OnGotFocus(CefRefPtr<CefBrowser> b) {
  if (!win_) return;
  CefRefPtr<Win> w(win_);
  Post([w, b] { w->PageFocused(b); });
}
// Only the start page, which is ours, may send commands, and only these.
bool PageClient::OnConsoleMessage(CefRefPtr<CefBrowser> b, cef_log_severity_t,
                                  const CefString& message, const CefString& source, int) {
  const std::string m = message.ToString();
  if (m.rfind("ob:", 0) != 0 || !win_ || !IsStart(source.ToString())) return false;
  auto v = CefParseJSON(m.substr(3), JSON_PARSER_RFC);
  if (!v || v->GetType() != VTYPE_DICTIONARY) return true;
  CefRefPtr<CefDictionaryValue> d = v->GetDictionary()->Copy(false);
  CefRefPtr<Win> w(win_);
  Post([w, b, d] { w->StartCommand(b, d); });
  return true;
}

void PopClient::OnAfterCreated(CefRefPtr<CefBrowser> b) {
  ++g_browsers;
  if (win_) win_->PopCreated(b);
  else b->GetHost()->CloseBrowser(true);
}
void PopClient::OnBeforeClose(CefRefPtr<CefBrowser> b) {
  if (win_) win_->PopClosed(b);
  --g_browsers;
  MaybeQuit();
}
// Links from a pop-over open as tabs in the window, and the pop-over closes.
bool PopClient::OnBeforePopup(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, int,
                              const CefString& target_url, const CefString&,
                              WindowOpenDisposition d, bool, const CefPopupFeatures&,
                              CefWindowInfo&, CefRefPtr<CefClient>&,
                              CefBrowserSettings&, CefRefPtr<CefDictionaryValue>&,
                              bool*) {
  if (!win_) return false;
  switch (d) {
    case CEF_WOD_NEW_FOREGROUND_TAB:
    case CEF_WOD_NEW_BACKGROUND_TAB:
    case CEF_WOD_NEW_WINDOW:
    case CEF_WOD_SINGLETON_TAB:
    case CEF_WOD_SWITCH_TO_TAB: {
      const std::string url = target_url.ToString();
      CefRefPtr<Win> w(win_);
      Post([w, url] { w->ClosePop(); w->NewTab(url, true); });
      return true;
    }
    default:
      return false;
  }
}
bool PopClient::OnConsoleMessage(CefRefPtr<CefBrowser> b, cef_log_severity_t,
                                 const CefString& message, const CefString&, int) {
  const std::string m = message.ToString();
  if (m.rfind("obpop:", 0) != 0 || !win_) return false;
  int w = 0, h = 0;
  if (sscanf(m.c_str() + 6, "%dx%d", &w, &h) == 2 && w > 0 && h > 0) {
    CefRefPtr<Win> win(win_);
    Post([win, b, w, h] { win->PopSize(b, w, h); });
  }
  return true;
}
void PopClient::OnLoadingStateChange(CefRefPtr<CefBrowser> b, bool loading, bool, bool) {
  if (!loading) b->GetMainFrame()->ExecuteJavaScript(kMeasure, "", 0);
}
bool PopClient::OnPreKeyEvent(CefRefPtr<CefBrowser>, const CefKeyEvent& e,
                              CefEventHandle, bool*) {
  if (e.type == KEYEVENT_RAWKEYDOWN && e.windows_key_code == 0x1B && win_) {
    CefRefPtr<Win> w(win_);
    Post([w] { w->ClosePop(); });
    return true;
  }
  return false;
}

void UiClient::OnAfterCreated(CefRefPtr<CefBrowser> b) {
  ++g_browsers;
  if (win_) win_->UiCreated(b);
}
void UiClient::OnBeforeClose(CefRefPtr<CefBrowser> b) {
  if (win_) win_->UiClosed(b);
  --g_browsers;
  MaybeQuit();
}
bool UiClient::OnConsoleMessage(CefRefPtr<CefBrowser>, cef_log_severity_t,
                                const CefString& message, const CefString& source,
                                int) {
  const std::string m = message.ToString();
  if (m.rfind("ob:", 0) != 0) return false;
  if (!win_ || source.ToString().rfind("file://" + g_ui + "/", 0) != 0) return true;
  auto v = CefParseJSON(m.substr(3), JSON_PARSER_RFC);
  if (!v || v->GetType() != VTYPE_DICTIONARY) return true;
  CefRefPtr<CefDictionaryValue> d = v->GetDictionary()->Copy(false);
  CefRefPtr<Win> w(win_);
  Post([w, d] { w->Command(d); });
  return true;
}
void UiClient::OnDraggableRegionsChanged(CefRefPtr<CefBrowser> b, CefRefPtr<CefFrame>,
                                         const std::vector<CefDraggableRegion>& regions) {
  if (win_) win_->DragRegions(b, regions);
}
bool UiClient::OnPreKeyEvent(CefRefPtr<CefBrowser>, const CefKeyEvent& e,
                             CefEventHandle, bool*) {
  return win_ ? win_->Key(e) : false;
}

// ---------------------------------------------------------------- app

std::string ExeDir() {
  char buf[PATH_MAX];
  ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
  if (n <= 0) return ".";
  std::string p(buf, static_cast<size_t>(n));
  return p.substr(0, p.find_last_of('/'));
}

class App : public CefApp, public CefBrowserProcessHandler {
 public:
  CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override { return this; }

  // Tabs are embedded as X11 child windows, which Wayland does not offer, so
  // the browser runs on X11 (XWayland on a Wayland desktop).
  void OnBeforeCommandLineProcessing(const CefString& process_type,
                                     CefRefPtr<CefCommandLine> cl) override {
    if (process_type.empty() && !cl->HasSwitch("ozone-platform"))
      cl->AppendSwitchWithValue("ozone-platform", "x11");
    // Any window Chromium opens on its own carries the same name.
    if (process_type.empty() && !cl->HasSwitch("class"))
      cl->AppendSwitchWithValue("class", "output-browser");
  }

  void OnContextInitialized() override {
    auto cl = CefCommandLine::GetGlobalCommandLine();
    g_ui = cl->GetSwitchValue("ui-dir").ToString();
    if (g_ui.empty()) g_ui = ExeDir() + "/ui";
    std::string url = cl->GetSwitchValue("url").ToString();
    if (url.empty()) {
      std::vector<CefString> args;
      cl->GetArguments(args);
      if (!args.empty()) url = args[0].ToString();
    }
    if (url.empty()) url = StartUrl();
    Win::OpenWindow(false, url);
  }
 private:
  IMPLEMENT_REFCOUNTING(App);
};

}  // namespace

// CEF starts its helper processes from main, so main must not use a stack
// protector.
NO_STACK_PROTECTOR
int main(int argc, char* argv[]) {
  CefMainArgs args(argc, argv);
  CefRefPtr<App> app(new App);
  int code = CefExecuteProcess(args, app.get(), nullptr);
  if (code >= 0) return code;

  auto cl = CefCommandLine::CreateCommandLine();
  cl->InitFromArgv(argc, argv);
  CefSettings settings;
  settings.no_sandbox = true;
  std::string root = cl->GetSwitchValue("profile-dir").ToString();
  if (root.empty()) {
    const char* home = getenv("HOME");
    root = std::string(home ? home : ".") + "/.local/share/output-browser";
  }
  g_root = root;
  LoadStore();
  CefString(&settings.root_cache_path).FromString(root);
  CefString(&settings.cache_path).FromString(root + "/default");
  if (!CefInitialize(args, settings, app.get(), nullptr)) return CefGetExitCode();
  CefRunMessageLoop();
  CefShutdown();
  return 0;
}
