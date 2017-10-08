/*
 * $Id$
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
  bool agent_forwarding, compression, close_on_disconnect, hide_statusbar;
  string terminal_type, startup_command, font_name, color_scheme, keyboard_theme, prompt;
  LTerminal::BeepType beep_type;
  LTerminal::TextEncoding text_encoding;
  LTerminal::EnterMode enter_mode;
  LTerminal::DeleteMode delete_mode;
  StringPairVec toolbar;
  vector<SSHClient::Params::Forward> local_forward, remote_forward;

  MyHostSettingsModel() { Load(); }
  MyHostSettingsModel(MySettingsDB *settings_db, int id) { Load(settings_db, id); }

  void Load() {
    settings_id = autocomplete_id = 0;
    agent_forwarding = close_on_disconnect = 0;
    hide_statusbar   = !ANDROIDOS;
    compression      = 1;
    terminal_type    = "xterm-color";
    startup_command  = "";
    font_name        = FLAGS_font;
    font_size        = 15;
    color_scheme     = "VGA";
    keyboard_theme   = "Light";
    beep_type        = LTerminal::BeepType_None;
    text_encoding    = LTerminal::TextEncoding_UTF8;
    delete_mode      = LTerminal::DeleteMode_Normal;
    prompt           = "$";
  }

  void Load(MySettingsDB *settings_db, int id) {
    LoadProto(*flatbuffers::GetRoot<LTerminal::HostSettings>
              (FindRefOrDie(settings_db->data, (settings_id = id)).blob.data()));
  }
  
  void LoadProto(const LTerminal::HostSettings &r) {
    agent_forwarding = r.agent_forwarding();
    compression = r.compression();
    close_on_disconnect = r.close_on_disconnect();
    terminal_type = GetFlatBufferString(r.terminal_type());
    startup_command = GetFlatBufferString(r.startup_command());
    font_name = r.font_name() ? r.font_name()->data() : FLAGS_font;
    font_size = r.font_size();
    color_scheme = GetFlatBufferString(r.color_scheme());
    keyboard_theme = GetFlatBufferString(r.keyboard_theme());
    beep_type = r.beep_type();
    text_encoding = r.text_encoding();
    enter_mode = r.enter_mode();
    delete_mode = r.delete_mode();
    autocomplete_id = r.autocomplete_id();
    prompt = GetFlatBufferString(r.prompt_string());
    if (auto ti = r.toolbar_items())  for (auto i : *ti) toolbar.emplace_back(GetFlatBufferString(i->key()), GetFlatBufferString(i->value()));
    if (auto lf = r.local_forward())  for (auto i : *lf) local_forward .push_back({ i->port(), GetFlatBufferString(i->target()), i->target_port() });
    if (auto rf = r.remote_forward()) for (auto i : *rf) remote_forward.push_back({ i->port(), GetFlatBufferString(i->target()), i->target_port() });
    hide_statusbar = flatbuffers::IsFieldPresent(&r, LTerminal::HostSettings::VT_HIDE_STATUSBAR) ? r.hide_statusbar() : !ANDROIDOS;
  }

  flatbuffers::Offset<LTerminal::HostSettings> SaveProto(FlatBufferBuilder &fb) const {
    vector<flatbuffers::Offset<LTerminal::ToolbarItem>> tb;
    vector<flatbuffers::Offset<LTerminal::PortForward>> lf, rf;
    for (auto &i : toolbar)        tb.push_back(LTerminal::CreateToolbarItem(fb, fb.CreateString(i.first), fb.CreateString(i.second)));
    for (auto &i : local_forward)  lf.push_back(LTerminal::CreatePortForward(fb, i.port, fb.CreateString(i.target_host), i.target_port));
    for (auto &i : remote_forward) rf.push_back(LTerminal::CreatePortForward(fb, i.port, fb.CreateString(i.target_host), i.target_port));
    fb.ForceDefaults(true);
    return LTerminal::CreateHostSettings
      (fb, agent_forwarding, compression, close_on_disconnect, fb.CreateString(terminal_type),
       fb.CreateString(startup_command), fb.CreateString(font_name), font_size, fb.CreateString(color_scheme),
       fb.CreateString(keyboard_theme), beep_type, text_encoding, enter_mode, delete_mode,
       tb.size() ? fb.CreateVector(tb) : 0, lf.size() ? fb.CreateVector(lf) : 0,
       rf.size() ? fb.CreateVector(rf) : 0, autocomplete_id, fb.CreateString(prompt), hide_statusbar);
  }

  FlatBufferPiece SaveBlob() const {
    return CreateFlatBuffer<LTerminal::HostSettings>(bind(&MyHostSettingsModel::SaveProto, this, _1));
  }
};

struct MyAppSettingsModel {
  static const int LatestVersion = 1;
  int version = LatestVersion, background_timeout = 180;
  bool keep_display_on=0;
  MyHostSettingsModel default_host_settings;

  MyAppSettingsModel() {}
  MyAppSettingsModel(MySettingsDB *settings_db) { Load(settings_db); }

  void Load() { *this = MyAppSettingsModel(); }
  void Load(MySettingsDB *settings_db) {
    auto s = flatbuffers::GetRoot<LTerminal::AppSettings>(FindRefOrDie(settings_db->data, 1).blob.data());
    CHECK(s->default_host_settings());
    version = s->version();
    default_host_settings.LoadProto(*s->default_host_settings());
    keep_display_on = s->keep_display_on();
    background_timeout = s->background_timeout();
  }

  flatbuffers::Offset<LTerminal::AppSettings> SaveProto(FlatBufferBuilder &fb) const {
    fb.ForceDefaults(true);
    return LTerminal::CreateAppSettings(fb, version, default_host_settings.SaveProto(fb), keep_display_on,
                                        background_timeout);
  }

  FlatBufferPiece SaveBlob() const {
    return CreateFlatBuffer<LTerminal::AppSettings>(bind(&MyAppSettingsModel::SaveProto, this, _1));
  }

  void Save(MySettingsDB *settings_db) const { settings_db->Update(1, SaveBlob()); }
};

struct MyCredentialModel {
  int cred_id;
  LTerminal::CredentialType credtype;
  string creddata, name, gentype, gendate;

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
      auto cred = flatbuffers::GetRoot<LTerminal::Credential>(it->second.blob.data());
      credtype = cred->type();
      creddata = GetFlatBufferString(cred->data());
      name = GetFlatBufferString(cred->displayname());
      gendate = GetFlatBufferString(cred->gendate());
      gentype = GetFlatBufferString(cred->gentype());
    } else Load();
  }

  flatbuffers::Offset<LTerminal::Credential> SaveProto(FlatBufferBuilder &fb) const {
    return LTerminal::CreateCredential
      (fb, credtype, fb.CreateVector(reinterpret_cast<const uint8_t*>(creddata.data()), creddata.size()),
       fb.CreateString(name), fb.CreateString(gendate), fb.CreateString(gentype)); 
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
    if      (x == LS("password")) return CredentialType_Password;
    else if (x == LS("key"))      return CredentialType_PEM;
    else                          return CredentialType_Ask;
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
  MyHostModel(int id, const LTerminal::Host *host) : host_id(id), fingerprint_type(0) { LoadTarget(host); }

  bool TargetEqual(const MyHostModel &h) const { return hostname==h.hostname && port == h.port && username==h.username; }

  string Hostport() const {
    string ret = hostname;
    if (port != 22) StrAppend(&ret, ":", port);
    return ret;
  }

  void SetProtocol(const string &p) {
    if      (p == LS("ssh"))         { protocol = LTerminal::Protocol_SSH; }
    else if (p == LS("vnc"))         { protocol = LTerminal::Protocol_RFB;        username.clear(); }
    else if (p == LS("telnet"))      { protocol = LTerminal::Protocol_Telnet;     username.clear(); cred.Load(); }
    else if (p == LS("local_shell")) { protocol = LTerminal::Protocol_LocalShell; username.clear(); cred.Load(); }
    else { FATAL("unknown protocol '", p, "'"); }
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
    auto host = flatbuffers::GetRoot<LTerminal::Host>(FindRefOrDie(host_db->data, (host_id = id)).blob.data());
    LoadTarget(host);
    displayname = GetFlatBufferString(host->displayname());
    folder      = GetFlatBufferString(host->folder()); 
    fingerprint = GetFlatBufferString(host->fingerprint());
    fingerprint_type = host->fingerprint_type();
    CHECK(host->settings_id());
    settings.Load(settings_db, host->settings_id());

    if (!host->credential() || host->credential()->db() != LTerminal::CredentialDBType_Table) cred.Load();
    else cred.Load(cred_db, host->credential()->id());
  }

  void LoadTarget(const LTerminal::Host *host) {
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
    username = GetFlatBufferString(host->username());
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

  int Save(MyHostDB *host_db, const LTerminal::CredentialRef &credref, int settings_row_id, int row_id=0, bool update_date=0) const {
    if (!row_id) row_id = host_db->Insert(        SaveBlob(credref, settings_row_id));
    else                  host_db->Update(row_id, SaveBlob(credref, settings_row_id), update_date);
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
             MyHostDB *host_db, MyCredentialDB *cred_db, MySettingsDB *settings_db, bool update_date=0) const {
    int cred_row_id = prevhost.cred.cred_id;
    if (prevhost.cred.credtype == CredentialType_Password) {
      if (cred.credtype == CredentialType_Password) {
        MyCredentialModel(CredentialType_Password, cred.creddata).Save(cred_db, cred_row_id);
      } else {
        cred_db->Erase(cred_row_id);
        if (cred.credtype == CredentialType_PEM) cred_row_id = cred.cred_id;
      }
    } else if (cred.credtype == CredentialType_Password) {
      cred_row_id = MyCredentialModel(CredentialType_Password, cred.creddata).Save(cred_db);
    } else if (cred.credtype == CredentialType_PEM) cred_row_id = cred.cred_id;
    LTerminal::CredentialRef credref
      (cred.credtype == CredentialType_Ask ? CredentialDBType_Null : CredentialDBType_Table, cred_row_id);
    settings_db->Update(prevhost.settings.settings_id, settings.SaveBlob());
    return Save(host_db, credref, prevhost.settings.settings_id, prevhost.host_id, update_date);
  }
};

struct MyGenKeyModel {
  string name, pw, algo;
  int bits=0;
};

struct MyTableViewController : public TableViewController {
  MyTableViewController(MyTerminalMenus*, unique_ptr<TableViewInterface> = unique_ptr<TableViewInterface>());
};

struct MyAddToolbarItemViewController : public MyTableViewController {
  MyAddToolbarItemViewController(MyTerminalMenus*);
};

struct MyKeyboardSettingsViewController : public MyTableViewController {
  MyTerminalMenus *menus;
  MyKeyboardSettingsViewController(MyTerminalMenus*);
  void UpdateViewFromModel(const MyHostSettingsModel&);
  void UpdateModelFromView(MyHostSettingsModel*) const;
};

struct MyNewKeyViewController : public MyTableViewController {
  MyNewKeyViewController(MyTerminalMenus*);
};

struct MyGenKeyViewController : public MyTableViewController {
  TableSectionInterface::ChangeSet algo_deps;
  MyGenKeyViewController(MyTerminalMenus*);
  void UpdateViewFromModel();
  bool UpdateModelFromView(MyGenKeyModel *model) const;
  void ApplyAlgoChangeSet(const string &n) { view->ApplyChangeSet(n, algo_deps); }
};

struct MyKeyInfoViewController : public MyTableViewController {
  MyTerminalMenus *menus;
  int cred_row_id=0;
  MyKeyInfoViewController(MyTerminalMenus*);
  void UpdateViewFromModel(const MyCredentialModel&);
};

struct MyKeysViewController : public MyTableViewController {
  MyTerminalMenus *menus;
  MyCredentialDB *model;
  MyKeysViewController(MyTerminalMenus*, MyCredentialDB*);
  void UpdateViewFromModel();
};

struct MyAboutViewController : public MyTableViewController {
  MyAboutViewController(MyTerminalMenus*);
};

struct MySupportViewController : public MyTableViewController {
  MySupportViewController(MyTerminalMenus*);
};

struct MyPrivacyViewController : public MyTableViewController {
  MyPrivacyViewController(MyTerminalMenus*);
};

struct MyAppSettingsViewController : public MyTableViewController {
  MyAppSettingsViewController(MyTerminalMenus*);
  void UpdateViewFromModel(const MyAppSettingsModel &model);
  void UpdateModelFromView(      MyAppSettingsModel *model);
};

struct MyTerminalInterfaceSettingsViewController : public MyTableViewController {
  MyTerminalInterfaceSettingsViewController(MyTerminalMenus*);
  void UpdateViewFromModel(const MyHostSettingsModel &host_model);
  void UpdateModelFromView(MyHostSettingsModel *host_model) const;
};

struct MyRFBInterfaceSettingsViewController : public MyTableViewController {
  MyRFBInterfaceSettingsViewController(MyTerminalMenus*);
  static vector<TableItem> GetBaseSchema(MyTerminalMenus*, NavigationViewInterface*);
  static vector<TableItem> GetSchema(MyTerminalMenus*, NavigationViewInterface*);
  void UpdateViewFromModel(const MyHostSettingsModel &host_model);
  void UpdateModelFromView(MyHostSettingsModel *host_model) const;
};

struct MySSHFingerprintViewController : public MyTableViewController {
  MySSHFingerprintViewController(MyTerminalMenus*);
  void UpdateViewFromModel(const MyHostModel &model);
};

struct MySSHPortForwardViewController : public MyTableViewController {
  TableSectionInterface::ChangeSet type_deps;
  MySSHPortForwardViewController(MyTerminalMenus*);
  void ApplyTypeChangeSet(const string &n) { view->ApplyChangeSet(n, type_deps); }
};

struct MySSHSettingsViewController : public MyTableViewController {
  MyTerminalMenus *menus;
  MySSHSettingsViewController(MyTerminalMenus *m);
  static vector<TableItem> GetSchema(MyTerminalMenus*);
  void UpdateViewFromModel(const MyHostModel &model);
  bool UpdateModelFromView(MyHostSettingsModel *model, string *folder) const;
};

struct MyTelnetSettingsViewController : public MyTableViewController {
  MyTerminalMenus *menus;
  MyTelnetSettingsViewController(MyTerminalMenus *m);
  static vector<TableItem> GetSchema(MyTerminalMenus*);
  void UpdateViewFromModel(const MyHostModel &model);
  bool UpdateModelFromView(MyHostSettingsModel *model, string *folder) const;
};

struct MyVNCSettingsViewController : public MyTableViewController {
  MyTerminalMenus *menus;
  MyVNCSettingsViewController(MyTerminalMenus *m);
  static vector<TableItem> GetSchema(MyTerminalMenus*);
  void UpdateViewFromModel(const MyHostModel &model);
  bool UpdateModelFromView(MyHostSettingsModel *model, string *folder) const;
};

struct MyLocalShellSettingsViewController : public MyTableViewController {
  MyTerminalMenus *menus;
  MyLocalShellSettingsViewController(MyTerminalMenus *m);
  static vector<TableItem> GetSchema(MyTerminalMenus*);
  void UpdateViewFromModel(const MyHostModel &model);
  bool UpdateModelFromView(MyHostSettingsModel *model, string *folder) const;
};

struct MyProtocolViewController : public MyTableViewController {
  MyProtocolViewController(MyTerminalMenus*);
};

struct MyQuickConnectViewController {
  static vector<TableItem> GetSchema(MyTerminalMenus*);
  static TableSectionInterface::ChangeSet GetProtoDepends(MyTerminalMenus*);
  static TableSectionInterface::ChangeSet GetAuthDepends(MyTerminalMenus*);
};

struct MyNewHostViewController : public MyTableViewController {
  MyTerminalMenus *menus;
  TableSectionInterface::ChangeSet proto_deps, auth_deps;
  MyNewHostViewController(MyTerminalMenus*);
  static vector<TableItem> GetSchema(MyTerminalMenus*);
  void UpdateViewFromModel();
  bool UpdateModelFromView(MyHostModel *model, MyCredentialDB *cred_db) const;
};

struct MyUpdateHostViewController : public MyTableViewController {
  MyTerminalMenus *menus;
  MyHostModel prev_model;
  TableSectionInterface::ChangeSet proto_deps, auth_deps;
  MyUpdateHostViewController(MyTerminalMenus*);
  static vector<TableItem> GetSchema(MyTerminalMenus*);
  void UpdateViewFromModel(const MyHostModel &host);
  bool UpdateModelFromView(MyHostModel *model, MyCredentialDB *cred_db) const;
};

struct MyHostsViewController : public MyTableViewController {
  MyTerminalMenus *menus;
  bool menu;
  string folder;
  MyHostsViewController(MyTerminalMenus*, bool menu);
  static vector<TableItem> GetBaseSchema(MyTerminalMenus*);
  void LoadFolderUI(MyHostDB *model);
  void LoadLockedUI(MyHostDB *model);
  void LoadUnlockedUI(MyHostDB *model);
  void UpdateViewFromModel(MyHostDB *model);
};

struct MyUpgradeViewController : public MyTableViewController {
  MyTerminalMenus *menus;
  unique_ptr<ProductInterface> product;
  bool loading_product=false, purchasing_product=false;
  MyUpgradeViewController(MyTerminalMenus*, const string&);
  void PurchaseUpgrade();
  void RestorePurchases();
  void HandleSuccessfulUpgrade();
};

struct MyTerminalMenus {
  typedef function<void(TerminalTabInterface*, int, const string&)> SavehostCB;
  bool pro_version = true, db_protected = false, db_opened = false, suspended_timer = false;
  WindowHolder *win;
  ToolkitInterface *toolkit;
  SQLite::Database db;
  MyHostDB host_db;
  MyCredentialDB credential_db;
  MySettingsDB settings_db;
  vector<MyTableViewController*> tableviews;
  unordered_map<int, shared_ptr<SSHClient::Identity>> identity_loaded;
  int key_icon, host_icon, host_locked_icon, bolt_icon, terminal_icon, settings_blue_icon, settings_gray_icon,
      audio_icon, eye_icon, recycle_icon, fingerprint_icon, info_icon, keyboard_icon, folder_icon, logo_image, logo_icon,
      plus_red_icon, plus_green_icon, vnc_icon, locked_icon, unlocked_icon, font_icon, toys_icon,
      arrowleft_icon, arrowright_icon, clipboard_upload_icon, clipboard_download_icon, keygen_icon,
      user_icon, calendar_icon, check_icon, stacked_squares_icon, ex_icon, none_icon;
  FrameBuffer icon_fb;
  string pw_default = StrCat("\x01", LS("ask_each_time")), pw_empty = "lfl_default", pro_product_id = "com.lucidfusionlabs.lterminal.paid", theme;
  PickerItem color_picker = PickerItem{ {{"VGA", "Solarized Dark", "Solarized Light"}}, {0} };
  Color green;

  unique_ptr<TimerInterface>          sessions_update_timer;
  unique_ptr<NavigationViewInterface> hosts_nav, interfacesettings_nav;
  unique_ptr<TextViewInterface>       credits;
  unique_ptr<PurchasesInterface>      purchases;
  MyAddToolbarItemViewController   addtoolbaritem;
  MyKeyboardSettingsViewController keyboardsettings;
  MyNewKeyViewController           newkey;
  MyGenKeyViewController           genkey;
  MyKeyInfoViewController          keyinfo;
  MyKeysViewController             keys;
  MyAboutViewController            about;
  MySupportViewController          support;
  MyPrivacyViewController          privacy;
  MyAppSettingsViewController      settings;
  MyTerminalInterfaceSettingsViewController terminalinterfacesettings;
  MyRFBInterfaceSettingsViewController rfbinterfacesettings;
  MySSHFingerprintViewController   sshfingerprint;
  MySSHPortForwardViewController   sshportforward;
  MySSHSettingsViewController      sshsettings;
  MyTelnetSettingsViewController   telnetsettings;
  MyVNCSettingsViewController      vncsettings;
  MyLocalShellSettingsViewController localshellsettings;
  MyProtocolViewController         protocol;
  MyNewHostViewController          newhost;
  MyUpdateHostViewController       updatehost;
  MyHostsViewController            hosts, hostsfolder;
  unique_ptr<ToolbarViewInterface>    upgrade_toolbar;
  unique_ptr<MyUpgradeViewController> upgrade;
  unique_ptr<AdvertisingViewInterface> advertising;
  unique_ptr<NagInterface>             nag;
  int sessions_update_len = 0;

  unordered_map<string, Callback> mobile_key_cmd = {
    { "[esc]",    bind([=]{ if (auto t = GetActiveTerminalTab()) { t->terminal->Escape();      if (t->controller->frame_on_keyboard_input) app->focused->Wakeup(); } }) },
    { "[tab]",    bind([=]{ if (auto t = GetActiveTerminalTab()) { t->terminal->Tab();         if (t->controller->frame_on_keyboard_input) app->focused->Wakeup(); } }) },
    { "[left]",   bind([=]{ if (auto t = GetActiveTerminalTab()) { t->terminal->CursorLeft();  if (t->controller->frame_on_keyboard_input) app->focused->Wakeup(); } }) },
    { "[right]",  bind([=]{ if (auto t = GetActiveTerminalTab()) { t->terminal->CursorRight(); if (t->controller->frame_on_keyboard_input) app->focused->Wakeup(); } }) },
    { "[up]",     bind([=]{ if (auto t = GetActiveTerminalTab()) { t->terminal->HistUp();      if (t->controller->frame_on_keyboard_input) app->focused->Wakeup(); } }) },
    { "[down]",   bind([=]{ if (auto t = GetActiveTerminalTab()) { t->terminal->HistDown();    if (t->controller->frame_on_keyboard_input) app->focused->Wakeup(); } }) },
    { "[pgup]",   bind([=]{ if (auto t = GetActiveTerminalTab()) { t->terminal->PageUp();      if (t->controller->frame_on_keyboard_input) app->focused->Wakeup(); } }) },
    { "[pgdown]", bind([=]{ if (auto t = GetActiveTerminalTab()) { t->terminal->PageDown();    if (t->controller->frame_on_keyboard_input) app->focused->Wakeup(); } }) },
    { "[home]",   bind([=]{ if (auto t = GetActiveTerminalTab()) { t->terminal->Home();        if (t->controller->frame_on_keyboard_input) app->focused->Wakeup(); } }) },
    { "[end]",    bind([=]{ if (auto t = GetActiveTerminalTab()) { t->terminal->End();         if (t->controller->frame_on_keyboard_input) app->focused->Wakeup(); } }) },
    { "[paste]",  bind([=]{ if (auto t = GetActiveTerminalTab()) { t->terminal->InputString(app->GetClipboardText()); } }) },
    { "[console]",bind([=]{ if (auto w = GetActiveWindow()) { if (!w->root->console) w->root->InitConsole(bind(&MyTerminalWindow::ConsoleAnimatingCB, w)); w->root->shell->console(StringVec()); } }) },
  };

  unordered_map<string, Callback> mobile_togglekey_cmd = {
    { "[ctrl]", bind([&]{ if (auto t = GetActiveTerminalTab()) { t->controller->ctrl_down = !t->controller->ctrl_down; } }) },
    { "[alt]",  bind([&]{ if (auto t = GetActiveTerminalTab()) { t->controller->alt_down  = !t->controller->alt_down;  } }) } };

  StringPairVec default_terminal_toolbar = {
    { "esc",        "[esc]"    },
    { "ctrl",       "[ctrl]"   },
    { "tab",        "[tab]"    },
    { "\U000025C0", "[left]"   },
    { "\U000025B6", "[right]"  },
    { "\U000025B2", "[up]"     }, 
    { "\U000025BC", "[down]"   },
    { "\U000023EB", "[pgup]"   },
    { "\U000023EC", "[pgdown]" }, 
    { "\U000023EA", "[home]"   },
    { "\U000023E9", "[end]"    }, 
  };

  StringPairVec default_rfb_toolbar {
    { "esc",        "[esc]"    },
    { "ctrl",       "[ctrl]"   },
    { "alt",        "[alt]"    },
    { "tab",        "[tab]"    },
    { "\U000025C0", "[left]"   },
    { "\U000025B6", "[right]"  },
    { "\U000025B2", "[up]"     }, 
    { "\U000025BC", "[down]"   },
    { "\U000023EB", "[pgup]"   },
    { "\U000023EC", "[pgdown]" }, 
    { "\U000023EA", "[home]"   },
    { "\U000023E9", "[end]"    }, 
  };

  ~MyTerminalMenus() { SQLite::Close(db); }
  MyTerminalMenus(WindowHolder *w, ToolkitInterface *t) : win(w), toolkit(t),
    db(SQLite::Open(StrCat(app->savedir, "lterm.db"))),
    key_icon               (CheckNotNull(app->LoadSystemImage("key"))),
    host_icon              (CheckNotNull(app->LoadSystemImage("host"))),
    host_locked_icon       (CheckNotNull(app->LoadSystemImage("host_locked"))),
    bolt_icon              (CheckNotNull(app->LoadSystemImage("bolt"))),
    terminal_icon          (CheckNotNull(app->LoadSystemImage("terminal"))),
    settings_blue_icon     (CheckNotNull(app->LoadSystemImage("settings_blue"))),
    settings_gray_icon     (CheckNotNull(app->LoadSystemImage("settings_gray"))),
    audio_icon             (CheckNotNull(app->LoadSystemImage("audio"))),
    eye_icon               (CheckNotNull(app->LoadSystemImage("eye"))),
    recycle_icon           (CheckNotNull(app->LoadSystemImage("recycle"))),
    fingerprint_icon       (CheckNotNull(app->LoadSystemImage("fingerprint"))),
    info_icon              (CheckNotNull(app->LoadSystemImage("info"))),
    keyboard_icon          (CheckNotNull(app->LoadSystemImage("keyboard"))),
    folder_icon            (CheckNotNull(app->LoadSystemImage("folder"))),
    logo_image             (CheckNotNull(app->LoadSystemImage("logo"))),
    logo_icon              (CheckNotNull(app->LoadSystemImage("logo_icon"))),
    plus_red_icon          (CheckNotNull(app->LoadSystemImage("plus_red"))),
    plus_green_icon        (CheckNotNull(app->LoadSystemImage("plus_green"))),
    vnc_icon               (CheckNotNull(app->LoadSystemImage("vnc"))),
    locked_icon            (CheckNotNull(app->LoadSystemImage("locked"))),
    unlocked_icon          (CheckNotNull(app->LoadSystemImage("unlocked"))),
    font_icon              (CheckNotNull(app->LoadSystemImage("font"))),
    toys_icon              (CheckNotNull(app->LoadSystemImage("toys"))),
    arrowleft_icon         (CheckNotNull(app->LoadSystemImage("arrowleft"))),
    arrowright_icon        (CheckNotNull(app->LoadSystemImage("arrowright"))),
    clipboard_upload_icon  (CheckNotNull(app->LoadSystemImage("clipboard_upload"))),
    clipboard_download_icon(CheckNotNull(app->LoadSystemImage("clipboard_download"))),
    keygen_icon            (CheckNotNull(app->LoadSystemImage("keygen"))),
    user_icon              (CheckNotNull(app->LoadSystemImage("user"))),
    calendar_icon          (CheckNotNull(app->LoadSystemImage("calendar"))),
    check_icon             (CheckNotNull(app->LoadSystemImage("check"))),
    stacked_squares_icon   (CheckNotNull(app->LoadSystemImage("stacked_squares_blue"))),
    ex_icon                (CheckNotNull(app->LoadSystemImage("ex"))),
    none_icon              (CheckNotNull(app->LoadSystemImage("none"))),
    icon_fb(app->focused), theme(Application::GetSetting("theme")), green(76, 217, 100),
    sessions_update_timer(SystemToolkit::CreateTimer(bind(&MyTerminalMenus::UpdateMainMenuSessionsSectionTimer, this))),
    hosts_nav(app->system_toolkit->CreateNavigationView(app->focused, "", theme)),
    interfacesettings_nav(app->system_toolkit->CreateNavigationView(app->focused, "", theme)), addtoolbaritem(this),
    keyboardsettings(this), newkey(this), genkey(this), keyinfo(this), keys(this, &credential_db), about(this),
    support(this), privacy(this), settings(this), terminalinterfacesettings(this), rfbinterfacesettings(this),
    sshfingerprint(this), sshportforward(this), sshsettings(this), telnetsettings(this), vncsettings(this),
    localshellsettings(this), protocol(this), newhost(this), updatehost(this), hosts(this, true),
    hostsfolder(this, false) {

#if defined(LFL_IOS) || defined(LFL_ANDROID)
    INFO("Loading in-app purchases");
#if defined(LFL_IOS)
    purchases = SystemToolkit::CreatePurchases(app, "");
#elif defined(LFL_ANDROID)
    purchases = SystemToolkit::CreatePurchases
      (app, "MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAm0qPVsRM2qpu87og/8y72CFMbtj+/IciLTaeUgIB60x+vCD"
       "/F+O3sr+/Vf/tHUVOmKLb+wfyupcEqP1H6lbIGWi4/XqqIAMG7opqoxUBY8P6OShxAK0IKE204Rei3pupr9NSQV53q6"
       "r7qsYKpGNSztzhYJVMmV8/PxwWF/j//sFyvNn0duuLOdYXGDiIHyrSgzjoHdMXhGb28ZDjGYV4jeeS+x4rK6lpaswfJ"
       "PX6UZS7JLEho/CqlsP4OuhiPjayYcrA0kqsjpW+K7/nBsv0Y+qN3I1DcrxySusXLgC7UgtEQ30iOkU4o1GyrrYWPnTQ"
       "yuAPT/FLzq25+lBngRXoYwIDAQAB");
#endif
    if (!(pro_version = purchases->HavePurchase(pro_product_id))) {
      upgrade = make_unique<MyUpgradeViewController>(this, pro_product_id);
      if ((upgrade_toolbar = app->system_toolkit->CreateToolbar(app->focused, theme, MenuItemVec{
        { LS("upgrade_to"), "", [=](){ hosts_nav->PushTableView(upgrade->view.get()); } }
      }, 0))) hosts.view->SetToolbar(upgrade_toolbar.get());
      if ((advertising = SystemToolkit::CreateAdvertisingView
           (AdvertisingViewInterface::Type::BANNER, VAlign::Bottom,
#if defined(LFL_IOS)
            "ca-app-pub-4814825103153665/8426276832", {"41a638aad263424dd2207fdef30f4c10"}
#elif defined(LFL_ANDROID)
            "ca-app-pub-4814825103153665/3996077236", {"0DBA42FB5610516D099B960C0424B343", "52615418751B72981EF42A9681544EDB", "BC7DC25BB8CF7F790300EB28DA44A4DC"}
#endif
           ))) advertising->Show(hosts.view.get(), true);

      nag = SystemToolkit::CreateNag(
#if defined(LFL_IOS)
                                     "1193359415",
#else
                                     app->name,
#endif
                                     7, 10, -1, 5);
    } else {
      hosts.view->SetTitle(LS("pro_name"));
      about.view->SetHeader(0, TableItem(LS("pro_name"), TableItem::Separator, "", "", 0, logo_image));
    }
#endif

    if (UnlockEncryptedDatabase(pw_empty)) hosts.LoadUnlockedUI(&host_db);
    else if ((db_protected = true))        hosts.LoadLockedUI  (&host_db);
    hostsfolder.LoadFolderUI(&host_db);
    app->open_url_cb = bind(&MyTerminalMenus::OpenURL, this, _1);
  }

  void PressKey (const string &key) { FindAndDispatch(mobile_key_cmd,       key); }
  void ToggleKey(const string &key) { FindAndDispatch(mobile_togglekey_cmd, key); }

  unique_ptr<ToolbarViewInterface> CreateKeyboardToolbar(const StringPairVec &v, const string &kb_theme) {
    MenuItemVec ret;
    for (auto &i : v) {
      if      (Contains(mobile_key_cmd,       i.second)) ret.push_back({ i.first, "",       bind(&MyTerminalMenus::PressKey,  this, i.second) });
      else if (Contains(mobile_togglekey_cmd, i.second)) ret.push_back({ i.first, "toggle", bind(&MyTerminalMenus::ToggleKey, this, i.second) });
      else ret.push_back({ i.first, "", [=](){ if (auto t = GetActiveTerminalTab()) { t->terminal->InputString(i.second); if (t->controller->frame_on_keyboard_input) app->focused->Wakeup(); } } });
    }
    return CreateToolbar(kb_theme, move(ret), ToolbarViewInterface::BORDERLESS_BUTTONS);
  }

  unique_ptr<ToolbarViewInterface> CreateToolbar(const string &theme, MenuItemVec items, int flags) {
#ifdef LFL_ANDROID
    items.insert(items.begin(), { "\U000025FB", "", bind(&MyTerminalMenus::ShowMainMenu, this, true), 0 });
#else
    items.insert(items.begin(), { "", "", bind(&MyTerminalMenus::ShowMainMenu, this, true), stacked_squares_icon });
#endif
    items.push_back({ "\U00002699", "", bind(&MyTerminalMenus::ShowInterfaceSettings, this) });
    return app->system_toolkit->CreateToolbar(app->focused, theme, move(items), flags);
  }

  bool UnlockEncryptedDatabase(const string &pw) {
    if (!(db_opened = SQLite::UsePassphrase(db, pw))) return false;
    host_db       = MyHostDB      (&db, "host");
    credential_db = MyCredentialDB(&db, "credential");
    settings_db   = MySettingsDB  (&db, "settings");
    if (settings_db.data.find(1) == settings_db.data.end()) {
      CHECK_EQ(1, settings_db.Insert(MyAppSettingsModel().SaveBlob()));
      CHECK_EQ(1, MyHostModel().SaveNew(&host_db, &credential_db, &settings_db));
    }
    ApplyGlobalSettings();
    return true;
  }
  
  void DisableLocalEncryption() {
    SQLite::ChangePassphrase(db, pw_empty);
    db_protected = false;
    settings.view->show_cb();
  }

  void EnableLocalEncryption(const string &pw, const string &confirm_pw) {
    if (pw != confirm_pw) return my_app->info_alert->ShowCB(LS("invalid_passphrase"), LS("passphrase_failed"), "", StringCB());
    SQLite::ChangePassphrase(db, pw);
    db_protected = true;
    settings.view->show_cb();
  }

  void ApplyGlobalSettings() {
    MyAppSettingsModel global_settings(&settings_db);
    my_app->background_timeout = global_settings.background_timeout;
    app->SetKeepScreenOn(global_settings.keep_display_on);
  }

  void ChangeTheme(const string &v) {
    theme = v;
    app->SetTheme(v);
    hosts_nav->SetTheme(v);
    interfacesettings_nav->SetTheme(v);
    if (upgrade_toolbar) upgrade_toolbar->SetTheme(v);
    for (auto &i : tableviews) i->view->SetTheme(v); 
    Application::SaveSettings(StringPairVec{ StringPair("theme", v) });
  }

  void ResetSettingsView() {
    MyHostModel host;
    localshellsettings.UpdateViewFromModel(host); 
    telnetsettings.UpdateViewFromModel(host); 
    vncsettings.UpdateViewFromModel(host); 
    sshsettings.UpdateViewFromModel(host); 
    sshfingerprint.UpdateViewFromModel(host);
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
  
  void UpdateSettingsViewFromModel(LTerminal::Protocol proto, const MyHostModel &model) {
    switch(proto) {
       case LTerminal::Protocol_LocalShell: localshellsettings.UpdateViewFromModel(model); break;
       case LTerminal::Protocol_Telnet:     telnetsettings    .UpdateViewFromModel(model); break;
       case LTerminal::Protocol_RFB:        vncsettings       .UpdateViewFromModel(model); break;
       case LTerminal::Protocol_SSH: sshsettings.UpdateViewFromModel(model); sshfingerprint.UpdateViewFromModel(model); break;
       default: ERROR("unknown protocol ", proto); break;
    }
  }

  int GenerateKey() {
    MyGenKeyModel gk;
    genkey.UpdateModelFromView(&gk);
    hosts_nav->PopView(1);

    string pubkey, privkey;
    if (!Crypto::GenerateKey(gk.algo, gk.bits, gk.pw, gk.pw, &pubkey, &privkey)) return ERRORv(0, "generate ", gk.algo, " key");

    MyCredentialModel cred(CredentialType_PEM, privkey, gk.name);
    cred.gentype = StrCat(gk.algo, " Key");
    cred.gendate = localhttptime(Now());
    int row_id = cred.Save(&credential_db);
    keys.view->show_cb();

    shared_ptr<SSHClient::Identity> new_identity = make_shared<SSHClient::Identity>();
    if (!Crypto::ParsePEM(cred.creddata.c_str(), &new_identity->rsa, &new_identity->dsa,
                          &new_identity->ec, &new_identity->ed25519, [&](string) { return gk.pw; })) return ERRORv(0, "load just generated key failed");
    identity_loaded[row_id] = new_identity;
    return row_id;
  }
  
  void PasteKey() {
    const char *pems=0, *peme=0, *pemhe=0;
    string pem = app->GetClipboardText();
    string pemtype = Crypto::ParsePEMHeader(pem.data(), &pems, &peme, &pemhe);
    if (pemtype.size()) {
      int row_id = MyCredentialModel(CredentialType_PEM, pem, pemtype).Save(&credential_db);
      keys.view->show_cb();
    } else {
      my_app->info_alert->ShowCB(LS("paste_key_failed"), LS("load_key_failed"), "", StringCB());
    }
  }
  
  void KeyInfo(int cred_row_id) {
    if (!cred_row_id) return;
    keyinfo.UpdateViewFromModel(MyCredentialModel(&credential_db, cred_row_id));
    hosts_nav->PushTableView(keyinfo.view.get());
  }

  void CopyKeyToClipboard(int cred_row_id, bool private_key) {
    if (!cred_row_id) return;
    MyCredentialModel cred(&credential_db, cred_row_id);
    if (cred.credtype == CredentialType_PEM) {
      if (private_key) {
        app->SetClipboardText(cred.creddata);
        my_app->info_alert->ShowCB(LS("copied_to_clipboard"), LS("copied_private_key_to_clipboard"), "", StringCB());
      } else {
        shared_ptr<SSHClient::Identity> identity = LoadIdentity(cred);
        if (!identity) return LoadNewIdentity
          (cred, [=](shared_ptr<SSHClient::Identity> new_identity){ CopyPublicKeyToClipboard(new_identity); });
        CopyPublicKeyToClipboard(identity);
      }
    }
  }

  void CopyPublicKeyToClipboard(shared_ptr<SSHClient::Identity> identity) {
    string comment;
    if      (identity->ed25519) app->SetClipboardText(Ed25519OpenSSHPublicKey(identity->ed25519, comment));
    else if (identity->ec)      app->SetClipboardText(ECDSAOpenSSHPublicKey  (identity->ec,      comment));
    else if (identity->rsa)     app->SetClipboardText(RSAOpenSSHPublicKey    (identity->rsa,     comment));
    else if (identity->dsa)     app->SetClipboardText(DSAOpenSSHPublicKey    (identity->dsa,     comment));
    my_app->info_alert->ShowCB(LS("copied_to_clipboard"), LS("copied_public_key_to_clipboard"), "", StringCB());
  }

  void ChooseKey(int cred_row_id) {
    hosts_nav->PopView(1);
    TableViewInterface *host_menu = hosts_nav->Back();
    CHECK(host_menu);
    int key_row = 2 + (host_menu->GetKey(0, 0) == LS("nickname"));
    host_menu->BeginUpdates();
    if (cred_row_id) {
      MyCredentialModel cred(&credential_db, cred_row_id);
      host_menu->ApplyChangeSet("Key", newhost.auth_deps);
      host_menu->SetTag(0, key_row, cred.cred_id);
      host_menu->SetValue(0, key_row, cred.name);
    } else {
      host_menu->ApplyChangeSet("Password", newhost.auth_deps);
      host_menu->SetValue(0, key_row, pw_default);
    }
    host_menu->EndUpdates();
  }

  void ChooseProtocol(const string &n) {
    hosts_nav->PopView(1);
    TableViewInterface *host_menu = hosts_nav->Back();
    CHECK(host_menu);
    host_menu->BeginUpdates();
    host_menu->ApplyChangeSet(n, newhost.proto_deps);
    host_menu->EndUpdates();
  }

  void ShowMainMenu(bool back) {
    if (auto t = GetActiveTab()) {
      t->ChangeShader("none");
      UpdateTabThumbnailSystemImage(t, Box(128, 128));
    }

    hosts.view->BeginUpdates();
    ReplaceMainMenuSessionsSection();
    hosts.view->SetTitle(pro_version ? LS("pro_name") : LS("app_name")); 
    if (!back) hosts.view->DelNavigationButton(HAlign::Left);
    else       hosts.view->AddNavigationButton
      (HAlign::Left, TableItem(LS("back"), TableItem::Button, "", "", 0, 0, 0, bind(&MyTerminalMenus::HideMainMenu, this)));
    hosts.view->EndUpdates();

    app->CloseTouchKeyboard();
    hosts_nav->Show(true);
    app->ShowSystemStatusBar(true);
    sessions_update_timer->Run(Seconds(1), true);
  }

  void ReplaceMainMenuSessionsSection() {
    Time now = Now();
    vector<TableItem> section;
    auto tw = GetActiveWindow();
    int count = 0, selected_row = -1;

    for (auto t : tw->tabs.tabs) {
      int section_ind = section.size(), conn_state = t->GetConnectionState();
      if (t == tw->tabs.top) selected_row = count;

      section.emplace_back
        (t->title, TableItem::Command,
         t->networked ? (t->connected != Time::zero() ? StrCat(LS("connected"), " ", intervalminutes(now - t->connected)) : LS(tolower(Connection::StateName(conn_state)).c_str())) : "",
         "", 0, t->thumbnail_system_image, ex_icon, [=](){
           HideMainMenu();
           tw->tabs.SelectTab(t);
           tw->root->Wakeup();
         }, [=](const string&){
           t->deleted_cb();
           hosts.view->BeginUpdates();
           hosts.view->SetHidden(0, section_ind, true);
           hosts.view->EndUpdates();
         }, TableItem::Flag::SubText | TableItem::Flag::ColoredSubText);
      if (t->networked) section.back().font.fg = Connection::ConnectState(conn_state) ? Color(0,255,0) : Color(255,0,0);
    }

    icon_fb.Release();
    sessions_update_len = section.size();
    hosts.view->ReplaceSection
      (0, TableItem(LS("sessions")), TableSectionInterface::Flag::DoubleRowHeight |
       TableSectionInterface::Flag::HighlightSelectedRow | TableSectionInterface::Flag::DeleteRowsWhenAllHidden |
       TableSectionInterface::Flag::ClearLeftNavWhenEmpty, move(section));
    hosts.view->SelectRow(0, selected_row);
  }

  void UpdateTabThumbnailSystemImage(TerminalTabInterface *t, const Box &iconb) {
    Box b(t->GetLastDrawBox().Dimension());
    if (!b.w || !b.h) return;
    icon_fb.Resize(b.w, b.h, FrameBuffer::Flag::CreateGL | FrameBuffer::Flag::CreateTexture);

    Texture screen_tex(app->focused), icon_tex(app->focused);
    GraphicsContext gc(app->focused->gd);
    gc.gd->Clear();
    t->DrawBox(gc.gd, b, false);
    gc.gd->ScreenshotBox(&screen_tex, b, Texture::Flag::FlipY);
    icon_tex.Resize(iconb.w, iconb.h, Texture::updatesystemimage_pf, Texture::Flag::CreateBuf);
    unique_ptr<VideoResamplerInterface> resampler(CreateVideoResampler());
    resampler->Open(b.w, b.h, Texture::preferred_pf, iconb.w, iconb.h, Texture::updatesystemimage_pf);
    resampler->Resample(screen_tex.buf, screen_tex.LineSize(), icon_tex.buf, icon_tex.LineSize(), 0);

    if (!t->thumbnail_system_image) t->thumbnail_system_image = app->LoadSystemImage("");
    app->UpdateSystemImage(t->thumbnail_system_image, icon_tex);
  }

  void UpdateMainMenuSessionsSectionTimer() {
    Time now = Now();
    StringVec val;
    vector<Color> color;
    auto tw = GetActiveWindow();
    bool still_counting = false;
    for (auto t : tw->tabs.tabs) {
      Box b(t->GetLastDrawBox().Dimension());
      if (!b.w || !b.h) continue;
      if (t->networked) {
        if (t->connected != Time::zero()) { 
          still_counting = true;
          color.emplace_back(0,255,0);
          val.emplace_back(StrCat(LS("connected"), " ", intervalminutes(now - t->connected)));
        } else { 
          int conn_state = t->GetConnectionState();
          color.emplace_back(Connection::ConnectState(conn_state) ? Color(0,255,0) : Color(255,0,0));
          val.emplace_back(LS(tolower(Connection::StateName(conn_state)).c_str()));
        }
      } else {
        color.push_back(Color::clear);
        val.emplace_back("");
      }
    }

    hosts.view->BeginUpdates();
    if (sessions_update_len != val.size()) {
      ReplaceMainMenuSessionsSection();
      still_counting = sessions_update_len != 0;
    } else {
      hosts.view->SetSectionColors(0, color);
      hosts.view->SetSectionValues(0, val);
    }
    hosts.view->EndUpdates();

    if (still_counting) sessions_update_timer->Run(Seconds(1), true);
  }

  void HideMainMenu() {
    if (auto t = GetActiveTab()) if (t->hide_statusbar) app->ShowSystemStatusBar(false);
    app->OpenTouchKeyboard();
    hosts_nav->PopToRoot();
    hosts_nav->Show(false);
    app->focused->Wakeup();
  }

  void ShowToysMenu() {
    HideInterfaceSettings();
    my_app->toys_menu->Show();
  }

  void HideInterfaceSettings() {
    if (auto t = GetActiveTab()) if (t->hide_statusbar) app->ShowSystemStatusBar(false);
    app->OpenTouchKeyboard();
    interfacesettings_nav->PopAll();
    interfacesettings_nav->Show(false);
    app->focused->Wakeup();
  }

  void ShowInterfaceSettings() {
    auto t = GetActiveTab();
    if (t) t->ChangeShader("none");
    if (!t || !t->connected_host_id || interfacesettings_nav->shown) return;
    MyHostModel host_model(&host_db, &credential_db, &settings_db, t->connected_host_id);
    keyboardsettings.UpdateViewFromModel(host_model.settings);
    if (host_model.protocol == LTerminal::Protocol_RFB) {
      rfbinterfacesettings.UpdateViewFromModel(host_model.settings);
      interfacesettings_nav->PushTableView(rfbinterfacesettings.view.get());
    } else {
      terminalinterfacesettings.UpdateViewFromModel(host_model.settings);
      interfacesettings_nav->PushTableView(terminalinterfacesettings.view.get());
    }
    app->CloseTouchKeyboard();
    interfacesettings_nav->Show(true);
    app->ShowSystemStatusBar(true);
  }

  void ApplyTerminalSettings(const MyHostSettingsModel &settings) {
    if (auto t = GetActiveTerminalTab()) {
      t->root->default_font.desc.name = settings.font_name;
      t->ChangeColors(settings.color_scheme, false);
      t->SetFontSize(settings.font_size);
      t->terminal->enter_char = settings.enter_mode  == LTerminal::EnterMode_ControlJ  ? '\n' : '\r';
      t->terminal->erase_char = settings.delete_mode == LTerminal::DeleteMode_ControlH ? '\b' : 0x7f;
    }
  }
  
  void ApplyToolbarSettings(const MyHostSettingsModel &settings) {
    if (auto t = GetActiveTab()) {
      if (t->last_toolbar) t->last_toolbar = CreateKeyboardToolbar(settings.toolbar, settings.keyboard_theme);
      else                 t->ChangeToolbar(CreateKeyboardToolbar(settings.toolbar, settings.keyboard_theme));
    }
  }

  void ShowAppSettings() {
    settings.UpdateViewFromModel(MyAppSettingsModel(&settings_db));
    hosts_nav->PushTableView(settings.view.get());
    // if (!app->OpenSystemAppPreferences()) {}
  }

  void ShowProtocolSettings(LTerminal::Protocol proto) {
    switch(proto) {
       case LTerminal::Protocol_LocalShell: hosts_nav->PushTableView(localshellsettings.view.get()); break;
       case LTerminal::Protocol_Telnet:     hosts_nav->PushTableView(telnetsettings    .view.get()); break;
       case LTerminal::Protocol_RFB:        hosts_nav->PushTableView(vncsettings       .view.get()); break;
       case LTerminal::Protocol_SSH:        hosts_nav->PushTableView(sshsettings       .view.get()); break;
       default: ERROR("unknown protocol ", proto); break;
    }
  }

  void ShowNewSSHPortForward() {
    hosts_nav->PushTableView(sshportforward.view.get());
  }

  void ShowNewHost() {
    ResetSettingsView();
    hosts_nav->PushTableView(newhost.view.get());
  }

  void ConnectHost(int host_id) {
    MyHostModel host(&host_db, &credential_db, &settings_db, host_id);
    MenuConnect(host, [=](int fpt, const StringPiece &fpb) -> bool {
      string fp = fpb.str();
      return host.FingerprintMatch(fpt, fp) ? true : ShowAcceptFingerprintAlert(fp);
    }, [=](TerminalTabInterface*, int fpt, const string &fp) mutable {
      if (host.FingerprintMatch(fpt, fp)) { host_db.UpdateDate(host_id, Now()); return; }
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
    if (auto w = GetActiveWindow())
      for (auto t : w->tabs.tabs) if (t->connected_host_id == host_id) t->connected_host_id = 0;
  }

  void HostInfo(int host_id) {
    MyHostModel host(&host_db, &credential_db, &settings_db, host_id);
    updatehost.UpdateViewFromModel(host);
    UpdateSettingsViewFromModel(host.protocol, host); 
    hosts_nav->PushTableView(updatehost.view.get());
  }

  void NewHostConnect() {
    hosts_nav->PopView(1);
    MyHostModel host;
    newhost.UpdateModelFromView(&host, &credential_db);
    UpdateModelFromSettingsView(host.protocol, &host.settings, &host.folder);
    newhost.UpdateViewFromModel();
    NewHostConnectTo(host);
  }

  void NewHostConnectTo(MyHostModel &host) {
    if (host.displayname.empty()) {
      if (host.protocol == LTerminal::Protocol_LocalShell) host.displayname = LS("local_shell");
      else host.displayname = StrCat(host.username.size() ? StrCat(host.username, "@") : "", host.hostname,
                                     host.port != host.DefaultPort() ? StrCat(":", host.port) : " ");
    }
    MenuConnect(host, SSHClient::FingerprintCB(), [=](TerminalTabInterface *tab, int fpt, const string &fp) mutable {
      host.SetFingerprint(fpt, fp);           
      tab->connected_host_id = host.SaveNew(&host_db, &credential_db, &settings_db);
    });
  }

  void UpdateHostConnect() {
    MyHostModel host = updatehost.prev_model;
    updatehost.UpdateModelFromView(&host, &credential_db);
    UpdateModelFromSettingsView(host.protocol, &host.settings, &host.folder);
    MenuConnect(host, [=](int fpt, const StringPiece &fpb) -> bool {
        string fp = fpb.str();
        return updatehost.prev_model.FingerprintMatch(fpt, fp) ? true : ShowAcceptFingerprintAlert(fp);
      }, [=](TerminalTabInterface *tab, int fpt, const string &fp) mutable {
        host.SetFingerprint(fpt, fp);
        tab->connected_host_id =
          host.Update(updatehost.prev_model, &host_db, &credential_db, &settings_db, true);
      });
  }

  void MenuStartSession() {
    app->CloseTouchKeyboardAfterReturn(false);
    app->OpenTouchKeyboard();
    hosts_nav->PopToRoot();
    hosts_nav->Show(false);
    app->focused->Wakeup();
  }

  shared_ptr<SSHClient::Identity> LoadIdentity(const MyCredentialModel &cred) {
    CHECK(cred.cred_id);
    auto it = identity_loaded.find(cred.cred_id);
    if (it != identity_loaded.end()) return it->second;

    bool password_needed = false;
    shared_ptr<SSHClient::Identity> identity = make_shared<SSHClient::Identity>();
    Crypto::ParsePEM(cred.creddata.c_str(), &identity->rsa, &identity->dsa, &identity->ec,
                     &identity->ed25519, [&](string v) { password_needed = true; return ""; });
    if (!password_needed) identity_loaded[cred.cred_id] = identity;
    return password_needed ? nullptr : identity;
  }

  void LoadNewIdentity(const MyCredentialModel &cred, SSHClient::IdentityCB success_cb) {
    my_app->passphrase_alert->ShowCB
      (LS("identity_passphrase"), LS("passphrase"), "", [=](const string &v) {
         shared_ptr<SSHClient::Identity> new_identity = make_shared<SSHClient::Identity>();
         if (!Crypto::ParsePEM(cred.creddata.c_str(), &new_identity->rsa, &new_identity->dsa,
                               &new_identity->ec, &new_identity->ed25519, [=](string) { return v; })) return;
         identity_loaded[cred.cred_id] = new_identity;
         if (success_cb) success_cb(new_identity);
       });
  }

  void MenuConnect(const MyHostModel &host,
                   SSHClient::FingerprintCB fingerprint_cb=SSHClient::FingerprintCB(),
                   SavehostCB cb=SavehostCB()) {
    unique_ptr<ToolbarViewInterface> toolbar = CreateKeyboardToolbar(host.settings.toolbar, host.settings.keyboard_theme);
    if (host.protocol == LTerminal::Protocol_SSH) {
      SSHClient::LoadIdentityCB reconnect_identity_cb;
      if (host.username.empty()) reconnect_identity_cb = [=](shared_ptr<SSHClient::Identity>*) { return false; };
      else if (host.cred.credtype == LTerminal::CredentialType_PEM)
        reconnect_identity_cb = [=](shared_ptr<SSHClient::Identity> *out)
          { *out = LoadIdentity(host.cred); return true; };
      auto tab = GetActiveWindow()->AddTerminalTab(host.host_id, host.settings.hide_statusbar, move(toolbar));
      auto ssh = tab->UseSSHTerminalController
        (SSHClient::Params{ host.Hostport(), host.username, host.settings.terminal_type,
         host.settings.startup_command.size() ? StrCat(host.settings.startup_command, "\r") : "",
         host.settings.compression, host.settings.agent_forwarding, host.settings.close_on_disconnect, true,
         host.settings.local_forward, host.settings.remote_forward }, false,
         host.cred.credtype == LTerminal::CredentialType_Password ? host.cred.creddata : "",
         move(reconnect_identity_cb), bind(move(cb), tab, _1, _2), move(fingerprint_cb));
      ApplyTerminalSettings(host.settings);
      if (host.username.empty()) {
        ssh->identity_cb = [=](shared_ptr<SSHClient::Identity> *out) { 
          my_app->text_alert->ShowCB
            (LS("login"), LS("username"), "", [=](const string &v) {
              SSHClient::SendAuthenticationRequest(ssh->conn, shared_ptr<SSHClient::Identity>(), &v);
            });
          return false;
        };
      } else if (host.cred.credtype == LTerminal::CredentialType_PEM) {
        ssh->identity_cb = [=](shared_ptr<SSHClient::Identity> *out) { 
          if ((*out = LoadIdentity(host.cred))) return true;
          LoadNewIdentity(host.cred, [=](shared_ptr<SSHClient::Identity> identity){
            SSHClient::SendAuthenticationRequest(ssh->conn, identity);
          });
          return false;
        };
      }
    } else if (host.protocol == LTerminal::Protocol_Telnet) {
      auto tab = GetActiveWindow()->AddTerminalTab(host.host_id, host.settings.hide_statusbar, move(toolbar));
      tab->UseTelnetTerminalController
        (host.Hostport(), false, host.settings.close_on_disconnect,
         (bool(cb) ? Callback([=](){ cb(tab, 0, ""); }) : Callback()));
      ApplyTerminalSettings(host.settings);
    } else if (host.protocol == LTerminal::Protocol_RFB) {
      GetActiveWindow()->AddRFBTab
        (host.host_id, host.settings.hide_statusbar, RFBClient::Params{host.Hostport()},
         host.cred.credtype == LTerminal::CredentialType_Password ? host.cred.creddata : "",
         (bool(cb) ? bind(move(cb), _1, 0, "") : TerminalTabCB()), move(toolbar));
    } else if (host.protocol == LTerminal::Protocol_LocalShell) {
      auto tab = GetActiveWindow()->AddTerminalTab(host.host_id, host.settings.hide_statusbar, move(toolbar));
      tab->UseShellTerminalController("");
      if (cb) cb(tab, 0, "");
      ApplyTerminalSettings(host.settings);
    }
    MenuStartSession();
  }

  bool ShowAcceptFingerprintAlert(const string &fp) {
    my_app->confirm_alert->ShowCB
      (LS("host_key_changed"), StrCat(LS("accept_new_key"), " Fingerprint MD5: ",
                                      fp.size() ? HexEscape(Crypto::MD5(fp), ":").substr(1) : ""),
       "", bind(&MyTerminalMenus::AcceptFingerprintCB, this, _1));
    return false;
  }

  void AcceptFingerprintCB(const string&) {
    if (auto t = GetActiveTerminalTab())
      if (auto controller = dynamic_cast<SSHTerminalController*>(t->controller.get()))
        if (auto conn = controller->conn)
            if (auto ssh = dynamic_cast<SSHClient::Handler*>(conn->handler.get()))
              if (conn->state == Connection::Connected && !ssh->accepted_hostkey)
                SSHClient::AcceptHostKeyAndBeginAuthRequest(conn);
  }

  void OpenURL(const string &url) {
    app->RunInMainThread([=]{
      MyHostModel host;
      string prot, hosttext, port, path;
      if (!HTTP::ParseURL(url.c_str(), &prot, &hosttext, &port, &path)) return;
      if      (prot == "ssh"    || prot == "lterm-ssh")    host.SetProtocol(LS("ssh"));
      else if (prot == "vnc"    || prot == "lterm-vnc")    host.SetProtocol(LS("vnc"));
      else if (prot == "telnet" || prot == "lterm-telnet") host.SetProtocol(LS("telnet"));
      else return ERROR("unknown url protocol ", prot);
      auto userhost = Split(hosttext, '@');
      if      (userhost.size() == 1) { host.hostname = userhost[0]; }
      else if (userhost.size() == 2) { host.username = userhost[0]; host.hostname = userhost[1]; }
      else return ERROR("unknown url hosttext ", hosttext);
      auto hostport = Split(host.hostname, ':');
      if (hostport.size() == 2) host.hostname = hostport[0];
      host.SetPort(hostport.size() == 2 ? atoi(hostport[1]) : 0);
      for (auto &hi : host_db.data) {
        if (hi.first == 1) continue;
        MyHostModel h(hi.first, flatbuffers::GetRoot<LTerminal::Host>(hi.second.blob.data()));
        if (h.TargetEqual(host)) return ConnectHost(hi.first);
      }
      host.cred.Load(CredentialType_Password, "");
      NewHostConnectTo(host);
    });
    if (hosts_nav->shown) {
      hosts_nav->PopToRoot();
      hosts_nav->Show(false);
    }
    if (interfacesettings_nav->shown) {
      interfacesettings_nav->PopAll();
      interfacesettings_nav->Show(false);
    }
    app->focused->Wakeup();
  }
};

}; // namespace LFL
#endif // LFL_TERM_TERM_MENU_H__
