/*
 * $Id: term.h 1336 2014-12-08 09:29:59Z justin $
 * Copyright (C) 2009 Lucid Fusion Labs

 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef LFL_TERM_TERM_MENU_H__
#define LFL_TERM_TERM_MENU_H__
namespace LFL {

using LTerminal::CredentialType;
using LTerminal::CredentialType_Ask;
using LTerminal::CredentialType_Password;
using LTerminal::CredentialType_PEM;
using LTerminal::CredentialDBType;
using LTerminal::CredentialDBType_Table;

struct MyRunSettingsModel {
  string font_name = "Menlo-Bold";
  int font_size = 15;
  LTerminal::TextEncoding text_encoding = LTerminal::TextEncoding_UTF8;
  LTerminal::DeleteMode delete_mode = LTerminal::DeleteMode_Normal;
  LTerminal::BeepType beep_type = LTerminal::BeepType_None;

  MyRunSettingsModel() {}
  MyRunSettingsModel(const LTerminal::RunSettings &r) :
    font_name(r.font_name() ? r.font_name()->data() : "Menlo-Bold"), font_size(r.font_size()),
    text_encoding(r.text_encoding()), delete_mode(r.delete_mode()), beep_type(r.beep_type()) {}

  void Load() { *this = MyRunSettingsModel(); }
  void Load(const LTerminal::RunSettings &r) { *this = MyRunSettingsModel(r); }
};

struct MyAppSettingsModel {
  MyRunSettingsModel runsettings;
  bool keep_display_on=0, encrypt_db=0;

  static void InitDatabase(SQLiteIdValueStore *settings_db) {
    if (settings_db->data.find(1) == settings_db->data.end()) {
#if 0
      auto proto = MakeFlatBufferOfType(LTerminal::AppSettings,
                                        LTerminal::CreateAppSettings(fb, LTerminal::CreateRunSettings(fb.CreateString("Menlo-Bold"))));
      CHECK_EQ(1, settings_db->Insert(MakeBlobPiece(proto)));
#endif
    }
  }
};

struct MyHostSettingsModel {
  int settings_id, autocomplete_id;
  MyRunSettingsModel runsettings;
  string terminal_type, startup_command, prompt;
  bool agent_forwarding, compression, close_on_disconnect;

  MyHostSettingsModel() { Load(); }
  MyHostSettingsModel(SQLiteIdValueStore *settings_db, int id) { Load(settings_db, id); }

  void Load() {
    settings_id = autocomplete_id = 0;
    runsettings.Load();
    terminal_type = "xterm-color";
    startup_command = prompt = "";
    agent_forwarding = close_on_disconnect = 0;
    compression = 1;
  }

  void Load(SQLiteIdValueStore *settings_db, int id) {
    auto s = flatbuffers::GetRoot<LTerminal::HostSettings>(FindRefOrDie(settings_db->data, (settings_id = id)).data());
    CHECK(s->run_settings());
    runsettings.Load(*s->run_settings());
    agent_forwarding = s->agent_forwarding();
    terminal_type = s->terminal_type() ? s->terminal_type()->data() : "xterm-color";
    startup_command = s->startup_command() ? s->startup_command()->data() : "";
    close_on_disconnect = s->close_on_disconnect();
    autocomplete_id = s->autocomplete_id();
    prompt = s->prompt_string() ? s->prompt_string()->data() : "$";
  }

  BlobPiece SaveBlob() const {
    return MakeBlobPiece(MakeFlatBufferOfType
      (LTerminal::HostSettings, LTerminal::CreateHostSettings
       (fb, LTerminal::CreateRunSettings(fb, fb.CreateString(runsettings.font_name)),
        agent_forwarding, compression, close_on_disconnect, fb.CreateString(terminal_type),
        fb.CreateString(startup_command), autocomplete_id, fb.CreateString(prompt))));
  };
};

struct MyCredentialModel {
  int cred_id;
  LTerminal::CredentialType credtype;
  string creddata, password, keyname;

  MyCredentialModel() { Load(); }
  MyCredentialModel(SQLiteIdValueStore *cred_db, int id) { Load(cred_db, id); }

  void Load(string pw="") {
    cred_id = 0;
    credtype = pw.size() ? LTerminal::CredentialType_Password : LTerminal::CredentialType_Ask;
    creddata = keyname = "";
    password = move(pw);
  }

  void Load(SQLiteIdValueStore *cred_db, int id) {
    auto cred = flatbuffers::GetRoot<LTerminal::Credential>(FindRefOrDie(cred_db->data, (cred_id = id)).data());
    credtype = cred->type();
    creddata = cred->data() ? string(MakeSigned(cred->data()->data()), cred->data()->size()) : "";
    password = credtype == LTerminal::CredentialType_Password ? move(creddata) : "";
    keyname = (credtype == LTerminal::CredentialType_PEM && cred->displayname()) ? cred->displayname()->data() : "";
  }

  static CredentialType GetCredentialType(const string &x) {
    if      (x == "Password") return CredentialType_Password;
    else if (x == "Key")      return CredentialType_PEM;
    else                      return CredentialType_Ask;
  }
};

struct MyHostModel {
  MyHostSettingsModel settings;
  MyCredentialModel cred;
  LTerminal::Protocol protocol;
  string hostname, username, displayname, folder;
  int host_id, port;

  MyHostModel() { Load(); }
  MyHostModel(SQLiteIdValueStore *host_db, SQLiteIdValueStore *cred_db, SQLiteIdValueStore *settings_db, int id) { Load(host_db, cred_db, settings_db, id); }

  string Hostport() const {
    string ret = hostname;
    if (port != 22) StrAppend(&ret, ":", port);
    return ret;
  }

  void Load() {
    host_id = port = 0;
    protocol = LTerminal::Protocol_SSH;
    hostname = username = displayname = folder = "";
    settings.Load();
    cred.Load();
  }

  void Load(SQLiteIdValueStore *host_db, SQLiteIdValueStore *cred_db, SQLiteIdValueStore *settings_db, int id) {
    auto host = flatbuffers::GetRoot<LTerminal::Host>(FindRefOrDie(host_db->data, (host_id = id)).data());
    protocol = host->protocol();

    if (host->hostport()) {
      hostname = host->hostport()->data();
      size_t colon = hostname.find(":");
      SetPort(colon != string::npos ? atoi(hostname.data() + colon) : 0);
      hostname = hostname.substr(0, colon);
    } else {
      hostname = "";
      SetPort(0);
    }

    username = host->username() ? host->username()->data() : "";
    displayname = host->displayname() ? host->displayname()->data() : "";
    folder = host->folder() ? host->folder()->data() : "";
    // CHECK(host->settings_id());
    if (host->settings_id()) settings.Load(settings_db, host->settings_id());

    if (!host->credential() || host->credential()->db() != LTerminal::CredentialDBType_Table) cred.Load();
    else cred.Load(cred_db, host->credential()->id());
  }

  void SetPort(int p) {
    if (p) port = p;
    else if (protocol == LTerminal::Protocol_SSH)    port = 22;
    else if (protocol == LTerminal::Protocol_Telnet) port = 23;
    else { FATAL("unknown protocol"); }
  }
};

struct MyGenKeyModel {
  string name, pw, algo;
  int bits=0;
};

struct TerminalMenuWindow;
struct TerminalMenuResources {
  SQLite::Database db;
  SQLiteIdValueStore credential_db, remote_db, settings_db;
  int key_icon, host_icon, bolt_icon, terminal_icon, settings_icon, audio_icon, eye_icon, recycle_icon, 
      fingerprint_icon, info_icon, keyboard_icon;
  string pw_default = "\x01""Ask each time";

  ~TerminalMenuResources() { SQLite::Close(db); }
  TerminalMenuResources() : db(SQLite::Open(StrCat(app->savedir, "lterm.db"))),
    credential_db(&db, "credential"), remote_db(&db, "remote"), settings_db(&db, "settings"),
    key_icon        (CheckNotNull(app->LoadSystemImage("drawable-xhdpi/key.png"))),
    host_icon       (CheckNotNull(app->LoadSystemImage("drawable-xhdpi/host.png"))),
    bolt_icon       (CheckNotNull(app->LoadSystemImage("drawable-xhdpi/bolt.png"))),
    terminal_icon   (CheckNotNull(app->LoadSystemImage("drawable-xhdpi/terminal.png"))),
    settings_icon   (CheckNotNull(app->LoadSystemImage("drawable-xhdpi/settings.png"))),
    audio_icon      (CheckNotNull(app->LoadSystemImage("drawable-xhdpi/audio.png"))),
    eye_icon        (CheckNotNull(app->LoadSystemImage("drawable-xhdpi/eye.png"))),
    recycle_icon    (CheckNotNull(app->LoadSystemImage("drawable-xhdpi/recycle.png"))),
    fingerprint_icon(CheckNotNull(app->LoadSystemImage("drawable-xhdpi/fingerprint.png"))),
    info_icon       (CheckNotNull(app->LoadSystemImage("drawable-xhdpi/info.png"))),
    keyboard_icon   (CheckNotNull(app->LoadSystemImage("drawable-xhdpi/keyboard.png"))) {
    MyAppSettingsModel::InitDatabase(&settings_db);
  }
} *my_res = nullptr;

struct MyHostsViewController {
  unique_ptr<SystemTableView> view;
  unique_ptr<SystemToolbarView> toolbar;
  TerminalMenuWindow *tw;
  MyHostsViewController(TerminalMenuWindow*, SQLiteIdValueStore *model);
  void UpdateViewFromModel(SQLiteIdValueStore *model);
};

struct MyRunSettingsViewController {
  unique_ptr<SystemTableView> view;
  MyRunSettingsViewController() {}
  static vector<TableItem> GetBaseSchema();
  static vector<TableItem> GetSchema();
  void UpdateViewFromModel(const MyRunSettingsModel &model);
  void UpdateModelFromView(MyRunSettingsModel *model) const;
};

struct MyAppSettingsViewController {
  unique_ptr<SystemTableView> view;
  MyAppSettingsViewController() : view(make_unique<SystemTableView>("Settings", "", GetSchema())) {}
  static vector<TableItem> GetSchema();
  void UpdateViewFromModel(SQLiteIdValueStore *model);
};

struct MyQuickConnectViewController {
  unique_ptr<SystemTableView> view;
  MyQuickConnectViewController(TerminalMenuWindow*) {}
  static vector<TableItem> GetSchema(TerminalMenuWindow*);
  void UpdateModelFromView(MyHostModel *model) const;
};

struct MyNewHostViewController {
  unique_ptr<SystemTableView> view;
  MyNewHostViewController(TerminalMenuWindow *w) :
    view(make_unique<SystemTableView>("New Host", "", GetSchema(w), 120)) { view->SelectRow(0, 1); }
  static vector<TableItem> GetSchema(TerminalMenuWindow*);
  bool UpdateModelFromView(MyHostModel *model, SQLiteIdValueStore *cred_db) const;
};

struct MyUpdateHostViewController {
  MyHostModel prev_model;
  unique_ptr<SystemTableView> view;
  MyUpdateHostViewController(TerminalMenuWindow *w) :
    view(make_unique<SystemTableView>("Update Host", "", GetSchema(w), 120)) {}
  static vector<TableItem> GetSchema(TerminalMenuWindow*);
  void UpdateViewFromModel(const MyHostModel &host);
  bool UpdateModelFromView(MyHostModel *model, SQLiteIdValueStore *cred_db) const;
};

struct MyHostSettingsViewController {
  unique_ptr<SystemTableView> view;
  MyHostSettingsViewController() : view(make_unique<SystemTableView>("Server Settings", "", GetSchema())) { view->SelectRow(-1, -1); }
  static vector<TableItem> GetSchema();
  void UpdateViewFromModel(const MyHostModel &model);
  bool UpdateModelFromView(MyHostSettingsModel *model, string *folder) const;
};

struct MyKeysViewController {
  TerminalMenuWindow *tw;
  SQLiteIdValueStore *model;
  unique_ptr<SystemTableView> view;
  MyKeysViewController(TerminalMenuWindow*, SQLiteIdValueStore *M);
  void UpdateViewFromModel();
};

struct MyNewKeyViewController {
  unique_ptr<SystemTableView> view;
  MyNewKeyViewController(TerminalMenuWindow*);
};

struct MyGenKeyViewController {
  unique_ptr<SystemTableView> view;
  MyGenKeyViewController(TerminalMenuWindow*);
  bool UpdateModelFromView(MyGenKeyModel *model) const;
};

struct MyAppearanceViewController {
  unique_ptr<SystemTableView> view;
  MyAppearanceViewController(TerminalMenuWindow*);
};

struct MyKeyboardSettingsViewController {
  unique_ptr<SystemTableView> view;
  MyKeyboardSettingsViewController();
};

struct TerminalMenuWindow : public BaseTerminalWindow {
  MyHostsViewController            hosts;
  unique_ptr<SystemNavigationView> hosts_nav;
  MyRunSettingsViewController      runsettings;
  MyAppSettingsViewController      settings;
  MyQuickConnectViewController     quickconnect;
  MyNewHostViewController          newhost;
  MyUpdateHostViewController       updatehost;
  MyHostSettingsViewController     hostsettings;
  MyKeysViewController             keys;
  MyNewKeyViewController           newkey;
  MyGenKeyViewController           genkey;
  MyAppearanceViewController       appearance;
  MyKeyboardSettingsViewController keyboard;
  unique_ptr<SystemToolbarView>    keyboard_toolbar;

  unordered_map<string, Callback> mobile_key_cmd = {
    { "left",   bind([=]{ terminal->CursorLeft();  if (controller->frame_on_keyboard_input) app->scheduler.Wakeup(screen); }) },
    { "right",  bind([=]{ terminal->CursorRight(); if (controller->frame_on_keyboard_input) app->scheduler.Wakeup(screen); }) },
    { "up",     bind([=]{ terminal->HistUp();      if (controller->frame_on_keyboard_input) app->scheduler.Wakeup(screen); }) },
    { "down",   bind([=]{ terminal->HistDown();    if (controller->frame_on_keyboard_input) app->scheduler.Wakeup(screen); }) },
    { "pgup",   bind([=]{ terminal->PageUp();      if (controller->frame_on_keyboard_input) app->scheduler.Wakeup(screen); }) },
    { "pgdown", bind([=]{ terminal->PageDown();    if (controller->frame_on_keyboard_input) app->scheduler.Wakeup(screen); }) } };

  unordered_map<string, Callback> mobile_togglekey_cmd = {
    { "ctrl", bind([&]{ controller->ctrl_down = !controller->ctrl_down; }) } };

  TerminalMenuWindow(Window *W) : BaseTerminalWindow(W), hosts(this, &my_res->remote_db),
  hosts_nav(make_unique<SystemNavigationView>(hosts.view.get())), quickconnect(this), newhost(this),
  updatehost(this), keys(this, &my_res->credential_db), newkey(this), genkey(this), appearance(this) {
    MyAppSettingsModel::InitDatabase(&my_res->settings_db);

    MenuItemVec keyboard_tb = {
      { "ctrl",       "", bind(&TerminalMenuWindow::ToggleKey, this, "ctrl") },
      { "\U000025C0", "", bind(&TerminalMenuWindow::PressKey,  this, "left") },
      { "\U000025B6", "", bind(&TerminalMenuWindow::PressKey,  this, "right") },
      { "\U000025B2", "", bind(&TerminalMenuWindow::PressKey,  this, "up") }, 
      { "\U000025BC", "", bind(&TerminalMenuWindow::PressKey,  this, "down") },
      { "\U000023EB", "", bind(&TerminalMenuWindow::PressKey,  this, "pgup") },
      { "\U000023EC", "", bind(&TerminalMenuWindow::PressKey,  this, "pgdown") }, 
      { "\U00002699", "", bind(&TerminalMenuWindow::ShowRunSettings, this) } };
    keyboard_toolbar = make_unique<SystemToolbarView>(keyboard_tb);
  }

  void SetupApp() {
    my_res = new TerminalMenuResources();
    hosts_nav->Show(true);
  }

  void PressKey (const string &key) { FindAndDispatch(mobile_key_cmd,       key); }
  void ToggleKey(const string &key) { FindAndDispatch(mobile_togglekey_cmd, key); }

  void CloseWindow() { controller->Close(); }
  void ShowToysMenu() {
    ChangeShader("none");
    my_app->toys_menu->Show();
  }

  void ShowAppSettings() {
    hosts_nav->PushTable(settings.view.get());
    // if (!app->OpenSystemAppPreferences()) {}
  }

  void ShowRunSettings() {}
  void HostSettings() {
    hosts_nav->PushTable(hostsettings.view.get());
  }

  void GenerateKey() {
    MyGenKeyModel gk;
    genkey.UpdateModelFromView(&gk);
    hosts_nav->PopTable(2);

    string pubkey, privkey;
    if (!Crypto::GenerateKey(gk.algo, gk.bits, "", "", &pubkey, &privkey)) return ERROR("generate ", gk.algo, " key");
    INFO("Generate ", gk.bits, " bits ", gk.algo, " keypair, PEM length ", privkey.size());

    int row_id = UpdateCredential(CredentialType_PEM, privkey, gk.name);
    keys.view->show_cb();
  }
  
  void PasteKey() {
    const char *pems=0, *peme=0, *pemhe=0;
    string pem = app->GetClipboardText();
    string pemtype = Crypto::ParsePEMHeader(pem.data(), &pems, &peme, &pemhe);
    if (pemtype.size()) {
      int row_id = UpdateCredential(CredentialType_PEM, pem, pemtype);
      keys.view->show_cb();
    } else {
      my_app->keypastefailed_alert->Show("");
    }
    hosts_nav->PopTable();
  }
  
  void ChooseKey(int cred_row_id) {
    hosts_nav->PopTable(1);
    SystemTableView *host_menu = hosts_nav->Back();
    int key_row = 3 + (host_menu->GetKey(0, 0) == "Name");
    host_menu->BeginUpdates();
    if (cred_row_id) {
      MyCredentialModel cred(&my_res->credential_db, cred_row_id);
      host_menu->SetTag(0, key_row, cred.cred_id);
      host_menu->SetValue(0, key_row, StrCat("nocontrol,", my_res->pw_default, ",", cred.keyname));
      host_menu->SetDropdown(0, key_row, 1);
    } else host_menu->SetDropdown(0, key_row, 0);
    host_menu->EndUpdates();
  }

  void StartShell() {
    UseShellTerminalController("");
    MenuStartSession();
  }

  void QuickConnect() {
    MyHostModel host;
    quickconnect.UpdateModelFromView(&host);
    auto proto = MakeFlatBufferOfType(LTerminal::HostSettings, LTerminal::CreateHostSettings(fb));
#if 0
    MenuConnect(host, port, user, "xterm-color", CredentialType_Password, cred, [&](){
                // ask to save
                });
#endif
  }

  void ConnectHost(int host_id) {
    MyHostModel host(&my_res->remote_db, &my_res->credential_db, &my_res->settings_db, host_id);
    MenuConnect(host);
  }

  void DeleteHost(int index, int host_id) {
    MyHostModel host(&my_res->remote_db, &my_res->credential_db, &my_res->settings_db, host_id);
    if (host.cred.credtype == LTerminal::CredentialType_Password) my_res->credential_db.Erase(host.cred.cred_id);
    my_res->remote_db.Erase(host.host_id);
  }
  
  void HostInfo(int host_id) {
    MyHostModel host(&my_res->remote_db, &my_res->credential_db, &my_res->settings_db, host_id);
    updatehost.UpdateViewFromModel(host);
    hostsettings.UpdateViewFromModel(host);
    hosts_nav->PushTable(updatehost.view.get());
  }

  void NewHostConnect() {
    MyHostModel host;
    newhost.UpdateModelFromView(&host, &my_res->credential_db);
    hostsettings.UpdateModelFromView(&host.settings, &host.folder);
    if (host.displayname.empty()) host.displayname = StrCat(host.username, "@", host.hostname);
    MenuConnect(host, [=](){ SaveNewHost(host); });
  }

  void UpdateHostConnect() {
    MyHostModel host;
    updatehost.UpdateModelFromView(&host, &my_res->credential_db);
    hostsettings.UpdateModelFromView(&host.settings, &host.folder);
    MenuConnect(host, [=](){ UpdateOldHost(updatehost.prev_model, host); });
  }

  void MenuStartSession() {
    hosts_nav->Show(false);
    keyboard_toolbar->Show(true);
    app->CloseTouchKeyboardAfterReturn(false);
    app->OpenTouchKeyboard();
  }

  void MenuConnect(const MyHostModel &host, Callback cb=Callback()) {
    UseSSHTerminalController(host.Hostport(), host.username, host.settings.terminal_type,
                             host.cred.credtype == LTerminal::CredentialType_Password ? host.cred.password : "",
                             host.cred.credtype == LTerminal::CredentialType_PEM      ? host.cred.creddata : "",
                             bind(&SystemToolbarView::ToggleButton, keyboard_toolbar.get(), _1), move(cb));
    MenuStartSession();
  }

  void SaveNewHost(const MyHostModel &host) {
    int settings_row_id = my_res->settings_db.Insert(host.settings.SaveBlob()), cred_row_id = 0;
    if      (host.cred.credtype == CredentialType_PEM) cred_row_id = host.cred.cred_id;
    else if (host.cred.credtype == CredentialType_Password) 
      cred_row_id = UpdateCredential(CredentialType_Password, host.cred.password, host.cred.keyname);
    LTerminal::CredentialRef credref(CredentialDBType_Table, cred_row_id);
    UpdateHost(FLAGS_ssh, FLAGS_login, host.displayname, credref);
  }

  void UpdateOldHost(const MyHostModel &prevhost, const MyHostModel &host) {
    int cred_id = prevhost.cred.cred_id;
    LTerminal::CredentialType credtype = host.cred.credtype;
    if (prevhost.cred.credtype == CredentialType_Password) {
      if (credtype != CredentialType_Password) my_res->credential_db.Erase(prevhost.cred.cred_id);
      else UpdateCredential(CredentialType_Password, host.cred.password, "", cred_id);
    } else if (credtype == CredentialType_Password) {
      cred_id = UpdateCredential(CredentialType_Password, host.cred.password, "");
    }
    if (credtype == CredentialType_PEM) {
      if (!(cred_id = updatehost.view->GetTag(0, 4))) credtype = CredentialType_Ask;
    }
    LTerminal::CredentialRef credref(credtype == CredentialType_Ask ? 0 : CredentialDBType_Table, cred_id);
    UpdateHost(FLAGS_ssh, FLAGS_login, host.displayname, credref, prevhost.host_id);
  }

  int UpdateCredential(LFL::CredentialType type, const string &data, const string &name, int row_id=0) {
    auto proto = MakeFlatBufferOfType
      (LTerminal::Credential,
       LTerminal::CreateCredential(fb, type,
                                   fb.CreateVector(reinterpret_cast<const uint8_t*>(data.data()), data.size()),
                                   fb.CreateString(name)));
    if (!row_id) row_id = my_res->credential_db.Insert(        MakeBlobPiece(proto));
    else                  my_res->credential_db.Update(row_id, MakeBlobPiece(proto));
    return row_id;
  }

  int UpdateHost(const string &host, const string &login, const string &name,
                 const LTerminal::CredentialRef &credref, int row_id=0) {
    auto proto = MakeFlatBufferOfType
      (LTerminal::Host,
       LTerminal::CreateHost(fb, LTerminal::Protocol_SSH, fb.CreateString(host), fb.CreateString(login),
                               &credref, fb.CreateString(name)));
    if (!row_id) row_id = my_res->remote_db.Insert(        MakeBlobPiece(proto));
    else                  my_res->remote_db.Update(row_id, MakeBlobPiece(proto));
    return row_id;
  }
};

}; // namespace LFL
#endif // LFL_TERM_TERM_MENU_H__
