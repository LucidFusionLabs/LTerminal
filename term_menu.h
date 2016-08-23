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

struct TerminalMenuWindow;
struct MyHostDB         : public SQLiteIdValueStore { using SQLiteIdValueStore::SQLiteIdValueStore; };
struct MyCredentialDB   : public SQLiteIdValueStore { using SQLiteIdValueStore::SQLiteIdValueStore; };
struct MySettingsDB     : public SQLiteIdValueStore { using SQLiteIdValueStore::SQLiteIdValueStore; };
struct MyAutocompleteDB : public SQLiteIdValueStore { using SQLiteIdValueStore::SQLiteIdValueStore; };

struct MyRunSettingsModel {
  string                  font_name     = FLAGS_font;
  int                     font_size     = 15;
  LTerminal::ColorScheme  color_scheme  = LTerminal::ColorScheme_VGA;
  LTerminal::BeepType     beep_type     = LTerminal::BeepType_None;
  LTerminal::TextEncoding text_encoding = LTerminal::TextEncoding_UTF8;
  LTerminal::DeleteMode   delete_mode   = LTerminal::DeleteMode_Normal;

  MyRunSettingsModel() {}
  MyRunSettingsModel(const LTerminal::RunSettings &r) :
    font_name(r.font_name() ? r.font_name()->data() : FLAGS_font), font_size(r.font_size()),
    color_scheme(r.color_scheme()), beep_type(r.beep_type()), text_encoding(r.text_encoding()),
    delete_mode(r.delete_mode()) {}

  void Load() { *this = MyRunSettingsModel(); }
  void Load(const LTerminal::RunSettings &r) { *this = MyRunSettingsModel(r); }

  flatbuffers::Offset<LTerminal::RunSettings> Save(FlatBufferBuilder &fb) const {
    return LTerminal::CreateRunSettings
      (fb, fb.CreateString(font_name), font_size, color_scheme, beep_type, text_encoding, delete_mode);
  }
};

struct MyAppSettingsModel {
  static const int LatestVersion = 1;
  int version = LatestVersion;
  bool encrypt_db=0, keep_display_on=0;
  MyRunSettingsModel runsettings;

  MyAppSettingsModel() {}
  MyAppSettingsModel(MySettingsDB *settings_db) { Load(settings_db); }

  void Load() { *this = MyAppSettingsModel(); }
  void Load(MySettingsDB *settings_db) {
    auto s = flatbuffers::GetRoot<LTerminal::AppSettings>(FindRefOrDie(settings_db->data, 1).data());
    CHECK(s->run_settings());
    runsettings.Load(*s->run_settings());
    version = s->version();
    encrypt_db = s->encrypt_db();
    keep_display_on = s->keep_display_on();
  }

  flatbuffers::Offset<LTerminal::AppSettings> SaveProto(FlatBufferBuilder &fb) const {
    return LTerminal::CreateAppSettings(fb, version, encrypt_db, keep_display_on, runsettings.Save(fb));
  }

  BlobPiece SaveBlob() const {
    return MakeBlobPiece(CreateFlatBuffer<LTerminal::AppSettings>
                         (bind(&MyAppSettingsModel::SaveProto, this, _1)));
  }

  static void InitDatabase(MySettingsDB *settings_db) {
    if (settings_db->data.find(1) == settings_db->data.end())
      CHECK_EQ(1, settings_db->Insert(MyAppSettingsModel().SaveBlob()));
  }
};

struct MyHostSettingsModel {
  int settings_id, autocomplete_id;
  MyRunSettingsModel runsettings;
  bool agent_forwarding, compression, close_on_disconnect;
  string terminal_type, startup_command, prompt;

  MyHostSettingsModel() { Load(); }
  MyHostSettingsModel(MySettingsDB *settings_db, int id) { Load(settings_db, id); }

  void Load() {
    settings_id = autocomplete_id = 0;
    runsettings.Load();
    terminal_type = "xterm-color";
    startup_command = "";
    prompt = "$";
    agent_forwarding = close_on_disconnect = 0;
    compression = 1;
  }

  void Load(MySettingsDB *settings_db, int id) {
    auto s = flatbuffers::GetRoot<LTerminal::HostSettings>(FindRefOrDie(settings_db->data, (settings_id = id)).data());
    CHECK(s->run_settings());
    runsettings.Load(*s->run_settings());
    agent_forwarding = s->agent_forwarding();
    compression = s->compression();
    close_on_disconnect = s->close_on_disconnect();
    terminal_type = s->terminal_type() ? s->terminal_type()->data() : "";
    startup_command = s->startup_command() ? s->startup_command()->data() : "";
    autocomplete_id = s->autocomplete_id();
    prompt = s->prompt_string() ? s->prompt_string()->data() : "";
  }

  flatbuffers::Offset<LTerminal::HostSettings> SaveProto(FlatBufferBuilder &fb) const {
    return LTerminal::CreateHostSettings
      (fb, runsettings.Save(fb), agent_forwarding, compression, close_on_disconnect,
       fb.CreateString(terminal_type), fb.CreateString(startup_command), autocomplete_id,
       fb.CreateString(prompt));
  }

  BlobPiece SaveBlob() const {
    return MakeBlobPiece(CreateFlatBuffer<LTerminal::HostSettings>
                         (bind(&MyHostSettingsModel::SaveProto, this, _1)));
  }
};

struct MyCredentialModel {
  int cred_id;
  LTerminal::CredentialType credtype;
  string creddata, name;

  MyCredentialModel(CredentialType t=CredentialType_Ask, string d="", string n="") { Load(t, move(d), move(n)); }
  MyCredentialModel(MyCredentialDB *cred_db, int id) { Load(cred_db, id); }

  void Load(CredentialType type=CredentialType_Ask, string data="", string n="") {
    cred_id = 0;
    credtype = type;
    creddata = move(data);
    name = move(n);
  }

  void Load(MyCredentialDB *cred_db, int id) {
    auto cred = flatbuffers::GetRoot<LTerminal::Credential>(FindRefOrDie(cred_db->data, (cred_id = id)).data());
    credtype = cred->type();
    creddata = cred->data() ? string(MakeSigned(cred->data()->data()), cred->data()->size()) : "";
    name = cred->displayname() ? cred->displayname()->data() : "";
  }

  flatbuffers::Offset<LTerminal::Credential> SaveProto(FlatBufferBuilder &fb) const {
    return LTerminal::CreateCredential
      (fb, credtype, fb.CreateVector(reinterpret_cast<const uint8_t*>(creddata.data()), creddata.size()),
       fb.CreateString(name)); 
  }

  BlobPiece SaveBlob() const {
    return MakeBlobPiece(CreateFlatBuffer<LTerminal::Credential>
                         (bind(&MyCredentialModel::SaveProto, this, _1)));
  }

  int Save(MyCredentialDB *cred_db, int row_id=0) const {
    if (!row_id) row_id = cred_db->Insert(        SaveBlob());
    else                  cred_db->Update(row_id, SaveBlob());
    return row_id;
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
  MyHostModel(MyHostDB *host_db, MyCredentialDB *cred_db, MySettingsDB *settings_db, int id) { Load(host_db, cred_db, settings_db, id); }

  string Hostport() const {
    string ret = hostname;
    if (port != 22) StrAppend(&ret, ":", port);
    return ret;
  }

  void SetPort(int p) {
    if (p) port = p;
    else if (protocol == LTerminal::Protocol_SSH)    port = 22;
    else if (protocol == LTerminal::Protocol_Telnet) port = 23;
    else { FATAL("unknown protocol"); }
  }

  void Load() {
    host_id = port = 0;
    protocol = LTerminal::Protocol_SSH;
    hostname = username = displayname = folder = "";
    settings.Load();
    cred.Load();
  }

  void Load(MyHostDB *host_db, MyCredentialDB *cred_db, MySettingsDB *settings_db, int id) {
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
    CHECK(host->settings_id());
    settings.Load(settings_db, host->settings_id());

    if (!host->credential() || host->credential()->db() != LTerminal::CredentialDBType_Table) cred.Load();
    else cred.Load(cred_db, host->credential()->id());
  }

  flatbuffers::Offset<LTerminal::Host>
  SaveProto(FlatBufferBuilder &fb, const LTerminal::CredentialRef &credref, int settings_row_id) const {
    return LTerminal::CreateHost
      (fb, protocol, fb.CreateString(Hostport()), fb.CreateString(username),
       &credref, fb.CreateString(displayname), fb.CreateString(folder), settings_row_id);
  }

  BlobPiece SaveBlob(const LTerminal::CredentialRef &credref, int settings_row_id) const {
    return MakeBlobPiece(CreateFlatBuffer<LTerminal::Host>
                         (bind(&MyHostModel::SaveProto, this, _1, credref, settings_row_id)));
  }

  int Save(MyHostDB *host_db, const LTerminal::CredentialRef &credref, int settings_row_id, int row_id=0) const {
    if (!row_id) row_id = host_db->Insert(        SaveBlob(credref, settings_row_id));
    else                  host_db->Update(row_id, SaveBlob(credref, settings_row_id));
    return row_id;
  }

  int SaveNew(MyHostDB *host_db, MyCredentialDB *cred_db, MySettingsDB *settings_db) const {
    int settings_row_id = settings_db->Insert(settings.SaveBlob()), cred_row_id = 0;
    if      (cred.credtype == CredentialType_PEM)      cred_row_id = cred.cred_id;
    else if (cred.credtype == CredentialType_Password) cred_row_id = cred.Save(cred_db);
    LTerminal::CredentialRef credref(CredentialDBType_Table, cred_row_id);
    return Save(host_db, credref, settings_row_id);
  }

  int Update(const MyHostModel &prevhost,
             MyHostDB *host_db, MyCredentialDB *cred_db, MySettingsDB *settings_db) const {
    int cred_id = prevhost.cred.cred_id;
    if (prevhost.cred.credtype == CredentialType_Password) {
      if (cred.credtype != CredentialType_Password) cred_db->Erase(cred_id);
      else MyCredentialModel(CredentialType_Password, cred.creddata).Save(cred_db, cred_id);
    } else if (cred.credtype == CredentialType_Password) {
      cred_id = MyCredentialModel(CredentialType_Password, cred.creddata).Save(cred_db);
    } else if (cred.credtype == CredentialType_PEM) cred_id = cred.cred_id;
    LTerminal::CredentialRef credref(cred.credtype == CredentialType_Ask ? 0 : CredentialDBType_Table, cred_id);
    settings_db->Update(prevhost.settings.settings_id, settings.SaveBlob());
    return Save(host_db, credref, prevhost.settings.settings_id, prevhost.host_id);
  }
};

struct MyGenKeyModel {
  string name, pw, algo;
  int bits=0;
};

struct MyAppearanceViewController {
  unique_ptr<SystemTableView> view;
  MyAppearanceViewController(TerminalMenuWindow*);
};

struct MyKeyboardSettingsViewController {
  unique_ptr<SystemTableView> view;
  MyKeyboardSettingsViewController();
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

struct MyKeysViewController {
  TerminalMenuWindow *tw;
  MyCredentialDB *model;
  unique_ptr<SystemTableView> view;
  MyKeysViewController(TerminalMenuWindow*, MyCredentialDB *M);
  void UpdateViewFromModel();
};

struct MyRunSettingsViewController {
  unique_ptr<SystemTableView> view;
  MyRunSettingsViewController(TerminalMenuWindow *w);
  static vector<TableItem> GetBaseSchema(TerminalMenuWindow*, SystemNavigationView*);
  static vector<TableItem> GetSchema(TerminalMenuWindow*, SystemNavigationView*);
  void UpdateViewFromModel(const MyAppSettingsModel &app_model, const MyHostSettingsModel &host_model);
  void UpdateModelFromView(MyAppSettingsModel *app_model, MyHostSettingsModel *host_model) const;
};

struct MyAppSettingsViewController {
  unique_ptr<SystemTableView> view;
  MyAppSettingsViewController(TerminalMenuWindow *w) :
    view(make_unique<SystemTableView>("Settings", "", GetSchema(w))) {}
  static vector<TableItem> GetSchema(TerminalMenuWindow*);
  void UpdateViewFromModel(const MyAppSettingsModel &model);
};

struct MyHostSettingsViewController {
  unique_ptr<SystemTableView> view;
  MyHostSettingsViewController(TerminalMenuWindow *w) :
    view(make_unique<SystemTableView>("Server Settings", "", GetSchema(w))) { view->SelectRow(-1, -1); }
  static vector<TableItem> GetSchema(TerminalMenuWindow*);
  void UpdateViewFromModel(const MyHostModel &model);
  bool UpdateModelFromView(MyHostSettingsModel *model, string *folder) const;
};

struct MyQuickConnectViewController {
  unique_ptr<SystemTableView> view;
  MyQuickConnectViewController(TerminalMenuWindow*) {}
  static vector<TableItem> GetSchema(TerminalMenuWindow*);
  void UpdateModelFromView(MyHostModel *model) const;
};

struct MyNewHostViewController {
  unique_ptr<SystemTableView> view;
  MyNewHostViewController(TerminalMenuWindow *w);
  static vector<TableItem> GetSchema(TerminalMenuWindow*);
  bool UpdateModelFromView(MyHostModel *model, MyCredentialDB *cred_db) const;
};

struct MyUpdateHostViewController {
  TerminalMenuWindow *tw;
  MyHostModel prev_model;
  unique_ptr<SystemTableView> view;
  MyUpdateHostViewController(TerminalMenuWindow *w);
  static vector<TableItem> GetSchema(TerminalMenuWindow*);
  void UpdateViewFromModel(const MyHostModel &host);
  bool UpdateModelFromView(MyHostModel *model, MyCredentialDB *cred_db) const;
};

struct MyHostsViewController {
  unique_ptr<SystemTableView> view;
  unique_ptr<SystemToolbarView> toolbar;
  TerminalMenuWindow *tw;
  MyHostsViewController(TerminalMenuWindow*, MyHostDB *model);
  void UpdateViewFromModel(MyHostDB *model);
};

struct TerminalMenuResources {
  SQLite::Database db;
  MyHostDB host_db;
  MyCredentialDB credential_db;
  MySettingsDB settings_db;
  int key_icon, host_icon, bolt_icon, terminal_icon, settings_icon, audio_icon, eye_icon, recycle_icon, 
      fingerprint_icon, info_icon, keyboard_icon, folder_icon;
  string pw_default = "\x01""Ask each time";

  ~TerminalMenuResources() { SQLite::Close(db); }
  TerminalMenuResources() : db(SQLite::Open(StrCat(app->savedir, "lterm.db"))),
    credential_db(&db, "credential"), host_db(&db, "remote"), settings_db(&db, "settings"),
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
    keyboard_icon   (CheckNotNull(app->LoadSystemImage("drawable-xhdpi/keyboard.png"))),
    folder_icon     (CheckNotNull(app->LoadSystemImage("drawable-xhdpi/folder.png"))) {
    MyAppSettingsModel::InitDatabase(&settings_db);
  }
};

struct TerminalMenuWindow : public BaseTerminalWindow {
  int                              second_col=120, connected_host_id=0;
  TerminalMenuResources           *res;
  unique_ptr<SystemNavigationView> hosts_nav, runsettings_nav;
  MyAppearanceViewController       appearance;
  MyKeyboardSettingsViewController keyboard;
  MyNewKeyViewController           newkey;
  MyGenKeyViewController           genkey;
  MyKeysViewController             keys;
  MyRunSettingsViewController      runsettings;
  MyAppSettingsViewController      settings;
  MyHostSettingsViewController     hostsettings;
  MyQuickConnectViewController     quickconnect;
  MyNewHostViewController          newhost;
  MyUpdateHostViewController       updatehost;
  MyHostsViewController            hosts;
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

  TerminalMenuWindow(Window *W) : BaseTerminalWindow(W), res(Singleton<TerminalMenuResources>::Get()),
  hosts_nav(make_unique<SystemNavigationView>()), runsettings_nav(make_unique<SystemNavigationView>()),
  appearance(this), newkey(this), genkey(this), keys(this, &res->credential_db), runsettings(this), 
  settings(this), hostsettings(this), quickconnect(this), newhost(this), updatehost(this),
  hosts(this, &res->host_db) {
    MyAppSettingsModel::InitDatabase(&res->settings_db);
    keyboard_toolbar = make_unique<SystemToolbarView>(MenuItemVec{
      { "\U00002699", "",       bind(&TerminalMenuWindow::ShowRunSettings, this) },
      { "\U000025C0", "",       bind(&TerminalMenuWindow::PressKey,        this, "left") },
      { "\U000025B6", "",       bind(&TerminalMenuWindow::PressKey,        this, "right") },
      { "\U000025B2", "",       bind(&TerminalMenuWindow::PressKey,        this, "up") }, 
      { "\U000025BC", "",       bind(&TerminalMenuWindow::PressKey,        this, "down") },
      { "\U000023EB", "",       bind(&TerminalMenuWindow::PressKey,        this, "pgup") },
      { "\U000023EC", "",       bind(&TerminalMenuWindow::PressKey,        this, "pgdown") }, 
      { "ctrl",       "toggle", bind(&TerminalMenuWindow::ToggleKey,       this, "ctrl") },
    });
    runsettings_nav->PushTable(runsettings.view.get());
    hosts_nav->PushTable(hosts.view.get());
    hosts_nav->Show(true);
  }

  void PressKey (const string &key) { FindAndDispatch(mobile_key_cmd,       key); }
  void ToggleKey(const string &key) { FindAndDispatch(mobile_togglekey_cmd, key); }
  void CloseWindow() { controller->Close(); }

  void GenerateKey() {
    MyGenKeyModel gk;
    genkey.UpdateModelFromView(&gk);
    hosts_nav->PopTable(2);

    string pubkey, privkey;
    if (!Crypto::GenerateKey(gk.algo, gk.bits, "", "", &pubkey, &privkey)) return ERROR("generate ", gk.algo, " key");

    int row_id = MyCredentialModel(CredentialType_PEM, privkey, gk.name).Save(&res->credential_db);
    keys.view->show_cb();
  }
  
  void PasteKey() {
    const char *pems=0, *peme=0, *pemhe=0;
    string pem = app->GetClipboardText();
    string pemtype = Crypto::ParsePEMHeader(pem.data(), &pems, &peme, &pemhe);
    if (pemtype.size()) {
      int row_id = MyCredentialModel(CredentialType_PEM, pem, pemtype).Save(&res->credential_db);
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
      MyCredentialModel cred(&res->credential_db, cred_row_id);
      host_menu->SetTag(0, key_row, cred.cred_id);
      host_menu->SetValue(0, key_row, StrCat("nocontrol,", res->pw_default, ",", cred.name));
      host_menu->SetDropdown(0, key_row, 1);
    } else host_menu->SetDropdown(0, key_row, 0);
    host_menu->EndUpdates();
  }

  void ShowToysMenu() {
    ChangeShader("none");
    my_app->toys_menu->Show();
  }

  void ShowRunSettings() {
    if (!connected_host_id || runsettings_nav->shown) return;
    MyAppSettingsModel app_model(&res->settings_db);
    MyHostModel host_model(&res->host_db, &res->credential_db, &res->settings_db, connected_host_id);
    runsettings.UpdateViewFromModel(app_model, host_model.settings);
    runsettings_nav->Show(true);
  }

  void ShowAppSettings() {
    settings.UpdateViewFromModel(MyAppSettingsModel(&res->settings_db));
    hosts_nav->PushTable(settings.view.get());
    // if (!app->OpenSystemAppPreferences()) {}
  }

  void ShowHostSettings() {
    hosts_nav->PushTable(hostsettings.view.get());
  }

  void ShowNewHost() {
    hostsettings.UpdateViewFromModel(MyHostModel()); 
    hosts_nav->PushTable(newhost.view.get());
  }

  void ShowQuickConnect() {
    hostsettings.UpdateViewFromModel(MyHostModel()); 
    hosts_nav->PushTable(quickconnect.view.get());
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
    MyHostModel host(&res->host_db, &res->credential_db, &res->settings_db, (connected_host_id = host_id));
    MenuConnect(host);
  }

  void DeleteHost(int index, int host_id) {
    MyHostModel host(&res->host_db, &res->credential_db, &res->settings_db, host_id);
    if (host.cred.credtype == LTerminal::CredentialType_Password) res->credential_db.Erase(host.cred.cred_id);
    res->host_db.Erase(host.host_id);
  }
  
  void HostInfo(int host_id) {
    MyHostModel host(&res->host_db, &res->credential_db, &res->settings_db, host_id);
    updatehost.UpdateViewFromModel(host);
    hostsettings.UpdateViewFromModel(host);
    hosts_nav->PushTable(updatehost.view.get());
  }

  void NewHostConnect() {
    connected_host_id = 0;
    MyHostModel host;
    newhost.UpdateModelFromView(&host, &res->credential_db);
    hostsettings.UpdateModelFromView(&host.settings, &host.folder);
    if (host.displayname.empty()) host.displayname = StrCat(host.username, "@", host.hostname);
    MenuConnect(host, [=](){ connected_host_id = host.SaveNew(&res->host_db, &res->credential_db, &res->settings_db); });
  }

  void UpdateHostConnect() {
    connected_host_id = 0;
    MyHostModel host;
    updatehost.UpdateModelFromView(&host, &res->credential_db);
    hostsettings.UpdateModelFromView(&host.settings, &host.folder);
    if (host.cred.credtype == CredentialType_PEM)
      if (!(host.cred.cred_id = updatehost.view->GetTag(0, 4))) host.cred.credtype = CredentialType_Ask;
    MenuConnect(host, [=](){ connected_host_id = host.Update
                               (updatehost.prev_model, &res->host_db, &res->credential_db, &res->settings_db); });
  }

  void MenuStartSession() {
    hosts_nav->Show(false);
    keyboard_toolbar->Show(true);
    app->CloseTouchKeyboardAfterReturn(false);
    app->OpenTouchKeyboard();
  }

  void MenuConnect(const MyHostModel &host, Callback cb=Callback()) {
    INFO("do conn compress=", host.settings.compression);
    UseSSHTerminalController(SSHClient::Params{host.Hostport(), host.username, host.settings.terminal_type, "",
                             host.settings.compression, host.settings.agent_forwarding, host.settings.close_on_disconnect},
                             host.cred.credtype == LTerminal::CredentialType_Password ? host.cred.creddata : "",
                             host.cred.credtype == LTerminal::CredentialType_PEM      ? host.cred.creddata : "",
                             bind(&SystemToolbarView::ToggleButton, keyboard_toolbar.get(), _1), move(cb));
    MenuStartSession();
  }
};

}; // namespace LFL
#endif // LFL_TERM_TERM_MENU_H__
