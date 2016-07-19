/*
 * $Id: term.cpp 1336 2014-12-08 09:29:59Z justin $
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

#include "core/app/app.h"
#include "core/app/gui.h"
#include "core/app/ipc.h"
#include "core/app/net/resolver.h"
#include "core/web/browser.h"
#include "core/web/document.h"
#ifdef LFL_CRYPTO
#include "core/app/crypto.h"
#include "core/app/net/ssh.h"
#endif
#include "core/app/db/sqlite.h"
#ifdef LFL_FLATBUFFERS
#include "term/term_generated.h"
#endif
#include "term.h"

#ifndef WIN32
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <termios.h>
#endif

namespace LFL {
#ifdef LFL_CRYPTO
DEFINE_string(ssh,         "",     "SSH to host");
DEFINE_string(login,       "",     "SSH user");
#endif
DEFINE_bool  (interpreter, false,  "Launch interpreter instead of shell");
DEFINE_string(telnet,      "",     "Telnet to host");
DEFINE_string(command,     "",     "Execute initial command");
DEFINE_string(screenshot,  "",     "Screenshot and exit");
DEFINE_string(record,      "",     "Record session to file");
DEFINE_string(playback,    "",     "Playback recorded session file");
DEFINE_bool  (draw_fps,    false,  "Draw FPS");
DEFINE_bool  (resize_grid, true,   "Resize window in glyph bound increments");
DEFINE_FLAG(dim, point, point(80,25), "Initial terminal dimensions");
extern FlagOfType<bool> FLAGS_enable_network_;

struct MyAppState {
  unordered_map<string, Shader> shader_map;
  unique_ptr<Browser> image_browser;
  unique_ptr<SystemMenuWidget> edit_menu, view_menu, toys_menu;
  unique_ptr<SystemToolbarWidget> hosts_toolbar, keyboard_toolbar;
  unique_ptr<SystemTableWidget> hosts_table, newhost_table;
  unique_ptr<SystemNavigationWidget> hosts_nav;
  int new_win_width = FLAGS_dim.x*Fonts::InitFontWidth(), new_win_height = FLAGS_dim.y*Fonts::InitFontHeight();
  int downscale_effects = 1;
  SQLite::Database db;
  SQLiteIdValueStore hosts_db;
  ~MyAppState() { SQLite::Close(db); }
} *my_app = nullptr;

struct PlaybackTerminalController : public Terminal::Controller {
  unique_ptr<FlatFile> playback;
  PlaybackTerminalController(unique_ptr<FlatFile> f) : playback(move(f)) {}
  int Open(TextArea*) { return -1; }
  int Write(const StringPiece &b) { return b.size(); }
  StringPiece Read() {
#ifdef LFL_FLATBUFFERS
    auto r = playback ? playback->Next<LTerminal::RecordLog>() : nullptr;
    auto ret = (r && r->data()) ? StringPiece(MakeSigned(r->data()->data()), r->data()->size()) : StringPiece();
    unsigned long long stamp = r ? r->stamp() : 0;
    fprintf(stderr, "Playback %llu \"%s\"\n", stamp, CHexEscapeNonAscii(ret).c_str());
    return ret;
#else
    ERROR("Playback not supported");
    return StringPiece();
#endif
  }
};

#ifndef WIN32
struct ReadBuffer {
  int size;
  Time stamp;
  string data;
  ReadBuffer(int S=0) : size(S), stamp(Now()), data(S, 0) {}
  void Reset() { stamp=Now(); data.resize(size); }
};

struct PTYTerminalController : public Terminal::Controller {
  int fd = -1;
  ProcessPipe process;
  ReadBuffer read_buf;
  PTYTerminalController() : read_buf(65536) {}
  virtual ~PTYTerminalController() {
    if (process.in) app->scheduler.DelFrameWaitSocket(screen, fileno(process.in));
  }

  int Open(TextArea*) {
    setenv("TERM", "xterm", 1);
    string shell = BlankNull(getenv("SHELL")), lang = BlankNull(getenv("LANG"));
    if (shell.empty()) setenv("SHELL", (shell = "/bin/bash").c_str(), 1);
    if (lang .empty()) setenv("LANG", "en_US.UTF-8", 1);
    const char *av[] = { shell.c_str(), 0 };
    CHECK_EQ(process.OpenPTY(av, app->startdir.c_str()), 0);
    return (fd = fileno(process.out));
  }

  int Write(const StringPiece &b) { return write(fd, b.data(), b.size()); }
  void IOCtlWindowSize(int w, int h) {
    struct winsize ws;
    memzero(ws);
    ws.ws_col = w;
    ws.ws_row = h;
    ioctl(fd, TIOCSWINSZ, &ws);
  }

  StringPiece Read() {
    if (!process.in) return StringPiece();
    read_buf.Reset();
    NBRead(fd, &read_buf.data);
    return read_buf.data;
  }
};
#endif

#ifdef LFL_CRYPTO
struct SSHTerminalController : public NetworkTerminalController {
  using NetworkTerminalController::NetworkTerminalController;
  Callback success_cb;
  bool save_host=0;

  int Open(TextArea *t) {
    Terminal *term = dynamic_cast<Terminal*>(t);
    term->Write(StrCat("Connecting to ", FLAGS_login, "@", FLAGS_ssh, "\r\n"));
    app->RunInNetworkThread([=](){
      success_cb = bind(&SSHTerminalController::SSHLoginCB, this, term);
      conn = SSHClient::Open(FLAGS_ssh, SSHClient::ResponseCB
                             (bind(&SSHTerminalController::SSHReadCB, this, _1, _2)), &detach_cb, &success_cb);
      if (!conn) { app->RunInMainThread(bind(&NetworkTerminalController::Dispose, this)); return; }
      SSHClient::SetTerminalWindowSize(conn, term->term_width, term->term_height);
      SSHClient::SetUser(conn, FLAGS_login);
#ifdef LFL_MOBILE
      SSHClient::SetPasswordCB(conn, bind(&Application::LoadPassword, app, _1, _2, _3),
                                     bind(&Application::SavePassword, app, _1, _2, _3));
#endif
    });
    return -1;
  }

  void SSHLoginCB(Terminal *term) {
    term->Write("Connected.\r\n");
    if (!save_host) return;
    save_host = false;
#ifdef LFL_MOBILE
    int row_id = my_app->hosts_db.Insert
      (MakeBlobPiece(MakeFlatBufferOfType(LTerminal::Host,
                                          LTerminal::CreateHost(fb, fb.CreateString(FLAGS_ssh),
                                                                fb.CreateString(FLAGS_login)))));
#endif
  }

  void SSHReadCB(Connection *c, const StringPiece &b) { 
    if (b.empty()) Close();
    else read_buf.append(b.data(), b.size());
  }

  void IOCtlWindowSize(int w, int h) { if (conn) SSHClient::SetTerminalWindowSize(conn, w, h); }
  int Write(const StringPiece &in) {
    StringPiece b = in;
#ifdef LFL_MOBILE
    char buf[1];
    if (b.size() == 1 && ctrl_down && !(ctrl_down = false)) {
      my_app->keyboard_toolbar->ToggleButton("ctrl");
      b = StringPiece(&(buf[0] = Key::CtrlModified(*MakeUnsigned(b.data()))), 1);
    }
#endif
    if (!conn || conn->state != Connection::Connected) return -1;
    return SSHClient::WriteChannelData(conn, b);
  }

  StringPiece Read() {
    if (conn && conn->state == Connection::Connected && NBReadable(conn->socket)) {
      if (conn->Read() < 0)                                 { ERROR(conn->Name(), ": Read");       Close(); return ""; }
      if (conn->rb.size() && conn->handler->Read(conn) < 0) { ERROR(conn->Name(), ": query read"); Close(); return ""; }
    }
    swap(read_buf, ret_buf);
    read_buf.clear();
    return ret_buf;
  }
};
#endif

struct ShellTerminalController : public InteractiveTerminalController {
  string ssh_usage="\r\nusage: ssh -l user host[:port]";
  Callback telnet_cb, ssh_cb;
  ShellTerminalController(const string &hdr, const Callback &tcb, const Callback &scb) :
    telnet_cb(tcb), ssh_cb(scb) {
    header = StrCat(hdr, "LTerminal 1.0", ssh_usage, "\r\n\r\n");
#ifdef LFL_CRYPTO
    shell.Add("ssh",      bind(&ShellTerminalController::MySSHCmd,      this, _1));
#endif
    shell.Add("telnet",   bind(&ShellTerminalController::MyTelnetCmd,   this, _1));
    shell.Add("nslookup", bind(&ShellTerminalController::MyNSLookupCmd, this, _1));
    shell.Add("help",     bind(&ShellTerminalController::MyHelpCmd,     this, _1));
  }

#ifdef LFL_CRYPTO
  void MySSHCmd(const vector<string> &arg) {
    int ind = 0;
    for (; ind < arg.size() && arg[ind][0] == '-'; ind += 2) if (arg[ind] == "-l") FLAGS_login = arg[ind+1];
    if (ind < arg.size()) FLAGS_ssh = arg[ind];
    if (FLAGS_login.empty() || FLAGS_ssh.empty()) { if (term) term->Write(ssh_usage); }
    else ssh_cb();
  }
#endif

  void MyTelnetCmd(const vector<string> &arg) {
    if (arg.empty()) { if (term) term->Write("\r\nusage: telnet host"); }
    else { FLAGS_telnet=arg[0]; telnet_cb(); }
  }

  void MyNSLookupCmd(const vector<string> &arg) {
    if (arg.empty() || !app->network_thread) return;
    if ((blocking = 1)) app->RunInNetworkThread(bind(&ShellTerminalController::MyNetworkThreadNSLookup, this, arg[0]));
  }

  void MyNetworkThreadNSLookup(const string &host) {
    app->net->system_resolver->NSLookup(host, bind(&ShellTerminalController::MyNetworkThreadNSLookupResponse, this, host, _1, _2));
  }

  void MyNetworkThreadNSLookupResponse(const string &host, IPV4::Addr ipv4_addr, DNS::Response*) {
    app->RunInMainThread(bind(&ShellTerminalController::UnBlockWithResponse, this, StrCat("host = ", IPV4::Text(ipv4_addr))));
  }

  void MyHelpCmd(const vector<string> &arg) {
    WriteText("\r\n\r\nLTerminal interpreter commands:\r\n\r\n"
              "* ssh -l user host[:port]\r\n"
              "* telnet host[:port]\r\n"
              "* nslookup host\r\n");
  }
};

struct MyTerminalWindow : public TerminalWindow {
  Shader *activeshader = &app->shaders->shader_default;
  Time join_read_interval = Time(100), refresh_interval = Time(33);
  int join_read_pending = 0;
  bool save_host = false;

  MyTerminalWindow(Window *W) :
    TerminalWindow(W->AddGUI(make_unique<Terminal>(nullptr, W, W->default_font, FLAGS_dim))) {
    terminal->new_link_cb      = bind(&MyTerminalWindow::NewLinkCB,   this, _1);
    terminal->hover_control_cb = bind(&MyTerminalWindow::HoverLinkCB, this, _1);
    if (terminal->bg_color) W->gd->clear_color = *terminal->bg_color;
  }

  void SetFontSize(int n) {
    bool drew = false;
    screen->default_font.desc.size = n;
    CHECK((terminal->style.font = screen->default_font.Load()));
    int font_width  = terminal->style.font->FixedWidth(), new_width  = font_width  * terminal->term_width;
    int font_height = terminal->style.font->Height(),     new_height = font_height * terminal->term_height;
    if (FLAGS_resize_grid) screen->SetResizeIncrements(font_width, font_height);
    if (new_width != screen->width || new_height != screen->height) drew = screen->Reshape(new_width, new_height);
    if (!drew) terminal->Redraw(true, true);
    INFO("Font: ", app->fonts->DefaultFontEngine()->DebugString(terminal->style.font));
  }

  void UsePlaybackTerminalController(unique_ptr<FlatFile> f) {
    ChangeController(make_unique<PlaybackTerminalController>(move(f)));
  }

  void UseShellTerminalController(const string &m) {
    ChangeController(make_unique<ShellTerminalController>(m, [=](){ UseTelnetTerminalController(); },
                                                          [=](){ UseSSHTerminalController(); }));
  }

  void UseSSHTerminalController() {
#ifdef LFL_CRYPTO
    auto ssh_controller =
      make_unique<SSHTerminalController>(app->net->tcp_client.get(), "",
                                         [=](){ UseShellTerminalController("\r\nsession ended.\r\n\r\n\r\n"); });
    ssh_controller->save_host = save_host;
    ChangeController(move(ssh_controller));
#endif
  }

  void UseTelnetTerminalController() {
    ChangeController(make_unique<NetworkTerminalController>(app->net->tcp_client.get(), FLAGS_telnet,
                                                            [=](){ UseShellTerminalController("\r\nsession ended.\r\n\r\n\r\n"); }));
  }

  void UseDefaultTerminalController() {
#if defined(WIN32) || defined(LFL_MOBILE)
    UseShellTerminalController("");
#else
    ChangeController(make_unique<PTYTerminalController>());
#endif
  }

  void UseInitialTerminalController() {
    if      (FLAGS_playback.size()) return UsePlaybackTerminalController(make_unique<FlatFile>(FLAGS_playback));
    else if (FLAGS_interpreter)     return UseShellTerminalController("");
#ifdef LFL_CRYPTO
    else if (FLAGS_ssh.size())      return UseSSHTerminalController();
#endif
    else if (FLAGS_telnet.size())   return UseTelnetTerminalController();
    else                            return UseDefaultTerminalController();
  }

  void OpenedController() {
    if (FLAGS_command.size()) CHECK_EQ(FLAGS_command.size()+1, controller->Write(StrCat(FLAGS_command, "\n")));
  }

  bool CustomShader() const { return activeshader != &app->shaders->shader_default; }
  void UpdateTargetFPS(Window *w) {
    bool animating = CustomShader() || (w->console && w->console->animating);
    app->scheduler.SetAnimating(w, animating);
    if (my_app->downscale_effects) app->SetDownScale(animating);
  }

  void ConsoleAnimatingCB(Window *w) { 
    UpdateTargetFPS(w);
    if (!w->console || !w->console->animating) {
      if ((w->console && w->console->Active()) || controller->frame_on_keyboard_input) app->scheduler.AddFrameWaitKeyboard(w);
      else                                                                             app->scheduler.DelFrameWaitKeyboard(w);
    }
  }

  void NewLinkCB(const shared_ptr<TextBox::Control> &link) {
    const char *args = FindChar(link->val.c_str() + 6, isint2<'?', ':'>);
    string image_url(link->val, 0, args ? args - link->val.c_str() : string::npos);
    // if (SuffixMatch(image_url, ".gifv")) return;
    if (!FileSuffix::Image(image_url)) {
      return;
      string prot, host, port, path;
      if (HTTP::ParseURL(image_url.c_str(), &prot, &host, &port, &path) &&
          SuffixMatch(host, "imgur.com") && !FileSuffix::Image(path)) {
        image_url += ".jpg";
      } else return;
    }
    image_url += BlankNull(args);
    if (app->render_process && app->render_process->conn)
      app->RunInNetworkThread([=](){ link->image = my_app->image_browser->doc.parser->OpenImage(image_url); });
  }

  void HoverLinkCB(TextBox::Control *link) {
    Texture *tex = link ? link->image.get() : 0;
    if (!tex) return;
    tex->Bind();
    screen->gd->EnableBlend();
    screen->gd->SetColor(Color::white - Color::Alpha(0.25));
    GraphicsContext::DrawTexturedBox1
      (screen->gd, Box::DelBorder(screen->Box(), screen->width*.2, screen->height*.2), tex->coord);
    screen->gd->ClearDeferred();
  }

  int Frame(Window *W, unsigned clicks, int flag) {
    bool effects = W->animating, downscale = effects && my_app->downscale_effects > 1;
    Box draw_box = W->Box();

    if (downscale) W->gd->RestoreViewport(DrawMode::_2D);
    terminal->CheckResized(draw_box);
    int read_size = ReadAndUpdateTerminalFramebuffer();
    if (downscale) {
      float scale = activeshader->scale = 1.0 / my_app->downscale_effects;
      draw_box.y *= scale;
      draw_box.h -= terminal->extra_height * scale;
    } else W->gd->DrawMode(DrawMode::_2D);

    if (!effects) {
#if defined(__APPLE__) && !defined(LFL_MOBILE) && !defined(LFL_QT)
      if (read_size && !(flag & LFApp::Frame::DontSkip)) {
        int *pending = &join_read_pending;
        bool join_read = read_size > 255;
        if (join_read) { if (1            && ++(*pending)) { if (app->scheduler.WakeupIn(W, join_read_interval)) return -1; } }
        else           { if ((*pending)<1 && ++(*pending)) { if (app->scheduler.WakeupIn(W,   refresh_interval)) return -1; } }
        *pending = 0;
      }
      app->scheduler.ClearWakeupIn(W);
#endif
    }

    W->gd->DisableBlend();
    terminal->Draw(draw_box, downscale ? Terminal::DrawFlag::DrawCursor : Terminal::DrawFlag::Default,
                   effects ? activeshader : NULL);
    if (effects) W->gd->UseShader(0);

    W->DrawDialogs();
    if (FLAGS_draw_fps) W->default_font->Draw(StringPrintf("FPS = %.2f", app->FPS()), point(W->width*.85, 0));
    if (FLAGS_screenshot.size()) ONCE(W->shell->screenshot(vector<string>(1, FLAGS_screenshot)); app->run=0;);
    return 0;
  }

  void FontCmd(const vector<string> &arg) {
    if (arg.size() < 2) return app->ShowSystemFontChooser(screen->default_font.desc, "choosefont");
    if (arg.size() > 2) FLAGS_font_flag = atoi(arg[2]);
    screen->default_font.desc.name = arg[0];
    SetFontSize(atof(arg[1]));
    app->scheduler.Wakeup(screen);
  }

  void ColorsCmd(const vector<string> &arg) {
    string colors_name = arg.size() ? arg[0] : "";
    if      (colors_name == "vga")             terminal->ChangeColors(Singleton<Terminal::StandardVGAColors>   ::Get());
    else if (colors_name == "solarized_dark")  terminal->ChangeColors(Singleton<Terminal::SolarizedDarkColors> ::Get());
    else if (colors_name == "solarized_light") terminal->ChangeColors(Singleton<Terminal::SolarizedLightColors>::Get());
    if (terminal->bg_color) screen->gd->clear_color = *terminal->bg_color;
    app->scheduler.Wakeup(screen);
  }

  void ShaderCmd(const vector<string> &arg) {
    string shader_name = arg.size() ? arg[0] : "";
    auto shader = my_app->shader_map.find(shader_name);
    bool found = shader != my_app->shader_map.end();
    if (found && !shader->second.ID) Shader::CreateShaderToy(shader_name, Asset::FileContents(StrCat(shader_name, ".frag")), &shader->second);
    activeshader = found ? &shader->second : &app->shaders->shader_default;
    UpdateTargetFPS(screen);
  }

  void EffectsControlsCmd(const vector<string>&) { 
    screen->shell->Run("slider shadertoy_blend 1.0 0.01");
    app->scheduler.Wakeup(screen);
  }

  void TransparencyControlsCmd(const vector<string>&) {
    SliderDialog::UpdatedCB cb(bind([=](Widget::Slider *s){ screen->SetTransparency(s->Percent()); }, _1));
    screen->AddDialog(make_unique<SliderDialog>(screen, "window transparency", cb, 0, 1.0, .025));
    app->scheduler.Wakeup(screen);
  }

#ifdef LFL_MOBILE
  unordered_map<string, Callback> mobile_key_cmd = {
    { "left",   bind([=]{ terminal->CursorLeft();  if (controller->frame_on_keyboard_input) app->scheduler.Wakeup(screen); }) },
    { "right",  bind([=]{ terminal->CursorRight(); if (controller->frame_on_keyboard_input) app->scheduler.Wakeup(screen); }) },
    { "up",     bind([=]{ terminal->HistUp();      if (controller->frame_on_keyboard_input) app->scheduler.Wakeup(screen); }) },
    { "down",   bind([=]{ terminal->HistDown();    if (controller->frame_on_keyboard_input) app->scheduler.Wakeup(screen); }) },
    { "pgup",   bind([=]{ terminal->PageUp();      if (controller->frame_on_keyboard_input) app->scheduler.Wakeup(screen); }) },
    { "pgdown", bind([=]{ terminal->PageDown();    if (controller->frame_on_keyboard_input) app->scheduler.Wakeup(screen); }) } };

  unordered_map<string, Callback> mobile_togglekey_cmd = {
    { "ctrl", bind([&]{ controller->ctrl_down = !controller->ctrl_down; }) } };

  void MobileKeyPressCmd (const vector<string> &arg) { if (arg.size()) FindAndDispatch(mobile_key_cmd, arg[0]); }
  void MobileKeyToggleCmd(const vector<string> &arg) { if (arg.size()) FindAndDispatch(mobile_togglekey_cmd, arg[0]); }
  void MobileCloseCmd(const vector<string> &arg) { controller->Close(); }
  void MobileToysMenuCmd(const vector<string> &arg) {
    ShaderCmd(vector<string>{"none"});
    my_app->toys_menu->Show();
  }

  void MobileNewHostCmd(const vector<string> &arg) {
    save_host = true;
    my_app->hosts_nav->PushTable(my_app->newhost_table.get());
  }

  void MobileStartShellCmd(const vector<string> &arg) {
    save_host = false;
    UseShellTerminalController("");
    MobileStartSession();
  }

  void MobileQuickConnectCmd(const vector<string> &arg) {
    save_host = false;
    my_app->hosts_nav->PushTable(my_app->newhost_table.get());
  }

  void MobileHostConnectCmd(const vector<string> &arg) {
    CHECK_EQ(1, arg.size());
    const string &val = FindOrDie(my_app->hosts_db.data, atoi(arg[0]));
    const LTerminal::Host *h = flatbuffers::GetRoot<LTerminal::Host>(val.data());
    FLAGS_ssh   = h->hostport() ? h->hostport()->data() : "";
    FLAGS_login = h->username() ? h->username()->data() : "";
    UseSSHTerminalController();
    MobileStartSession();
  }

  void MobileStartSession() {
    my_app->hosts_nav->Show(false);
    my_app->keyboard_toolbar->Show(true);
    app->CloseTouchKeyboardAfterReturn(false);
    app->OpenTouchKeyboard();
  }

  void MobileConnectCmd(const vector<string>&) {
    StringPairVec v = my_app->newhost_table->GetSectionText(0);
    CHECK_EQ(4, v.size());
    FLAGS_ssh = v[0].second;
    if (v[1].second != "22") StrAppend(&FLAGS_ssh, ":", v[1].second);
    FLAGS_login = v[2].second;
    UseSSHTerminalController();
    MobileStartSession();
  }
#endif
};

void MyWindowInit(Window *W) {
  W->width = my_app->new_win_width;
  W->height = my_app->new_win_height;
  W->caption = app->name;
}

void MyWindowStart(Window *W) {
  if (!W->user1.v) {
    auto tw = new MyTerminalWindow(W);
    tw->UseDefaultTerminalController();
    W->user1 = MakeTyped(tw);
  }

  auto tw = GetTyped<MyTerminalWindow*>(W->user1);
  if (FLAGS_console) W->InitConsole(bind(&MyTerminalWindow::ConsoleAnimatingCB, tw, W));
  W->frame_cb = bind(&MyTerminalWindow::Frame, tw, _1, _2, _3);
  W->default_textbox = [=]{ return tw->terminal; };
  if (FLAGS_resize_grid) W->SetResizeIncrements(tw->terminal->style.font->FixedWidth(), tw->terminal->style.font->Height());
  app->scheduler.AddFrameWaitMouse(W);

  W->shell = make_unique<Shell>();
  W->shell->Add("choosefont",   bind(&MyTerminalWindow::FontCmd,                 tw, _1));
  W->shell->Add("colors",       bind(&MyTerminalWindow::ColorsCmd,               tw, _1));
  W->shell->Add("shader",       bind(&MyTerminalWindow::ShaderCmd,               tw, _1));
  W->shell->Add("fxctl",        bind(&MyTerminalWindow::EffectsControlsCmd,      tw, _1));
  W->shell->Add("transparency", bind(&MyTerminalWindow::TransparencyControlsCmd, tw, _1));
#ifdef LFL_MOBILE                                                                           
  W->shell->Add("close",        bind(&MyTerminalWindow::MobileCloseCmd,          tw, _1));
  W->shell->Add("toysmenu",     bind(&MyTerminalWindow::MobileToysMenuCmd,       tw, _1));
  W->shell->Add("keypress",     bind(&MyTerminalWindow::MobileKeyPressCmd,       tw, _1));
  W->shell->Add("togglekey",    bind(&MyTerminalWindow::MobileKeyToggleCmd,      tw, _1));
  W->shell->Add("newhost",      bind(&MyTerminalWindow::MobileNewHostCmd   ,     tw, _1));
  W->shell->Add("startshell",   bind(&MyTerminalWindow::MobileStartShellCmd,     tw, _1));
  W->shell->Add("quickconnect", bind(&MyTerminalWindow::MobileQuickConnectCmd,   tw, _1));
  W->shell->Add("hostconnect",  bind(&MyTerminalWindow::MobileHostConnectCmd,    tw, _1));
  W->shell->Add("connect",      bind(&MyTerminalWindow::MobileConnectCmd,        tw, _1));
  tw->terminal->line_fb.align_top_or_bot = tw->terminal->cmd_fb.align_top_or_bot = true;
#endif
  if (my_app->image_browser) W->shell->AddBrowserCommands(my_app->image_browser.get());

  BindMap *binds = W->AddInputController(make_unique<BindMap>());
#ifndef WIN32
  binds->Add('n',       Key::Modifier::Cmd, Bind::CB(bind(&Application::CreateNewWindow, app)));
#endif                  
  binds->Add('6',       Key::Modifier::Cmd, Bind::CB(bind([=](){ W->shell->console(vector<string>()); })));
  binds->Add('=',       Key::Modifier::Cmd, Bind::CB(bind([=](){ tw->SetFontSize(W->default_font.desc.size + 1); })));
  binds->Add('-',       Key::Modifier::Cmd, Bind::CB(bind([=](){ tw->SetFontSize(W->default_font.desc.size - 1); })));
  binds->Add(Key::Up,   Key::Modifier::Cmd, Bind::CB(bind([=](){ tw->terminal->ScrollUp();   app->scheduler.Wakeup(screen); })));
  binds->Add(Key::Down, Key::Modifier::Cmd, Bind::CB(bind([=](){ tw->terminal->ScrollDown(); app->scheduler.Wakeup(screen); })));
}

void MyWindowClosed(Window *W) {
  delete GetTyped<MyTerminalWindow*>(W->user1);
  delete W;
}

}; // naemspace LFL
using namespace LFL;

extern "C" void MyAppCreate(int argc, const char* const* argv) {
  FLAGS_enable_video = FLAGS_enable_input = 1;
  app = new Application(argc, argv);
  screen = new Window();
  my_app = new MyAppState();
  app->name = "LTerminal";
  app->exit_cb = []() { delete my_app; };
  app->window_closed_cb = MyWindowClosed;
  app->window_start_cb = MyWindowStart;
  app->window_init_cb = MyWindowInit;
  app->window_init_cb(screen);
#ifdef LFL_MOBILE
  my_app->downscale_effects = app->SetExtraScale(true);
  app->SetTitleBar(false);
  app->SetKeepScreenOn(false);
#endif
}

extern "C" int MyAppMain() {
  if (app->Create(__FILE__)) return -1;
  SettingsFile::Load();
  Terminal::Colors *colors = Singleton<Terminal::SolarizedDarkColors>::Get();
  app->splash_color = colors->GetColor(colors->background_index);
  bool start_network_thread = !(FLAGS_enable_network_.override && !FLAGS_enable_network);

#ifdef WIN32
  app->asset_cache["MenuAtlas,0,255,255,255,0.0000.glyphs.matrix"] = app->LoadResource(200);
  app->asset_cache["MenuAtlas,0,255,255,255,0.0000.png"]           = app->LoadResource(201);
  app->asset_cache["default.vert"]                                 = app->LoadResource(202);
  app->asset_cache["default.frag"]                                 = app->LoadResource(203);
  app->asset_cache["alien.frag"]                                   = app->LoadResource(204);
  app->asset_cache["emboss.frag"]                                  = app->LoadResource(205);
  app->asset_cache["fire.frag"]                                    = app->LoadResource(206);
  app->asset_cache["fractal.frag"]                                 = app->LoadResource(207);
  app->asset_cache["darkly.frag"]                                  = app->LoadResource(208);
  app->asset_cache["stormy.frag"]                                  = app->LoadResource(209);
  app->asset_cache["twistery.frag"]                                = app->LoadResource(210);
  app->asset_cache["warper.frag"]                                  = app->LoadResource(211);
  app->asset_cache["water.frag"]                                   = app->LoadResource(212);
  app->asset_cache["waves.frag"]                                   = app->LoadResource(213);
  if (FLAGS_console) {
    app->asset_cache["VeraMoBd.ttf,32,255,255,255,4.0000.glyphs.matrix"] = app->LoadResource(214);
    app->asset_cache["VeraMoBd.ttf,32,255,255,255,4.0000.png"]           = app->LoadResource(215);
  }
#endif

  if (app->Init()) return -1;
  CHECK(screen->gd->have_framebuffer);
#ifdef WIN32
  app->input->paste_bind = Bind(Mouse::Button::_2);
#endif

  if (start_network_thread) {
    app->net = make_unique<Network>();
#if !defined(LFL_MOBILE)
    app->log_pid = true;
    app->render_process = make_unique<ProcessAPIClient>();
    app->render_process->StartServerProcess(StrCat(app->bindir, "lterm-render-sandbox", LocalFile::ExecutableSuffix));
#endif
    CHECK(app->CreateNetworkThread(false, true));
  }

  my_app->image_browser = make_unique<Browser>();

  auto tw = new MyTerminalWindow(screen);
  if (FLAGS_record.size()) tw->record = make_unique<FlatFile>(FLAGS_record);
#ifndef LFL_MOBILE
  tw->UseInitialTerminalController();
#endif
  screen->user1 = MakeTyped(tw);
  app->StartNewWindow(screen);
  tw->SetFontSize(screen->default_font.desc.size);
  my_app->new_win_width  = tw->terminal->style.font->FixedWidth() * tw->terminal->term_width;
  my_app->new_win_height = tw->terminal->style.font->Height()     * tw->terminal->term_height;
  tw->terminal->Draw(screen->Box());

#ifndef LFL_MOBILE
  vector<MenuItem> view_menu{
#ifdef __APPLE__
    MenuItem{ "=", "Zoom In", "" }, MenuItem{ "-", "Zoom Out", "" },
#endif
    MenuItem{ "", "Fonts", "choosefont" },
#ifndef LFL_MOBILE
    MenuItem{ "", "Transparency", "transparency" },
#endif
    MenuItem{ "", "VGA Colors", "colors vga", }, MenuItem{ "", "Solarized Dark Colors", "colors solarized_dark" }, MenuItem { "", "Solarized Light Colors", "colors solarized_light" }
  };
  my_app->edit_menu = SystemMenuWidget::CreateEditMenu(vector<MenuItem>());
  my_app->view_menu = make_unique<SystemMenuWidget>("View", view_menu);
#endif

  vector<MenuItem> effects_menu{
    MenuItem{ "", "None",     "shader none"     }, MenuItem{ "", "Warper", "shader warper" }, MenuItem{ "", "Water", "shader water" },
    MenuItem{ "", "Twistery", "shader twistery" }, MenuItem{ "", "Fire",   "shader fire"   }, MenuItem{ "", "Waves", "shader waves" },
    MenuItem{ "", "Emboss",   "shader emboss"   }, MenuItem{ "", "Stormy", "shader stormy" }, MenuItem{ "", "Alien", "shader alien" },
    MenuItem{ "", "Fractal",  "shader fractal"  }, MenuItem{ "", "Darkly", "shader darkly" },
    MenuItem{ "", "<separator>", "" }, MenuItem{ "", "Controls", "fxctl" } };
  my_app->toys_menu = make_unique<SystemMenuWidget>("Toys", effects_menu);

  MakeValueTuple(&my_app->shader_map, "warper", "water", "twistery");
  MakeValueTuple(&my_app->shader_map, "warper", "water", "twistery");
  MakeValueTuple(&my_app->shader_map, "fire",   "waves", "emboss");
  MakeValueTuple(&my_app->shader_map, "stormy", "alien", "fractal");
  MakeValueTuple(&my_app->shader_map, "darkly");

#ifdef LFL_MOBILE
  my_app->db = SQLite::Open(StrCat(app->savedir, "lterm.db"));
  my_app->hosts_db.Open(&my_app->db, "hosts");

  vector<pair<string,string>> keyboard_tb = { { "ctrl", "togglekey ctrl" },
    { "\U000025C0", "keypress left" }, { "\U000025B6", "keypress right" }, { "\U000025B2", "keypress up" }, 
    { "\U000025BC", "keypress down" }, { "\U000023EB", "keypress pgup" }, { "\U000023EC", "keypress pgdown" }, 
    { "fonts", "choosefont" }, { "toys", "toysmenu" } };
  my_app->keyboard_toolbar = make_unique<SystemToolbarWidget>(keyboard_tb);

  vector<pair<string,string>> hosts_tb = { { "\U00002699", "appsettings" }, { "+", "newhost" } };
  my_app->hosts_toolbar = make_unique<SystemToolbarWidget>(hosts_tb);

  vector<MenuItem> hosts_table{ MenuItem{ "Quick connect", "command", "quickconnect" },
    MenuItem{ "Interactive Shell", "command", "startshell" }, MenuItem{ "<separator>", "", "" } };
  for (auto host : my_app->hosts_db.data) {
    const LTerminal::Host *h = flatbuffers::GetRoot<LTerminal::Host>(host.second.data());
    string hostport = h->hostport() ? h->hostport()->data() : "";
    string username = h->username() ? h->username()->data() : "";
    hosts_table.emplace_back(StrCat(username, "@", hostport), "command", StrCat("hostconnect ", host.first));
  }
  my_app->hosts_table = make_unique<SystemTableWidget>("Hosts", hosts_table);
  my_app->hosts_table->AddToolbar(my_app->hosts_toolbar.get());

  vector<MenuItem> newhost_table{ MenuItem{ "SSH,Telnet", "textinput,textinput", "" },
    MenuItem{ "Port", "numinput", "22" }, MenuItem{ "Username", "textinput", "" },
    MenuItem{ "Password,Identity", "pwinput,pwinput", "" }, MenuItem{ "<separator>", "", "" },
    MenuItem{ "Connect", "button", "connect" }, MenuItem{ "<separator>", "", "" },
    MenuItem{ "Server Settings", "button", "" }
  };
  my_app->newhost_table = make_unique<SystemTableWidget>("New Host", newhost_table);

  my_app->hosts_nav = make_unique<SystemNavigationWidget>(my_app->hosts_table.get());
  my_app->hosts_nav->Show(true);
#endif

  INFO("Starting ", app->name, " ", screen->default_font.desc.name, " (w=", tw->terminal->style.font->FixedWidth(),
       ", h=", tw->terminal->style.font->Height(), ", scale=", my_app->downscale_effects, ")");
  return app->Main();
}
