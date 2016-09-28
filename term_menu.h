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
using LTerminal::CredentialDBType_Null;
using LTerminal::CredentialDBType_Table;

struct MyTerminalMenus;
struct MyHostDB         : public SQLiteIdValueStore { using SQLiteIdValueStore::SQLiteIdValueStore; };
struct MyCredentialDB   : public SQLiteIdValueStore { using SQLiteIdValueStore::SQLiteIdValueStore; };
struct MySettingsDB     : public SQLiteIdValueStore { using SQLiteIdValueStore::SQLiteIdValueStore; };
struct MyAutocompleteDB : public SQLiteIdValueStore { using SQLiteIdValueStore::SQLiteIdValueStore; };

struct MyHostSettingsModel {
  int settings_id, autocomplete_id, font_size;
  bool agent_forwarding, compression, close_on_disconnect;
  string terminal_type, startup_command, font_name, prompt;
  LTerminal::ColorScheme color_scheme;
  LTerminal::BeepType beep_type;
  LTerminal::TextEncoding text_encoding;
  LTerminal::DeleteMode delete_mode;
  vector<SSHClient::Params::Forward> local_forward, remote_forward;

  MyHostSettingsModel() { Load(); }
  MyHostSettingsModel(MySettingsDB *settings_db, int id) { Load(settings_db, id); }

  void Load() {
    settings_id = autocomplete_id = 0;
    agent_forwarding = close_on_disconnect = 0;
    compression      = 1;
    terminal_type    = "xterm-color";
    startup_command  = "";
    font_name        = FLAGS_font;
    font_size        = 15;
    color_scheme     = LTerminal::ColorScheme_VGA;
    beep_type        = LTerminal::BeepType_None;
    text_encoding    = LTerminal::TextEncoding_UTF8;
    delete_mode      = LTerminal::DeleteMode_Normal;
    prompt           = "$";
  }

  void Load(MySettingsDB *settings_db, int id) {
    LoadProto(*flatbuffers::GetRoot<LTerminal::HostSettings>
              (FindRefOrDie(settings_db->data, (settings_id = id)).data()));
  }
  
  void LoadProto(const LTerminal::HostSettings &r) {
    agent_forwarding = r.agent_forwarding();
    compression = r.compression();
    close_on_disconnect = r.close_on_disconnect();
    terminal_type = GetFlatBufferString(r.terminal_type());
    startup_command = GetFlatBufferString(r.startup_command());
    font_name = r.font_name() ? r.font_name()->data() : FLAGS_font;
    font_size = r.font_size();
    color_scheme = r.color_scheme();
    beep_type = r.beep_type();
    text_encoding = r.text_encoding();
    delete_mode = r.delete_mode();
    autocomplete_id = r.autocomplete_id();
    prompt = GetFlatBufferString(r.prompt_string());
    if (auto lf = r.local_forward())  for (auto f : *lf) local_forward.push_back({ f->port(), GetFlatBufferString(f->target()), f->target_port() });
    if (auto rf = r.remote_forward()) for (auto f : *rf) local_forward.push_back({ f->port(), GetFlatBufferString(f->target()), f->target_port() });
  }

  flatbuffers::Offset<LTerminal::HostSettings> SaveProto(FlatBufferBuilder &fb) const {
    vector<flatbuffers::Offset<LTerminal::PortForward>> lf, rf;
    for (auto &f : local_forward)  lf.push_back(LTerminal::CreatePortForward(fb, f.port, fb.CreateString(f.target_host), f.target_port));
    for (auto &f : remote_forward) rf.push_back(LTerminal::CreatePortForward(fb, f.port, fb.CreateString(f.target_host), f.target_port));
    return LTerminal::CreateHostSettings
      (fb, agent_forwarding, compression, close_on_disconnect, fb.CreateString(terminal_type),
       fb.CreateString(startup_command), fb.CreateString(font_name), font_size, color_scheme, beep_type,
       text_encoding, delete_mode, lf.size() ? fb.CreateVector(lf) : 0,
       rf.size() ? fb.CreateVector(rf) : 0, autocomplete_id, fb.CreateString(prompt));
  }

  FlatBufferPiece SaveBlob() const {
    return CreateFlatBuffer<LTerminal::HostSettings>(bind(&MyHostSettingsModel::SaveProto, this, _1));
  }
};

struct MyAppSettingsModel {
  static const int LatestVersion = 1;
  int version = LatestVersion;
  bool keep_display_on=0;
  MyHostSettingsModel default_host_settings;

  MyAppSettingsModel() {}
  MyAppSettingsModel(MySettingsDB *settings_db) { Load(settings_db); }

  void Load() { *this = MyAppSettingsModel(); }
  void Load(MySettingsDB *settings_db) {
    auto s = flatbuffers::GetRoot<LTerminal::AppSettings>(FindRefOrDie(settings_db->data, 1).data());
    CHECK(s->default_host_settings());
    version = s->version();
    default_host_settings.LoadProto(*s->default_host_settings());
    keep_display_on = s->keep_display_on();
  }

  flatbuffers::Offset<LTerminal::AppSettings> SaveProto(FlatBufferBuilder &fb) const {
    return LTerminal::CreateAppSettings(fb, version, default_host_settings.SaveProto(fb), keep_display_on);
  }

  FlatBufferPiece SaveBlob() const {
    return CreateFlatBuffer<LTerminal::AppSettings>(bind(&MyAppSettingsModel::SaveProto, this, _1));
  }

  void Save(MySettingsDB *settings_db) const { settings_db->Update(1, SaveBlob()); }
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
    auto it = cred_db->data.find((cred_id = id));
    if (it != cred_db->data.end()) {
      auto cred = flatbuffers::GetRoot<LTerminal::Credential>(it->second.data());
      credtype = cred->type();
      creddata = GetFlatBufferString(cred->data());
      name = GetFlatBufferString(cred->displayname());
    } else Load();
  }

  flatbuffers::Offset<LTerminal::Credential> SaveProto(FlatBufferBuilder &fb) const {
    return LTerminal::CreateCredential
      (fb, credtype, fb.CreateVector(reinterpret_cast<const uint8_t*>(creddata.data()), creddata.size()),
       fb.CreateString(name)); 
  }

  FlatBufferPiece SaveBlob() const {
    return CreateFlatBuffer<LTerminal::Credential>(bind(&MyCredentialModel::SaveProto, this, _1));
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
  string hostname, username, displayname, fingerprint, folder;
  int host_id, port, fingerprint_type;

  MyHostModel() { Load(); }
  MyHostModel(MyHostDB *host_db, MyCredentialDB *cred_db, MySettingsDB *settings_db, int id) { Load(host_db, cred_db, settings_db, id); }

  string Hostport() const {
    string ret = hostname;
    if (port != 22) StrAppend(&ret, ":", port);
    return ret;
  }

  void SetProtocol(const string &p) {
    if      (p == "SSH")         { protocol = LTerminal::Protocol_SSH; }
    else if (p == "VNC")         { protocol = LTerminal::Protocol_RFB;        username.clear(); }
    else if (p == "Telnet")      { protocol = LTerminal::Protocol_Telnet;     username.clear(); cred.Load(); }
    else if (p == "Local Shell") { protocol = LTerminal::Protocol_LocalShell; username.clear(); cred.Load(); }
    else { FATAL("unknown protocol"); }
  }

  void SetPort(int p) { port = p ? p : DefaultPort(); }
  int DefaultPort() const {
    if      (protocol == LTerminal::Protocol_SSH)        return 22;
    else if (protocol == LTerminal::Protocol_Telnet)     return 23;
    else if (protocol == LTerminal::Protocol_RFB)        return 5900;
    else if (protocol == LTerminal::Protocol_LocalShell) return 0;
    else { FATAL("unknown protocol"); }
  };

  bool FingerprintMatch(int t, const string &fp) const { return fingerprint_type == t && fingerprint == fp; }
  void SetFingerprint(int t, const string &fp) {
    fingerprint_type = t;
    fingerprint = fp;
  }

  void Load() {
    host_id = port = fingerprint_type = 0;
    protocol = LTerminal::Protocol_SSH;
    hostname = username = displayname = fingerprint = folder = "";
    settings.Load();
    cred.Load();
  }

  void Load(MyHostDB *host_db, MyCredentialDB *cred_db, MySettingsDB *settings_db, int id) {
    auto host = flatbuffers::GetRoot<LTerminal::Host>(FindRefOrDie(host_db->data, (host_id = id)).data());
    protocol = host->protocol();
    fingerprint_type = host->fingerprint_type();

    if (host->hostport()) {
      hostname = host->hostport()->data();
      size_t colon = hostname.find(":");
      SetPort(colon != string::npos ? atoi(hostname.data() + colon) : 0);
      hostname = hostname.substr(0, colon);
    } else {
      hostname = "";
      SetPort(0);
    }

    username    = GetFlatBufferString(host->username());
    displayname = GetFlatBufferString(host->displayname());
    fingerprint = GetFlatBufferString(host->fingerprint());
    folder      = GetFlatBufferString(host->folder()); 
    CHECK(host->settings_id());
    settings.Load(settings_db, host->settings_id());

    if (!host->credential() || host->credential()->db() != LTerminal::CredentialDBType_Table) cred.Load();
    else cred.Load(cred_db, host->credential()->id());
  }

  flatbuffers::Offset<LTerminal::Host>
  SaveProto(FlatBufferBuilder &fb, const LTerminal::CredentialRef &credref, int settings_row_id) const {
    return LTerminal::CreateHost
      (fb, protocol, fb.CreateString(Hostport()), fb.CreateString(username),
       &credref, fb.CreateString(displayname), fb.CreateString(folder), settings_row_id,
       fb.CreateVector(reinterpret_cast<const uint8_t*>(fingerprint.data()), fingerprint.size()),
       fingerprint_type);
  }

  FlatBufferPiece SaveBlob(const LTerminal::CredentialRef &credref, int settings_row_id) const {
    return CreateFlatBuffer<LTerminal::Host>
      (bind(&MyHostModel::SaveProto, this, _1, credref, settings_row_id));
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
    LTerminal::CredentialRef credref
      (cred.credtype == CredentialType_Ask ? CredentialDBType_Null : CredentialDBType_Table, cred_row_id);
    return Save(host_db, credref, settings_row_id);
  }

  int Update(const MyHostModel &prevhost,
             MyHostDB *host_db, MyCredentialDB *cred_db, MySettingsDB *settings_db) const {
    int cred_row_id = prevhost.cred.cred_id;
    if (prevhost.cred.credtype == CredentialType_Password) {
      if (cred.credtype != CredentialType_Password) cred_db->Erase(cred_row_id);
      else MyCredentialModel(CredentialType_Password, cred.creddata).Save(cred_db, cred_row_id);
    } else if (cred.credtype == CredentialType_Password) {
      cred_row_id = MyCredentialModel(CredentialType_Password, cred.creddata).Save(cred_db);
    } else if (cred.credtype == CredentialType_PEM) cred_row_id = cred.cred_id;
    LTerminal::CredentialRef credref
      (cred.credtype == CredentialType_Ask ? CredentialDBType_Null : CredentialDBType_Table, cred_row_id);
    settings_db->Update(prevhost.settings.settings_id, settings.SaveBlob());
    return Save(host_db, credref, prevhost.settings.settings_id, prevhost.host_id);
  }
};

struct MyGenKeyModel {
  string name, pw, algo;
  int bits=0;
};

struct MyLocalEncryptionViewController {
  unique_ptr<SystemTableView> view;
  MyLocalEncryptionViewController(MyTerminalMenus*);
};

struct MyAppearanceViewController {
  unique_ptr<SystemTableView> view;
  MyAppearanceViewController(MyTerminalMenus*);
};

struct MyKeyboardSettingsViewController {
  unique_ptr<SystemTableView> view;
  MyKeyboardSettingsViewController(MyTerminalMenus*);
  void UpdateViewFromModel(const MyHostSettingsModel&);
};

struct MyNewKeyViewController {
  unique_ptr<SystemTableView> view;
  MyNewKeyViewController(MyTerminalMenus*);
};

struct MyGenKeyViewController {
  unique_ptr<SystemTableView> view;
  MyGenKeyViewController(MyTerminalMenus*);
  bool UpdateModelFromView(MyGenKeyModel *model) const;
};

struct MyKeysViewController {
  MyTerminalMenus *menus;
  MyCredentialDB *model;
  bool add_or_edit;
  unique_ptr<SystemTableView> view;
  MyKeysViewController(MyTerminalMenus*, MyCredentialDB*, bool add_or_edit);
  void UpdateViewFromModel();
};

struct MyAppSettingsViewController {
  unique_ptr<SystemTableView> view;
  MyAppSettingsViewController(MyTerminalMenus*);
  static vector<TableItem> GetSchema(MyTerminalMenus*);
  void UpdateViewFromModel(const MyAppSettingsModel &model);
  void UpdateModelFromView(      MyAppSettingsModel *model);
};

struct MyTerminalInterfaceSettingsViewController {
  unique_ptr<SystemTableView> view;
  MyTerminalInterfaceSettingsViewController(MyTerminalMenus*);
  static vector<TableItem> GetBaseSchema(MyTerminalMenus*, SystemNavigationView*);
  static vector<TableItem> GetSchema(MyTerminalMenus*, SystemNavigationView*);
  void UpdateViewFromModel(const MyAppSettingsModel &app_model, const MyHostSettingsModel &host_model);
  void UpdateModelFromView(MyAppSettingsModel *app_model, MyHostSettingsModel *host_model) const;
};

struct MyRFBInterfaceSettingsViewController {
  unique_ptr<SystemTableView> view;
  MyRFBInterfaceSettingsViewController(MyTerminalMenus*);
  static vector<TableItem> GetBaseSchema(MyTerminalMenus*, SystemNavigationView*);
  static vector<TableItem> GetSchema(MyTerminalMenus*, SystemNavigationView*);
  void UpdateViewFromModel(const MyAppSettingsModel &app_model, const MyHostSettingsModel &host_model);
  void UpdateModelFromView(MyAppSettingsModel *app_model, MyHostSettingsModel *host_model) const;
};

struct MySSHFingerprintViewController {
  unique_ptr<SystemTableView> view;
  MySSHFingerprintViewController(MyTerminalMenus*);
  void UpdateViewFromModel(const MyHostModel &model);
};

struct MySSHPortForwardViewController {
  unique_ptr<SystemTableView> view;
  MySSHPortForwardViewController(MyTerminalMenus*);
};

struct MySSHSettingsViewController {
  MyTerminalMenus *menus;
  unique_ptr<SystemTableView> view;
  MySSHSettingsViewController(MyTerminalMenus *m);
  static vector<TableItem> GetSchema(MyTerminalMenus*);
  void UpdateViewFromModel(const MyHostModel &model);
  bool UpdateModelFromView(MyHostSettingsModel *model, string *folder) const;
};

struct MyTelnetSettingsViewController {
  MyTerminalMenus *menus;
  unique_ptr<SystemTableView> view;
  MyTelnetSettingsViewController(MyTerminalMenus *m);
  static vector<TableItem> GetSchema(MyTerminalMenus*);
  void UpdateViewFromModel(const MyHostModel &model);
  bool UpdateModelFromView(MyHostSettingsModel *model, string *folder) const;
};

struct MyVNCSettingsViewController {
  MyTerminalMenus *menus;
  unique_ptr<SystemTableView> view;
  MyVNCSettingsViewController(MyTerminalMenus *m);
  static vector<TableItem> GetSchema(MyTerminalMenus*);
  void UpdateViewFromModel(const MyHostModel &model);
  bool UpdateModelFromView(MyHostSettingsModel *model, string *folder) const;
};

struct MyLocalShellSettingsViewController {
  MyTerminalMenus *menus;
  unique_ptr<SystemTableView> view;
  MyLocalShellSettingsViewController(MyTerminalMenus *m);
  static vector<TableItem> GetSchema(MyTerminalMenus*);
  void UpdateViewFromModel(const MyHostModel &model);
  bool UpdateModelFromView(MyHostSettingsModel *model, string *folder) const;
};

struct MyQuickConnectViewController {
  MyTerminalMenus *menus;
  unique_ptr<SystemTableView> view;
  MyQuickConnectViewController(MyTerminalMenus*);
  static vector<TableItem> GetSchema(MyTerminalMenus*);
  void UpdateViewFromModel();
  bool UpdateModelFromView(MyHostModel *model, MyCredentialDB *cred_db) const;
};

struct MyNewHostViewController {
  MyTerminalMenus *menus;
  unique_ptr<SystemTableView> view;
  MyNewHostViewController(MyTerminalMenus*);
  static vector<TableItem> GetSchema(MyTerminalMenus*);
  void UpdateViewFromModel();
  bool UpdateModelFromView(MyHostModel *model, MyCredentialDB *cred_db) const;
};

struct MyUpdateHostViewController {
  MyTerminalMenus *menus;
  MyHostModel prev_model;
  unique_ptr<SystemTableView> view;
  MyUpdateHostViewController(MyTerminalMenus*);
  static vector<TableItem> GetSchema(MyTerminalMenus*);
  void UpdateViewFromModel(const MyHostModel &host);
  bool UpdateModelFromView(MyHostModel *model, MyCredentialDB *cred_db) const;
};

struct MyHostsViewController {
  MyTerminalMenus *menus;
  bool menu;
  string folder;
  unique_ptr<SystemTableView> view;
  MyHostsViewController(MyTerminalMenus*, bool menu);
  static vector<TableItem> GetBaseSchema(MyTerminalMenus*);
  void LoadFolderUI(MyHostDB *model);
  void LoadLockedUI(MyHostDB *model);
  void LoadUnlockedUI(MyHostDB *model);
  void UpdateViewFromModel(MyHostDB *model);
};

struct MyTerminalMenus {
  bool db_protected = false, db_opened = false;
  SQLite::Database db;
  MyHostDB host_db;
  MyCredentialDB credential_db;
  MySettingsDB settings_db;
  unordered_map<int, shared_ptr<SSHClient::Identity>> identity_loaded;
  int key_icon, host_icon, host_locked_icon, bolt_icon, terminal_icon, settings_blue_icon, settings_gray_icon,
      audio_icon, eye_icon, recycle_icon, fingerprint_icon, info_icon, keyboard_icon, folder_icon,
      plus_red_icon, plus_green_icon, vnc_icon, locked_icon, unlocked_icon, font_icon, toys_icon,
      arrowleft_icon, arrowright_icon;
  string pw_default = "\x01""Ask each time", pw_empty = "lfl_default";

  int                              second_col=120, connected_host_id=0;
  unique_ptr<SystemNavigationView> hosts_nav, interfacesettings_nav;
  MyLocalEncryptionViewController  encryption;
  MyAppearanceViewController       appearance;
  MyKeyboardSettingsViewController keyboard;
  MyNewKeyViewController           newkey;
  MyGenKeyViewController           genkey;
  MyKeysViewController             keys, editkeys;
  MyAppSettingsViewController      settings;
  MyTerminalInterfaceSettingsViewController terminalinterfacesettings;
  MyRFBInterfaceSettingsViewController rfbinterfacesettings;
  MySSHFingerprintViewController   sshfingerprint;
  MySSHPortForwardViewController   sshportforward;
  MySSHSettingsViewController      sshsettings;
  MyTelnetSettingsViewController   telnetsettings;
  MyVNCSettingsViewController      vncsettings;
  MyLocalShellSettingsViewController localshellsettings;
  MyQuickConnectViewController     quickconnect;
  MyNewHostViewController          newhost;
  MyUpdateHostViewController       updatehost;
  MyHostsViewController            hosts, hostsfolder;
  unique_ptr<SystemMenuView>       sessions_menu;
  unique_ptr<SystemToolbarView>    keyboard_toolbar;

  unordered_map<string, Callback> mobile_key_cmd = {
    { "left",   bind([=]{ if (auto t = GetActiveTerminalTab()) { t->terminal->CursorLeft();  if (t->controller->frame_on_keyboard_input) app->scheduler.Wakeup(app->focused); } }) },
    { "right",  bind([=]{ if (auto t = GetActiveTerminalTab()) { t->terminal->CursorRight(); if (t->controller->frame_on_keyboard_input) app->scheduler.Wakeup(app->focused); } }) },
    { "up",     bind([=]{ if (auto t = GetActiveTerminalTab()) { t->terminal->HistUp();      if (t->controller->frame_on_keyboard_input) app->scheduler.Wakeup(app->focused); } }) },
    { "down",   bind([=]{ if (auto t = GetActiveTerminalTab()) { t->terminal->HistDown();    if (t->controller->frame_on_keyboard_input) app->scheduler.Wakeup(app->focused); } }) },
    { "pgup",   bind([=]{ if (auto t = GetActiveTerminalTab()) { t->terminal->PageUp();      if (t->controller->frame_on_keyboard_input) app->scheduler.Wakeup(app->focused); } }) },
    { "pgdown", bind([=]{ if (auto t = GetActiveTerminalTab()) { t->terminal->PageDown();    if (t->controller->frame_on_keyboard_input) app->scheduler.Wakeup(app->focused); } }) },
    { "home",   bind([=]{ if (auto t = GetActiveTerminalTab()) { t->terminal->Home();        if (t->controller->frame_on_keyboard_input) app->scheduler.Wakeup(app->focused); } }) },
    { "end",    bind([=]{ if (auto t = GetActiveTerminalTab()) { t->terminal->End();         if (t->controller->frame_on_keyboard_input) app->scheduler.Wakeup(app->focused); } }) } };

  unordered_map<string, Callback> mobile_togglekey_cmd = {
    { "ctrl", bind([&]{ if (auto t = GetActiveTerminalTab()) { t->controller->ctrl_down = !t->controller->ctrl_down; } }) },
    { "alt",  bind([&]{ if (auto t = GetActiveTerminalTab()) { t->controller->alt_down  = !t->controller->alt_down;  } }) } };

  ~MyTerminalMenus() { SQLite::Close(db); }
  MyTerminalMenus() : db(SQLite::Open(StrCat(app->savedir, "lterm.db"))),
    key_icon           (CheckNotNull(app->LoadSystemImage("drawable-xhdpi/key.png"))),
    host_icon          (CheckNotNull(app->LoadSystemImage("drawable-xhdpi/host.png"))),
    host_locked_icon   (CheckNotNull(app->LoadSystemImage("drawable-xhdpi/host_locked.png"))),
    bolt_icon          (CheckNotNull(app->LoadSystemImage("drawable-xhdpi/bolt.png"))),
    terminal_icon      (CheckNotNull(app->LoadSystemImage("drawable-xhdpi/terminal.png"))),
    settings_blue_icon (CheckNotNull(app->LoadSystemImage("drawable-xhdpi/settings_blue.png"))),
    settings_gray_icon (CheckNotNull(app->LoadSystemImage("drawable-xhdpi/settings_gray.png"))),
    audio_icon         (CheckNotNull(app->LoadSystemImage("drawable-xhdpi/audio.png"))),
    eye_icon           (CheckNotNull(app->LoadSystemImage("drawable-xhdpi/eye.png"))),
    recycle_icon       (CheckNotNull(app->LoadSystemImage("drawable-xhdpi/recycle.png"))),
    fingerprint_icon   (CheckNotNull(app->LoadSystemImage("drawable-xhdpi/fingerprint.png"))),
    info_icon          (CheckNotNull(app->LoadSystemImage("drawable-xhdpi/info.png"))),
    keyboard_icon      (CheckNotNull(app->LoadSystemImage("drawable-xhdpi/keyboard.png"))),
    folder_icon        (CheckNotNull(app->LoadSystemImage("drawable-xhdpi/folder.png"))),
    plus_red_icon      (CheckNotNull(app->LoadSystemImage("drawable-xhdpi/plus_red.png"))),
    plus_green_icon    (CheckNotNull(app->LoadSystemImage("drawable-xhdpi/plus_green.png"))),
    vnc_icon           (CheckNotNull(app->LoadSystemImage("drawable-xhdpi/vnc.png"))),
    locked_icon        (CheckNotNull(app->LoadSystemImage("drawable-xhdpi/locked.png"))),
    unlocked_icon      (CheckNotNull(app->LoadSystemImage("drawable-xhdpi/unlocked.png"))),
    font_icon          (CheckNotNull(app->LoadSystemImage("drawable-xhdpi/font.png"))),
    toys_icon          (CheckNotNull(app->LoadSystemImage("drawable-xhdpi/toys.png"))),
    arrowleft_icon     (CheckNotNull(app->LoadSystemImage("drawable-xhdpi/arrowleft.png"))),
    arrowright_icon    (CheckNotNull(app->LoadSystemImage("drawable-xhdpi/arrowright.png"))),
    hosts_nav(make_unique<SystemNavigationView>()), interfacesettings_nav(make_unique<SystemNavigationView>()),
    encryption(this), appearance(this), keyboard(this), newkey(this), genkey(this),
    keys(this, &credential_db, true), editkeys(this, &credential_db, false), settings(this),
    terminalinterfacesettings(this), rfbinterfacesettings(this), sshfingerprint(this), sshportforward(this),
    sshsettings(this), telnetsettings(this), vncsettings(this), localshellsettings(this), quickconnect(this),
    newhost(this), updatehost(this), hosts(this, true), hostsfolder(this, false) {

    keyboard_toolbar = make_unique<SystemToolbarView>(MenuItemVec{
      { "\U00002699", "",       bind(&MyTerminalMenus::ShowInterfaceSettings, this) },
      { "esc",        "toggle", bind(&MyTerminalMenus::ToggleKey,        this, "esc") },
      { "\U000025C0", "",       bind(&MyTerminalMenus::PressKey,         this, "left") },
      { "\U000025B6", "",       bind(&MyTerminalMenus::PressKey,         this, "right") },
      { "\U000025B2", "",       bind(&MyTerminalMenus::PressKey,         this, "up") }, 
      { "\U000025BC", "",       bind(&MyTerminalMenus::PressKey,         this, "down") },
      { "\U000023EB", "",       bind(&MyTerminalMenus::PressKey,         this, "pgup") },
      { "\U000023EC", "",       bind(&MyTerminalMenus::PressKey,         this, "pgdown") }, 
      { "\U000023EA", "",       bind(&MyTerminalMenus::PressKey,         this, "home") },
      { "\U000023E9", "",       bind(&MyTerminalMenus::PressKey,         this, "end") }, 
      { "ctrl",       "toggle", bind(&MyTerminalMenus::ToggleKey,        this, "ctrl") },
      { "alt",        "toggle", bind(&MyTerminalMenus::ToggleKey,        this, "alt") },
      { "\U000025F0", "",       bind(&MyTerminalMenus::ShowSessionsMenu, this) },
    });

    if (UnlockEncryptedDatabase(pw_empty)) hosts.LoadUnlockedUI(&host_db);
    else if ((db_protected = true))        hosts.LoadLockedUI  (&host_db);
    hostsfolder.LoadFolderUI(&host_db);
    hosts_nav->PushTable(hosts.view.get());
  }

  void PressKey (const string &key) { FindAndDispatch(mobile_key_cmd,       key); }
  void ToggleKey(const string &key) { FindAndDispatch(mobile_togglekey_cmd, key); }

  bool UnlockEncryptedDatabase(const string &pw) {
    if (!(db_opened = SQLite::UsePassphrase(db, pw))) return false;
    host_db       = MyHostDB      (&db, "host");
    credential_db = MyCredentialDB(&db, "credential");
    settings_db   = MySettingsDB  (&db, "settings");
    if (settings_db.data.find(1) == settings_db.data.end()) {
      CHECK_EQ(1, settings_db.Insert(MyAppSettingsModel().SaveBlob()));
      CHECK_EQ(1, MyHostModel().SaveNew(&host_db, &credential_db, &settings_db));
    }
    return true;
  }
  
  void DisableLocalEncryption() {
    SQLite::ChangePassphrase(db, pw_empty);
    db_protected = false;
    encryption.view->show_cb();
  }

  void EnableLocalEncryption(const string &pw, const string &confirm_pw) {
    if (pw != confirm_pw) return my_app->passphrasefailed_alert->Show("");
    SQLite::ChangePassphrase(db, pw);
    db_protected = true;
    encryption.view->show_cb();
  }

  void ResetSettingsView() {
    MyHostModel host;
    localshellsettings.UpdateViewFromModel(host); 
    telnetsettings.UpdateViewFromModel(host); 
    vncsettings.UpdateViewFromModel(host); 
    sshsettings.UpdateViewFromModel(host); 
    sshfingerprint.UpdateViewFromModel(host);
  }
  
  void UpdateSettingsViewFromModel(LTerminal::Protocol proto, const MyHostModel &model) {
    switch(proto) {
       case LTerminal::Protocol_LocalShell: localshellsettings.UpdateViewFromModel(model); break;
       case LTerminal::Protocol_Telnet:     telnetsettings    .UpdateViewFromModel(model); break;
       case LTerminal::Protocol_RFB:        vncsettings       .UpdateViewFromModel(model); break;
       case LTerminal::Protocol_SSH: sshsettings.UpdateViewFromModel(model); sshfingerprint.UpdateViewFromModel(model); break;
       default: ERROR("unknown protocol ", proto); break;
    }
  }

  void UpdateModelFromSettingsView(LTerminal::Protocol proto, MyHostSettingsModel *model, string *folder) {
    switch(proto) {
       case LTerminal::Protocol_SSH:        sshsettings       .UpdateModelFromView(model, folder); break;
       case LTerminal::Protocol_Telnet:     telnetsettings    .UpdateModelFromView(model, folder); break;
       case LTerminal::Protocol_RFB:        vncsettings       .UpdateModelFromView(model, folder); break;
       case LTerminal::Protocol_LocalShell: localshellsettings.UpdateModelFromView(model, folder); break;
       default: ERROR("unknown protocol ", proto); break;
    }
  }

  void GenerateKey() {
    MyGenKeyModel gk;
    genkey.UpdateModelFromView(&gk);
    hosts_nav->PopTable(2);

    string pubkey, privkey;
    if (!Crypto::GenerateKey(gk.algo, gk.bits, "", "", &pubkey, &privkey)) return ERROR("generate ", gk.algo, " key");

    int row_id = MyCredentialModel(CredentialType_PEM, privkey, gk.name).Save(&credential_db);
    keys.view->show_cb();
  }
  
  void PasteKey() {
    const char *pems=0, *peme=0, *pemhe=0;
    string pem = app->GetClipboardText();
    string pemtype = Crypto::ParsePEMHeader(pem.data(), &pems, &peme, &pemhe);
    if (pemtype.size()) {
      int row_id = MyCredentialModel(CredentialType_PEM, pem, pemtype).Save(&credential_db);
      keys.view->show_cb();
    } else {
      my_app->keypastefailed_alert->Show("");
    }
    hosts_nav->PopTable();
  }
  
  void ChooseKey(int cred_row_id) {
    hosts_nav->PopTable(1);
    SystemTableView *host_menu = hosts_nav->Back();
    int key_row = 3 + (host_menu->GetKey(0, 0) == "");
    host_menu->BeginUpdates();
    if (cred_row_id) {
      MyCredentialModel cred(&credential_db, cred_row_id);
      host_menu->SetTag(0, key_row, cred.cred_id);
      host_menu->SetValue(0, key_row, StrCat(",", pw_default, ",", cred.name));
      host_menu->SetDropdown(0, key_row, 1);
    } else host_menu->SetDropdown(0, key_row, 0);
    host_menu->EndUpdates();
  }

  void ShowSessionsMenu() {
    interfacesettings_nav->PopAll();
    interfacesettings_nav->Show(false);
    MenuItemVec v;
    auto tw = GetActiveWindow();
    for (auto t : tw->tabs.tabs) v.push_back
      (MenuItem{"", t->title, [=](){ tw->tabs.SelectTab(t); app->scheduler.Wakeup(tw->root); } });
    v.push_back(MenuItem{"", "New", [=](){ hosts.view->SetTitle("New Session"); hosts_nav->Show(true); } }); 
    sessions_menu = make_unique<SystemMenuView>("Sessions", v);
    sessions_menu->Show();
  }

  void ShowToysMenu() {
    interfacesettings_nav->PopAll();
    interfacesettings_nav->Show(false);
    my_app->toys_menu->Show();
  }

  void ShowInterfaceSettings() {
    if (auto t = GetActiveTerminalTab()) t->ChangeShader("none");
    if (!connected_host_id || interfacesettings_nav->shown) return;
    MyAppSettingsModel app_model(&settings_db);
    MyHostModel host_model(&host_db, &credential_db, &settings_db, connected_host_id);
    interfacesettings_nav->PopAll();
    if (host_model.protocol == LTerminal::Protocol_RFB) {
      rfbinterfacesettings.UpdateViewFromModel(app_model, host_model.settings);
      interfacesettings_nav->PushTable(rfbinterfacesettings.view.get());
    } else {
      terminalinterfacesettings.UpdateViewFromModel(app_model, host_model.settings);
      interfacesettings_nav->PushTable(terminalinterfacesettings.view.get());
    }
    interfacesettings_nav->Show(true);
  }

  void ShowAppSettings() {
    settings.UpdateViewFromModel(MyAppSettingsModel(&settings_db));
    hosts_nav->PushTable(settings.view.get());
    // if (!app->OpenSystemAppPreferences()) {}
  }

  void ShowProtocolSettings(LTerminal::Protocol proto) {
    switch(proto) {
       case LTerminal::Protocol_LocalShell: hosts_nav->PushTable(localshellsettings.view.get()); break;
       case LTerminal::Protocol_Telnet:     hosts_nav->PushTable(telnetsettings    .view.get()); break;
       case LTerminal::Protocol_RFB:        hosts_nav->PushTable(vncsettings       .view.get()); break;
       case LTerminal::Protocol_SSH:        hosts_nav->PushTable(sshsettings       .view.get()); break;
       default: ERROR("unknown protocol ", proto); break;
    }
  }

  void ShowNewSSHPortForward() {
    hosts_nav->PushTable(sshportforward.view.get());
  }

  void ShowNewHost() {
    ResetSettingsView();
    hosts_nav->PushTable(newhost.view.get());
  }

  void ShowQuickConnect() {
    ResetSettingsView();
    hosts_nav->PushTable(quickconnect.view.get());
  }

  void StartShell() {
    connected_host_id = 1;
    GetActiveWindow()->AddTerminalTab()->UseShellTerminalController("");
    MenuStartSession();
    app->scheduler.Wakeup(app->focused);
  }

  void QuickConnect() {
    hosts_nav->PopTable(1);
    connected_host_id = 0;
    MyHostModel host;
    quickconnect.UpdateModelFromView(&host, &credential_db);
    UpdateModelFromSettingsView(host.protocol, &host.settings, &host.folder);
    quickconnect.UpdateViewFromModel();
    MenuConnect(host, SSHClient::FingerprintCB(),
                [=](int fingerprint_type, const string &fingerprint){ /* ask to save */ });
  }

  void ConnectHost(int host_id) {
    MyHostModel host(&host_db, &credential_db, &settings_db, (connected_host_id = host_id));
    MenuConnect(host, [=](int fpt, const StringPiece &fp) -> bool {
      return host.FingerprintMatch(fpt, fp.str()) ? true : ShowAcceptFingerprintAlert();
    }, [=](int fpt, const string &fp) mutable {
      if (host.FingerprintMatch(fpt, fp)) return;
      host.SetFingerprint(fpt, fp);
      host.Update(host, &host_db, &credential_db, &settings_db);
    });
  }

  void DeleteKey(int index, int key_id) {
    credential_db.Erase(key_id);
  }

  void DeleteHost(int index, int host_id) {
    MyHostModel host(&host_db, &credential_db, &settings_db, host_id);
    if (host.cred.credtype == LTerminal::CredentialType_Password) credential_db.Erase(host.cred.cred_id);
    host_db.Erase(host.host_id);
  }

  void HostInfo(int host_id) {
    MyHostModel host(&host_db, &credential_db, &settings_db, host_id);
    updatehost.UpdateViewFromModel(host);
    UpdateSettingsViewFromModel(host.protocol, host); 
    hosts_nav->PushTable(updatehost.view.get());
  }

  void NewHostConnect() {
    hosts_nav->PopTable(1);
    connected_host_id = 0;
    MyHostModel host;
    newhost.UpdateModelFromView(&host, &credential_db);
    UpdateModelFromSettingsView(host.protocol, &host.settings, &host.folder);
    newhost.UpdateViewFromModel();
    if (host.displayname.empty()) {
      if (host.protocol == LTerminal::Protocol_LocalShell) host.displayname = "Local Shell";
      else host.displayname = StrCat(host.username.size() ? StrCat(host.username, "@") : "", host.hostname,
                                     host.port != host.DefaultPort() ? StrCat(":", host.port) : " ");
    }
    MenuConnect(host, SSHClient::FingerprintCB(), [=](int fpt, const string &fp) mutable {
      host.SetFingerprint(fpt, fp);           
      connected_host_id = host.SaveNew(&host_db, &credential_db, &settings_db);
    });
  }

  void UpdateHostConnect() {
    hosts_nav->PopToRoot();
    connected_host_id = 0;
    MyHostModel host;
    updatehost.UpdateModelFromView(&host, &credential_db);
    UpdateModelFromSettingsView(host.protocol, &host.settings, &host.folder);
    if (host.cred.credtype == CredentialType_PEM)
      if (!(host.cred.cred_id = updatehost.view->GetTag(0, 4))) host.cred.credtype = CredentialType_Ask;
    MenuConnect(host, [=](int fpt, const StringPiece &fp) -> bool {
        return updatehost.prev_model.FingerprintMatch(fpt, fp.str()) ? true : ShowAcceptFingerprintAlert();
      }, [=](int fpt, const string &fp) mutable {
        host.SetFingerprint(fpt, fp);
        connected_host_id = host.Update(updatehost.prev_model, &host_db, &credential_db,
                                        &settings_db);
      });
  }

  void MenuStartSession() {
    hosts_nav->Show(false);
    hosts.view->AddNavigationButton(HAlign::Left, TableItem("Back", TableItem::Button, "", "", 0, 0, 0, [=](){ hosts_nav->Show(false); }));
    keyboard_toolbar->Show(true);
    app->CloseTouchKeyboardAfterReturn(false);
    app->OpenTouchKeyboard();
  }

  void MenuConnect(const MyHostModel &host,
                   SSHClient::FingerprintCB fingerprint_cb=SSHClient::FingerprintCB(),
                   SSHTerminalController::SavehostCB cb=SSHTerminalController::SavehostCB()) {
    if (host.protocol == LTerminal::Protocol_SSH) {
      shared_ptr<SSHClient::Identity> identity;
      if (host.cred.credtype == LTerminal::CredentialType_PEM) {
        CHECK(host.cred.cred_id);
        auto it = identity_loaded.find(host.cred.cred_id);
        if (it != identity_loaded.end()) identity = it->second;
        else {
          bool password_needed = false;
          identity = make_shared<SSHClient::Identity>();
          Crypto::ParsePEM(host.cred.creddata.c_str(), &identity->rsa, &identity->dsa, &identity->ec,
                           &identity->ed25519, [&](string v) { password_needed = true; return ""; });
          if (!password_needed) identity_loaded[host.cred.cred_id] = identity;
          else return my_app->passphrase_alert->ShowCB
            ("Identity passphrase", "Passphrase", "", [=](const string &v) {
               shared_ptr<SSHClient::Identity> new_identity = make_shared<SSHClient::Identity>();
               if (!Crypto::ParsePEM(host.cred.creddata.c_str(), &new_identity->rsa, &new_identity->dsa,
                                     &new_identity->ec, &new_identity->ed25519, [=](string) { return v; })) return;
               identity_loaded[host.cred.cred_id] = new_identity;
               MenuConnect(host, fingerprint_cb, cb);
             });
        }
      }
      GetActiveWindow()->AddTerminalTab()->UseSSHTerminalController
        (SSHClient::Params{ host.Hostport(), host.username, host.settings.terminal_type,
         host.settings.startup_command.size() ? StrCat(host.settings.startup_command, "\r") : "",
         host.settings.compression, host.settings.agent_forwarding, host.settings.close_on_disconnect, 
         host.settings.local_forward, host.settings.remote_forward },
         host.cred.credtype == LTerminal::CredentialType_Password ? host.cred.creddata : "", identity, 
         bind(&SystemToolbarView::ToggleButton, keyboard_toolbar.get(), _1), move(cb), move(fingerprint_cb));
    } else if (host.protocol == LTerminal::Protocol_Telnet) {
      GetActiveWindow()->AddTerminalTab()->UseTelnetTerminalController
        (host.Hostport(), (bool(cb) ? Callback([=](){ cb(0, ""); }) : Callback()));
    } else if (host.protocol == LTerminal::Protocol_RFB) {
      GetActiveWindow()->AddRFBTab(RFBClient::Params{host.Hostport(), host.username},
                                   host.cred.credtype == LTerminal::CredentialType_Password ? host.cred.creddata : "",
                                   (bool(cb) ? Callback([=](){ cb(0, ""); }) : Callback()));
    } else if (host.protocol == LTerminal::Protocol_LocalShell) {
      StartShell();
      if (cb) cb(0, "");
    }
    MenuStartSession();
  }

  bool ShowAcceptFingerprintAlert() {
    my_app->hostkey_alert->ShowCB("Accept fingerprint", "", "", bind(&MyTerminalMenus::AcceptFingerprintCB, this, _1));
    return false;
  }

  void AcceptFingerprintCB(const string&) {
    if (auto t = GetActiveTerminalTab())
      if (auto ssh = dynamic_cast<SSHTerminalController*>(t->controller.get()))
        SSHClient::AcceptHostKeyAndBeginAuthRequest(ssh->conn);
  }
};

}; // namespace LFL
#endif // LFL_TERM_TERM_MENU_H__
