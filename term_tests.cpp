#include "gtest/gtest.h"
#include "core/app/app.h"
#include "core/app/shell.h"
#include "core/app/gl/view.h"
#include "core/app/gl/terminal.h"
#include "core/web/browser.h"
#include "core/app/crypto.h"
#include "core/app/net/resolver.h"
#include "core/app/net/ssh.h"
#include "core/app/net/rfb.h"
#include "core/app/db/sqlite.h"
#include "LTerminal/term_generated.h"

namespace LFL {
struct MyTerminalTab;
struct MyTerminalMenus;

struct MyApp : public Application {
  using Application::Application;
  unique_ptr<Browser> image_browser;
  unique_ptr<TimerInterface> flash_timer;
  unique_ptr<AlertViewInterface> flash_alert, info_alert, confirm_alert, text_alert, passphrase_alert, passphraseconfirm_alert;
  unique_ptr<MenuViewInterface> edit_menu, view_menu, toys_menu;
  unique_ptr<MyTerminalMenus> menus;
  int background_timeout = 180;
  Shader *GetShader(const string &shader_name) { return 0; }
} *app;

inline string   LS  (const char *n) { return app->GetLocalizedString(n); }
inline String16 LS16(const char *n) { return app->GetLocalizedString16(n); }

}; // namespace LFL
#include "term.h"
namespace LFL {
  
struct MyTerminalWindow : public TerminalWindowInterface<TerminalTabInterface> {
  MyTerminalWindow(Window *W) : TerminalWindowInterface(W) {}
  MyTerminalTab *AddTerminalTab(int host_id, bool hide_sb, unique_ptr<ToolbarViewInterface> tb=unique_ptr<ToolbarViewInterface>()) { return 0; }
  TerminalTabInterface *AddRFBTab(int host_id, bool hide_sb, RFBClient::Params p, string, TerminalTabCB savehost_cb=TerminalTabCB(),
                                  unique_ptr<ToolbarViewInterface> tb=unique_ptr<ToolbarViewInterface>()) { return 0; }
  void CloseActiveTab() {}
  void ConsoleAnimatingCB() {}
};

struct MyTerminalTab : public TerminalTab {
  TerminalWindowInterface<TerminalTabInterface> *parent;
  virtual ~MyTerminalTab() { root->DelView(terminal); }
  MyTerminalTab(Window *W, TerminalWindowInterface<TerminalTabInterface> *P, int host_id, bool hide_sb) :
    TerminalTab(W, W->AddView(make_unique<Terminal>(nullptr, W, W->default_font, point(80,25))), host_id, hide_sb), parent(P) {}

  void ChangeColors(const string &colors_name, bool redraw=true) {}
  void UseShellTerminalController(const string &m, bool commands=true, Callback reconnect_cb=Callback()) {}
  void UseTelnetTerminalController(const string &hostport, bool from_shell=false, bool close_on_disconn=false,
                                   Callback savehost_cb=Callback()) {}

  SSHTerminalController*
  UseSSHTerminalController(SSHClient::Params params, bool from_shell=false, const string &pw="",
                           SSHClient::LoadIdentityCB identity_cb=SSHClient::LoadIdentityCB(),
                           SSHTerminalController::SavehostCB savehost_cb=SSHTerminalController::SavehostCB(),
                           SSHClient::FingerprintCB fingerprint_cb=SSHClient::FingerprintCB()) { return nullptr; }
};

inline MyTerminalWindow *GetActiveWindow() {
  if (auto w = app->focused) return w->GetOwnView<MyTerminalWindow>(0);
  else                       return nullptr;
}

inline TerminalTabInterface *GetActiveTab() { return GetActiveWindow()->tabs.top; }
inline MyTerminalTab *GetActiveTerminalTab() { return dynamic_cast<MyTerminalTab*>(GetActiveTab()); }
}; // namespace LFL

#include "term_menu.h"
#include "term_menu.cpp"

namespace LFL {
void ResetTerminalMenus() {
  app->localfs.unlink(StrCat(app->savedir, "lterm.db"));
  app->menus = make_unique<MyTerminalMenus>(app, app->system_toolkit);
}

}; // namespace LFL
using namespace LFL;

extern "C" LFApp *MyAppCreate(int argc, const char* const* argv) {
  app = make_unique<MyApp>(argc, argv).release();
  app->focused = app->framework->ConstructWindow(app).release();
  return app;
}

extern "C" int MyAppMain(LFApp*) {
  testing::InitGoogleTest(&app->argc, const_cast<char**>(app->argv));
  LFL::FLAGS_font = LFL::FakeFontEngine::Filename();
  CHECK_EQ(0, LFL::app->Create(__FILE__));
  LFL::ResetTerminalMenus();
  exit(RUN_ALL_TESTS());
}

TEST(MenusTest, GenerateKey) {
  { // RSA
    app->menus->genkey.view->SetSectionValues(1, StringVec{"RSA"});
    app->menus->genkey.view->SetSectionValues(2, StringVec{"2048"});
    int row1_id = app->menus->GenerateKey();
    int row2_id = app->menus->GenerateKey();
    EXPECT_NE(0, row2_id);
    EXPECT_NE(row1_id, row2_id);
    MyCredentialModel cred1(&app->menus->credential_db, row1_id);
    MyCredentialModel cred2(&app->menus->credential_db, row2_id);
    EXPECT_EQ(row1_id, cred1.cred_id);
    EXPECT_EQ(row2_id, cred2.cred_id);
    EXPECT_EQ(CredentialType_PEM, cred1.credtype);
    EXPECT_EQ(CredentialType_PEM, cred2.credtype);
    EXPECT_NE(string(), cred1.name);
    EXPECT_NE(string(), cred2.name);
    EXPECT_NE(string(), cred1.gentype);
    EXPECT_NE(string(), cred2.gentype);
    EXPECT_NE(string(), cred1.gendate);
    EXPECT_NE(string(), cred2.gendate);
    auto identity1 = FindOrNull(app->menus->identity_loaded, row1_id);
    auto identity2 = FindOrNull(app->menus->identity_loaded, row2_id);
    EXPECT_NE(nullptr, identity1);
    EXPECT_NE(nullptr, identity2);
    string pk1 = RSAOpenSSHPublicKey(identity1->rsa, "");
    string pk2 = RSAOpenSSHPublicKey(identity2->rsa, "");
    EXPECT_NE(string(), pk1);
    EXPECT_NE(string(), pk2);
    EXPECT_NE(pk1, pk2);
  }

  { // ECDSA
    app->menus->genkey.view->SetSectionValues(1, StringVec{"ECDSA"});
    app->menus->genkey.view->SetSectionValues(2, StringVec{"256"});
    int row1_id = app->menus->GenerateKey();
    int row2_id = app->menus->GenerateKey();
    EXPECT_NE(0, row2_id);
    EXPECT_NE(row1_id, row2_id);
    MyCredentialModel cred1(&app->menus->credential_db, row1_id);
    MyCredentialModel cred2(&app->menus->credential_db, row2_id);
    EXPECT_EQ(row1_id, cred1.cred_id);
    EXPECT_EQ(row2_id, cred2.cred_id);
    EXPECT_EQ(CredentialType_PEM, cred1.credtype);
    EXPECT_EQ(CredentialType_PEM, cred2.credtype);
    EXPECT_NE(string(), cred1.name);
    EXPECT_NE(string(), cred2.name);
    EXPECT_NE(string(), cred1.gentype);
    EXPECT_NE(string(), cred2.gentype);
    EXPECT_NE(string(), cred1.gendate);
    EXPECT_NE(string(), cred2.gendate);
    auto identity1 = FindOrNull(app->menus->identity_loaded, row1_id);
    auto identity2 = FindOrNull(app->menus->identity_loaded, row2_id);
    EXPECT_NE(nullptr, identity1);
    EXPECT_NE(nullptr, identity2);
    string pk1 = ECDSAOpenSSHPublicKey(identity1->ec, "");
    string pk2 = ECDSAOpenSSHPublicKey(identity2->ec, "");
    EXPECT_NE(string(), pk1);
    EXPECT_NE(string(), pk2);
    EXPECT_NE(pk1, pk2);
  }

  { // Ed25519
    app->menus->genkey.view->SetSectionValues(1, StringVec{"Ed25519"});
    app->menus->genkey.view->SetSectionValues(2, StringVec{"256"});
    int row1_id = app->menus->GenerateKey();
    int row2_id = app->menus->GenerateKey();
    EXPECT_NE(0, row2_id);
    EXPECT_NE(row1_id, row2_id);
    MyCredentialModel cred1(&app->menus->credential_db, row1_id);
    MyCredentialModel cred2(&app->menus->credential_db, row2_id);
    EXPECT_EQ(row1_id, cred1.cred_id);
    EXPECT_EQ(row2_id, cred2.cred_id);
    EXPECT_EQ(CredentialType_PEM, cred1.credtype);
    EXPECT_EQ(CredentialType_PEM, cred2.credtype);
    EXPECT_NE(string(), cred1.name);
    EXPECT_NE(string(), cred2.name);
    EXPECT_NE(string(), cred1.gentype);
    EXPECT_NE(string(), cred2.gentype);
    EXPECT_NE(string(), cred1.gendate);
    EXPECT_NE(string(), cred2.gendate);
    auto identity1 = FindOrNull(app->menus->identity_loaded, row1_id);
    auto identity2 = FindOrNull(app->menus->identity_loaded, row2_id);
    EXPECT_NE(nullptr, identity1);
    EXPECT_NE(nullptr, identity2);
    string pk1 = Ed25519OpenSSHPublicKey(identity1->ed25519, "");
    string pk2 = Ed25519OpenSSHPublicKey(identity2->ed25519, "");
    EXPECT_NE(string(), pk1);
    EXPECT_NE(string(), pk2);
    EXPECT_NE(pk1, pk2);
  }
}

