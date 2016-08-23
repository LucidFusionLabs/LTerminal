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
MyAppearanceViewController::MyAppearanceViewController(TerminalMenuWindow *tw) :
  view(make_unique<SystemTableView>("Appearance", "", vector<TableItem>{
    TableItem("Font", "command", "", "", 0, 0, 0, [=](){ ShellRun("choosefont"); }),
    TableItem("Toys", "command", "", "", 0, 0, 0, bind(&TerminalMenuWindow::ShowToysMenu, tw))
  })) {}

MyKeyboardSettingsViewController::MyKeyboardSettingsViewController() {
  vector<TableItem> table{ TableItem("Delete Sends ^H", "toggle", "") };
  // model.settings.runsettings.delete_mode == LTerminal::DeleteMode_ControlH ? "1" : "",
  // string deletemode = "", &deletemode,
}

MyNewKeyViewController::MyNewKeyViewController(TerminalMenuWindow *tw) {
  view = make_unique<SystemTableView>("New Key", "", vector<TableItem>{
    TableItem("Generate New Key", "command", "", "", 0, 0, 0, [=](){ tw->hosts_nav->PushTable(tw->genkey.view.get()); }),
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

MyKeysViewController::MyKeysViewController(TerminalMenuWindow *w, MyCredentialDB *M) : tw(w), model(M),
view(make_unique<SystemTableView>("Keys", "", vector<TableItem>{})) {
  view->AddNavigationButton(TableItem("+", "", "", "", 0, 0, 0, [=](){ tw->hosts_nav->PushTable(tw->newkey.view.get()); }), HAlign::Right);
  view->SetEditableSection(IntIntCB(), 0);
  view->show_cb = bind(&MyKeysViewController::UpdateViewFromModel, this);
}

void MyKeysViewController::UpdateViewFromModel() {
  vector<TableItem> section;
  section.push_back({"None", "command", "", "", 0, 0, 0, bind(&TerminalMenuWindow::ChooseKey, tw, 0)});
  for (auto credential : model->data) {
    auto c = flatbuffers::GetRoot<LTerminal::Credential>(credential.second.data());
    if (c->type() != CredentialType_PEM) continue;
    string name = c->displayname() ? c->displayname()->data() : "";
    section.push_back({name, "command", "", "", 0, 0, 0, bind(&TerminalMenuWindow::ChooseKey, tw, credential.first)});
  }
  view->BeginUpdates();
  view->ReplaceSection(section, 0);
  view->EndUpdates();
}

MyRunSettingsViewController::MyRunSettingsViewController(TerminalMenuWindow *w) :
  view(make_unique<SystemTableView>("Settings", "", GetSchema(w, w->runsettings_nav.get()))) {
  view->AddNavigationButton(TableItem("Done", "", "", "", 0, 0, 0, [](){}), HAlign::Right);
  view->AddNavigationButton(TableItem("Back", "", "", "", 0, 0, 0,
                                      bind(&SystemNavigationView::Show, w->runsettings_nav.get(), false)),
                            HAlign::Left);
}

vector<TableItem> MyRunSettingsViewController::GetBaseSchema(TerminalMenuWindow *tw, SystemNavigationView *nav) {
  return vector<TableItem>{
    TableItem("Appearance",      "command", "", ">", 0, tw->res->eye_icon,      0, bind(&SystemNavigationView::PushTable, nav, tw->appearance.view.get())),
    TableItem("Keyboard",        "",        "", ">", 0, tw->res->keyboard_icon, 0),
    TableItem("Beep",            "",        "", ">", 0, tw->res->audio_icon,    0),
    TableItem("Keep Display On", "toggle",  ""),
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
  view->SetSectionValues(vector<string>{
    "", "", "", app_model.keep_display_on ? "1" : "",
    LTerminal::EnumNameTextEncoding(host_model.runsettings.text_encoding) }, 0);
  view->SetSectionValues(vector<string>{ host_model.autocomplete_id ? "1" : "", host_model.prompt, "" }, 1);
  view->EndUpdates();
}

void MyRunSettingsViewController::UpdateModelFromView(MyAppSettingsModel *app_model, MyHostSettingsModel *host_model) const {
#if 0
  if (!view->GetSectionText(1, {&forwarding, &termtype, &deletemode, &disconclose,
                            &startup, &fingerprint})) return ERROR("parse newhostconnect settings1");
  if (!view->GetSectionText(2, {&complete, &prompt, &reset})) return ERROR("parse newhostconnect settings2");
#endif
}

vector<TableItem> MyAppSettingsViewController::GetSchema(TerminalMenuWindow *tw) {
  vector<TableItem> ret = MyRunSettingsViewController::GetBaseSchema(tw, tw->hosts_nav.get());
  VectorCat(ret, vector<TableItem>{
    TableItem("", "separator", ""),
    TableItem("Touch ID & Passcode", "", "", ">", 0, tw->res->fingerprint_icon),
    TableItem("Keys",                "", "", ">", 0, tw->res->key_icon),
    TableItem("", "separator", ""),
    TableItem("About LTerminal",     "", ""),
    TableItem("Support",             "", ""),
    TableItem("Privacy",             "", "") });
  return ret;
}

void MyAppSettingsViewController::UpdateViewFromModel(const MyAppSettingsModel &model) {
  view->BeginUpdates();
  view->SetSectionValues(vector<string>{ "", "", "", model.keep_display_on ? "1" : "",
    LTerminal::EnumNameTextEncoding(model.runsettings.text_encoding) }, 0);
  view->EndUpdates();
}

vector<TableItem> MyHostSettingsViewController::GetSchema(TerminalMenuWindow *tw) {
  return vector<TableItem>{
    TableItem("Folder", "textinput", "", "", 0, tw->res->folder_icon),
    TableItem("Advanced", "separator", ""),
    TableItem("Host Key Fingerprint", "", "", ">", 0, tw->res->fingerprint_icon),
    TableItem("Terminal Type", "textinput", "", "", 0, tw->res->terminal_icon),
    TableItem("Agent Forwarding", "toggle", ""),
    TableItem("Compression", "toggle", ""),
    TableItem("Close on Disconnect", "toggle", ""),
    TableItem("Startup Command", "textinput", "") };
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

void MyQuickConnectViewController::UpdateModelFromView(MyHostModel *model) const {
  string prot = "Protocol", host = "", port = "Port", user = "Username", credtype = "Password", cred = "";
  if (!view->GetSectionText(0, {&prot, &host, &port, &user, &credtype, &cred})) return ERROR("parse quickconnect");
  string displayname = StrCat(FLAGS_ssh, "@", FLAGS_login);
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

MyHostsViewController::MyHostsViewController(TerminalMenuWindow *w, MyHostDB *model) : tw(w) {
  toolbar = make_unique<SystemToolbarView>(MenuItemVec{
    { "\U00002699", "", bind(&TerminalMenuWindow::ShowAppSettings, tw) },
    { "+",          "", bind(&TerminalMenuWindow::ShowNewHost,     tw) }
  });
  view = make_unique<SystemTableView>("Hosts", "", TableItemVec{
    TableItem("Quick connect",     "command", "", ">", 0, tw->res->bolt_icon,     0, bind(&TerminalMenuWindow::ShowQuickConnect, tw)),
    TableItem("Interactive Shell", "command", "", ">", 0, tw->res->terminal_icon, 0, bind(&TerminalMenuWindow::StartShell, tw)),
    TableItem("", "separator", "")
  });
  view->AddNavigationButton(TableItem("Edit", "", ""), HAlign::Right);
  view->AddToolbar(toolbar.get());
  view->SetEditableSection(bind(&TerminalMenuWindow::DeleteHost, tw, _1, _2), 1);
  view->show_cb = bind(&MyHostsViewController::UpdateViewFromModel, this, model);
}

void MyHostsViewController::UpdateViewFromModel(MyHostDB *model) {
  vector<TableItem> section; 
  for (auto host : model->data) {
    const LTerminal::Host *h = flatbuffers::GetRoot<LTerminal::Host>(host.second.data());
    string displayname = h->displayname() ? h->displayname()->data() : "";
    section.emplace_back(displayname, "command", "", "", host.first, tw->res->host_icon, 
                         tw->res->settings_icon, bind(&TerminalMenuWindow::ConnectHost, tw, host.first),
                         bind(&TerminalMenuWindow::HostInfo, tw, host.first));
  }
  view->BeginUpdates();
  view->ReplaceSection(section, 1);
  view->EndUpdates();
}

}; // namespace LFL
