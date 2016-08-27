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
MyLocalEncryptionViewController::MyLocalEncryptionViewController(TerminalMenuWindow *tw) :
  view(make_unique<SystemTableView>("Local Encryption", "", vector<TableItem>{
       TableItem("Enable Encryption", "command", "", "", 0, 0, 0,
                 bind(&SystemAlertView::ShowCB, my_app->passphrase_alert.get(), "Enable encryption", "", [=](const string &pw){
                      my_app->passphraseconfirm_alert->ShowCB("Confirm enable encryption", "", StringCB(bind(&TerminalMenuWindow::ChangeLocalEncryptionPassphrase, tw, pw, _1))); }))
  })) {}

MyAppearanceViewController::MyAppearanceViewController(TerminalMenuWindow *tw) :
  view(make_unique<SystemTableView>("Appearance", "", vector<TableItem>{
    TableItem("Font", "command", "", "", 0, 0, 0, bind(&TerminalMenuWindow::ChangeFont, tw, StringVec())),
    TableItem("Toys", "command", "", "", 0, 0, 0, bind(&TerminalMenuWindow::ShowToysMenu, tw))
  })) {}

MyKeyboardSettingsViewController::MyKeyboardSettingsViewController() :
  view(make_unique<SystemTableView>("Keyboard", "", vector<TableItem>{
    TableItem("Delete Sends ^H", "toggle", "")
  })) {}

void MyKeyboardSettingsViewController::UpdateViewFromModel(const MyRunSettingsModel &model) {
  view->BeginUpdates();
  view->SetSectionValues(vector<string>{ model.delete_mode == LTerminal::DeleteMode_ControlH ? "1" : "" });
  view->EndUpdates();
}

MyNewKeyViewController::MyNewKeyViewController(TerminalMenuWindow *tw) {
  view = make_unique<SystemTableView>("New Key", "", vector<TableItem>{
    TableItem("Generate New Key",     "command", "", "", 0, 0, 0, [=](){ tw->hosts_nav->PushTable(tw->genkey.view.get()); }),
    TableItem("Paste from Clipboard", "command", "", "", 0, 0, 0, bind(&TerminalMenuWindow::PasteKey, tw))
  });
}

MyGenKeyViewController::MyGenKeyViewController(TerminalMenuWindow *tw) {
  view = make_unique<SystemTableView>("Generate New Key", "", vector<TableItem>{
    TableItem("Name", "textinput", ""),
    TableItem("Passphrase", "pwinput", ""),
    TableItem("Type", "separator", ""),
    TableItem("Algo", "select", "RSA,Ed25519,ECDSA", "", 0, 0, 0, Callback(), Callback(),
              {{"RSA", {{2,0,"2048,4096"}}}, {"Ed25519", {{2,0,"256"}}}, {"ECDSA", {{2,0,"256,384,521"}}}}),
    TableItem("Size", "separator", ""),
    TableItem("Bits", "select", "2048,4096")
  }, tw->second_col);
  view->AddNavigationButton(TableItem("Generate", "", "", "", 0, 0, 0, bind(&TerminalMenuWindow::GenerateKey, tw)), HAlign::Right);
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

MyKeysViewController::MyKeysViewController(TerminalMenuWindow *w, MyCredentialDB *M, bool add) :
  tw(w), model(M), add_or_edit(add), view(make_unique<SystemTableView>("Keys", "", vector<TableItem>{})) {
  if (add_or_edit) view->AddNavigationButton(TableItem("+", "", "", "", 0, 0, 0, [=](){ tw->hosts_nav->PushTable(tw->newkey.view.get()); }), HAlign::Right);
  else {
    view->AddNavigationButton(TableItem("Edit", "", ""), HAlign::Right);
    view->SetEditableSection(bind(&TerminalMenuWindow::DeleteKey, tw, _1, _2), 0);
  }
  view->show_cb = bind(&MyKeysViewController::UpdateViewFromModel, this);
}

void MyKeysViewController::UpdateViewFromModel() {
  vector<TableItem> section;
  if (add_or_edit)
    section.push_back({"None", "command", "", "", 0, 0, 0, bind(&TerminalMenuWindow::ChooseKey, tw, 0)});
  for (auto credential : model->data) {
    auto c = flatbuffers::GetRoot<LTerminal::Credential>(credential.second.data());
    if (c->type() != CredentialType_PEM) continue;
    string name = c->displayname() ? c->displayname()->data() : "";
    section.push_back({name, add_or_edit ? "command" : "", "", "", credential.first, 0, 0,
                      Callback(bind(&TerminalMenuWindow::ChooseKey, tw, credential.first))});
  }
  view->BeginUpdates();
  view->ReplaceSection(section, 0);
  view->EndUpdates();
  view->changed = false;
}

MyRunSettingsViewController::MyRunSettingsViewController(TerminalMenuWindow *w) :
  view(make_unique<SystemTableView>("Settings", "", GetSchema(w, w->runsettings_nav.get()))) {
  view->AddNavigationButton(TableItem("Back", "", "", "", 0, 0, 0,
                                      bind(&SystemNavigationView::Show, w->runsettings_nav.get(), false)),
                            HAlign::Left);
}

vector<TableItem> MyRunSettingsViewController::GetBaseSchema(TerminalMenuWindow *tw, SystemNavigationView *nav) {
  return vector<TableItem>{
    TableItem("Appearance",      "command", "", ">", 0, tw->res->eye_icon,      0, bind(&SystemNavigationView::PushTable, nav, tw->appearance.view.get())),
    TableItem("Keyboard",        "",        "", ">", 0, tw->res->keyboard_icon, 0),
    TableItem("Beep",            "",        "", ">", 0, tw->res->audio_icon,    0),
    TableItem("Text Encoding",   "label",   "") };
}

vector<TableItem> MyRunSettingsViewController::GetSchema(TerminalMenuWindow *tw, SystemNavigationView *nav) {
  auto ret = GetBaseSchema(tw, nav);
  VectorCat(ret, vector<TableItem>{
            TableItem("Autocomplete",           "separator", ""), 
            TableItem("Autocomplete",           "toggle",    ""),
            TableItem("Prompt String",          "textinput", ""),
            TableItem("Reset Autcomplete Data", "command",   "") });
  return ret;
}

void MyRunSettingsViewController::UpdateViewFromModel(const MyAppSettingsModel &app_model, const MyHostSettingsModel &host_model) {
  view->BeginUpdates();
  view->SetSectionValues(vector<string>{ "", "", "", LTerminal::EnumNameTextEncoding(host_model.runsettings.text_encoding) }, 0);
  view->SetSectionValues(vector<string>{ host_model.autocomplete_id ? "1" : "", host_model.prompt, "" }, 1);
  view->EndUpdates();
}

void MyRunSettingsViewController::UpdateModelFromView(MyAppSettingsModel *app_model, MyHostSettingsModel *host_model) const {
  host_model->prompt = "Prompt String";
  string appearance="Appearance", keyboard="Keyboard", beep="Beep",
         textencoding="Text Encoding", autocomplete="Autocomplete", reset;
  if (!view->GetSectionText(0, {&appearance, &keyboard, &beep, &textencoding})) return ERROR("parse runsettings1");
  if (!view->GetSectionText(1, {&autocomplete, &host_model->prompt, &reset})) return ERROR("parse runsettings2");
}

MyAppSettingsViewController::MyAppSettingsViewController(TerminalMenuWindow *w) :
  view(make_unique<SystemTableView>("Settings", "", GetSchema(w))) {
  view->hide_cb = [=](){
    if (view->changed) {
      MyAppSettingsModel settings(&w->res->settings_db);
      UpdateModelFromView(&settings);
      settings.Save(&w->res->settings_db);
    }
  };
}

vector<TableItem> MyAppSettingsViewController::GetSchema(TerminalMenuWindow *tw) {
  return vector<TableItem>{
    TableItem("Local Encryption",    "command", "", ">", 0, tw->res->fingerprint_icon, 0, bind(&SystemNavigationView::PushTable, tw->hosts_nav.get(), tw->encryption.view.get())),
    TableItem("Keys",                "command", "", ">", 0, tw->res->key_icon,         0, bind(&SystemNavigationView::PushTable, tw->hosts_nav.get(), tw->editkeys  .view.get())),
    TableItem("Keep Display On",     "toggle",  ""),
    TableItem("", "separator", ""),
    TableItem("About LTerminal",     "", ""),
    TableItem("Support",             "", ""),
    TableItem("Privacy",             "", "") };
}

void MyAppSettingsViewController::UpdateViewFromModel(const MyAppSettingsModel &model) {
  view->BeginUpdates();
  view->SetSectionValues(vector<string>{ "", "", model.keep_display_on ? "1" : "" }, 0);
  view->EndUpdates();
}

void MyAppSettingsViewController::UpdateModelFromView(MyAppSettingsModel *model) {
  string a="", b="", keepdisplayon="Keep Display On";
  if (!view->GetSectionText(0, {&a, &b, &keepdisplayon})) return ERROR("parse appsettings0");
  model->keep_display_on = keepdisplayon == "1";
}

MyHostFingerprintViewController::MyHostFingerprintViewController(TerminalMenuWindow *w) :
  view(make_unique<SystemTableView>("Host Fingerprint", "", TableItemVec{
    TableItem("Type", "label", ""), TableItem("MD5", "label", ""), TableItem("SHA256", "label", ""),
    TableItem("", "separator", ""), TableItem("Clear", "button", "", "", 0, 0, 0, [=](){})
  })) { view->SelectRow(-1, -1); }
  
void MyHostFingerprintViewController::UpdateViewFromModel(const MyHostModel &model) {
  view->BeginUpdates();
  view->SetSectionValues(StringVec{ SSH::Key::Name(model.fingerprint_type),
    model.fingerprint.size() ? HexEscape(Crypto::MD5(model.fingerprint), ":").substr(1) : "",
    model.fingerprint.size() ? Singleton<Base64>::Get()->Encode(Crypto::SHA256(model.fingerprint)) : ""}, 0);
  view->EndUpdates();
}

vector<TableItem> MyHostSettingsViewController::GetSchema(TerminalMenuWindow *tw) {
  return vector<TableItem>{
    TableItem("Folder",               "textinput", "", "",  0, tw->res->folder_icon),
    TableItem("Advanced",             "separator", ""),
    TableItem("Host Key Fingerprint", "command",   "", ">", 0, tw->res->fingerprint_icon, 0,
              bind(&SystemNavigationView::PushTable, tw->hosts_nav.get(), tw->hostfingerprint.view.get())),
    TableItem("Terminal Type",        "textinput", "", "",  0, tw->res->terminal_icon),
    TableItem("Agent Forwarding",     "toggle",    ""),
    TableItem("Compression",          "toggle",    ""),
    TableItem("Close on Disconnect",  "toggle",    ""),
    TableItem("Startup Command",      "textinput", "") };
}

void MyHostSettingsViewController::UpdateViewFromModel(const MyHostModel &model) {
  view->BeginUpdates();
  view->SetSectionValues(vector<string>{ model.folder }, 0);
  view->SetSectionValues(vector<string>{ "",
    model.settings.terminal_type,
    model.settings.agent_forwarding ? "1" : "",
    model.settings.compression ? "1" : "",
    model.settings.close_on_disconnect ? "1" : "",
    model.settings.startup_command }, 1);
  view->EndUpdates();
}

bool MyHostSettingsViewController::UpdateModelFromView(MyHostSettingsModel *model, string *folder) const {
  *folder = "Folder";
  model->terminal_type = "Terminal Type";
  model->startup_command = "Startup Command";
  string fingerprint = "Host Key Fingerprint", forwarding = "Agent Forwarding", compression = "Compression",
         disconclose = "Close on Disconnect";
  if (!view->GetSectionText(0, {folder})) return ERRORv(false, "parse newhostconnect settings0");
  if (!view->GetSectionText(1, {&fingerprint, &model->terminal_type, &forwarding, &compression, &disconclose,
                            &model->startup_command})) return ERRORv(false, "parse newhostconnect settings1");
  model->agent_forwarding    = forwarding  == "1";
  model->compression         = compression == "1";
  model->close_on_disconnect = disconclose == "1";
  return true;
}

MyQuickConnectViewController::MyQuickConnectViewController(TerminalMenuWindow *w) :
  view(make_unique<SystemTableView>("Quick Connect", "", GetSchema(w), w->second_col)) {}

vector<TableItem> MyQuickConnectViewController::GetSchema(TerminalMenuWindow *tw) {
  return vector<TableItem>{
    TableItem("Protocol,SSH,Telnet", "dropdown,textinput,textinput", ",\x01Hostname,\x01Hostname"),
    TableItem("Port", "numinput", "\x02""22"),
    TableItem("Username", "textinput", "\x01Username"),
    TableItem("Credential,Password,Key", "dropdown,pwinput,label", StrCat("nocontrol,", tw->res->pw_default, ","),
              "", 0, 0, tw->res->key_icon, Callback(), bind(&SystemNavigationView::PushTable, tw->hosts_nav.get(), tw->keys.view.get())),
    TableItem("", "separator", ""),
    TableItem("Connect", "button", "", "", 0, 0, 0, bind(&TerminalMenuWindow::QuickConnect, tw)),
    TableItem("", "separator", ""),
    TableItem("Server Settings", "button", "", "", 0, 0, 0, bind(&TerminalMenuWindow::ShowHostSettings, tw)) };
}

bool MyQuickConnectViewController::UpdateModelFromView(MyHostModel *model, MyCredentialDB *cred_db) const {
  model->hostname    = "";
  model->username    = "Username";
  string prot = "Protocol", port = "Port", credtype = "Credential", cred = "";
  if (!view->GetSectionText(0, {&prot, &model->hostname, &port, &model->username,
                            &credtype, &cred})) return ERRORv(false, "parse quickconnect");

  model->displayname = StrCat(model->hostname, "@", model->username);
  model->SetPort(atoi(port));
  auto ct = MyCredentialModel::GetCredentialType(credtype);
  if      (ct == CredentialType_Password) model->cred.Load(CredentialType_Password, cred);
  else if (ct == CredentialType_PEM)      model->cred.Load(cred_db, view->GetTag(0, 3));
  else                                    model->cred.Load();
  return true;
}

MyNewHostViewController::MyNewHostViewController(TerminalMenuWindow *w) :
  view(make_unique<SystemTableView>("New Host", "", GetSchema(w), w->second_col)) { view->SelectRow(0, 1); }

vector<TableItem> MyNewHostViewController::GetSchema(TerminalMenuWindow *tw) {
  vector<TableItem> ret = MyQuickConnectViewController::GetSchema(tw);
  ret[5].CheckAssign("Connect", bind(&TerminalMenuWindow::NewHostConnect, tw));
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

  model->SetPort(atoi(port));
  auto ct = MyCredentialModel::GetCredentialType(credtype);
  if      (ct == CredentialType_Password) model->cred.Load(CredentialType_Password, cred);
  else if (ct == CredentialType_PEM)      model->cred.Load(cred_db, view->GetTag(0, 4));
  else                                    model->cred.Load();
  return true;
}

MyUpdateHostViewController::MyUpdateHostViewController(TerminalMenuWindow *w) : tw(w),
  view(make_unique<SystemTableView>("Update Host", "", GetSchema(w), tw->second_col)) {}

vector<TableItem> MyUpdateHostViewController::GetSchema(TerminalMenuWindow *tw) {
  vector<TableItem> ret = MyNewHostViewController::GetSchema(tw);
  ret[6].CheckAssign("Connect", bind(&TerminalMenuWindow::UpdateHostConnect, tw));
  return ret;
}

void MyUpdateHostViewController::UpdateViewFromModel(const MyHostModel &host) {
  prev_model = host;
  bool pw = host.cred.credtype == CredentialType_Password, pem = host.cred.credtype == CredentialType_PEM;
  view->BeginUpdates();
  view->SetSectionValues(vector<string>{
    host.displayname, StrCat(",", host.hostname, ","), StrCat(host.port), host.username,
    StrCat("nocontrol,", pw ? host.cred.creddata : tw->res->pw_default, ",", host.cred.name) }, 0);
  view->SetDropdown(0, 1, 0);
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

  model->SetPort(atoi(port));
  auto ct = MyCredentialModel::GetCredentialType(credtype);
  if      (ct == CredentialType_Password) model->cred.Load(CredentialType_Password, cred);
  else if (ct == CredentialType_PEM)      model->cred.Load(cred_db, view->GetTag(0, 4));
  else                                    model->cred.Load();
  return true;
}

MyHostsViewController::MyHostsViewController(TerminalMenuWindow *w, MyHostDB *model, bool m) : menu(m), tw(w) {
  toolbar = make_unique<SystemToolbarView>(MenuItemVec{
    { "\U00002699", "", bind(&TerminalMenuWindow::ShowAppSettings, tw) },
    { "+",          "", bind(&TerminalMenuWindow::ShowNewHost,     tw) }
  });
  view = make_unique<SystemTableView>("Hosts", "", !menu ? TableItemVec() : TableItemVec{
    TableItem("Quick connect",     "command", "", ">", 0, tw->res->bolt_icon,     0, bind(&TerminalMenuWindow::ShowQuickConnect, tw)),
    TableItem("Interactive Shell", "command", "", ">", 0, tw->res->terminal_icon, 0, bind(&TerminalMenuWindow::StartShell, tw)),
    TableItem("", "separator", "")
  });
}

void MyHostsViewController::LoadLockedUI() {
}

void MyHostsViewController::LoadUnlockedUI(MyHostDB *model) {
  view->AddToolbar(toolbar.get());
  view->AddNavigationButton(TableItem("Edit", "", ""), HAlign::Right);
  view->SetEditableSection(bind(&TerminalMenuWindow::DeleteHost, tw, _1, _2), menu);
  view->show_cb = bind(&MyHostsViewController::UpdateViewFromModel, this, model);
}

void MyHostsViewController::UpdateViewFromModel(MyHostDB *model) {
  vector<TableItem> section; 
  unordered_set<string> seen_folders;
  for (auto &host : model->data) {
    if (host.first == 1) continue;
    const LTerminal::Host *h = flatbuffers::GetRoot<LTerminal::Host>(host.second.data());
    string displayname = GetFlatBufferString(h->displayname()), host_folder = GetFlatBufferString(h->folder());
    if (folder.size()) { if (host_folder != folder) continue; }
    else if (host_folder.size()) {
      if (seen_folders.insert(host_folder).second)
        section.emplace_back(host_folder, "command", "", "", 0, tw->res->folder_icon, 0, [=](){
          tw->hostsfolder.view->SetTitle(StrCat((tw->hostsfolder.folder = move(host_folder)), " Folder"));
          tw->hosts_nav->PushTable(tw->hostsfolder.view.get()); });
      continue;
    }
    section.emplace_back(displayname, "command", "", "", host.first, tw->res->host_icon, 
                         tw->res->settings_icon, bind(&TerminalMenuWindow::ConnectHost, tw, host.first),
                         bind(&TerminalMenuWindow::HostInfo, tw, host.first));
  }
  view->BeginUpdates();
  view->ReplaceSection(section, menu);
  view->EndUpdates();
  view->changed = false;
}

}; // namespace LFL
