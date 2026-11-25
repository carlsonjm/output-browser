// Output Browser.
//
// Pages are CEF Chrome-style browsers, so they behave as Chrome does. The row,
// the tab strip, the menu and the start page are our own HTML, drawn in small
// Alloy-style browsers around the page.
//
// CEF allows one Chrome-style browser in a window, and only while that window
// has no other browser yet. So every tab is created in its own hidden holder
// window, and the tab that is showing is moved into the main window. Moving a
// browser that already exists between windows is supported.
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <climits>
#include <cmath>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "include/base/cef_compiler_specific.h"
#include "include/cef_app.h"
#include "include/cef_browser.h"
#include "include/cef_command_ids.h"
#include "include/cef_command_line.h"
#include "include/cef_image.h"
#include "include/cef_parser.h"
#include "include/cef_request_context.h"
#include "include/cef_task.h"
#include "include/views/cef_box_layout.h"
#include "include/views/cef_browser_view.h"
#include "include/views/cef_browser_view_delegate.h"
#include "include/views/cef_fill_layout.h"
#include "include/views/cef_label_button.h"
#include "include/views/cef_overlay_controller.h"
#include "include/views/cef_panel.h"
#include "include/views/cef_window.h"
#include "include/views/cef_window_delegate.h"

namespace {

constexpr cef_color_t kChrome = 0xFF1C1C1C;  // window chrome
constexpr cef_color_t kMenu = 0xFF1A1A1A;    // menu surface
constexpr int kNavH = 44;    // the row
constexpr int kStripH = 48;  // the tab strip, 40 plus its 8 below
constexpr int kInset = 8;    // page box margin: 0 8 8
constexpr int kRadius = 10;  // page box corners

std::string g_ui;  // folder holding bar.html, menu.html and start.html
int g_browsers = 0;
int g_windows = 0;

std::string UiUrl(const std::string& f) { return "file://" + g_ui + "/" + f; }
std::string StartUrl() { return UiUrl("start.html"); }
bool IsStart(const std::string& u) { return u.rfind(StartUrl(), 0) == 0; }

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
// Runs |f| on the UI thread after the current callback returns. Views are
// never moved from inside a CEF callback.
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

// ---------------------------------------------------------------- delegates

class PageDelegate : public CefBrowserViewDelegate {
 public:
  ChromeToolbarType GetChromeToolbarType(CefRefPtr<CefBrowserView>) override {
    return CEF_CTT_NONE;  // the row is ours
  }
  cef_runtime_style_t GetBrowserRuntimeStyle() override { return CEF_RUNTIME_STYLE_CHROME; }
 private:
  IMPLEMENT_REFCOUNTING(PageDelegate);
};

class UiDelegate : public CefBrowserViewDelegate {
 public:
  int height = kNavH;
  cef_runtime_style_t GetBrowserRuntimeStyle() override { return CEF_RUNTIME_STYLE_ALLOY; }
  CefSize GetPreferredSize(CefRefPtr<CefView>) override { return CefSize(0, height); }
 private:
  IMPLEMENT_REFCOUNTING(UiDelegate);
};

class Inert : public CefButtonDelegate {
 public:
  void OnButtonPressed(CefRefPtr<CefButton>) override {}
 private:
  IMPLEMENT_REFCOUNTING(Inert);
};

// A hidden window that holds one tab while it is not showing, and is where
// the tab's browser is created.
class HolderWin : public CefWindowDelegate {
 public:
  HolderWin(CefRefPtr<CefBrowserView> v, CefSize size) : view_(v), size_(size) {}
  void OnWindowCreated(CefRefPtr<CefWindow> w) override {
    w->SetToFillLayout();
    w->AddChildView(view_);
  }
  void OnWindowDestroyed(CefRefPtr<CefWindow>) override { view_ = nullptr; }
  bool IsFrameless(CefRefPtr<CefWindow>) override { return true; }
  CefRect GetInitialBounds(CefRefPtr<CefWindow>) override {
    return CefRect(0, 0, size_.width, size_.height);
  }
  bool CanClose(CefRefPtr<CefWindow>) override {
    auto b = view_ ? view_->GetBrowser() : nullptr;
    return b ? b->GetHost()->TryCloseBrowser() : true;
  }
 private:
  CefRefPtr<CefBrowserView> view_;
  CefSize size_;
  IMPLEMENT_REFCOUNTING(HolderWin);
};

// Round page corners: four small images, drawn over the page in the chrome
// colour outside a 10 px arc. corner: 0 top left, 1 top right, 2 bottom left,
// 3 bottom right.
CefRefPtr<CefImage> CornerImage(int corner) {
  auto img = CefImage::CreateImage();
  for (int scale : {1, 2, 3}) {
    const int n = kRadius * scale;
    std::vector<uint8_t> px(static_cast<size_t>(n) * n * 4);
    for (int y = 0; y < n; ++y) {
      for (int x = 0; x < n; ++x) {
        int out = 0;
        for (int sy = 0; sy < 4; ++sy) {
          for (int sx = 0; sx < 4; ++sx) {
            double dx = (x + (sx + 0.5) / 4.0) / scale;
            double dy = (y + (sy + 0.5) / 4.0) / scale;
            if (corner == 1 || corner == 3) dx = kRadius - dx;
            if (corner == 2 || corner == 3) dy = kRadius - dy;
            if (std::hypot(kRadius - dx, kRadius - dy) > kRadius) ++out;
          }
        }
        const int a = out * 255 / 16;
        const uint8_t c = static_cast<uint8_t>(0x1C * a / 255);
        uint8_t* p = &px[(static_cast<size_t>(y) * n + x) * 4];
        p[0] = c; p[1] = c; p[2] = c; p[3] = static_cast<uint8_t>(a);
      }
    }
    img->AddBitmap(static_cast<float>(scale), n, n, CEF_COLOR_TYPE_BGRA_8888,
                   CEF_ALPHA_TYPE_PREMULTIPLIED, px.data(), px.size());
  }
  return img;
}

// ------------------------------------------------------------------ window

class Win;

struct Tab {
  int id = 0;
  int browser_id = 0;
  CefRefPtr<CefBrowserView> view;
  CefRefPtr<CefWindow> holder;
  std::string url, title, fav;
  bool loading = false, back = false, fwd = false, closing = false;
};

class PageClient : public CefClient, public CefLifeSpanHandler,
                   public CefDisplayHandler, public CefLoadHandler,
                   public CefKeyboardHandler, public CefDownloadHandler {
 public:
  explicit PageClient(Win* w) : win_(w) {}
  void Detach() { win_ = nullptr; }
  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
  CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }
  CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }
  CefRefPtr<CefKeyboardHandler> GetKeyboardHandler() override { return this; }
  CefRefPtr<CefDownloadHandler> GetDownloadHandler() override { return this; }

  void OnAfterCreated(CefRefPtr<CefBrowser> b) override;
  bool DoClose(CefRefPtr<CefBrowser> b) override;
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
  bool OnPreKeyEvent(CefRefPtr<CefBrowser>, const CefKeyEvent& e,
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
                 public CefDisplayHandler, public CefKeyboardHandler {
 public:
  explicit UiClient(Win* w) : win_(w) {}
  void Detach() { win_ = nullptr; }
  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
  CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }
  CefRefPtr<CefKeyboardHandler> GetKeyboardHandler() override { return this; }
  void OnAfterCreated(CefRefPtr<CefBrowser>) override { ++g_browsers; }
  void OnBeforeClose(CefRefPtr<CefBrowser>) override { --g_browsers; MaybeQuit(); }
  bool OnConsoleMessage(CefRefPtr<CefBrowser>, cef_log_severity_t,
                        const CefString& message, const CefString& source,
                        int) override;
  bool OnPreKeyEvent(CefRefPtr<CefBrowser>, const CefKeyEvent& e,
                     CefEventHandle, bool*) override;
 private:
  Win* win_;
  IMPLEMENT_REFCOUNTING(UiClient);
};

class Win : public CefWindowDelegate {
 public:
  Win(bool priv, std::string first) : private_(priv), first_(std::move(first)) {
    page_client_ = new PageClient(this);
    ui_client_ = new UiClient(this);
    ui_delegate_ = new UiDelegate;
    if (private_) {
      CefRequestContextSettings s;  // no cache path: kept in memory only
      context_ = CefRequestContext::CreateContext(s, nullptr);
    }
  }

  // ---- window

  void OnWindowCreated(CefRefPtr<CefWindow> w) override {
    window_ = w;
    ++g_windows;
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

    content_ = CefPanel::CreatePanel(this);
    content_->SetBackgroundColor(kChrome);
    content_layout_ = content_->SetToBoxLayout(ContentLayout());
    w->AddChildView(content_);
    layout->SetFlexForView(content_, 1);

    if (!CefCommandLine::GetGlobalCommandLine()->HasSwitch("square-corners")) {
      for (int i = 0; i < 4; ++i) {
        auto b = CefLabelButton::CreateLabelButton(new Inert, "");
        b->SetImage(CEF_BUTTON_STATE_NORMAL, CornerImage(i));
        b->SetInkDropEnabled(false);
        b->SetFocusable(false);
        b->SetInsets(CefInsets(0, 0, 0, 0));
        b->SetBackgroundColor(0);
        b->SetHorizontalAlignment(CEF_HORIZONTAL_ALIGNMENT_LEFT);
        corners_[i] = w->AddOverlayView(b, CEF_DOCKING_MODE_CUSTOM, false);
        corners_[i]->SetVisible(false);
      }
    }

    // The menu is its own small browser, laid over the page when open.
    CefBrowserSettings ms;
    ms.background_color = kMenu;
    auto md = new UiDelegate;
    md->height = 0;
    menu_view_ = CefBrowserView::CreateBrowserView(ui_client_, UiUrl("menu.html"), ms,
                                                   nullptr, nullptr, md);
    menu_ = w->AddOverlayView(menu_view_, CEF_DOCKING_MODE_CUSTOM, true);
    menu_->SetBounds(CefRect(0, 0, 1, 1));
    menu_->SetVisible(false);

    w->Show();
    NewTab(first_, true, false);
  }

  void OnWindowDestroyed(CefRefPtr<CefWindow>) override {
    page_client_->Detach();
    ui_client_->Detach();
    for (auto& c : corners_) c = nullptr;
    menu_ = nullptr;
    menu_view_ = nullptr;
    ui_view_ = nullptr;
    content_layout_ = nullptr;
    content_ = nullptr;
    window_ = nullptr;
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
    std::vector<int> ids = order_;
    for (int id : ids) CloseTab(id);
    if (!tabs_.empty()) return false;
    bool ok = true;
    for (auto v : {ui_view_, menu_view_}) {
      if (v && v->GetBrowser()) ok = v->GetBrowser()->GetHost()->TryCloseBrowser() && ok;
    }
    return ok;
  }

  void OnLayoutChanged(CefRefPtr<CefView> v, const CefRect& r) override {
    if (!content_ || !v || !v->IsSame(content_)) return;
    PlaceCorners(r);
    if (menu_open_) CloseMenu();
  }

  // ---- tabs

  void NewTab(const std::string& url, bool activate, bool after_active) {
    if (!window_ || closing_all_) return;
    auto t = std::make_unique<Tab>();
    t->id = ++next_id_;
    t->url = url;
    t->title = IsStart(url) ? "New tab" : url;
    t->view = CefBrowserView::CreateBrowserView(page_client_, url, CefBrowserSettings(),
                                                nullptr, context_, new PageDelegate);
    CefRect cb = content_->GetBounds();
    CefSize size(std::max(200, cb.width - 2 * kInset), std::max(200, cb.height - kInset));
    t->holder = CefWindow::CreateTopLevelWindow(new HolderWin(t->view, size));
    if (auto b = t->view->GetBrowser()) t->browser_id = b->GetIdentifier();
    const int id = t->id;
    auto pos = order_.end();
    if (after_active) {
      auto a = std::find(order_.begin(), order_.end(), active_);
      if (a != order_.end()) pos = a + 1;
    }
    order_.insert(pos, id);
    tabs_[id] = std::move(t);
    if (activate) Activate(id);
    else PushState();
  }

  void Activate(int id) {
    Tab* t = Find(id);
    if (!t || t->closing || !window_) return;
    if (active_ == id) { FocusPage(); return; }
    if (Tab* a = Find(active_)) MoveToHolder(a);
    t->holder->RemoveChildView(t->view);
    content_->AddChildView(t->view);
    content_layout_->SetFlexForView(t->view, 1);
    content_->Layout();
    active_ = id;
    FocusPage();
    PushState();
  }

  void CloseTab(int id) {
    Tab* t = Find(id);
    if (!t || t->closing) return;
    t->closing = true;
    if (active_ == id) {
      const int next = Neighbour(id);
      MoveToHolder(t);
      active_ = 0;
      if (next && !closing_all_) Activate(next);
      else if (!closing_all_ && window_) {
        CefRefPtr<CefWindow> w = window_;
        Post([w] { if (!w->IsClosed()) w->Close(); });  // last tab: close the window
      }
    }
    t->holder->Close();
    PushState();
  }

  // ---- from the page client

  Tab* TabFor(CefRefPtr<CefBrowser> b) {
    const int bid = b->GetIdentifier();
    for (auto& [id, t] : tabs_) {
      if (t->browser_id == bid) return t.get();
      if (!t->browser_id && t->view->GetBrowser() && t->view->GetBrowser()->IsSame(b)) {
        t->browser_id = bid;
        return t.get();
      }
    }
    return nullptr;
  }
  bool IsActive(Tab* t) { return t && t->id == active_; }

  // A page asked to close itself (window.close()). Close it as a tab, so the
  // main window is not closed with it.
  bool PageWantsClose(CefRefPtr<CefBrowser> b) {
    Tab* t = TabFor(b);
    if (!t || t->closing) return false;
    CefRefPtr<Win> self(this);
    const int id = t->id;
    Post([self, id] { self->CloseTab(id); });
    return true;
  }

  void PageClosed(CefRefPtr<CefBrowser> b) {
    Tab* t = TabFor(b);
    if (!t) return;
    const int id = t->id;
    if (active_ == id) active_ = 0;
    order_.erase(std::remove(order_.begin(), order_.end(), id), order_.end());
    tabs_.erase(id);
    PushState();
    if (closing_all_ && tabs_.empty() && window_) {
      CefRefPtr<CefWindow> w = window_;
      Post([w] { if (!w->IsClosed()) w->Close(); });
    }
  }

  void Fullscreen(CefRefPtr<CefBrowser> b, bool fs) {
    if (!IsActive(TabFor(b)) || !window_) return;
    fullscreen_ = fs;
    CloseMenu();
    ui_view_->SetVisible(!fs);
    content_layout_ = content_->SetToBoxLayout(ContentLayout());
    if (Tab* t = Find(active_)) content_layout_->SetFlexForView(t->view, 1);
    for (auto& c : corners_) if (c) c->SetVisible(!fs);
    if (window_->IsFullscreen() != fs) window_->SetFullscreen(fs);
    window_->Layout();
  }

  void PushState() {
    if (!window_) return;
    Tab* a = Find(active_);
    std::string s = "{\"tabs\":[";
    bool first = true;
    for (int id : order_) {
      Tab* t = Find(id);
      if (!t || t->closing) continue;
      if (!first) s += ",";
      first = false;
      s += "{\"id\":" + std::to_string(id) + ",\"t\":" + Q(t->title) +
           ",\"f\":" + Q(t->fav) + ",\"cur\":" + B(id == active_) + "}";
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
    s += ",\"fold\":" + std::string(B(fold_)) + ",\"menu\":" + B(menu_open_) +
         ",\"private\":" + B(private_) + "}";
    Js(ui_view_, "window.ob&&ob.set(" + s + ")");
  }

  // ---- from our own HTML

  void Command(CefRefPtr<CefDictionaryValue> d) {
    if (!window_) return;
    const std::string c = d->GetString("c").ToString();
    CefRefPtr<CefBrowser> b;
    if (Tab* a = Find(active_)) b = a->view->GetBrowser();

    if (c == "ready") { PushState(); return; }
    if (c == "menuready") { MenuState(); return; }
    if (c == "menusize") {
      menu_w_ = d->GetInt("w");
      menu_h_ = d->GetInt("h");
      return;
    }
    if (c == "menu") { menu_open_ ? CloseMenu() : OpenMenu(); return; }
    if (c == "menuclose") { CloseMenu(); return; }
    if (c == "fold") { SetFold(!fold_); return; }
    if (c == "tab") { Activate(d->GetInt("id")); return; }
    if (c == "close") { CloseTab(d->GetInt("id")); return; }
    if (c == "focuspage") { FocusPage(); return; }
    if (c == "zoomin" || c == "zoomout") {
      if (b) b->GetHost()->Zoom(c == "zoomin" ? CEF_ZOOM_COMMAND_IN : CEF_ZOOM_COMMAND_OUT);
      CefRefPtr<Win> self(this);
      Post([self] { self->MenuState(); }, 120);
      return;
    }

    // Everything below closes the menu first.
    CloseMenu();
    if (c == "newtab") NewTab(StartUrl(), true, false);
    else if (c == "newwindow") OpenWindow(false, StartUrl());
    else if (c == "private") OpenWindow(true, StartUrl());
    else if (c == "history") NewTab("chrome://history", true, true);
    else if (c == "bookmarks") NewTab("chrome://bookmarks", true, true);
    else if (c == "downloads") NewTab("chrome://downloads", true, true);
    else if (c == "settings") NewTab("chrome://settings", true, true);
    else if (!b) return;
    else if (c == "back") b->GoBack();
    else if (c == "fwd") b->GoForward();
    else if (c == "reload") b->Reload();
    else if (c == "stop") b->StopLoad();
    else if (c == "go") {
      const std::string u = d->GetString("url").ToString();
      if (!u.empty()) b->GetMainFrame()->LoadURL(u);
      FocusPage();
    } else if (c == "find") {
#ifdef IDC_FIND
      b->GetHost()->ExecuteChromeCommand(IDC_FIND, CEF_WOD_CURRENT_TAB);
#endif
    } else if (c == "reader") {
#ifdef IDC_SHOW_READING_MODE_SIDE_PANEL
      b->GetHost()->ExecuteChromeCommand(IDC_SHOW_READING_MODE_SIDE_PANEL, CEF_WOD_CURRENT_TAB);
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
    if (!ctrl || alt) return false;
    if (k == 'T' && !shift) return run([](Win* w) { w->NewTab(StartUrl(), true, false); });
    if (k == 'T' && shift) return true;  // reopen closed tab: not built yet
    if (k == 'N') return run([shift](Win* w) { w->OpenWindow(shift, StartUrl()); });
    if (k == 'W' && !shift) return run([](Win* w) { w->CloseTab(w->active_); });
    if (k == 'L') return run([](Win* w) { w->FocusAddress(); });
    if (k == 'H' && !shift) return run([](Win* w) { w->NewTab("chrome://history", true, true); });
    if (k == 'J' && !shift) return run([](Win* w) { w->NewTab("chrome://downloads", true, true); });
    if (k == 'D') return true;  // Chrome's bookmark bubble needs Chrome's toolbar
    if (k == 0x09) return run([shift](Win* w) { w->Cycle(shift ? -1 : 1); });
    if (k >= '1' && k <= '9' && !shift) {
      const int n = k - '1';
      return run([n](Win* w) {
        if (w->order_.empty()) return;
        w->Activate(n == 8 || n >= static_cast<int>(w->order_.size()) ? w->order_.back()
                                                                      : w->order_[n]);
      });
    }
    return false;
  }

 private:
  Tab* Find(int id) {
    auto it = tabs_.find(id);
    return it == tabs_.end() ? nullptr : it->second.get();
  }

  int Neighbour(int id) {
    auto it = std::find(order_.begin(), order_.end(), id);
    if (it == order_.end()) return 0;
    for (auto r = it + 1; r != order_.end(); ++r)
      if (!Find(*r)->closing) return *r;
    for (auto l = it; l != order_.begin();) {
      --l;
      if (!Find(*l)->closing) return *l;
    }
    return 0;
  }

  void Cycle(int dir) {
    std::vector<int> open;
    for (int id : order_) if (!Find(id)->closing) open.push_back(id);
    if (open.size() < 2) return;
    auto it = std::find(open.begin(), open.end(), active_);
    int i = it == open.end() ? 0 : static_cast<int>(it - open.begin());
    i = (i + dir + static_cast<int>(open.size())) % static_cast<int>(open.size());
    Activate(open[i]);
  }

  void MoveToHolder(Tab* t) {
    content_->RemoveChildView(t->view);
    t->holder->AddChildView(t->view);
  }

  CefBoxLayoutSettings ContentLayout() {
    CefBoxLayoutSettings c;
    c.horizontal = true;
    const int i = fullscreen_ ? 0 : kInset;
    c.inside_border_insets = {0, i, i, i};
    return c;
  }

  int UiHeight() const { return fullscreen_ ? 0 : kNavH + (fold_ ? kStripH : 0); }

  void SetFold(bool open) {
    fold_ = open;
    ui_delegate_->height = UiHeight();
    ui_view_->InvalidateLayout();
    window_->Layout();
    PushState();
  }

  void PlaceCorners(const CefRect& r) {
    if (!corners_[0]) return;
    const int x = r.x + kInset, y = r.y;
    const int w = r.width - 2 * kInset, h = r.height - kInset;
    const CefRect at[4] = {{x, y, kRadius, kRadius},
                           {x + w - kRadius, y, kRadius, kRadius},
                           {x, y + h - kRadius, kRadius, kRadius},
                           {x + w - kRadius, y + h - kRadius, kRadius, kRadius}};
    for (int i = 0; i < 4; ++i) {
      corners_[i]->SetBounds(at[i]);
      corners_[i]->SetVisible(!fullscreen_ && w > 2 * kRadius && h > 2 * kRadius);
    }
  }

  void OpenMenu() {
    // A click on ⋯ while the menu is open first closes it (it loses focus),
    // so a second open within a moment is that same click.
    if (NowMs() - menu_closed_at_ < 300) return;
    const int w = menu_w_ > 0 ? menu_w_ : 294, h = menu_h_ > 0 ? menu_h_ : 400;
    const int ww = ui_view_->GetBounds().width;
    menu_->SetBounds(CefRect(ww - 10 - w, UiHeight() + 2, w, h));
    menu_->SetVisible(true);
    menu_open_ = true;
    MenuState();
    menu_view_->RequestFocus();
    PushState();
  }

  void CloseMenu() {
    if (!menu_open_) return;
    menu_open_ = false;
    menu_closed_at_ = NowMs();
    if (menu_) menu_->SetVisible(false);
    FocusPage();
    PushState();
  }

  void MenuState() {
    int pct = 100;
    if (Tab* a = Find(active_)) {
      if (auto b = a->view->GetBrowser())
        pct = static_cast<int>(std::lround(std::pow(1.2, b->GetHost()->GetZoomLevel()) * 100));
    }
    Js(menu_view_, "window.ob&&ob.set({\"zoom\":" + std::to_string(pct) + "})");
  }

  void FocusPage() {
    if (Tab* a = Find(active_)) a->view->RequestFocus();
  }

  void FocusAddress() {
    ui_view_->RequestFocus();
    Js(ui_view_, "window.ob&&ob.focusAddress()");
  }

  static void Js(CefRefPtr<CefBrowserView> v, const std::string& code) {
    if (!v) return;
    if (auto b = v->GetBrowser()) b->GetMainFrame()->ExecuteJavaScript(code, "", 0);
  }

 public:
  static void OpenWindow(bool priv, const std::string& url) {
    CefWindow::CreateTopLevelWindow(new Win(priv, url));
  }

 private:
  bool private_;
  std::string first_;
  CefRefPtr<CefRequestContext> context_;
  CefRefPtr<PageClient> page_client_;
  CefRefPtr<UiClient> ui_client_;
  CefRefPtr<UiDelegate> ui_delegate_;
  CefRefPtr<CefWindow> window_;
  CefRefPtr<CefBrowserView> ui_view_, menu_view_;
  CefRefPtr<CefPanel> content_;
  CefRefPtr<CefBoxLayout> content_layout_;
  CefRefPtr<CefOverlayController> corners_[4];
  CefRefPtr<CefOverlayController> menu_;
  std::map<int, std::unique_ptr<Tab>> tabs_;
  std::vector<int> order_;
  int next_id_ = 0, active_ = 0;
  int menu_w_ = 0, menu_h_ = 0;
  int64_t menu_closed_at_ = 0;
  bool fold_ = false, menu_open_ = false, fullscreen_ = false, closing_all_ = false;
  IMPLEMENT_REFCOUNTING(Win);
};

// ------------------------------------------------------- client callbacks

void PageClient::OnAfterCreated(CefRefPtr<CefBrowser> b) {
  ++g_browsers;
  if (win_) win_->TabFor(b);  // records the browser id
}
bool PageClient::DoClose(CefRefPtr<CefBrowser> b) {
  return win_ ? win_->PageWantsClose(b) : false;
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
      Post([w, url, fg] { w->NewTab(url, fg, true); });
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
  if (Tab* t = win_->TabFor(b)) { t->url = url.ToString(); win_->PushState(); }
}
void PageClient::OnTitleChange(CefRefPtr<CefBrowser> b, const CefString& title) {
  if (!win_) return;
  if (Tab* t = win_->TabFor(b)) { t->title = title.ToString(); win_->PushState(); }
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
  if (win_) win_->Fullscreen(b, fs);
}
void PageClient::OnLoadingStateChange(CefRefPtr<CefBrowser> b, bool loading,
                                      bool back, bool fwd) {
  if (!win_) return;
  if (Tab* t = win_->TabFor(b)) {
    t->loading = loading; t->back = back; t->fwd = fwd;
    win_->PushState();
  }
}
bool PageClient::OnPreKeyEvent(CefRefPtr<CefBrowser> b, const CefKeyEvent& e,
                               CefEventHandle, bool*) {
  return win_ && win_->TabFor(b) ? win_->Key(e) : false;
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
  CefString(&settings.root_cache_path).FromString(root);
  CefString(&settings.cache_path).FromString(root + "/default");
  if (!CefInitialize(args, settings, app.get(), nullptr)) return CefGetExitCode();
  CefRunMessageLoop();
  CefShutdown();
  return 0;
}
