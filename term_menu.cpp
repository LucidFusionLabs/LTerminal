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
MyKeyboardSettingsViewController::MyKeyboardSettingsViewController(MyTerminalMenus *m) :
  view(make_unique<SystemTableView>("Keyboard", "", vector<TableItem>{
    TableItem("Delete Sends ^H", TableItem::Toggle, "")
  })) {}

void MyKeyboardSettingsViewController::UpdateViewFromModel(const MyHostSettingsModel &model) {
  view->BeginUpdates();
  view->SetSectionValues(0, vector<string>{ model.delete_mode == LTerminal::DeleteMode_ControlH ? "1" : "" });
  view->EndUpdates();
}

MyNewKeyViewController::MyNewKeyViewController(MyTerminalMenus *m) {
  view = make_unique<SystemTableView>("New Key", "", vector<TableItem>{
    TableItem("Generate New Key",     TableItem::Command, "", "", 0, 0, 0, [=](){ m->hosts_nav->PushTableView(m->genkey.view.get()); }),
    TableItem("Paste from Clipboard", TableItem::Command, "", "", 0, 0, 0, bind(&MyTerminalMenus::PasteKey, m))
  });
}

MyGenKeyViewController::MyGenKeyViewController(MyTerminalMenus *m) {
  view = make_unique<SystemTableView>("Generate New Key", "", vector<TableItem>{
    TableItem("Name", TableItem::TextInput, "\x01Nickname"),
    TableItem("Passphrase", TableItem::PasswordInput, ""),
    TableItem("Type", TableItem::Separator, ""),
    TableItem("Algo", TableItem::Selector, "RSA,Ed25519,ECDSA", "", 0, 0, 0, Callback(), Callback(),
              {{"RSA", {{2,0,"2048,4096"}}}, {"Ed25519", {{2,0,"256"}}}, {"ECDSA", {{2,0,"256,384,521"}}}}),
    TableItem("Size", TableItem::Separator, ""),
    TableItem("Bits", TableItem::Selector, "2048,4096"),
    TableItem("", TableItem::Separator, ""),
    TableItem("Generate", TableItem::Command, "", ">", 0, m->keygen_icon, 0, bind(&MyTerminalMenus::GenerateKey, m))
    });
  view->show_cb = bind(&MyGenKeyViewController::UpdateViewFromModel, this);
}

void MyGenKeyViewController::UpdateViewFromModel() {
  view->BeginUpdates();
  view->SetSectionValues(0, vector<string>{ "\x01Nickname", "" });
  view->SelectRow(0, 1);
  view->EndUpdates();
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

MyKeyInfoViewController::MyKeyInfoViewController(MyTerminalMenus *m) : menus(m),
  view(make_unique<SystemTableView>("Info", "", TableItemVec{
    TableItem("Name", TableItem::TextInput, "", "", 0, m->font_icon),
    TableItem("Type", TableItem::Label,     "", "", 0, m->key_icon),
    TableItem("Date", TableItem::Label,     "", "", 0, m->calendar_icon),
    TableItem("", TableItem::Separator, ""), TableItem("Copy Public Key to Clipboard",  TableItem::Command, "", ">", 0, m->clipboard_upload_icon, 0, [=](){ m->CopyKeyToClipboard(cred_row_id, false); }),
    TableItem("", TableItem::Separator, ""), TableItem("Copy Private Key to Clipboard", TableItem::Command, "", ">", 0, m->clipboard_upload_icon, 0, [=](){ m->CopyKeyToClipboard(cred_row_id, true);  })
  })) {
  view->hide_cb = [=](){
    if (view->changed) {
      INFO("key changed");
    }
  };
}

void MyKeyInfoViewController::UpdateViewFromModel(const MyCredentialModel &m) {
  cred_row_id = m.cred_id;
  view->BeginUpdates();
  view->SetSectionValues(0, StringVec{ m.name, m.gentype, m.gendate });
  view->EndUpdates();
}

MyKeysViewController::MyKeysViewController(MyTerminalMenus *m, MyCredentialDB *mo) :
  menus(m), model(mo), view(make_unique<SystemTableView>("Choose Key", "", vector<TableItem>{
    TableItem("None",                     TableItem::Command, "", ">", 0, m->none_icon,               0, bind(&MyTerminalMenus::ChooseKey, menus, 0)),
    TableItem("Paste Key From Clipboard", TableItem::Command, "", ">", 0, m->clipboard_download_icon, 0, bind(&MyTerminalMenus::PasteKey, m)),
    TableItem("Generate New Key",         TableItem::Command, "", ">", 0, m->keygen_icon,             0, [=](){ m->hosts_nav->PushTableView(m->genkey.view.get()); }),
  })) {
  view->SetEditableSection(1, 0, bind(&MyTerminalMenus::DeleteKey, m, _1, _2));
  view->show_cb = bind(&MyKeysViewController::UpdateViewFromModel, this);
}

void MyKeysViewController::UpdateViewFromModel() {
  vector<TableItem> section;
  for (auto credential : model->data) {
    auto c = flatbuffers::GetRoot<LTerminal::Credential>(credential.second.data());
    if (c->type() != CredentialType_PEM) continue;
    string name = c->displayname() ? c->displayname()->data() : "";
    section.push_back({name, TableItem::Command, "", "", credential.first, menus->key_icon, menus->settings_gray_icon,
                      Callback(bind(&MyTerminalMenus::ChooseKey, menus, credential.first)),
                      Callback(bind(&MyTerminalMenus::KeyInfo,   menus, credential.first))});
  }
  view->BeginUpdates();
  view->ReplaceSection(1, section.size() ? "Keys" : "", 0,
                       section.size() ? (SystemTableView::EditButton | SystemTableView::EditableIfHasTag) : 0,
                       section);
  view->EndUpdates();
  view->changed = false;
}

MyAboutViewController::MyAboutViewController(MyTerminalMenus *m) :
  view(make_unique<SystemTableView>("About", "", vector<TableItem>{})) {
  view->BeginUpdates();
  view->ReplaceSection(0, "LTerminal", m->logo_icon, 0, TableItemVec{
    TableItem("Version",                 TableItem::None,    "", app->GetVersion(), 0, 0, 0),
    TableItem("Credits",                 TableItem::Command, "", ">", 0, 0, 0, [=](){ if (!m->credits) m->credits = make_unique<SystemTextView>("Credits", Asset::FileContents("credits.txt")); m->hosts_nav->PushTextView(m->credits.get()); }),
    TableItem("LTerminal Web Page",      TableItem::Command, "", ">", 0, 0, 0, bind(&Application::OpenSystemBrowser, app, "http://www.lucidfusionlabs.com/terminal/")),
    TableItem("Lucid Fusion Labs, LLC.", TableItem::Command, "", ">", 0, 0, 0, bind(&Application::OpenSystemBrowser, app, "http://www.lucidfusionlabs.com/")),
  });
  view->EndUpdates(); 
}

MySupportViewController::MySupportViewController(MyTerminalMenus *m) :
  view(make_unique<SystemTableView>("Support", "", vector<TableItem>{
    TableItem("Reference", TableItem::Separator, ""),
    TableItem("LTerminal", TableItem::Command, "", ">", 0, 0, 0, bind(&Application::OpenSystemBrowser, app, "http://www.lucidfusionlabs.com/terminal/")),
  })) {
  view->BeginUpdates();
  view->ReplaceSection(0, "Contact", 0, 0, TableItemVec{
    TableItem("Email",   TableItem::Command, "", "support@lucidfusionlabs.com", 0, 0, 0, bind(&Application::OpenSystemBrowser, app, "mailto:info@lucidfusionlabs.com")),
    TableItem("Twitter", TableItem::Command, "", "@LucidFusionLabs",            0, 0, 0, bind(&Application::OpenSystemBrowser, app, "https://twitter.com/intent/tweet?text=@LucidFusionLabs"))
  });
  view->EndUpdates(); 
}

MyPrivacyViewController::MyPrivacyViewController(MyTerminalMenus *m) :
  view(make_unique<SystemTableView>("Privacy", "", vector<TableItem>{
    TableItem("Send Crash Data and Statistics",      TableItem::Toggle,    "", "", 0, 0, 0),
    TableItem("Relaunch for changes to take effect", TableItem::Separator, ""),
    TableItem("Write log file",                      TableItem::Toggle,    ""),
    TableItem("Record session",                      TableItem::Toggle,    ""),
    TableItem("Crash Log Identifiers",               TableItem::Separator, ""),
    TableItem("Name",                                TableItem::TextInput, "", "", 0, 0, 0),
    TableItem("Email",                               TableItem::TextInput, "", "", 0, 0, 0),
  })) {
  view->show_cb = [=](){
    view->BeginUpdates();
    view->SetSectionValues(0, StringVec{ Application::GetSetting("send_crash_reports") });
    view->SetSectionValues(1, StringVec{ Application::GetSetting("write_log_file"),
                                         Application::GetSetting("record_session") });
    view->SetSectionValues(2, StringVec{ Application::GetSetting("crash_report_name"),
                                         Application::GetSetting("crash_report_email") });
    view->EndUpdates();
    view->changed = false;
  };
  view->hide_cb = [=](){
    if (view->changed) {
      StringPairVec s0 = view->GetSectionText(0), s1 = view->GetSectionText(1), s2 = view->GetSectionText(2);
      CHECK_EQ(1, s0.size());
      CHECK_EQ(2, s1.size());
      CHECK_EQ(2, s2.size());
      Application::SaveSettings(StringPairVec{ 
        StringPair("send_crash_reports", s0[0].second), StringPair("write_log_file", s1[0].second),
        StringPair("record_session", s1[1].second), StringPair("crash_report_name", s2[0].second),
        StringPair("crash_report_email", s2[1].second)
      });
    }
  };
}

MyAppSettingsViewController::MyAppSettingsViewController(MyTerminalMenus *m) :
  view(make_unique<SystemTableView>("Global Settings", "", GetSchema(m))) {
  view->show_cb = [=](){
    view->BeginUpdates();
    view->SetHidden(0, 0, !m->db_opened ||  m->db_protected); 
    view->SetHidden(0, 1, !m->db_opened || !m->db_protected); 
    view->EndUpdates();
  };
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
    TableItem("SQLCipher", TableItem::Command, "", "Disabled >", 0, m->unlocked_icon, 0,
              bind(&SystemAlertView::ShowCB, my_app->passphrase_alert.get(), "Enable encryption", "Passphrase", "", [=](const string &pw){
                   my_app->passphraseconfirm_alert->ShowCB("Confirm enable encryption", "Passphrase", "", StringCB(bind(&MyTerminalMenus::EnableLocalEncryption, m, pw, _1))); })),
    TableItem("SQLCipher", TableItem::Command, "", "Enabled >", 0, m->locked_icon, 0, bind(&MyTerminalMenus::DisableLocalEncryption, m)),
    TableItem("Keep Display On", TableItem::Toggle,  ""),
    TableItem("",                TableItem::Separator, ""),
    TableItem("About",           TableItem::Command, "", ">", 0, 0, 0, bind(&SystemNavigationView::PushTableView, m->hosts_nav.get(), m->about.view.get())),
    TableItem("Support",         TableItem::Command, "", ">", 0, 0, 0, bind(&SystemNavigationView::PushTableView, m->hosts_nav.get(), m->support.view.get())),
    TableItem("Privacy",         TableItem::Command, "", ">", 0, 0, 0, bind(&SystemNavigationView::PushTableView, m->hosts_nav.get(), m->privacy.view.get())) };
}

void MyAppSettingsViewController::UpdateViewFromModel(const MyAppSettingsModel &model) {
  view->BeginUpdates();
  view->SetValue(0, 2, model.keep_display_on ? "1" : "");
  view->EndUpdates();
}

void MyAppSettingsViewController::UpdateModelFromView(MyAppSettingsModel *model) {
  string a="", b="", keepdisplayon="Keep Display On";
  if (!view->GetSectionText(0, {&a, &b, &keepdisplayon})) return ERROR("parse appsettings0");
  model->keep_display_on = keepdisplayon == "1";
}

MyTerminalInterfaceSettingsViewController::MyTerminalInterfaceSettingsViewController(MyTerminalMenus *m) :
  view(make_unique<SystemTableView>("Interface Settings", "", GetSchema(m, m->interfacesettings_nav.get()))) {
  view->AddNavigationButton(HAlign::Left,
                            TableItem("Back", TableItem::Button, "", "", 0, 0, 0,
                                      bind(&MyTerminalMenus::HideInterfaceSettings, m)));
  view->hide_cb = [=](){
    auto t = GetActiveTerminalTab();
    if (view->changed && t && t->connected_host_id) {
      MyHostModel host(&m->host_db, &m->credential_db, &m->settings_db, t->connected_host_id);
      UpdateModelFromView(&host.settings);
      host.Update(host, &m->host_db, &m->credential_db, &m->settings_db);
      m->ApplyTerminalSettings(host.settings);
    }
  };
}

vector<TableItem> MyTerminalInterfaceSettingsViewController::GetBaseSchema(MyTerminalMenus *m, SystemNavigationView *nav) {
  return vector<TableItem>{
    TableItem("Font",     TableItem::Label,      "", "",  0, m->font_icon,     0),
    TableItem("",         TableItem::FontPicker, "", "",  0, 0,                0, Callback(), Callback(), TableItem::Depends(), true),
    TableItem("Colors",   TableItem::Label,      "", "",  0, m->eye_icon,      0, [=](){}),
    TableItem("",         TableItem::Picker,     "", "",  0, 0,                0, Callback(), Callback(), TableItem::Depends(), true, &m->color_picker),
    TableItem("Beep",     TableItem::Label,      "", "",  0, m->audio_icon,    0, [=](){}),
    TableItem("Keyboard", TableItem::Command,    "", ">", 0, m->keyboard_icon, 0, bind(&SystemNavigationView::PushTableView, nav, m->keyboard.view.get())),
    TableItem("Toys",     TableItem::Command,    "", ">", 0, m->toys_icon,     0, bind(&MyTerminalMenus::ShowToysMenu, m))
  };
}

vector<TableItem> MyTerminalInterfaceSettingsViewController::GetSchema(MyTerminalMenus *m, SystemNavigationView *nav) {
  return VectorCat<TableItem>(GetBaseSchema(m, nav), vector<TableItem>{});
}

void MyTerminalInterfaceSettingsViewController::UpdateViewFromModel(const MyHostSettingsModel &host_model) {
  view->BeginUpdates();
  view->SetSectionValues(0, vector<string>{ 
    StrCat(app->focused->default_font.desc.name, " ", app->focused->default_font.desc.size), "",
    host_model.color_scheme, "", "None", "", "" });
  view->EndUpdates();
  view->changed = false;
}

void MyTerminalInterfaceSettingsViewController::UpdateModelFromView(MyHostSettingsModel *host_model) const {
  host_model->color_scheme = "Colors";
  string font="Font", fontchooser, colorchooser, beep="Beep", keyboard="Keyboard", toys;
  if (!view->GetSectionText(0, {&font, &fontchooser, &host_model->color_scheme, &colorchooser,
                            &beep, &keyboard, &toys})) return ERROR("parse runsettings1");
  PickerItem *picker = view->GetPicker(0, 1);
  host_model->font_name = picker->Picked(0);
  host_model->font_size = atoi(picker->Picked(1));
}

MyRFBInterfaceSettingsViewController::MyRFBInterfaceSettingsViewController(MyTerminalMenus *m) :
  view(make_unique<SystemTableView>("Interface Settings", "", GetSchema(m, m->interfacesettings_nav.get()))) {
  view->AddNavigationButton(HAlign::Left,
                            TableItem("Back", TableItem::Button, "", "", 0, 0, 0,
                                      bind(&MyTerminalMenus::HideInterfaceSettings, m)));
}

vector<TableItem> MyRFBInterfaceSettingsViewController::GetSchema(MyTerminalMenus *m, SystemNavigationView *nav) { return GetBaseSchema(m, nav); }
vector<TableItem> MyRFBInterfaceSettingsViewController::GetBaseSchema(MyTerminalMenus *m, SystemNavigationView *nav) {
  return vector<TableItem>{
    TableItem("Toys", TableItem::Command, "", ">", 0, m->toys_icon, 0, bind(&MyTerminalMenus::ShowToysMenu, m))
  };
}

void MyRFBInterfaceSettingsViewController::UpdateViewFromModel(const MyHostSettingsModel &host_model) {
  view->BeginUpdates();
  view->EndUpdates();
}

void MyRFBInterfaceSettingsViewController::UpdateModelFromView(MyHostSettingsModel *host_model) const {
}

MySSHFingerprintViewController::MySSHFingerprintViewController(MyTerminalMenus *m) :
  view(make_unique<SystemTableView>("Fingerprint", "", TableItemVec{
    TableItem("Type", TableItem::Label, ""), TableItem("MD5", TableItem::Label, ""),
    TableItem("SHA256", TableItem::Label, ""), TableItem("", TableItem::Separator, ""),
    TableItem("Clear", TableItem::Command, "", ">", 0, m->none_icon, 0, [=](){
      m->updatehost.prev_model.SetFingerprint(0, "");
      UpdateViewFromModel(m->updatehost.prev_model);
    })
  })) { view->SelectRow(-1, -1); }
  
void MySSHFingerprintViewController::UpdateViewFromModel(const MyHostModel &model) {
  view->BeginUpdates();
  view->SetSectionValues(0, StringVec{ SSH::Key::Name(model.fingerprint_type),
    model.fingerprint.size() ? HexEscape(Crypto::MD5(model.fingerprint), ":").substr(1) : "",
    model.fingerprint.size() ? Singleton<Base64>::Get()->Encode(Crypto::SHA256(model.fingerprint)) : ""});
  view->EndUpdates();
}

MySSHPortForwardViewController::MySSHPortForwardViewController(MyTerminalMenus *m) :
  view(make_unique<SystemTableView>("New Port Forward", "", TableItemVec{
    TableItem("Type", TableItem::Selector, "Local,Remote", "", 0, 0, 0, Callback(), Callback(), {
              {"Local",  {{1,0,"\x01Port",0,0,0,0,"Local Port"},  {1,1,"\x01Hostname",0,0,0,0,"Target Host"}, {1,2,"\x01Port",0,0,0,0,"Target Port"} }},
              {"Remote", {{1,0,"\x01Port",0,0,0,0,"Remote Port"}, {1,1,"\x01Hostname",0,0,0,0,"Target Host"}, {1,2,"\x01Port",0,0,0,0,"Target Port"}  }}, }),
    TableItem("", TableItem::Separator, ""),
    TableItem("Local Port", TableItem::NumberInput, "\x01Port"),
    TableItem("Target Host", TableItem::TextInput, "\x01Hostname"),
    TableItem("Target Port", TableItem::NumberInput, "\x01Port"),
    TableItem("", TableItem::Separator, ""),
    TableItem("Add", TableItem::Command, "", ">", 0, m->plus_green_icon, 0, [=](){
      string type, port_text, target_host, target_port_text;
      view->GetSectionText(0, {&type});
      view->GetSectionText(1, {&port_text, &target_host, &target_port_text});
      int port = atoi(port_text), target_port = atoi(target_port_text);
      if (port && target_port && ContainsChar(target_host.data(), isalnum, target_host.size())) {
        bool local = type == "Local";
        string k = StrCat(type, " ", port), v = StrCat(target_host, ":", target_port);
        m->sshsettings.view->BeginUpdates();
        m->sshsettings.view->AddRow(2, TableItem{ k, TableItem::Label, v, "", 0, local ? m->arrowright_icon : m->arrowleft_icon });
        m->sshsettings.view->EndUpdates();
      }
      m->hosts_nav->PopView();
    })
  })) {
  view->show_cb = [=](){
    view->BeginUpdates();
    view->SetSectionValues(1, vector<string>{"\x01Port", "\x01Hostname", "\x01Port"});
    view->EndUpdates();
  };
}

MySSHSettingsViewController::MySSHSettingsViewController(MyTerminalMenus *m) : menus(m),
  view(make_unique<SystemTableView>("SSH Settings", "", GetSchema(m))) {
  view->SetEditableSection(2, 1, [=](int, int){});
  view->SelectRow(-1, -1);
}

vector<TableItem> MySSHSettingsViewController::GetSchema(MyTerminalMenus *m) {
  return vector<TableItem>{
    TableItem("Folder",               TableItem::TextInput, "", "",  0, m->folder_icon),
    TableItem("Terminal Type",        TableItem::TextInput, "", "",  0, m->terminal_icon),
    TableItem("Text Encoding",        TableItem::Label,     "", "",  0, m->font_icon),
    TableItem("Host Key Fingerprint", TableItem::Command,   "", ">", 0, m->fingerprint_icon, 0,
              bind(&SystemNavigationView::PushTableView, m->hosts_nav.get(), m->sshfingerprint.view.get())),
    TableItem("Advanced",             TableItem::Separator, ""),
    TableItem("Agent Forwarding",     TableItem::Toggle,    ""),
    TableItem("Compression",          TableItem::Toggle,    ""),
    TableItem("Close on Disconnect",  TableItem::Toggle,    ""),
    TableItem("Startup Command",      TableItem::TextInput, "") };
}

void MySSHSettingsViewController::UpdateViewFromModel(const MyHostModel &model) {
  TableItemVec forwards{
    TableItem("Add", TableItem::Command, "", ">", 0, menus->plus_green_icon, 0, bind(&MyTerminalMenus::ShowNewSSHPortForward, menus))
  };
  for (auto &f : model.settings.local_forward)  forwards.emplace_back(StrCat("Local ",  f.port), TableItem::Label, StrCat(f.target_host, ":", f.target_port), "", 0, menus->arrowright_icon);
  for (auto &f : model.settings.remote_forward) forwards.emplace_back(StrCat("Remote ", f.port), TableItem::Label, StrCat(f.target_host, ":", f.target_port), "", 0, menus->arrowleft_icon);
  view->BeginUpdates();
  view->SetSectionValues(0, vector<string>{
    model.folder.size() ? model.folder : "\x01none", model.settings.terminal_type,
    LTerminal::EnumNameTextEncoding(model.settings.text_encoding), "" });
  view->SetSectionValues(1, vector<string>{
    model.settings.agent_forwarding ? "1" : "",
    model.settings.compression ? "1" : "",
    model.settings.close_on_disconnect ? "1" : "",
    model.settings.startup_command.size() ? model.settings.startup_command : "\x01none" });
  view->ReplaceSection(2, "Port Forwarding", 0, SystemTableView::EditButton, forwards);
  view->EndUpdates();
}

bool MySSHSettingsViewController::UpdateModelFromView(MyHostSettingsModel *model, string *folder) const {
  *folder = "Folder";
  model->terminal_type = "Terminal Type";
  model->startup_command = "Startup Command";
  string textencoding="Text Encoding", fingerprint = "Host Key Fingerprint", forwarding = "Agent Forwarding",
         compression = "Compression", disconclose = "Close on Disconnect";
  if (!view->GetSectionText(0, {folder, &model->terminal_type, &textencoding, &fingerprint})) return ERRORv(false, "parse newhostconnect settings0");
  if (!view->GetSectionText(1, {&forwarding, &compression, &disconclose, &model->startup_command})) return ERRORv(false, "parse newhostconnect settings1");
  model->agent_forwarding    = forwarding  == "1";
  model->compression         = compression == "1";
  model->close_on_disconnect = disconclose == "1";
  model->local_forward.clear();
  model->remote_forward.clear();
  StringPairVec forwards = view->GetSectionText(2);
  for (auto i = forwards.begin()+1, e = forwards.end(); i != e; ++i) {
    string target, target_port;
    if (2 != Split(i->second, isint<':'>, &target, &target_port)) continue;
    if      (PrefixMatch(i->first, "Local "))  model->local_forward .push_back({ atoi(i->first.data()+6), target, atoi(target_port) });
    else if (PrefixMatch(i->first, "Remote ")) model->remote_forward.push_back({ atoi(i->first.data()+7), target, atoi(target_port) });
  }
  return true;
}

MyTelnetSettingsViewController::MyTelnetSettingsViewController(MyTerminalMenus *m) : menus(m),
  view(make_unique<SystemTableView>("Telnet Settings", "", GetSchema(m))) {
  view->SelectRow(-1, -1);
}

vector<TableItem> MyTelnetSettingsViewController::GetSchema(MyTerminalMenus *m) {
  return vector<TableItem>{
    TableItem("Folder",               TableItem::TextInput, "", "",  0, m->folder_icon),
    TableItem("Terminal Type",        TableItem::TextInput, "", "",  0, m->terminal_icon),
    TableItem("Text Encoding",        TableItem::Label,     "", "",  0, m->font_icon),
    TableItem("Close on Disconnect",  TableItem::Toggle,    "") };
}

void MyTelnetSettingsViewController::UpdateViewFromModel(const MyHostModel &model) {
  view->BeginUpdates();
  view->SetSectionValues(0, vector<string>{
    model.folder.size() ? model.folder : "\x01none", model.settings.terminal_type,
    LTerminal::EnumNameTextEncoding(model.settings.text_encoding),
    model.settings.close_on_disconnect ? "1" : "" });
  view->EndUpdates();
}

bool MyTelnetSettingsViewController::UpdateModelFromView(MyHostSettingsModel *model, string *folder) const {
  *folder = "Folder";
  model->terminal_type = "Terminal Type";
  model->startup_command = "Startup Command";
  string textencoding="Text Encoding", disconclose = "Close on Disconnect";
  if (!view->GetSectionText(0, {folder, &model->terminal_type, &textencoding, &disconclose})) return ERRORv(false, "parse newhostconnect settings0");
  model->close_on_disconnect = disconclose == "1";
  return true;
}

MyVNCSettingsViewController::MyVNCSettingsViewController(MyTerminalMenus *m) : menus(m),
  view(make_unique<SystemTableView>("VNC Settings", "", GetSchema(m))) {
  view->SelectRow(-1, -1);
}

vector<TableItem> MyVNCSettingsViewController::GetSchema(MyTerminalMenus *m) {
  return vector<TableItem>{
    TableItem("Folder",               TableItem::TextInput, "", "",  0, m->folder_icon),
    TableItem("Close on Disconnect",  TableItem::Toggle,    "") };
}

void MyVNCSettingsViewController::UpdateViewFromModel(const MyHostModel &model) {
  view->BeginUpdates();
  view->SetSectionValues(0, vector<string>{
    model.folder.size() ? model.folder : "\x01none", model.settings.close_on_disconnect ? "1" : "" });
  view->EndUpdates();
}

bool MyVNCSettingsViewController::UpdateModelFromView(MyHostSettingsModel *model, string *folder) const {
  *folder = "Folder";
  string disconclose = "Close on Disconnect";
  if (!view->GetSectionText(0, {folder, &disconclose})) return ERRORv(false, "parse vncsettings");
  model->close_on_disconnect = disconclose == "1";
  return true;
}

MyLocalShellSettingsViewController::MyLocalShellSettingsViewController(MyTerminalMenus *m) : menus(m),
  view(make_unique<SystemTableView>("Local Shell Settings", "", GetSchema(m))) {
  view->SelectRow(-1, -1);
}

vector<TableItem> MyLocalShellSettingsViewController::GetSchema(MyTerminalMenus *m) {
  return vector<TableItem>{
    TableItem("Folder",               TableItem::TextInput, "", "",  0, m->folder_icon) };
}

void MyLocalShellSettingsViewController::UpdateViewFromModel(const MyHostModel &model) {
  view->BeginUpdates();
  view->SetSectionValues(0, vector<string>{ model.folder.size() ? model.folder : "\x01none" });
  view->EndUpdates();
}

bool MyLocalShellSettingsViewController::UpdateModelFromView(MyHostSettingsModel *model, string *folder) const {
  *folder = "Folder";
  if (!view->GetSectionText(0, {folder})) return ERRORv(false, "parse local shell settings");
  return true;
}

MyProtocolViewController::MyProtocolViewController(MyTerminalMenus *m) :
  view(make_unique<SystemTableView>("Protocol", "", TableItemVec{
  TableItem("SSH",         TableItem::Command, "", ">", 0, m->host_locked_icon, 0, bind(&MyTerminalMenus::ChooseProtocol, m, "SSH")),
  TableItem("Telnet",      TableItem::Command, "", ">", 0, m->host_icon,        0, bind(&MyTerminalMenus::ChooseProtocol, m, "Telnet")), 
  TableItem("VNC",         TableItem::Command, "", ">", 0, m->vnc_icon,         0, bind(&MyTerminalMenus::ChooseProtocol, m, "VNC")),
  TableItem("Local Shell", TableItem::Command, "", ">", 0, m->terminal_icon,    0, bind(&MyTerminalMenus::ChooseProtocol, m, "Local Shell")) })) {
}

vector<TableItem> MyQuickConnectViewController::GetSchema(MyTerminalMenus *m) {
  return vector<TableItem>{
    TableItem("SSH", TableItem::TextInput, "\x01Host[:port]", "", 0, m->host_locked_icon, 0, bind(&SystemNavigationView::PushTableView, m->hosts_nav.get(), m->protocol.view.get()), Callback(), {
              {"SSH",         {{0,0,"\x01Host[:port]",false,m->host_locked_icon,0,TableItem::TextInput,"SSH"},         {0,1,"\x01Username"}, {0,2,m->pw_default,false,0,0,0,"",Callback(),0},                            {2,0,"",false,m->settings_gray_icon,0,0,"SSH Settings",        bind(&MyTerminalMenus::ShowProtocolSettings, m, LTerminal::Protocol_SSH) } }},
              {"Telnet",      {{0,0,"\x01Host[:port]",false,m->host_icon,       0,TableItem::TextInput,"Telnet"},      {0,1,"",true},        {0,2,"",true,0,0},                                                          {2,0,"",false,m->settings_gray_icon,0,0,"Telnet Settings",     bind(&MyTerminalMenus::ShowProtocolSettings, m, LTerminal::Protocol_Telnet) } }},
              {"VNC",         {{0,0,"\x01Host[:port]",false,m->vnc_icon,        0,TableItem::TextInput,"VNC"},         {0,1,"",true},        {0,2,m->pw_default,false,0,0,0,"",Callback(),TableItem::Flag::FixDropdown}, {2,0,"",false,m->settings_gray_icon,0,0,"VNC Settings",        bind(&MyTerminalMenus::ShowProtocolSettings, m, LTerminal::Protocol_RFB) } }},
              {"Local Shell", {{0,0,"",               false,m->terminal_icon,   0,TableItem::None,     "Local Shell"}, {0,1,"",true},        {0,2,"",true,0,0},                                                          {2,0,"",false,m->settings_gray_icon,0,0,"Local Shell Settings",bind(&MyTerminalMenus::ShowProtocolSettings, m, LTerminal::Protocol_LocalShell) }}} }, false, nullptr, "Protocol"),
    TableItem("Username", TableItem::TextInput, "\x01Username", "", 0, m->user_icon),
    TableItem("Password", TableItem::PasswordInput, m->pw_default, "", 0, m->locked_icon, 0, bind(&SystemNavigationView::PushTableView, m->hosts_nav.get(), m->keys.view.get()), Callback(), {
              {"Password", {{0,2,m->pw_default,false,m->locked_icon,0,TableItem::PasswordInput,"Password"}}},
              {"Key",      {{0,2,"",           false,m->key_icon   ,0,TableItem::Label,        "Key"     }}} }, false, nullptr, "Credential"),
    TableItem("", TableItem::Separator, ""),
    TableItem("Connect", TableItem::Command, "", ">", 0, m->plus_red_icon, 0, [=](){}),
    TableItem("", TableItem::Separator, ""),
    TableItem("SSH Settings", TableItem::Command, "", ">", 0, m->settings_gray_icon, 0, bind(&MyTerminalMenus::ShowProtocolSettings, m, LTerminal::Protocol_SSH)) };
}

MyNewHostViewController::MyNewHostViewController(MyTerminalMenus *m) : menus(m),
  view(make_unique<SystemTableView>("New Host", "", GetSchema(m))) { view->SelectRow(0, 1); }

vector<TableItem> MyNewHostViewController::GetSchema(MyTerminalMenus *m) {
  vector<TableItem> ret = MyQuickConnectViewController::GetSchema(m);
  for (auto &r : ret) for (auto &dep : r.depends) for (auto &d : dep.second) if (d.section == 0) d.row++;
  ret[4].CheckAssign("Connect", bind(&MyTerminalMenus::NewHostConnect, m));
  ret.insert(ret.begin(), TableItem{"Nickname", TableItem::TextInput, "\x01Nickname", "", 0, m->logo_icon});
  return ret;
}

void MyNewHostViewController::UpdateViewFromModel() {
  view->BeginUpdates();
  view->SetKey(0, 1, "SSH");
  view->SetKey(0, 3, "Password");
  view->SetSectionValues(0, vector<string>{ "\x01Nickname", "\x01Host[:port]",
                         "\x01Username", menus->pw_default });
  view->EndUpdates();
}

bool MyNewHostViewController::UpdateModelFromView(MyHostModel *model, MyCredentialDB *cred_db) const {
  model->displayname = "";
  model->hostname    = "";
  model->username    = "Username";
  string prot = "Protocol", credtype = "Credential", cred = "";
  if (!view->GetSectionText(0, {&model->displayname, &prot, &model->hostname, &model->username,
                            &credtype, &cred})) return ERRORv(false, "parse newhostconnect");

  model->SetProtocol(prot);
  model->SetPort(0);
  auto ct = MyCredentialModel::GetCredentialType(credtype);
  if      (ct == CredentialType_Password) model->cred.Load(CredentialType_Password, cred);
  else if (ct == CredentialType_PEM)      model->cred.Load(cred_db, view->GetTag(0, 3));
  else                                    model->cred.Load();
  return true;
}

MyUpdateHostViewController::MyUpdateHostViewController(MyTerminalMenus *m) : menus(m),
  view(make_unique<SystemTableView>("Update Host", "", GetSchema(m))) {}

vector<TableItem> MyUpdateHostViewController::GetSchema(MyTerminalMenus *m) {
  vector<TableItem> ret = MyNewHostViewController::GetSchema(m);
  ret[5].CheckAssign("Connect", bind(&MyTerminalMenus::UpdateHostConnect, m));
  ret[5].left_icon = m->bolt_icon;
  return ret;
}

void MyUpdateHostViewController::UpdateViewFromModel(const MyHostModel &host) {
  prev_model = host;
  bool pw = host.cred.credtype == CredentialType_Password, pem = host.cred.credtype == CredentialType_PEM;
  string hostv, proto_name;
  if      (host.protocol == LTerminal::Protocol_Telnet)     { hostv = host.hostname; proto_name = "Telnet"; }
  else if (host.protocol == LTerminal::Protocol_RFB)        { hostv = host.hostname; proto_name = "RFB"; }
  else if (host.protocol == LTerminal::Protocol_LocalShell) { hostv = "";            proto_name = "Local Shell"; }
  else                                                      { hostv = host.hostname; proto_name = "SSH"; }
  view->BeginUpdates();
  view->SetKey(0, 1, proto_name);
  view->SetKey(0, 3, pem ? "Key" : "Password");
  view->SetSectionValues(0, vector<string>{
    host.displayname, host.port != host.DefaultPort() ? StrCat(hostv, ":", host.port) : hostv, host.username,
    pem ? host.cred.name : (pw ? host.cred.creddata : menus->pw_default)});
  view->SetTag(0, 3, pem ? host.cred.cred_id : 0);
  view->SelectRow(-1, -1);
  view->EndUpdates();
}

bool MyUpdateHostViewController::UpdateModelFromView(MyHostModel *model, MyCredentialDB *cred_db) const {
  model->displayname = "";
  model->hostname    = "";
  model->username    = "Username";
  string prot = "Protocol", credtype = "Credential", cred = "";
  if (!view->GetSectionText(0, {&model->displayname, &prot, &model->hostname, &model->username,
                            &credtype, &cred})) return ERRORv(false, "parse updatehostconnect");

  model->SetProtocol(prot);
  model->SetPort(0);
  auto ct = MyCredentialModel::GetCredentialType(credtype);
  if      (ct == CredentialType_Password) model->cred.Load(CredentialType_Password, cred);
  else if (ct == CredentialType_PEM)      model->cred.Load(cred_db, view->GetTag(0, 3));
  else                                    model->cred.Load();
  return true;
}

MyHostsViewController::MyHostsViewController(MyTerminalMenus *m, bool me) :
  menus(m), menu(me), view(make_unique<SystemTableView>("LTerminal", "indent", TableItemVec())) {}

vector<TableItem> MyHostsViewController::GetBaseSchema(MyTerminalMenus *m) { return TableItemVec{}; };
void MyHostsViewController::LoadFolderUI(MyHostDB *model) {
  CHECK(!menu);
  view->AddNavigationButton(HAlign::Right, TableItem("Edit", TableItem::Button, ""));
  view->SetEditableSection(0, 0, bind(&MyTerminalMenus::DeleteHost, menus, _1, _2));
  view->show_cb = bind(&MyHostsViewController::UpdateViewFromModel, this, model);
}

void MyHostsViewController::LoadLockedUI(MyHostDB *model) {
  CHECK(menu);
  view->BeginUpdates();
  view->ReplaceSection(1, "", 0, 0, TableItemVec{});
  view->ReplaceSection(0, "", 0, 0, TableItemVec{
    TableItem("Unlock", TableItem::Command, "", ">", 0, 0, 0, [=](){
      my_app->passphrase_alert->ShowCB("Unlock", "Passphrase", "", [=](const string &pw){
        if (menus->UnlockEncryptedDatabase(pw)) { LoadUnlockedUI(model); view->show_cb(); }
      }); })
  });
  view->EndUpdates();
  view->changed = false;
}

void MyHostsViewController::LoadUnlockedUI(MyHostDB *model) {
  CHECK(menu);
  view->BeginUpdates();
  view->ReplaceSection(0, "", 0, 0, VectorCat<TableItem>(GetBaseSchema(menus), TableItemVec{
    TableItem("New",      TableItem::Command, "", ">", 0, menus->plus_red_icon,      0, bind(&MyTerminalMenus::ShowNewHost,     menus)),
    TableItem("Settings", TableItem::Command, "", ">", 0, menus->settings_gray_icon, 0, bind(&MyTerminalMenus::ShowAppSettings, menus))
  }));
  view->EndUpdates();
  view->SetEditableSection(menu, 0, bind(&MyTerminalMenus::DeleteHost, menus, _1, _2));
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
        section.emplace_back(host_folder, TableItem::Command, "", ">", 0, menus->folder_icon, 0, [=](){
          menus->hostsfolder.view->SetTitle(StrCat((menus->hostsfolder.folder = move(host_folder)), " Folder"));
          menus->hosts_nav->PushTableView(menus->hostsfolder.view.get()); });
      continue;
    }
    int host_icon = menus->host_icon;
    if      (h->protocol() == LTerminal::Protocol_SSH)        host_icon = menus->host_locked_icon;
    else if (h->protocol() == LTerminal::Protocol_RFB)        host_icon = menus->vnc_icon;
    else if (h->protocol() == LTerminal::Protocol_LocalShell) host_icon = menus->terminal_icon;
    section.emplace_back(displayname, TableItem::Command, "", "", host.first, host_icon, 
                         menus->settings_blue_icon, bind(&MyTerminalMenus::ConnectHost, menus, host.first),
                         bind(&MyTerminalMenus::HostInfo, menus, host.first));
  }
  view->BeginUpdates();
  if (!menu) view->ReplaceSection(0, "", 0, SystemTableView::EditableIfHasTag, section);
  else view->ReplaceSection
    (1, section.size() ? "Hosts" : "", 0,
     (section.size() ? SystemTableView::EditButton : 0) | SystemTableView::EditableIfHasTag, section);
  view->EndUpdates();
  view->changed = false;
}

MySessionsViewController::MySessionsViewController(MyTerminalMenus *m) :
  menus(m), view(make_unique<SystemTableView>("Sessions", "big", TableItemVec{
    TableItem("",              TableItem::Separator),
    TableItem{"Close Session", TableItem::Command, "", ">", 0, m->none_icon,       0, bind(&MyTerminalMenus::CloseActiveSession, m)},
    TableItem{"New",           TableItem::Command, "", ">", 0, m->plus_green_icon, 0, bind(&MyTerminalMenus::ShowNewSessionMenu, m, "New Session", true)},
  })) {
  view->AddNavigationButton(HAlign::Left,
                            TableItem("Back", TableItem::Button, "", "", 0, 0, 0,
                                      bind(&MyTerminalMenus::HideNewSessionMenu, m)));
}

}; // namespace LFL
