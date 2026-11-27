// Output Browser on Firefox: settings for its own profile.
user_pref("toolkit.legacyUserProfileCustomizations.stylesheets", true);
// Dark, compact, our own frame drawn in the window's title bar area.
user_pref("ui.systemUsesDarkTheme", 1);
user_pref("extensions.activeThemeID", "firefox-compact-dark@mozilla.org");
user_pref("browser.theme.toolbar-theme", 0);
user_pref("browser.theme.content-theme", 0);
user_pref("browser.tabs.inTitlebar", 1);
user_pref("browser.uidensity", 0);
user_pref("sidebar.verticalTabs", false);
user_pref("sidebar.revamp", false);
user_pref("browser.toolbars.bookmarks.visibility", "never");
user_pref("browser.tabs.firefox-view", false);
user_pref("browser.tabs.tabmanager.enabled", false);
// The start page is ours.
user_pref("browser.startup.page", 1);
user_pref("browser.startup.homepage", "@START@");
user_pref("browser.aboutwelcome.enabled", false);
user_pref("browser.startup.homepage_override.mstone", "ignore");
user_pref("startup.homepage_welcome_url", "");
user_pref("startup.homepage_welcome_url.additional", "");
user_pref("browser.shell.checkDefaultBrowser", false);
user_pref("trailhead.firstrun.didSeeAboutWelcome", true);
user_pref("datareporting.policy.dataSubmissionPolicyBypassNotification", true);
// Searches go to DuckDuckGo, as Search's web line does.
user_pref("browser.urlbar.placeholderName", "DuckDuckGo");
user_pref("browser.search.defaultenginename", "DuckDuckGo");
// The desktop's own file dialogs.
user_pref("widget.use-xdg-desktop-portal.file-picker", 1);
user_pref("widget.use-xdg-desktop-portal.mime-handler", 1);
// Video: Widevine for Netflix, and the system's codecs.
user_pref("media.eme.enabled", true);
user_pref("media.gmp-widevinecdm.visible", true);
user_pref("media.gmp-widevinecdm.enabled", true);
user_pref("media.ffmpeg.vaapi.enabled", true);
user_pref("privacy.file_unique_origin", false);
user_pref("browser.tabs.groups.enabled", true);
// Open where you left off: the last session's tabs and groups come back.
user_pref("browser.startup.page", 3);
// Firefox's own AI features stay off; the browser's helpers will be ours.
user_pref("browser.ml.enable", false);
user_pref("browser.ml.chat.enabled", false);
user_pref("browser.ml.chat.sidebar", false);
user_pref("browser.ml.linkPreview.enabled", false);
user_pref("browser.tabs.groups.smart.enabled", false);
user_pref("browser.tabs.groups.smart.userEnabled", false);
