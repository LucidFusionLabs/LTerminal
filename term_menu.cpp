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

namespace LFL {
MyLocalEncryptionViewController::MyLocalEncryptionViewController(MyTerminalMenus *m) :
  view(make_unique<SystemTableView>("Local Encryption", "", vector<TableItem>{
       TableItem("Enable Encryption", "command", "", "", 0, 0, 0,
                 bind(&SystemAlertView::ShowCB, my_app->passphrase_alert.get(), "Enable encryption", "", [=](const string &pw){
                      my_app->passphraseconfirm_alert->ShowCB("Confirm enable encryption", "", StringCB(bind(&MyTerminalMenus::EnableLocalEncryption, m, pw, _1))); })),
       TableItem("Disable Encryption", "command", "", "", 0, 0, 0, bind(&MyTerminalMenus::DisableLocalEncryption, m))
       })) {
  view->show_cb = [=](){
    view->BeginUpdates();
    view->SetHidden(0, 0, !m->db_opened ||  m->db_protected); 
    view->SetHidden(0, 1, !m->db_opened || !m->db_protected); 
    view->EndUpdates();
  };
}

MyAppearanceViewController::MyAppearanceViewController(MyTerminalMenus *m) :
  view(make_unique<SystemTableView>("Appearance", "", vector<TableItem>{
    TableItem("Font", "command", "", ">", 0, 0, 0, [=](){ if (auto t = GetActiveTerminalTab()) t->ChangeFont(StringVec()); }),
    TableItem("Toys", "command", "", ">", 0, 0, 0, bind(&MyTerminalMenus::ShowToysMenu, m))
  })) {}

MyKeyboardSettingsViewController::MyKeyboardSettingsViewController(MyTerminalMenus *m) :
  view(make_unique<SystemTableView>("Keyboard", "", vector<TableItem>{
    TableItem("Delete Sends ^H", "toggle", "")
  })) {}

void MyKeyboardSettingsViewController::UpdateViewFromModel(const MyHostSettingsModel &model) {
  view->BeginUpdates();
  view->SetSectionValues(0, vector<string>{ model.delete_mode == LTerminal::DeleteMode_ControlH ? "1" : "" });
  view->EndUpdates();
}

MyNewKeyViewController::MyNewKeyViewController(MyTerminalMenus *m) {
  view = make_unique<SystemTableView>("New Key", "", vector<TableItem>{
    TableItem("Generate New Key",     "command", "", "", 0, 0, 0, [=](){ m->hosts_nav->PushTable(m->genkey.view.get()); }),
    TableItem("Paste from Clipboard", "command", "", "", 0, 0, 0, bind(&MyTerminalMenus::PasteKey, m))
  });
}

MyGenKeyViewController::MyGenKeyViewController(MyTerminalMenus *m) {
  view = make_unique<SystemTableView>("Generate New Key", "", vector<TableItem>{
    TableItem("Name", "textinput", ""),
    TableItem("Passphrase", "pwinput", ""),
    TableItem("Type", "separator", ""),
    TableItem("Algo", "select", "RSA,Ed25519,ECDSA", "", 0, 0, 0, Callback(), Callback(),
              {{"RSA", {{2,0,"2048,4096"}}}, {"Ed25519", {{2,0,"256"}}}, {"ECDSA", {{2,0,"256,384,521"}}}}),
    TableItem("Size", "separator", ""),
    TableItem("Bits", "select", "2048,4096")
  }, m->second_col);
  view->AddNavigationButton(HAlign::Right, TableItem("Generate", "", "", "", 0, 0, 0, bind(&MyTerminalMenus::GenerateKey, m)));
}

bool MyGenKeyViewController::UpdateModelFromView(MyGenKeyModel *model) const {
  model->name = "Name";
  model->pw   = "Passphrase";
  model->algo = "Algo";
  string bits = "Bits";
  if (!view->GetSectionText(0, {&model->name, &model->pw}) ||
      !view->GetSectionText(1, {&model->algo}) ||
      !view->GetSectionText(2, {&bits})) return ERRORv(false, "parse genkey");
  if (model->name.empty()) model->name = StrCat(model->algo, " key");
  model->bits = atoi(bits);
  return true;
}

MyKeysViewController::MyKeysViewController(MyTerminalMenus *m, MyCredentialDB *mo, bool add) :
  menus(m), model(mo), add_or_edit(add), view(make_unique<SystemTableView>("Keys", "", vector<TableItem>{})) {
  if (add_or_edit) view->AddNavigationButton(HAlign::Right, TableItem("+", "", "", "", 0, 0, 0, [=](){ m->hosts_nav->PushTable(m->newkey.view.get()); }));
  else {
    view->AddNavigationButton(HAlign::Right, TableItem("Edit", "", ""));
    view->SetEditableSection(0, 0, bind(&MyTerminalMenus::DeleteKey, m, _1, _2));
  }
  view->show_cb = bind(&MyKeysViewController::UpdateViewFromModel, this);
}

void MyKeysViewController::UpdateViewFromModel() {
  vector<TableItem> section;
  if (add_or_edit)
    section.push_back({"None", "command", "", "", 0, 0, 0, bind(&MyTerminalMenus::ChooseKey, menus, 0)});
  for (auto credential : model->data) {
    auto c = flatbuffers::GetRoot<LTerminal::Credential>(credential.second.data());
    if (c->type() != CredentialType_PEM) continue;
    string name = c->displayname() ? c->displayname()->data() : "";
    section.push_back({name, add_or_edit ? "command" : "", "", "", credential.first, 0, 0,
                      Callback(bind(&MyTerminalMenus::ChooseKey, menus, credential.first))});
  }
  view->BeginUpdates();
  view->ReplaceSection(0, "", 0, section);
  view->EndUpdates();
  view->changed = false;
}

MyRunSettingsViewController::MyRunSettingsViewController(MyTerminalMenus *m) :
  view(make_unique<SystemTableView>("Settings", "", GetSchema(m, m->runsettings_nav.get()))) {
  view->AddNavigationButton(HAlign::Left,
                            TableItem("Back", "", "", "", 0, 0, 0,
                                      bind(&SystemNavigationView::Show, m->runsettings_nav.get(), false)));
}

vector<TableItem> MyRunSettingsViewController::GetBaseSchema(MyTerminalMenus *m, SystemNavigationView *nav) {
  return vector<TableItem>{
    TableItem("Appearance",      "command", "", ">", 0, m->eye_icon,      0, bind(&SystemNavigationView::PushTable, nav, m->appearance.view.get())),
    TableItem("Keyboard",        "",        "", ">", 0, m->keyboard_icon, 0),
    TableItem("Beep",            "",        "", ">", 0, m->audio_icon,    0),
    TableItem("Text Encoding",   "label",   "") };
}

vector<TableItem> MyRunSettingsViewController::GetSchema(MyTerminalMenus *m, SystemNavigationView *nav) {
  return VectorCat<TableItem>(GetBaseSchema(m, nav), vector<TableItem>{
    TableItem("Autocomplete",           "separator", ""), 
    TableItem("Autocomplete",           "toggle",    ""),
    TableItem("Prompt String",          "textinput", ""),
    TableItem("Reset Autcomplete Data", "command",   "") });
}

void MyRunSettingsViewController::UpdateViewFromModel(const MyAppSettingsModel &app_model, const MyHostSettingsModel &host_model) {
  view->BeginUpdates();
  view->SetSectionValues(0, vector<string>{ "", "", "", LTerminal::EnumNameTextEncoding(host_model.text_encoding) });
  view->SetSectionValues(1, vector<string>{ host_model.autocomplete_id ? "1" : "", host_model.prompt, "" });
  view->EndUpdates();
}

void MyRunSettingsViewController::UpdateModelFromView(MyAppSettingsModel *app_model, MyHostSettingsModel *host_model) const {
  host_model->prompt = "Prompt String";
  string appearance="Appearance", keyboard="Keyboard", beep="Beep",
         textencoding="Text Encoding", autocomplete="Autocomplete", reset;
  if (!view->GetSectionText(0, {&appearance, &keyboard, &beep, &textencoding})) return ERROR("parse runsettings1");
  if (!view->GetSectionText(1, {&autocomplete, &host_model->prompt, &reset})) return ERROR("parse runsettings2");
}

MyAppSettingsViewController::MyAppSettingsViewController(MyTerminalMenus *m) :
  view(make_unique<SystemTableView>("Settings", "", GetSchema(m))) {
  view->hide_cb = [=](){
    if (view->changed) {
      MyAppSettingsModel settings(&m->settings_db);
      UpdateModelFromView(&settings);
      settings.Save(&m->settings_db);
    }
  };
}

vector<TableItem> MyAppSettingsViewController::GetSchema(MyTerminalMenus *m) {
  return vector<TableItem>{
    TableItem("Local Encryption",    "command", "", ">", 0, m->fingerprint_icon, 0, bind(&SystemNavigationView::PushTable, m->hosts_nav.get(), m->encryption.view.get())),
    TableItem("Keys",                "command", "", ">", 0, m->key_icon,         0, bind(&SystemNavigationView::PushTable, m->hosts_nav.get(), m->editkeys  .view.get())),
    TableItem("Keep Display On",     "toggle",  ""),
    TableItem("", "separator", ""),
    TableItem("About LTerminal",     "", "", ">"),
    TableItem("Support",             "", "", ">"),
    TableItem("Privacy",             "", "", ">") };
}

void MyAppSettingsViewController::UpdateViewFromModel(const MyAppSettingsModel &model) {
  view->BeginUpdates();
  view->SetSectionValues(0, vector<string>{ "", "", model.keep_display_on ? "1" : "" });
  view->EndUpdates();
}

void MyAppSettingsViewController::UpdateModelFromView(MyAppSettingsModel *model) {
  string a="", b="", keepdisplayon="Keep Display On";
  if (!view->GetSectionText(0, {&a, &b, &keepdisplayon})) return ERROR("parse appsettings0");
  model->keep_display_on = keepdisplayon == "1";
}

MySSHFingerprintViewController::MySSHFingerprintViewController(MyTerminalMenus *m) :
  view(make_unique<SystemTableView>("Fingerprint", "", TableItemVec{
    TableItem("Type", "label", ""), TableItem("MD5", "label", ""), TableItem("SHA256", "label", ""),
    TableItem("", "separator", ""), TableItem("Clear", "button", "", "", 0, 0, 0, [=](){})
  })) { view->SelectRow(-1, -1); }
  
void MySSHFingerprintViewController::UpdateViewFromModel(const MyHostModel &model) {
  view->BeginUpdates();
  view->SetSectionValues(0, StringVec{ SSH::Key::Name(model.fingerprint_type),
    model.fingerprint.size() ? HexEscape(Crypto::MD5(model.fingerprint), ":").substr(1) : "",
    model.fingerprint.size() ? Singleton<Base64>::Get()->Encode(Crypto::SHA256(model.fingerprint)) : ""});
  view->EndUpdates();
}

vector<TableItem> MySSHSettingsViewController::GetSchema(MyTerminalMenus *m) {
  return vector<TableItem>{
    TableItem("Folder",               "textinput", "", "",  0, m->folder_icon),
    TableItem("Terminal Type",        "textinput", "", "",  0, m->terminal_icon),
    TableItem("Host Key Fingerprint", "command",   "", ">", 0, m->fingerprint_icon, 0,
              bind(&SystemNavigationView::PushTable, m->hosts_nav.get(), m->sshfingerprint.view.get())),
    TableItem("Advanced",             "separator", ""),
    TableItem("Agent Forwarding",     "toggle",    ""),
    TableItem("Compression",          "toggle",    ""),
    TableItem("Close on Disconnect",  "toggle",    ""),
    TableItem("Startup Command",      "textinput", "") };
}

void MySSHSettingsViewController::UpdateViewFromModel(const MyHostModel &model) {
  view->BeginUpdates();
  view->SetSectionValues(0, vector<string>{
    model.folder.size() ? model.folder : "\x01none", model.settings.terminal_type, "" });
  view->SetSectionValues(1, vector<string>{
    model.settings.agent_forwarding ? "1" : "",
    model.settings.compression ? "1" : "",
    model.settings.close_on_disconnect ? "1" : "",
    model.settings.startup_command.size() ? model.settings.startup_command : "\x01none" });
  view->EndUpdates();
}

bool MySSHSettingsViewController::UpdateModelFromView(MyHostSettingsModel *model, string *folder) const {
  *folder = "Folder";
  model->terminal_type = "Terminal Type";
  model->startup_command = "Startup Command";
  string fingerprint = "Host Key Fingerprint", forwarding = "Agent Forwarding", compression = "Compression",
         disconclose = "Close on Disconnect";
  if (!view->GetSectionText(0, {folder, &model->terminal_type, &fingerprint})) return ERRORv(false, "parse newhostconnect settings0");
  if (!view->GetSectionText(1, {&forwarding, &compression, &disconclose, &model->startup_command})) return ERRORv(false, "parse newhostconnect settings1");
  model->agent_forwarding    = forwarding  == "1";
  model->compression         = compression == "1";
  model->close_on_disconnect = disconclose == "1";
  return true;
}

MyQuickConnectViewController::MyQuickConnectViewController(MyTerminalMenus *m) :
  view(make_unique<SystemTableView>("Quick Connect", "", GetSchema(m), m->second_col)) {}

vector<TableItem> MyQuickConnectViewController::GetSchema(MyTerminalMenus *m) {
  string pwv = StrCat("nocontrol,", m->pw_default, ",");
  return vector<TableItem>{
    TableItem("Protocol,SSH,Telnet,VNC", "dropdown,textinput,textinput,textinput",
              ",\x01Hostname,\x01Hostname,\x01Hostname", "", 0, 0, 0, Callback(), Callback(), {
              {"SSH",    {{0,1,"\x01""22"},   {0,2,"\x01Username"}, {0,3,pwv,false,m->key_icon} }},
              {"Telnet", {{0,1,"\x01""23"},   {0,2,"",true},        {0,3,"",true,-1} }},
              {"VNC",    {{0,1,"\x01""5900"}, {0,2,"",true},        {0,3,pwv,false,-1} }}}),
    TableItem("Port", "numinput", "\x02""22"),
    TableItem("Username", "textinput", "\x01Username"),
    TableItem("Credential,Password,Key", "dropdown,pwinput,label", pwv, "", 0, 0, m->key_icon, Callback(),
              bind(&SystemNavigationView::PushTable, m->hosts_nav.get(), m->keys.view.get())),
    TableItem("", "separator", ""),
    TableItem("Connect", "button", "", "", 0, 0, 0, bind(&MyTerminalMenus::QuickConnect, m)),
    TableItem("", "separator", ""),
    TableItem("Host Settings", "button", "", "", 0, 0, 0, bind(&MyTerminalMenus::ShowProtocolSettings, m)) };
}

bool MyQuickConnectViewController::UpdateModelFromView(MyHostModel *model, MyCredentialDB *cred_db) const {
  model->hostname = "";
  model->username = "Username";
  string prot = "Protocol", port = "Port", credtype = "Credential", cred = "";
  if (!view->GetSectionText(0, {&prot, &model->hostname, &port, &model->username,
                            &credtype, &cred})) return ERRORv(false, "parse quickconnect");

  model->displayname = StrCat(model->username, model->username.size() ? "@" : "", model->hostname);
  model->SetProtocol(prot);
  model->SetPort(atoi(port));
  auto ct = MyCredentialModel::GetCredentialType(credtype);
  if      (ct == CredentialType_Password) model->cred.Load(CredentialType_Password, cred);
  else if (ct == CredentialType_PEM)      model->cred.Load(cred_db, view->GetTag(0, 3));
  else                                    model->cred.Load();
  return true;
}

MyNewHostViewController::MyNewHostViewController(MyTerminalMenus *m) :
  view(make_unique<SystemTableView>("New Host", "", GetSchema(m), m->second_col)) { view->SelectRow(0, 1); }

vector<TableItem> MyNewHostViewController::GetSchema(MyTerminalMenus *m) {
  vector<TableItem> ret = MyQuickConnectViewController::GetSchema(m);
  for (auto &dep : ret[0].depends) for (auto &d : dep.second) d.row++;
  ret[5].CheckAssign("Connect", bind(&MyTerminalMenus::NewHostConnect, m));
  ret.insert(ret.begin(), TableItem{"Name", "textinput", "\x01Nickname"});
  return ret;
}

bool MyNewHostViewController::UpdateModelFromView(MyHostModel *model, MyCredentialDB *cred_db) const {
  model->displayname = "Name";
  model->hostname    = "";
  model->username    = "Username";
  string prot = "Protocol", port = "Port", credtype = "Credential", cred = "";
  if (!view->GetSectionText(0, {&model->displayname, &prot, &model->hostname, &port, &model->username,
                            &credtype, &cred})) return ERRORv(false, "parse newhostconnect");

  model->SetProtocol(prot);
  model->SetPort(atoi(port));
  auto ct = MyCredentialModel::GetCredentialType(credtype);
  if      (ct == CredentialType_Password) model->cred.Load(CredentialType_Password, cred);
  else if (ct == CredentialType_PEM)      model->cred.Load(cred_db, view->GetTag(0, 4));
  else                                    model->cred.Load();
  return true;
}

MyUpdateHostViewController::MyUpdateHostViewController(MyTerminalMenus *m) : menus(m),
  view(make_unique<SystemTableView>("Update Host", "", GetSchema(m), m->second_col)) {}

vector<TableItem> MyUpdateHostViewController::GetSchema(MyTerminalMenus *m) {
  vector<TableItem> ret = MyNewHostViewController::GetSchema(m);
  ret[6].CheckAssign("Connect", bind(&MyTerminalMenus::UpdateHostConnect, m));
  return ret;
}

void MyUpdateHostViewController::UpdateViewFromModel(const MyHostModel &host) {
  prev_model = host;
  bool pw = host.cred.credtype == CredentialType_Password, pem = host.cred.credtype == CredentialType_PEM;
  int proto_dropdown;
  string hostv;
  if      (host.protocol == LTerminal::Protocol_Telnet) { hostv = StrCat(",,", host.hostname, ","); proto_dropdown = 1; }
  else if (host.protocol == LTerminal::Protocol_RFB)    { hostv = StrCat(",,,", host.hostname, ""); proto_dropdown = 2; }
  else                                                  { hostv = StrCat(",", host.hostname, ",,"); proto_dropdown = 0; }
  view->BeginUpdates();
  view->SetDropdown(0, 1, proto_dropdown);
  view->SetSectionValues(0, vector<string>{
    host.displayname, hostv, StrCat(host.port), host.username,
    StrCat("nocontrol,", pw ? host.cred.creddata : menus->pw_default, ",", host.cred.name) });
  view->SetDropdown(0, 4, pem);
  view->SetTag(0, 4, pem ? host.cred.cred_id : 0);
  view->EndUpdates();
}

bool MyUpdateHostViewController::UpdateModelFromView(MyHostModel *model, MyCredentialDB *cred_db) const {
  model->displayname = "Name";
  model->hostname    = "";
  model->username    = "Username";
  string prot = "Protocol", port = "Port", credtype = "Credential", cred = "";
  if (!view->GetSectionText(0, {&model->displayname, &prot, &model->hostname, &port, &model->username,
                            &credtype, &cred})) return ERRORv(false, "parse updatehostconnect");

  model->SetProtocol(prot);
  model->SetPort(atoi(port));
  auto ct = MyCredentialModel::GetCredentialType(credtype);
  if      (ct == CredentialType_Password) model->cred.Load(CredentialType_Password, cred);
  else if (ct == CredentialType_PEM)      model->cred.Load(cred_db, view->GetTag(0, 4));
  else                                    model->cred.Load();
  return true;
}

MyHostsViewController::MyHostsViewController(MyTerminalMenus *m, bool me) :
  menus(m), menu(me), view(make_unique<SystemTableView>("LTerminal", "", TableItemVec())) {}

vector<TableItem> MyHostsViewController::GetBaseSchema(MyTerminalMenus *m) {
  return TableItemVec{
    TableItem("Quick connect",     "command", "", ">", 0, m->bolt_icon,     0, bind(&MyTerminalMenus::ShowQuickConnect, m)),
    // TableItem("Interactive Shell", "command", "", ">", 0, m->terminal_icon, 0, bind(&MyTerminalMenus::StartShell, m))
  };
}

void MyHostsViewController::LoadFolderUI(MyHostDB *model) {
  CHECK(!menu);
  view->AddNavigationButton(HAlign::Right, TableItem("Edit", "", ""));
  view->SetEditableSection(0, 0, bind(&MyTerminalMenus::DeleteHost, menus, _1, _2));
  view->show_cb = bind(&MyHostsViewController::UpdateViewFromModel, this, model);
}

void MyHostsViewController::LoadLockedUI(MyHostDB *model) {
  CHECK(menu);
  view->BeginUpdates();
  view->ReplaceSection(0, "", 0, GetBaseSchema(menus));
  view->ReplaceSection(1, "Hosts", 0, TableItemVec{
    TableItem("Unlock", "button", "", "", 0, 0, 0, [=](){
      my_app->passphrase_alert->ShowCB("Unlock", "", [=](const string &pw){
        if (menus->UnlockEncryptedDatabase(pw)) { LoadUnlockedUI(model); view->show_cb(); }
      }); })
  });
  view->EndUpdates();
  view->changed = false;
}

void MyHostsViewController::LoadUnlockedUI(MyHostDB *model) {
  CHECK(menu);
  view->BeginUpdates();
  view->ReplaceSection(0, "", 0, VectorCat<TableItem>(GetBaseSchema(menus), TableItemVec{
    TableItem("Settings", "command", "", ">", 0, menus->settings_icon, 0, bind(&MyTerminalMenus::ShowAppSettings, menus)),
  }));
  view->EndUpdates();
  view->SetEditableSection(menu, 1, bind(&MyTerminalMenus::DeleteHost, menus, _1, _2));
  view->show_cb = bind(&MyHostsViewController::UpdateViewFromModel, this, model);
}

void MyHostsViewController::UpdateViewFromModel(MyHostDB *model) {
  vector<TableItem> section;
  if (menu) section.emplace_back("New", "command", "", ">", 0, menus->plus_icon, 0, bind(&MyTerminalMenus::ShowNewHost, menus));
  unordered_set<string> seen_folders;
  for (auto &host : model->data) {
    if (host.first == 1) continue;
    const LTerminal::Host *h = flatbuffers::GetRoot<LTerminal::Host>(host.second.data());
    string displayname = GetFlatBufferString(h->displayname()), host_folder = GetFlatBufferString(h->folder());
    if (folder.size()) { if (host_folder != folder) continue; }
    else if (host_folder.size()) {
      if (seen_folders.insert(host_folder).second)
        section.emplace_back(host_folder, "command", "", ">", 0, menus->folder_icon, 0, [=](){
          menus->hostsfolder.view->SetTitle(StrCat((menus->hostsfolder.folder = move(host_folder)), " Folder"));
          menus->hosts_nav->PushTable(menus->hostsfolder.view.get()); });
      continue;
    }
    section.emplace_back(displayname, "command", "", "", host.first, menus->host_icon, 
                         menus->settings_icon, bind(&MyTerminalMenus::ConnectHost, menus, host.first),
                         bind(&MyTerminalMenus::HostInfo, menus, host.first));
  }
  view->BeginUpdates();
  view->ReplaceSection(menu, menu ? "Hosts" : "",
                       (menu ? SystemTableView::EditButton : 0) | SystemTableView::EditableIfHasTag, section);
  view->EndUpdates();
  view->changed = false;
}

}; // namespace LFL
