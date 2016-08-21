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
MyHostsViewController::MyHostsViewController(TerminalMenuWindow *w, SQLiteIdValueStore *model) : tw(w) {
  MenuItemVec tb = { { "\U00002699", "", bind(&TerminalMenuWindow::ShowAppSettings, tw) },
    { "+", "", [=](){ tw->hosts_nav->PushTable(tw->newhost.view.get()); } } };
  toolbar = make_unique<SystemToolbarView>(tb);
  vector<TableItem> table{
    TableItem("Quick connect", "command", "", ">", 0, my_res->bolt_icon, 0, [=]{ tw->hosts_nav->PushTable(tw->quickconnect.view.get()); }),
    TableItem("Interactive Shell", "command", "", ">", 0, my_res->terminal_icon, 0, bind(&TerminalMenuWindow::StartShell, tw)),
    TableItem("", "separator", "") };
  view = make_unique<SystemTableView>("Hosts", "", table);
  view->AddNavigationButton(TableItem("Edit", "", ""), HAlign::Right);
  view->AddToolbar(toolbar.get());
  view->SetEditableSection(bind(&TerminalMenuWindow::DeleteHost, tw, _1, _2), 1);
  view->show_cb = bind(&MyHostsViewController::UpdateViewFromModel, this, model);
}

void MyHostsViewController::UpdateViewFromModel(SQLiteIdValueStore *model) {
  vector<TableItem> section; 
  for (auto host : model->data) {
    const LTerminal::Host *h = flatbuffers::GetRoot<LTerminal::Host>(host.second.data());
    string displayname = h->displayname() ? h->displayname()->data() : "";
    section.emplace_back(displayname, "command", "", "", host.first, my_res->host_icon, 
                         my_res->settings_icon, bind(&TerminalMenuWindow::ConnectHost, tw, host.first),
                         bind(&TerminalMenuWindow::HostInfo, tw, host.first));
  }
  view->BeginUpdates();
  view->ReplaceSection(section, 1);
  view->EndUpdates();
}

vector<TableItem> MyRunSettingsViewController::GetBaseSchema() {
  return vector<TableItem>{ TableItem("Appearance", "", "", "", 0, my_res->eye_icon),
    TableItem("Keyboard", "", "", "", 0, my_res->keyboard_icon),
    TableItem("Beep", "", "", "", 0, my_res->audio_icon),
    TableItem("Keep Display On", "toggle", ""), TableItem("Text Encoding", "", "") };
}

vector<TableItem> MyRunSettingsViewController::GetSchema() {
  auto ret = GetBaseSchema();
  VectorCat(ret, vector<TableItem>{ TableItem("Autocomplete", "separator", ""), 
            TableItem("Autocomplete", "toggle", ""), TableItem("Prompt String", "textinput", ""),
            TableItem("Reset Autcomplete Data", "command", "") });
  return ret;
}

void MyRunSettingsViewController::UpdateViewFromModel(const MyRunSettingsModel &model) {
}

void MyRunSettingsViewController::UpdateModelFromView(MyRunSettingsModel *model) const {
#if 0
  if (!view->GetSectionText(1, {&forwarding, &termtype, &deletemode, &disconclose,
                            &startup, &fingerprint})) return ERROR("parse newhostconnect settings1");
  if (!view->GetSectionText(2, {&complete, &prompt, &reset})) return ERROR("parse newhostconnect settings2");
#endif
}

vector<TableItem> MyAppSettingsViewController::GetSchema() {
  vector<TableItem> ret = MyRunSettingsViewController::GetBaseSchema();
  VectorCat(ret, vector<TableItem>{ TableItem("", "separator", ""),
    TableItem("Touch ID & Passcode", "", "", "", 0, my_res->fingerprint_icon),
    TableItem("Keys", "", "", "", 0, my_res->key_icon), TableItem("", "separator", ""),
    TableItem("About LTerminal", "", ""), TableItem("Support", "", ""), TableItem("Privacy", "", "") });
  return ret;
}

void MyAppSettingsViewController::UpdateViewFromModel(SQLiteIdValueStore *model) {
  const LTerminal::AppSettings *s =
    flatbuffers::GetRoot<LTerminal::AppSettings>(model->data[1].data());
  vector<string> settings0{ "", LTerminal::EnumNameBeepType(s->run_settings()->beep_type()),
    s->keep_display_on() ? "1" : "" };
  view->BeginUpdates();
  //view->SetSectionValues(settings0, 0);
  view->EndUpdates();
}

vector<TableItem> MyQuickConnectViewController::GetSchema(TerminalMenuWindow *tw) {
  return vector<TableItem>{
    TableItem("Protocol,SSH,Telnet", "dropdown,textinput,textinput", ",\x01Hostname,\x01Hostname"),
    TableItem("Port", "numinput", "\x02""22"),
    TableItem("Username", "textinput", "\x01Username"),
    TableItem("Credential,Password,Key", "dropdown,pwinput,label", StrCat("nocontrol,", my_res->pw_default, ","),
              "", 0, 0, my_res->key_icon, Callback(), bind(&SystemNavigationView::PushTable, tw->hosts_nav.get(), tw->keys.view.get())),
    TableItem("", "separator", ""),
    TableItem("Connect", "button", "", "", 0, 0, 0, bind(&TerminalMenuWindow::QuickConnect, tw)),
    TableItem("", "separator", ""),
    TableItem("Server Settings", "button", "", "", 0, 0, 0, bind(&TerminalMenuWindow::HostSettings, tw)) };
}

void MyQuickConnectViewController::UpdateModelFromView(MyHostModel *model) const {
  string prot = "Protocol", host = "", port = "Port", user = "Username", credtype = "Password", cred = "";
  if (!view->GetSectionText(0, {&prot, &host, &port, &user, &credtype, &cred})) return ERROR("parse quickconnect");
  string displayname = StrCat(FLAGS_ssh, "@", FLAGS_login);
}

vector<TableItem> MyNewHostViewController::GetSchema(TerminalMenuWindow *tw) {
  vector<TableItem> ret = MyQuickConnectViewController::GetSchema(tw);
  ret[5].CheckAssign("Connect", bind(&TerminalMenuWindow::NewHostConnect, tw));
  ret.insert(ret.begin(), TableItem{"Name", "textinput", "\x01Nickname"});
  return ret;
}

bool MyNewHostViewController::UpdateModelFromView(MyHostModel *model, SQLiteIdValueStore *cred_db) const {
  model->displayname = "Name";
  model->hostname    = "";
  model->username    = "Username";
  string prot = "Protocol", port = "Port", credtype = "Credential", cred = "";
  if (!view->GetSectionText(0, {&model->displayname, &prot, &model->hostname, &port, &model->username,
                            &credtype, &cred})) return ERRORv(false, "parse newhostconnect");

  model->SetPort(atoi(port));
  auto ct = MyCredentialModel::GetCredentialType(credtype);
  if      (ct == LTerminal::CredentialType_Password) model->cred.Load(cred);
  else if (ct == LTerminal::CredentialType_PEM)      model->cred.Load(cred_db, view->GetTag(0, 4));
  else                                               model->cred.Load();
  return true;
}

vector<TableItem> MyUpdateHostViewController::GetSchema(TerminalMenuWindow *tw) {
  vector<TableItem> ret = MyNewHostViewController::GetSchema(tw);
  ret[6].CheckAssign("Connect", bind(&TerminalMenuWindow::UpdateHostConnect, tw));
  return ret;
}

void MyUpdateHostViewController::UpdateViewFromModel(const MyHostModel &host) {
  prev_model = host;
  bool pem = host.cred.credtype == CredentialType_PEM;
  vector<string> v{ host.displayname, StrCat(",", host.hostname, ","),
    StrCat(host.port), host.username,
    StrCat("nocontrol,", host.cred.password.size() ? host.cred.password : my_res->pw_default,
           ",", host.cred.keyname) };
  view->BeginUpdates();
  view->SetSectionValues(v);
  view->SetDropdown(0, 1, 0);
  view->SetDropdown(0, 4, pem);
  view->SetTag(0, 4, pem ? host.cred.cred_id : 0);
  view->EndUpdates();
}

bool MyUpdateHostViewController::UpdateModelFromView(MyHostModel *model, SQLiteIdValueStore *cred_db) const {
  model->displayname = "Name";
  model->hostname    = "";
  model->username    = "Username";
  string prot = "Protocol", port = "Port", credtype = "Credential", cred = "";
  if (!view->GetSectionText(0, {&model->displayname, &prot, &model->hostname, &port, &model->username,
                            &credtype, &cred})) return ERRORv(false, "parse updatehostconnect");
  model->SetPort(atoi(port));
  auto ct = MyCredentialModel::GetCredentialType(credtype);
  if      (ct == LTerminal::CredentialType_Password) model->cred.Load(cred);
  else if (ct == LTerminal::CredentialType_PEM)      model->cred.Load(cred_db, view->GetTag(0, 4));
  else                                               model->cred.Load();
  return true;
}

vector<TableItem> MyHostSettingsViewController::GetSchema() {
  return vector<TableItem>{ TableItem("Folder", "textinput", ""),
    TableItem("Advanced", "separator", ""), TableItem("Agent Forwarding", "toggle", ""),
    TableItem("Compression", "toggle", ""), TableItem("Close on Disconnect", "toggle", ""),
    TableItem("Host Key Fingerprint", "", "", ">", 0, my_res->fingerprint_icon),
    TableItem("Terminal Type", "textinput", "", "", 0, my_res->terminal_icon),
    TableItem("Startup Command", "textinput", "") };
}

void MyHostSettingsViewController::UpdateViewFromModel(const MyHostModel &model) {
  vector<string> settings0{ LTerminal::EnumNameTextEncoding(model.settings.runsettings.text_encoding),
    model.folder };
  vector<string> settings1{ model.settings.agent_forwarding ? "1" : "", model.settings.terminal_type,
    model.settings.runsettings.delete_mode == LTerminal::DeleteMode_ControlH ? "1" : "",
    model.settings.close_on_disconnect ? "1" : "", model.settings.startup_command, "" };
  vector<string> settings2{ model.settings.autocomplete_id ? "1" : "", model.settings.prompt, "" };
  view->BeginUpdates();
  //view->SetSectionValues(settings0, 0);
  //view->SetSectionValues(settings1, 1);
  //view->SetSectionValues(settings2, 2);
  view->EndUpdates();
}

bool MyHostSettingsViewController::UpdateModelFromView(MyHostSettingsModel *model, string *folder) const {
  *folder = "Folder";
  string encoding = "Text Encoding", forwarding = "Agent Forwarding",
         termtype = "Terminal Type", deletemode = "", disconclose = "Close on Disconnect",
         startup = "Startup Command", fingerprint, complete = "Autocomplete", prompt = "Prompt String",
         reset, compression;
  if (!view->GetSectionText(0, {&encoding, folder})) return ERRORv(false, "parse newhostconnect settings0");
  if (!view->GetSectionText(1, {&forwarding, &termtype, &deletemode, &disconclose, &startup, &fingerprint})) return ERRORv(false, "parse newhostconnect settings1");
  if (!view->GetSectionText(2, {&complete, &prompt, &reset})) return ERRORv(false, "parse newhostconnect settings2");
  return true;
}

MyKeysViewController::MyKeysViewController(TerminalMenuWindow *tw, SQLiteIdValueStore *M) : model(M),
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

MyNewKeyViewController::MyNewKeyViewController(TerminalMenuWindow *tw) {
  vector<TableItem> table{
    TableItem("Generate New Key", "command", "", "", 0, 0, 0, [=](){ tw->hosts_nav->PushTable(tw->genkey.view.get()); }),
    TableItem("Paste from Clipboard", "command", "", "", 0, 0, 0, bind(&TerminalMenuWindow::PasteKey, tw)) };
  view = make_unique<SystemTableView>("New Key", "", move(table));
}

MyGenKeyViewController::MyGenKeyViewController(TerminalMenuWindow *tw) {
  vector<TableItem> table{ TableItem("Name", "textinput", ""), TableItem("Passphrase", "pwinput", ""),
    TableItem("Type", "separator", ""), TableItem("Algo", "select", "RSA,Ed25519,ECDSA", "", 0, 0, 0, Callback(), Callback(),
                                                  {{"RSA", {{2,0,"2048,4096"}}}, {"Ed25519", {{2,0,"256"}}}, {"ECDSA", {{2,0,"256,384,521"}}}}),
    TableItem("Size", "separator", ""), TableItem("Bits", "select", "2048,4096") };
  view = make_unique<SystemTableView>("Generate New Key", "", move(table), 120);
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

MyAppearanceViewController::MyAppearanceViewController(TerminalMenuWindow *tw) {
  vector<TableItem> table{ TableItem("Font", "command", "", "", 0, 0, 0, [=](){ ShellRun("choosefont"); }),
    TableItem("Toys", "command", "", "", 0, 0, 0, bind(&TerminalMenuWindow::ShowToysMenu, tw)) };
}

MyKeyboardSettingsViewController::MyKeyboardSettingsViewController() {
  vector<TableItem> table{ TableItem("Delete Sends ^H", "toggle", "") };
}

};
