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

#ifndef LFL_TERM_TERM_MOBILE_H__
#define LFL_TERM_TERM_MOBILE_H__
namespace LFL {
  
struct MyHostRecord {
  int remote_id=0, cred_id=0, settings_id=0;
  const LTerminal::Remote *host=0;
  const LTerminal::Credential *cred=0;
  const LTerminal::RemoteSettings *settings=0;

  MyHostRecord() {}
  MyHostRecord(SQLiteIdValueStore *cred_db, int cred_id) { LoadCredential(cred_db, cred_id); }
  MyHostRecord(SQLiteIdValueStore *remote_db, SQLiteIdValueStore *cred_db, int remote_id) {
    LoadRemote(remote_db, remote_id);
    if (host->credential() && host->credential()->db() == LTerminal::CredentialDBType_Table) 
      LoadCredential(cred_db, host->credential()->id());
  }

  void Clear() {
    remote_id = cred_id = settings_id = 0;
    host = 0;
    cred = 0;
    settings = 0;
  }

  void LoadRemote(SQLiteIdValueStore *remote_db, int id) {
    host = flatbuffers::GetRoot<LTerminal::Remote>(FindRefOrDie(remote_db->data, (remote_id = id)).data());
  }

  void LoadCredential(SQLiteIdValueStore *cred_db, int id) {
    cred = flatbuffers::GetRoot<LTerminal::Credential>(FindRefOrDie(cred_db->data, (cred_id = id)).data());
  }

  void LoadSettings(SQLiteIdValueStore *settings_db, int id) {
    settings = flatbuffers::GetRoot<LTerminal::RemoteSettings>(FindRefOrDie(settings_db->data, (settings_id = id)).data());
  }

  string Username() const { return host && host->username() ? host->username()->data() : ""; }
  string Displayname() const { return host && host->displayname() ? host->displayname()->data() : ""; }
  LTerminal::CredentialType Credtype() const { return cred ? cred->type() : LTerminal::CredentialType_Ask; }

  string Hostname() const {
    if (!host || !host->hostport()) return "";
    string ret(host->hostport()->data());
    return ret.substr(0, ret.find(":"));
  }

  int Port(int default_port) const { 
    if (!host || !host->hostport()) return default_port;
    if (const char *colon = strchr(host->hostport()->data(), ':')) return atoi(colon+1);
    return default_port;
  }

  string Creddata() const {
    return cred && cred->data() ? string(MakeSigned(cred->data()->data()), cred->data()->size()) : "";
  }

  string Password() const {
    return cred && cred->type() == LTerminal::CredentialType_Password ? Creddata() : "";
  }

  string Keyname() const {
    if (!cred || cred->type() != LTerminal::CredentialType_PEM || !cred->displayname()) return "";
    return cred->displayname()->data();
  }

  string TextEncoding() const {
    return settings ? LTerminal::EnumNameTextEncoding(settings->text_encoding()) : "UTF8";
  }

  string Folder() const { return (settings && settings->folder()) ? settings->folder()->data() : ""; }
  bool AgentForwarding() const { return settings ? settings->agent_forwarding() : 0; }

  string TerminalType() const {
    return (settings && settings->terminal_type()) ? settings->terminal_type()->data() : "xterm-color";
  }

  int DeleteMode() const { return settings ? settings->delete_mode() : 0; }
  bool CloseOnDisconnect() const { return settings ? settings->close_on_disconnect() : 0; }

  string StartupCommand() const {
    return (settings && settings->startup_command()) ? settings->startup_command()->data() : "";
  }

  bool Autocomplete() const { return settings ? settings->close_on_disconnect() : true; }

  string PromptString() const {
    return (settings && settings->prompt_string()) ? settings->prompt_string()->data() : "$";
  }
};

struct TerminalMenuWindow : public BaseTerminalWindow {
  MyHostRecord mobile_host;

  unordered_map<string, Callback> mobile_key_cmd = {
    { "left",   bind([=]{ terminal->CursorLeft();  if (controller->frame_on_keyboard_input) app->scheduler.Wakeup(screen); }) },
    { "right",  bind([=]{ terminal->CursorRight(); if (controller->frame_on_keyboard_input) app->scheduler.Wakeup(screen); }) },
    { "up",     bind([=]{ terminal->HistUp();      if (controller->frame_on_keyboard_input) app->scheduler.Wakeup(screen); }) },
    { "down",   bind([=]{ terminal->HistDown();    if (controller->frame_on_keyboard_input) app->scheduler.Wakeup(screen); }) },
    { "pgup",   bind([=]{ terminal->PageUp();      if (controller->frame_on_keyboard_input) app->scheduler.Wakeup(screen); }) },
    { "pgdown", bind([=]{ terminal->PageDown();    if (controller->frame_on_keyboard_input) app->scheduler.Wakeup(screen); }) } };

  unordered_map<string, Callback> mobile_togglekey_cmd = {
    { "ctrl", bind([&]{ controller->ctrl_down = !controller->ctrl_down; }) } };

  using BaseTerminalWindow::BaseTerminalWindow;

  void KeyPressCmd (const vector<string> &arg) { if (arg.size()) FindAndDispatch(mobile_key_cmd, arg[0]); }
  void KeyToggleCmd(const vector<string> &arg) { if (arg.size()) FindAndDispatch(mobile_togglekey_cmd, arg[0]); }
  void CloseCmd(const vector<string> &arg) { controller->Close(); }
  void ToysMenuCmd(const vector<string> &arg) {
    ShaderCmd(vector<string>{"none"});
    my_app->toys_menu->Show();
  }

  void AppSettingsCmd(const vector<string> &arg) {
    const LTerminal::AppSettings *s =
      flatbuffers::GetRoot<LTerminal::AppSettings>(my_app->settings_db.data[1].data());
    vector<string> settings0{ "", LTerminal::EnumNameBeepType(s->beep_type()),
      s->keep_display_on() ? "1" : "" };
    my_app->settings_table->BeginUpdates();
    my_app->settings_table->SetSectionValues(settings0, 0);
    my_app->settings_table->EndUpdates();
    my_app->hosts_nav->PushTable(my_app->settings_table.get());
    // if (!app->OpenSystemAppPreferences()) {}
  }

  void RemoteSettingsCmd(const vector<string> &arg) {
    vector<string> settings0{ mobile_host.TextEncoding(), mobile_host.Folder() };
    vector<string> settings1{ mobile_host.AgentForwarding() ? "1" : "", mobile_host.TerminalType(),
      mobile_host.DeleteMode() ? "1" : "", mobile_host.CloseOnDisconnect() ? "1" : "",
      mobile_host.StartupCommand(), "" };
    vector<string> settings2{ mobile_host.Autocomplete() ? "1" : "", mobile_host.PromptString(), "" };
    my_app->remotesettings_table->BeginUpdates();
    my_app->remotesettings_table->SetSectionValues(settings0, 0);
    my_app->remotesettings_table->SetSectionValues(settings1, 1);
    my_app->remotesettings_table->SetSectionValues(settings2, 2);
    my_app->remotesettings_table->EndUpdates();
    my_app->hosts_nav->PushTable(my_app->remotesettings_table.get());
  }

  void GenKeyCmd(const vector<string> &arg) {
    my_app->hosts_nav->PopTable(2);
    string name = "Name", pw = "Passphrase", algo = "Algo", bits = "Bits";
    if (!my_app->genkey_table->GetSectionText(0, {&name, &pw}) ||
        !my_app->genkey_table->GetSectionText(1, {&algo}) ||
        !my_app->genkey_table->GetSectionText(2, {&bits})) return ERROR("parse genkey");

    if (name.empty()) name = StrCat(algo, " key");

    string pubkey, privkey;
    if (!Crypto::GenerateKey(algo, atoi(bits), "", "", &pubkey, &privkey)) return ERROR("generate ", algo, " key");
    INFO("Generate ", bits, " bits ", algo, " keypair, PEM length ", privkey.size());

    int row_id = UpdateCredential(CredentialType_PEM, privkey, name);
    my_app->keys_table->show_cb(my_app->keys_table.get());
  }
  
  void PasteKeyCmd(const vector<string> &arg) {
    const char *pems=0, *peme=0, *pemhe=0;
    string pem = app->GetClipboardText();
    string pemtype = Crypto::ParsePEMHeader(pem.data(), &pems, &peme, &pemhe);
    if (pemtype.size()) {
      int row_id = UpdateCredential(CredentialType_PEM, pem, pemtype);
      my_app->keys_table->show_cb(my_app->keys_table.get());
    } else {
      my_app->keypastefailed_alert->Show("");
    }
    my_app->hosts_nav->PopTable();
  }
  
  void ChooseKeyCmd(const vector<string> &arg) {
    CHECK_EQ(1, arg.size());
    my_app->hosts_nav->PopTable(1);
    SystemTableWidget *host_menu = my_app->hosts_nav->Back();
    int key_row = 3 + (host_menu->GetKey(0, 0) == "Name");
    host_menu->BeginUpdates();
    if (int cred_row_id = atoi(arg[0])) {
      MyHostRecord cred(&my_app->credential_db, cred_row_id);
      host_menu->SetTag(0, key_row, cred.cred_id);
      host_menu->SetValue(0, key_row, StrCat("nocontrol,", pw_default, ",", cred.Keyname()));
      host_menu->SetDropdown(0, key_row, 1);
    } else host_menu->SetDropdown(0, key_row, 0);
    host_menu->EndUpdates();
  }
  
  void StartShellCmd(const vector<string> &arg) {
    UseShellTerminalController("");
    MenuStartSession();
  }

  void HostInfoCmd(const vector<string> &arg) {
    CHECK_EQ(1, arg.size());
    mobile_host = MyHostRecord(&my_app->remote_db, &my_app->credential_db, atoi(arg[0]));
    bool pem = mobile_host.Credtype() == CredentialType_PEM;
    string pw = mobile_host.Password();
    vector<string> updatehost{ mobile_host.Displayname(), StrCat(",", mobile_host.Hostname(), ","),
      StrCat(mobile_host.Port(22)), mobile_host.Username(),
      StrCat("nocontrol,", pw.size() ? pw : pw_default, ",", mobile_host.Keyname()) };
    my_app->updatehost_table->BeginUpdates();
    my_app->updatehost_table->SetSectionValues(updatehost);
    my_app->updatehost_table->SetDropdown(0, 1, 0);
    my_app->updatehost_table->SetDropdown(0, 4, pem);
    my_app->updatehost_table->SetTag(0, 4, pem ? mobile_host.cred_id : 0);
    my_app->updatehost_table->EndUpdates();
    my_app->hosts_nav->PushTable(my_app->updatehost_table.get());
  }

  void HostConnectCmd(const vector<string> &arg) {
    CHECK_EQ(1, arg.size());
    mobile_host = MyHostRecord(&my_app->remote_db, &my_app->credential_db, atoi(arg[0]));
    MenuConnect(mobile_host.Hostname(), StrCat(mobile_host.Port(22)), mobile_host.Username(),
                mobile_host.Credtype(), mobile_host.Creddata());
  }

  void MenuStartSession() {
    my_app->hosts_nav->Show(false);
    my_app->keyboard_toolbar->Show(true);
    app->CloseTouchKeyboardAfterReturn(false);
    app->OpenTouchKeyboard();
  }

  void QuickConnectCmd(const vector<string> &arg) {
    string prot = "Protocol", host = "", port = "Port", user = "Username", credtype = "Password", cred = "";
    if (!my_app->quickconnect_table->GetSectionText(0, {&prot, &host, &port, &user, &credtype, &cred})) return ERROR("parse quickconnect");
    string displayname = StrCat(FLAGS_ssh, "@", FLAGS_login);
    auto proto = MakeFlatBufferOfType(LTerminal::RemoteSettings, LTerminal::CreateRemoteSettings(fb));

    MenuConnect(host, port, user, CredentialType_Password, cred, [&](){
      // ask to save
    });
  }

  void UpdateHostConnectCmd(const vector<string> &arg) {
    string name = "Name", prot = "Protocol", host = "", port = "Port", user = "Username",
           credtype = "Credential", cred = "", *flags_ssh = &FLAGS_ssh, *flags_login = &FLAGS_login;
    if (!my_app->updatehost_table->GetSectionText(0, {&name, &prot, &host, &port, &user, &credtype, &cred})) return ERROR("parse updatehostconnect");

    CredentialType cred_type = GetCredentialType(credtype);
    LoadPEM(my_app->updatehost_table.get(), 4, &cred_type, &cred);
    MenuConnect(host, port, user, cred_type, cred, [=](){
      CredentialType cred_type = GetCredentialType(credtype), prev_cred_type = mobile_host.Credtype();
      if (prev_cred_type == CredentialType_Password) {
        if (cred_type != CredentialType_Password) my_app->credential_db.Erase(mobile_host.cred_id);
        else UpdateCredential(CredentialType_Password, cred, name, mobile_host.cred_id);
      } else if (cred_type == CredentialType_Password) {
        mobile_host.cred_id = UpdateCredential(CredentialType_Password, cred, name);
      }
      if (cred_type == CredentialType_PEM) {
        if (!(mobile_host.cred_id = my_app->updatehost_table->GetTag(0, 4)))
          cred_type = CredentialType_Ask;
      }

      LTerminal::CredentialRef credref
        (cred_type == CredentialType_Ask ? 0 : CredentialDBType_Table, mobile_host.cred_id);
      UpdateRemote(*flags_ssh, *flags_login, name, credref, mobile_host.remote_id);
    });
  }

  void NewHostConnectCmd(const vector<string> &arg) {
    string name = "Name", prot = "Protocol", host = "", port = "Port", user = "Username",
           credtype = "Credential", cred = "", *flags_ssh = &FLAGS_ssh, *flags_login = &FLAGS_login;
    if (!my_app->newhost_table->GetSectionText(0, {&name, &prot, &host, &port, &user, &credtype, &cred})) return ERROR("parse newhostconnect");

    string encoding = "Text Encoding", folder = "Folder", forwarding = "Agent Forwarding",
           termtype = "Terminal Type", deletemode = "", disconclose = "Close on Disconnect",
           startup = "Startup Command", fingerprint, complete = "Autocomplete", prompt = "Prompt String", reset;
    if (!my_app->remotesettings_table->GetSectionText(0, {&encoding, &folder})) return ERROR("parse newhostconnect settings0");
    if (!my_app->remotesettings_table->GetSectionText(1, {&forwarding, &termtype, &deletemode, &disconclose,
                                                          &startup, &fingerprint})) return ERROR("parse newhostconnect settings1");
    if (!my_app->remotesettings_table->GetSectionText(2, {&complete, &prompt, &reset})) return ERROR("parse newhostconnect settings2");
    auto proto = MakeFlatBufferOfType
      (LTerminal::RemoteSettings, LTerminal::CreateRemoteSettings
       (fb, LTerminal::TextEncoding_UTF8, fb.CreateString(folder), forwarding == "1", fb.CreateString(termtype),
        deletemode == "1", disconclose == "1", fb.CreateString(startup), complete == "1", fb.CreateString(prompt)));
    BlobPiece settings = MakeBlobPiece(proto);

    string pw_or_id = cred;
    CredentialType cred_type = GetCredentialType(credtype);
    if (int cred_id = LoadPEM(my_app->newhost_table.get(), 4, &cred_type, &cred)) pw_or_id = StrCat(cred_id);
    MenuConnect(host, port, user, cred_type, cred, [=](){
      SaveNewHost(*flags_ssh, *flags_login, name, cred_type, pw_or_id, settings);
    });
  }

  void DeleteHostCmd(const vector<string> &arg) {
    CHECK_EQ(2, arg.size());
    MyHostRecord delete_host(&my_app->remote_db, &my_app->credential_db, atoi(arg[1]));
    if (delete_host.Credtype() == LTerminal::CredentialType_Password) 
      my_app->credential_db.Erase(delete_host.cred_id);
    my_app->remote_db.Erase(delete_host.remote_id);
  }

  void MenuConnect(string host, string port, string user,
                   CredentialType credtype, string cred, Callback cb=Callback()) {
    FLAGS_ssh = move(host);
    FLAGS_login = move(user);
    if (port != "22") StrAppend(&FLAGS_ssh, ":", port);
    UseSSHTerminalController(credtype, move(cred), move(cb));
    MenuStartSession();
  }

  int LoadPEM(SystemTableWidget *table, int key_row, CredentialType *cred_type, string *cred) {
    if (*cred_type == CredentialType_PEM) {
      if (int cred_id = table->GetTag(0, key_row)) {
        *cred = MyHostRecord(&my_app->credential_db, cred_id).Creddata();
        return cred_id;
      } else *cred_type = CredentialType_Ask;
    }
    return 0;
  }

  void SaveNewHost(const string &host, const string &user, const string &name,
                   CredentialType cred_type, const string &pw_or_id, const BlobPiece &settings) {
    int settings_row_id = my_app->settings_db.Insert(settings), cred_row_id = 0;

    if      (cred_type == CredentialType_PEM)      cred_row_id = atoi(pw_or_id);
    else if (cred_type == CredentialType_Password) cred_row_id = UpdateCredential(cred_type, pw_or_id, name);
    string displayname = name.size() ? name : StrCat(user, "@", host);
    LTerminal::CredentialRef credref(CredentialDBType_Table, cred_row_id);
    UpdateRemote(host, user, displayname, credref);
  }

  int UpdateCredential(LFL::CredentialType type, const string &data, const string &name, int row_id=0) {
    auto proto = MakeFlatBufferOfType
      (LTerminal::Credential,
       LTerminal::CreateCredential(fb, type,
                                   fb.CreateVector(reinterpret_cast<const uint8_t*>(data.data()), data.size()),
                                   fb.CreateString(name)));
    if (!row_id) row_id = my_app->credential_db.Insert(        MakeBlobPiece(proto));
    else                  my_app->credential_db.Update(row_id, MakeBlobPiece(proto));
    return row_id;
  }

  int UpdateRemote(const string &host, const string &login, const string &name,
                   const LTerminal::CredentialRef &credref, int row_id=0) {
    auto proto = MakeFlatBufferOfType
      (LTerminal::Remote,
       LTerminal::CreateRemote(fb, fb.CreateString(host), fb.CreateString(login),
                               &credref, fb.CreateString(name)));
    if (!row_id) row_id = my_app->remote_db.Insert(        MakeBlobPiece(proto));
    else                  my_app->remote_db.Update(row_id, MakeBlobPiece(proto));
    return row_id;
  }

  static string pw_default;
};

string TerminalMenuWindow::pw_default = "\x01""Ask each time";

void SetupTerminalMenuApp() {
  my_app->db = SQLite::Open(StrCat(app->savedir, "lterm.db"));
  my_app->credential_db.Open(&my_app->db, "credential");
  my_app->remote_db    .Open(&my_app->db, "remote");
  my_app->settings_db  .Open(&my_app->db, "settings");
  if (my_app->settings_db.data.find(1) == my_app->settings_db.data.end()) {
    auto proto = MakeFlatBufferOfType(LTerminal::AppSettings,
                                      LTerminal::CreateAppSettings(fb, fb.CreateString("Menlo-Bold")));
    CHECK_EQ(1, my_app->settings_db.Insert(MakeBlobPiece(proto)));
  }

  int key_icon         = CheckNotNull(app->LoadSystemImage("drawable-xhdpi/key.png"));
  int host_icon        = CheckNotNull(app->LoadSystemImage("drawable-xhdpi/host.png"));
  int bolt_icon        = CheckNotNull(app->LoadSystemImage("drawable-xhdpi/bolt.png"));
  int terminal_icon    = CheckNotNull(app->LoadSystemImage("drawable-xhdpi/terminal.png"));
  int settings_icon    = CheckNotNull(app->LoadSystemImage("drawable-xhdpi/settings.png"));
  int audio_icon       = CheckNotNull(app->LoadSystemImage("drawable-xhdpi/audio.png"));
  int eye_icon         = CheckNotNull(app->LoadSystemImage("drawable-xhdpi/eye.png"));
  int recycle_icon     = CheckNotNull(app->LoadSystemImage("drawable-xhdpi/recycle.png"));
  int fingerprint_icon = CheckNotNull(app->LoadSystemImage("drawable-xhdpi/fingerprint.png"));
  int info_icon        = CheckNotNull(app->LoadSystemImage("drawable-xhdpi/info.png"));
  int keyboard_icon    = CheckNotNull(app->LoadSystemImage("drawable-xhdpi/keyboard.png"));

  vector<pair<string,string>> keyboard_tb = { { "ctrl", "togglekey ctrl" },
    { "\U000025C0", "keypress left" }, { "\U000025B6", "keypress right" }, { "\U000025B2", "keypress up" }, 
    { "\U000025BC", "keypress down" }, { "\U000023EB", "keypress pgup" }, { "\U000023EC", "keypress pgdown" }, 
    { "\U00002699", "choosefont" }, { "toys", "toysmenu" } };
  my_app->keyboard_toolbar = make_unique<SystemToolbarWidget>(keyboard_tb);

  vector<pair<string,string>> hosts_tb = { { "\U00002699", "appsettings" }, { "+", "newhostmenu" } };
  my_app->hosts_toolbar = make_unique<SystemToolbarWidget>(hosts_tb);

  vector<TableItem> hosts_table{ TableItem("Quick connect", "command", "quickconnectmenu", ">", 0, bolt_icon),
    TableItem("Interactive Shell", "command", "startshell", ">", 0, terminal_icon), TableItem("", "separator", "") };
  my_app->hosts_table = make_unique<SystemTableWidget>("Hosts", "", hosts_table);
  my_app->hosts_table->AddNavigationButton(TableItem("Edit", "", ""), HAlign::Right);
  my_app->hosts_table->AddToolbar(my_app->hosts_toolbar.get());
  my_app->hosts_table->SetEditableSection("deletehost", 1);
  my_app->hosts_table->show_cb = [=](SystemTableWidget *table) {
    vector<TableItem> section; 
    for (auto host : my_app->remote_db.data) {
      const LTerminal::Remote *h = flatbuffers::GetRoot<LTerminal::Remote>(host.second.data());
      string displayname = h->displayname() ? h->displayname()->data() : "";
      section.push_back({displayname, "command", StrCat("hostconnect ", host.first),
                        "", host.first, host_icon, settings_icon, StrCat("hostinfo ", host.first)});
    }
    table->BeginUpdates();
    table->ReplaceSection(section, 1);
    table->EndUpdates();
  };

  vector<TableItem> quickconnect_table{ TableItem("Protocol,SSH,Telnet", "dropdown,textinput,textinput", ",\x01Hostname,\x01Hostname"),
    TableItem("Port", "numinput", "\x02""22"), TableItem("Username", "textinput", "\x01Username"),
    TableItem("Credential,Password,Key", "dropdown,pwinput,label", StrCat("nocontrol,", TerminalMenuWindow::pw_default, ","), "", 0, 0, key_icon, "choosekeymenu"),
    TableItem("", "separator", ""), TableItem("Connect", "button", "quickconnect"),
    TableItem("", "separator", ""), TableItem("Server Settings", "button", "remotesettingsmenu")
  };
  vector<TableItem> newhost_table = quickconnect_table;
  newhost_table[5].CheckAssign("Connect", "newhostconnect");
  newhost_table.insert(newhost_table.begin(), TableItem{"Name", "textinput", "\x01Nickname"});
  vector<TableItem> updatehost_table = newhost_table;
  updatehost_table[6].CheckAssign("Connect", "updatehostconnect");
  my_app->newhost_table      = make_unique<SystemTableWidget>("New Host",      "", move(newhost_table),      120);
  my_app->updatehost_table   = make_unique<SystemTableWidget>("Update Host",   "", move(updatehost_table),   120);
  my_app->quickconnect_table = make_unique<SystemTableWidget>("Quick Connect", "", move(quickconnect_table), 120);
  my_app->newhost_table->SelectRow(0, 1);

  vector<TableItem> settings_table{ TableItem("Appearance", "", "", "", 0, eye_icon), TableItem("Beep", "", "", "", 0, audio_icon),
    TableItem("Keep Display On", "toggle", ""), TableItem("", "separator", ""),
    TableItem("Keys", "", "", "", 0, key_icon), TableItem("Touch ID & Passcode", "", "", "", 0, fingerprint_icon),
    TableItem("Keyboard", "", "", "", 0, keyboard_icon), TableItem("", "separator", ""),
    TableItem("About LTerminal", "", ""), TableItem("Support", "", ""), TableItem("Privacy", "", "") };
  my_app->settings_table = make_unique<SystemTableWidget>("Settings", "", move(settings_table));

  vector<TableItem> remotesettings_table{ TableItem("Text Encoding", "", ""), TableItem("Folder", "", ""),
    TableItem("Advanced", "separator", ""), TableItem("Agent Forwarding", "toggle", ""), TableItem("Terminal Type", "textinput", ""),
    TableItem("Delete Sends ^H", "toggle", ""), TableItem("Close on Disconnect", "toggle", ""), TableItem("Startup Command", "textinput", ""),
    TableItem("Host Key Fingerprint", "", ""), TableItem("Autocomplete", "separator", ""), TableItem("Autocomplete", "toggle", ""),
    TableItem("Prompt String", "textinput", ""), TableItem("Reset Autcomplete Data", "command", "") };
  my_app->remotesettings_table = make_unique<SystemTableWidget>("Server Settings", "", move(remotesettings_table));

  vector<TableItem> keys_table{};
  my_app->keys_table = make_unique<SystemTableWidget>("Keys", "", move(keys_table));
  my_app->keys_table->AddNavigationButton(TableItem("+", "", "newkeymenu"), HAlign::Right);
  my_app->keys_table->SetEditableSection("", 0);
  my_app->keys_table->show_cb = [=](SystemTableWidget *table) {
    vector<TableItem> section;
    section.push_back({"None", "command", "choosekey 0"});
    for (auto credential : my_app->credential_db.data) {
      auto c = flatbuffers::GetRoot<LTerminal::Credential>(credential.second.data());
      if (c->type() != CredentialType_PEM) continue;
      string name = c->displayname() ? c->displayname()->data() : "";
      section.push_back({name, "command", StrCat("choosekey ", credential.first)});
    }
    table->BeginUpdates();
    table->ReplaceSection(section, 0);
    table->EndUpdates();
  };

  vector<TableItem> newkey_table{ TableItem("Generate New Key", "command", "genkeymenu"),
    TableItem("Paste from Clipboard", "command", "pastekey") };
  my_app->newkey_table = make_unique<SystemTableWidget>("New Key", "", move(newkey_table));

  vector<TableItem> genkey_table{ TableItem("Name", "textinput", ""), TableItem("Passphrase", "pwinput", ""),
    TableItem("Type", "separator", ""), TableItem("Algo", "select", "RSA,Ed25519,ECDSA", "", 0, 0, 0, "",
                                                  {{"RSA", {{2,0,"2048,4096"}}}, {"Ed25519", {{2,0,"256"}}}, {"ECDSA", {{2,0,"256,384,521"}}}}),
    TableItem("Size", "separator", ""), TableItem("Bits", "select", "2048,4096") };
  my_app->genkey_table = make_unique<SystemTableWidget>("Generate New Key", "", move(genkey_table), 120);
  my_app->genkey_table->AddNavigationButton(TableItem("Generate", "", "genkey"), HAlign::Right);

  my_app->hosts_nav = make_unique<SystemNavigationWidget>(my_app->hosts_table.get());
  my_app->hosts_nav->Show(true);
}

void StartTerminalMenuWindow(Window *W, TerminalMenuWindow *tw) {
  W->shell->Add("close",              bind(&TerminalMenuWindow::CloseCmd,          tw, _1));
  W->shell->Add("toysmenu",           bind(&TerminalMenuWindow::ToysMenuCmd,       tw, _1));
  W->shell->Add("keypress",           bind(&TerminalMenuWindow::KeyPressCmd,       tw, _1));
  W->shell->Add("togglekey",          bind(&TerminalMenuWindow::KeyToggleCmd,      tw, _1));
  W->shell->Add("appsettings",        bind(&TerminalMenuWindow::AppSettingsCmd,    tw, _1));
  W->shell->Add("remotesettingsmenu", bind(&TerminalMenuWindow::RemoteSettingsCmd, tw, _1));
  W->shell->Add("newhostmenu",        [=](const vector<string>&) { tw->mobile_host.Clear(); my_app->hosts_nav->PushTable(my_app->newhost_table.get()); });
  W->shell->Add("newkeymenu",         [=](const vector<string>&) { my_app->hosts_nav->PushTable(my_app->newkey_table.get()); });
  W->shell->Add("genkeymenu",         [=](const vector<string>&) { my_app->hosts_nav->PushTable(my_app->genkey_table.get()); });
  W->shell->Add("choosekeymenu",      [=](const vector<string>&) { my_app->hosts_nav->PushTable(my_app->keys_table.get()); });
  W->shell->Add("quickconnectmenu",   [=](const vector<string>&) { my_app->hosts_nav->PushTable(my_app->quickconnect_table.get()); });
  W->shell->Add("genkey",             bind(&TerminalMenuWindow::GenKeyCmd,         tw, _1));
  W->shell->Add("pastekey",           bind(&TerminalMenuWindow::PasteKeyCmd,       tw, _1));
  W->shell->Add("choosekey",          bind(&TerminalMenuWindow::ChooseKeyCmd,      tw, _1));
  W->shell->Add("startshell",         bind(&TerminalMenuWindow::StartShellCmd,     tw, _1));
  W->shell->Add("hostinfo",           bind(&TerminalMenuWindow::HostInfoCmd,       tw, _1));
  W->shell->Add("hostconnect",        bind(&TerminalMenuWindow::HostConnectCmd,    tw, _1));
  W->shell->Add("quickconnect",       bind(&TerminalMenuWindow::QuickConnectCmd,   tw, _1));
  W->shell->Add("newhostconnect",     bind(&TerminalMenuWindow::NewHostConnectCmd, tw, _1));
  W->shell->Add("updatehostconnect",  bind(&TerminalMenuWindow::UpdateHostConnectCmd, tw, _1));
  W->shell->Add("deletehost",         bind(&TerminalMenuWindow::DeleteHostCmd,     tw, _1));
  tw->terminal->line_fb.align_top_or_bot = tw->terminal->cmd_fb.align_top_or_bot = true;
}

}; // namespace LFL
#endif // LFL_TERM_TERM_MOBILE_H__
