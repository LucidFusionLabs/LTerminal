#include "gtest/gtest.h"
#include "core/app/app.h"
#include "core/app/gui.h"
#include "core/web/browser.h"
#include "core/app/crypto.h"
#include "core/app/net/resolver.h"
#include "core/app/net/ssh.h"
#include "core/app/net/rfb.h"
#include "core/app/db/sqlite.h"
#include "term/term_generated.h"
#include "term.h"

namespace LFL {
struct MyTerminalTab;
struct MyTerminalMenus;

struct MyAppState {
  unordered_map<string, Shader> shader_map;
  unique_ptr<Browser> image_browser;
  unique_ptr<SystemAlertView> passphrase_alert, passphraseconfirm_alert, passphrasefailed_alert, keypastefailed_alert, hostkey_alert;
  unique_ptr<SystemMenuView> edit_menu, view_menu, toys_menu;
  unique_ptr<MyTerminalMenus> menus;
  int downscale_effects = 1;
  virtual ~MyAppState() {}
  Shader *GetShader(const string &shader_name) { return 0; }
} *my_app = nullptr;
  
struct MyTerminalWindow : public TerminalWindowInterface<TerminalTabInterface> {
  MyTerminalWindow(Window *W) : TerminalWindowInterface(W) {}
  MyTerminalTab *AddTerminalTab() { return 0; }
  void CloseActiveTab() {}
};

struct MyTerminalTab : public TerminalTab {
  TerminalWindowInterface<TerminalTabInterface> *parent;
  virtual ~MyTerminalTab() { root->DelGUI(terminal); }
  MyTerminalTab(Window *W, TerminalWindowInterface<TerminalTabInterface> *P) :
    TerminalTab(W, W->AddGUI(make_unique<Terminal>(nullptr, W, W->default_font, point(80,25)))), parent(P) {}
  void ChangeColors(const string &colors_name, bool redraw=true) {}
  void UseShellTerminalController(const string &m) {}
  void UseSSHTerminalController(SSHClient::Params params, const string &pw="",
                                shared_ptr<SSHClient::Identity> identity=shared_ptr<SSHClient::Identity>(),
                                StringCB metakey_cb=StringCB(),
                                SSHTerminalController::SavehostCB savehost_cb=SSHTerminalController::SavehostCB(),
                                SSHClient::FingerprintCB fingerprint_cb=SSHClient::FingerprintCB()) {}
  void UseTelnetTerminalController(const string &hostport, Callback savehost_cb=Callback()) {}
};

inline MyTerminalWindow *GetActiveWindow() {
  if (auto w = app->focused) return w->GetOwnGUI<MyTerminalWindow>(0);
  else                       return nullptr;
}

inline TerminalTabInterface *GetActiveTab() { return GetActiveWindow()->tabs.top; }
inline MyTerminalTab *GetActiveTerminalTab() { return dynamic_cast<MyTerminalTab*>(GetActiveTab()); }
}; // namespace LFL

#include "term_menu.h"

using namespace LFL;

extern "C" void MyAppCreate(int argc, const char* const* argv) {
  app = new Application(argc, argv);
  app->focused = new Window();
}

extern "C" int MyAppMain() {
  testing::InitGoogleTest(&app->argc, const_cast<char**>(app->argv));
  LFL::FLAGS_font = LFL::FakeFontEngine::Filename();
  CHECK_EQ(0, LFL::app->Create(__FILE__));
  exit(RUN_ALL_TESTS());
}

TEST(SquaresTests, Names) {
}

