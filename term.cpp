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
#include "core/web/dom.h"
#include "core/web/css.h"
#include "core/app/flow.h"
#include "core/app/gui.h"
#include "core/app/crypto.h"
#include "core/app/net/ssh.h"
#include "core/app/ipc.h"
#include "core/app/net/resolver.h"
#include "core/app/browser.h"
#include "core/web/html.h"
#include "core/web/document.h"
#include "term.h"

#ifndef WIN32
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <termios.h>
#endif

namespace LFL {
DEFINE_bool  (interpreter, false,  "Launch interpreter instead of shell");
DEFINE_string(telnet,      "",     "Telnet to host");
DEFINE_string(ssh,         "",     "SSH to host");
DEFINE_string(login,       "",     "SSH user");
DEFINE_string(command,     "",     "Execute initial command");
DEFINE_string(screenshot,  "",     "Screenshot and exit");
DEFINE_bool  (draw_fps,    false,  "Draw FPS");
extern FlagOfType<bool> FLAGS_lfapp_network_;

struct MyAppState {
  unordered_map<string, Shader> shader_map;
  unique_ptr<Browser> image_browser;
  int new_win_width = 80*Fonts::InitFontWidth(), new_win_height = 25*Fonts::InitFontHeight();
  int downscale_effects = 1;
} *my_app = nullptr;

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
  virtual ~PTYTerminalController() { if (process.in) app->scheduler.DelWaitForeverSocket(fileno(process.in)); }

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

struct SSHTerminalController : public NetworkTerminalController {
  using NetworkTerminalController::NetworkTerminalController;

  int Open(TextArea *t) {
    Terminal *term = dynamic_cast<Terminal*>(t);
    app->RunInNetworkThread([=](){
      conn = SSHClient::Open(FLAGS_ssh, SSHClient::ResponseCB
                             (bind(&SSHTerminalController::SSHReadCB, this, _1, _2)), &detach_cb);
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
      app->ToggleToolbarButton("ctrl");
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

struct ShellTerminalController : public InteractiveTerminalController {
  string ssh_usage="\r\nusage: ssh -l user host[:port]";
  Callback telnet_cb, ssh_cb;
  ShellTerminalController(const string &hdr, const Callback &tcb, const Callback &scb) :
    telnet_cb(tcb), ssh_cb(scb) {
    header = StrCat(hdr, "LTerminal 1.0", ssh_usage, "\r\n\r\n");
    shell.Add("ssh",      bind(&ShellTerminalController::MySSHCmd,      this, _1));
    shell.Add("telnet",   bind(&ShellTerminalController::MyTelnetCmd,   this, _1));
    shell.Add("nslookup", bind(&ShellTerminalController::MyNSLookupCmd, this, _1));
    shell.Add("help",     bind(&ShellTerminalController::MyHelpCmd,     this, _1));
  }

  void MySSHCmd(const vector<string> &arg) {
    int ind = 0;
    for (; ind < arg.size() && arg[ind][0] == '-'; ind += 2) if (arg[ind] == "-l") FLAGS_login = arg[ind+1];
    if (ind < arg.size()) FLAGS_ssh = arg[ind];
    if (FLAGS_login.empty() || FLAGS_ssh.empty()) { if (term) term->Write(ssh_usage); }
    else ssh_cb();
  }

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
  MyTerminalWindow(Window *W) :
    TerminalWindow(W->AddGUI(make_unique<Terminal>(nullptr, W->gd, W->default_font)), 80, 25) {
    terminal->new_link_cb   = bind(&MyTerminalWindow::NewLinkCB,   this, _1);
    terminal->hover_link_cb = bind(&MyTerminalWindow::HoverLinkCB, this, _1);
  }

  void SetFontSize(int n) {
    screen->default_font.desc.size = n;
    CHECK((terminal->font = screen->default_font.Load()));
    int font_width  = terminal->font->FixedWidth(), new_width  = font_width  * terminal->term_width;
    int font_height = terminal->font->Height(),     new_height = font_height * terminal->term_height;
    if (new_width != screen->width || new_height != screen->height) screen->Reshape(new_width, new_height);
    else                                                            terminal->Redraw();
    screen->SetResizeIncrements(font_width, font_height);
    INFO("Font: ", app->fonts->DefaultFontEngine()->DebugString(terminal->font));
  }

  void UseShellTerminalController(const string &m) {
    ChangeController(make_unique<ShellTerminalController>(m, [=](){ UseTelnetTerminalController(); },
                                                          [=](){ UseSSHTerminalController(); }));
  }

  void UseSSHTerminalController() {
    ChangeController(make_unique<SSHTerminalController>(app->net->tcp_client.get(), "",
                                                        [=](){ UseShellTerminalController("\r\nsession ended.\r\n\r\n\r\n"); }));
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
    if      (FLAGS_interpreter)  return UseShellTerminalController("");
    else if (!FLAGS_ssh.empty()) return UseSSHTerminalController();
    else                         return UseDefaultTerminalController();
  }

  void OpenedController() {
    if (FLAGS_command.size()) CHECK_EQ(FLAGS_command.size()+1, controller->Write(StrCat(FLAGS_command, "\n")));
  }

  bool CustomShader() const { return activeshader != &app->shaders->shader_default; }
  void UpdateTargetFPS() { app->scheduler.SetAnimating(CustomShader() || (screen->console && screen->console->animating)); }

  void ConsoleAnimatingCB(Window *W) { 
    UpdateTargetFPS();
    if (!W->console || !W->console->animating) {
      if ((W->console && W->console->Active()) || controller->frame_on_keyboard_input) app->scheduler.AddWaitForeverKeyboard();
      else                                                                             app->scheduler.DelWaitForeverKeyboard();
    }
  }

  void NewLinkCB(const shared_ptr<TextBox::Link> &link) {
    const char *args = FindChar(link->link.c_str() + 6, isint2<'?', ':'>);
    string image_url(link->link, 0, args ? args - link->link.c_str() : string::npos);
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

  void HoverLinkCB(TextBox::Link *link) {
    Texture *tex = link ? link->image.get() : 0;
    if (!tex) return;
    tex->Bind();
    screen->gd->EnableBlend();
    screen->gd->SetColor(Color::white - Color::Alpha(0.25));
    Box::DelBorder(screen->Box(), screen->width*.2, screen->height*.2).Draw(tex->coord);
    screen->gd->ClearDeferred();
  }

  int Frame(Window *W, unsigned clicks, int flag) {
    bool effects = screen->animating, downscale = effects && my_app->downscale_effects > 1;
    Box draw_box = screen->Box();

    if (downscale) W->gd->RestoreViewport(DrawMode::_2D);
    terminal->CheckResized(draw_box);
    int read_size = ReadAndUpdateTerminalFramebuffer();
    if (downscale) {
      float scale = activeshader->scale = 1.0 / my_app->downscale_effects;
      W->gd->ViewPort(Box(screen->width*scale, screen->height*scale));
    }

    if (!effects) {
#if defined(__APPLE__) && !defined(LFL_MOBILE) && !defined(LFL_QT)
      if (read_size && !(flag & LFApp::Frame::DontSkip)) {
        int *pending = &join_read_pending;
        bool join_read = read_size > 255;
        if (join_read) { if (1            && ++(*pending)) { if (app->scheduler.WakeupIn(0, join_read_interval)) return -1; } }
        else           { if ((*pending)<1 && ++(*pending)) { if (app->scheduler.WakeupIn(0,   refresh_interval)) return -1; } }
        *pending = 0;
      }
      app->scheduler.ClearWakeupIn();
#endif
    }

    W->gd->DrawMode(DrawMode::_2D);
    W->gd->DisableBlend();
    terminal->Draw(draw_box, Terminal::DrawFlag::Default, effects ? activeshader : NULL);
    if (effects) screen->gd->UseShader(0);

    W->DrawDialogs();
    if (FLAGS_draw_fps) W->default_font->Draw(StringPrintf("FPS = %.2f", app->FPS()), point(W->width*.85, 0));
    if (FLAGS_screenshot.size()) ONCE(screen->shell->screenshot(vector<string>(1, FLAGS_screenshot)); app->run=0;);
    return 0;
  }

  void FontCmd(const vector<string> &arg) {
    if (arg.size() < 2) return app->LaunchNativeFontChooser(screen->default_font.desc, "choosefont");
    if (arg.size() > 2) FLAGS_default_font_flag = atoi(arg[2]);
    screen->default_font.desc.name = arg[0];
    SetFontSize(atof(arg[1]));
    app->scheduler.Wakeup(0);
  }

  void ColorsCmd(const vector<string> &arg) {
    string colors_name = arg.size() ? arg[0] : "";
    if      (colors_name == "vga")             terminal->ChangeColors(Singleton<Terminal::StandardVGAColors>   ::Get());
    else if (colors_name == "solarized_dark")  terminal->ChangeColors(Singleton<Terminal::SolarizedDarkColors> ::Get());
    else if (colors_name == "solarized_light") terminal->ChangeColors(Singleton<Terminal::SolarizedLightColors>::Get());
    app->scheduler.Wakeup(0);
  }

  void ShaderCmd(const vector<string> &arg) {
    string shader_name = arg.size() ? arg[0] : "";
    auto shader = my_app->shader_map.find(shader_name);
    bool found = shader != my_app->shader_map.end();
    if (found && !shader->second.ID) Shader::CreateShaderToy(shader_name, Asset::FileContents(StrCat(shader_name, ".glsl")), &shader->second);
    activeshader = found ? &shader->second : &app->shaders->shader_default;
    UpdateTargetFPS();
  }

  void EffectsControlsCmd(const vector<string>&) { 
    screen->shell->Run("slider shadertoy_blend 1.0 0.01");
    app->scheduler.Wakeup(0);
  }

  void TransparencyControlsCmd(const vector<string>&) {
    SliderDialog::UpdatedCB cb(bind([=](Widget::Slider *s){ screen->SetTransparency(s->Percent()); }, _1));
    screen->AddDialog(make_unique<SliderDialog>(screen->gd, "window transparency", cb, 0, 1.0, .025));
    app->scheduler.Wakeup(0);
  }

#ifdef LFL_MOBILE
  unordered_map<string, Callback> mobile_key_cmd = {
    { "left",   bind([=]{ terminal->CursorLeft();  if (controller->frame_on_keyboard_input) app->scheduler.Wakeup(0); }) },
    { "right",  bind([=]{ terminal->CursorRight(); if (controller->frame_on_keyboard_input) app->scheduler.Wakeup(0); }) },
    { "up",     bind([=]{ terminal->HistUp();      if (controller->frame_on_keyboard_input) app->scheduler.Wakeup(0); }) },
    { "down",   bind([=]{ terminal->HistDown();    if (controller->frame_on_keyboard_input) app->scheduler.Wakeup(0); }) },
    { "pgup",   bind([=]{ terminal->PageUp();      if (controller->frame_on_keyboard_input) app->scheduler.Wakeup(0); }) },
    { "pgdown", bind([=]{ terminal->PageDown();    if (controller->frame_on_keyboard_input) app->scheduler.Wakeup(0); }) } };
  unordered_map<string, Callback> mobile_togglekey_cmd = {
    { "ctrl", bind([&]{ controller->ctrl_down = !controller->ctrl_down; }) } };
  void MobileKeyPressCmd (const vector<string> &arg) { if (arg.size()) FindAndDispatch(mobile_key_cmd, arg[0]); }
  void MobileKeyToggleCmd(const vector<string> &arg) { if (arg.size()) FindAndDispatch(mobile_togglekey_cmd, arg[0]); }
  void MobileCloseCmd(const vector<string> &arg) { controller->Close(); }
  void MobileMenuCmd(const vector<string> &arg) {
    ShaderCmd(vector<string>{"none"});
    if (arg.size()) app->LaunchNativeMenu(arg[0]);
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
  W->SetResizeIncrements(tw->terminal->font->FixedWidth(), tw->terminal->font->Height());

  W->shell = make_unique<Shell>(nullptr, nullptr, nullptr);
  W->shell->Add("choosefont",   bind(&MyTerminalWindow::FontCmd,                 tw, _1));
  W->shell->Add("colors",       bind(&MyTerminalWindow::ColorsCmd,               tw, _1));
  W->shell->Add("shader",       bind(&MyTerminalWindow::ShaderCmd,               tw, _1));
  W->shell->Add("fxctl",        bind(&MyTerminalWindow::EffectsControlsCmd,      tw, _1));
  W->shell->Add("transparency", bind(&MyTerminalWindow::TransparencyControlsCmd, tw, _1));
#ifdef LFL_MOBILE                                                                           
  W->shell->Add("close",        bind(&MyTerminalWindow::MobileCloseCmd,          tw, _1));
  W->shell->Add("menu",         bind(&MyTerminalWindow::MobileMenuCmd,           tw, _1));
  W->shell->Add("keypress",     bind(&MyTerminalWindow::MobileKeyPressCmd,       tw, _1));
  W->shell->Add("togglekey",    bind(&MyTerminalWindow::MobileKeyToggleCmd,      tw, _1));
#endif

  BindMap *binds = W->AddInputController(make_unique<BindMap>());
#ifndef WIN32
  binds->Add('n',       Key::Modifier::Cmd, Bind::CB(bind(&Application::CreateNewWindow, app)));
#endif                  
  binds->Add('6',       Key::Modifier::Cmd, Bind::CB(bind([=](){ W->shell->console(vector<string>()); })));
  binds->Add('=',       Key::Modifier::Cmd, Bind::CB(bind([=](){ tw->SetFontSize(W->default_font.desc.size + 1); })));
  binds->Add('-',       Key::Modifier::Cmd, Bind::CB(bind([=](){ tw->SetFontSize(W->default_font.desc.size - 1); })));
  binds->Add(Key::Up,   Key::Modifier::Cmd, Bind::CB(bind([=](){ tw->terminal->ScrollUp();   app->scheduler.Wakeup(0); })));
  binds->Add(Key::Down, Key::Modifier::Cmd, Bind::CB(bind([=](){ tw->terminal->ScrollDown(); app->scheduler.Wakeup(0); })));
}

void MyWindowClosed(Window *W) {
  delete GetTyped<MyTerminalWindow*>(W->user1);
  delete W;
}

}; // naemspace LFL
using namespace LFL;

extern "C" void MyAppCreate() {
  FLAGS_lfapp_video = FLAGS_lfapp_input = 1;
  app = new Application();
  screen = new Window();
  my_app = new MyAppState();
#ifdef LFL_DEBUG
  app->logfilename = StrCat(LFAppDownloadDir(), "lterm.txt");
#endif
  app->name = "LTerminal";
  app->exit_cb = []() { delete my_app; };
  app->window_closed_cb = MyWindowClosed;
  app->window_start_cb = MyWindowStart;
  app->window_init_cb = MyWindowInit;
  app->window_init_cb(screen);
#ifdef LFL_MOBILE
  my_app->downscale_effects = app->SetExtraScale(true);
#endif
}

extern "C" int MyAppMain(int argc, const char* const* argv) {
  if (!app) MyAppCreate();
  if (app->Create(argc, argv, __FILE__)) return -1;
  app->splash_color = &Singleton<Terminal::SolarizedDarkColors>::Get()->c[Terminal::Colors::bg_index];
  bool start_network_thread = !(FLAGS_lfapp_network_.override && !FLAGS_lfapp_network);

#ifdef WIN32
  Asset::cache["MenuAtlas,0,255,255,255,0.0000.glyphs.matrix"] = app->LoadResource(200);
  Asset::cache["MenuAtlas,0,255,255,255,0.0000.png"]           = app->LoadResource(201);
  Asset::cache["lfapp_vertex.glsl"]                            = app->LoadResource(202);
  Asset::cache["lfapp_pixel.glsl"]                             = app->LoadResource(203);
  Asset::cache["alien.glsl"]                                   = app->LoadResource(204);
  Asset::cache["emboss.glsl"]                                  = app->LoadResource(205);
  Asset::cache["fire.glsl"]                                    = app->LoadResource(206);
  Asset::cache["fractal.glsl"]                                 = app->LoadResource(207);
  Asset::cache["darkly.glsl"]                                  = app->LoadResource(208);
  Asset::cache["stormy.glsl"]                                  = app->LoadResource(209);
  Asset::cache["twistery.glsl"]                                = app->LoadResource(210);
  Asset::cache["warper.glsl"]                                  = app->LoadResource(211);
  Asset::cache["water.glsl"]                                   = app->LoadResource(212);
  Asset::cache["waves.glsl"]                                   = app->LoadResource(213);
  if (FLAGS_lfapp_console) {
    Asset::cache["VeraMoBd.ttf,32,255,255,255,4.0000.glyphs.matrix"] = app->LoadResource(214);
    Asset::cache["VeraMoBd.ttf,32,255,255,255,4.0000.png"]           = app->LoadResource(215);
  }
#endif

  if (app->Init()) return -1;
  CHECK(screen->gd->have_framebuffer);
  app->scheduler.AddWaitForeverMouse();
#ifdef WIN32
  app->input.paste_bind = Bind(Mouse::Button::_2);
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
  tw->UseInitialTerminalController();
  screen->user1 = MakeTyped(tw);
  app->StartNewWindow(screen);
  tw->SetFontSize(screen->default_font.desc.size);
  my_app->new_win_width  = tw->terminal->font->FixedWidth() * tw->terminal->term_width;
  my_app->new_win_height = tw->terminal->font->Height()     * tw->terminal->term_height;
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
  app->AddNativeEditMenu();
  app->AddNativeMenu("View", view_menu);
#endif

  vector<MenuItem> effects_menu{
    MenuItem{ "", "None",     "shader none"     }, MenuItem{ "", "Warper", "shader warper" }, MenuItem{ "", "Water", "shader water" },
    MenuItem{ "", "Twistery", "shader twistery" }, MenuItem{ "", "Fire",   "shader fire"   }, MenuItem{ "", "Waves", "shader waves" },
    MenuItem{ "", "Emboss",   "shader emboss"   }, MenuItem{ "", "Stormy", "shader stormy" }, MenuItem{ "", "Alien", "shader alien" },
    MenuItem{ "", "Fractal",  "shader fractal"  }, MenuItem{ "", "Darkly", "shader darkly" },
    MenuItem{ "", "<seperator>", "" }, MenuItem{ "", "Controls", "fxctl" } };
  app->AddNativeMenu("Effects", effects_menu);

  MakeValueTuple(&my_app->shader_map, "warper", "water", "twistery");
  MakeValueTuple(&my_app->shader_map, "warper", "water", "twistery");
  MakeValueTuple(&my_app->shader_map, "fire",   "waves", "emboss");
  MakeValueTuple(&my_app->shader_map, "stormy", "alien", "fractal");
  MakeValueTuple(&my_app->shader_map, "darkly");

#ifdef LFL_MOBILE                                                                            
  vector<pair<string,string>> toolbar_menu = { { "fx", "menu Effects" }, { "ctrl", "togglekey ctrl" },
    { "\U000025C0", "keypress left" }, { "\U000025B6", "keypress right" }, { "\U000025B2", "keypress up" }, 
    { "\U000025BC", "keypress down" }, { "\U000023EB", "keypress pgup" }, { "\U000023EC", "keypress pgdown" }, 
    { "quit", "close" } };
  app->CloseTouchKeyboardAfterReturn(false);
  app->OpenTouchKeyboard();
  app->AddToolbar(toolbar_menu);
#endif

  INFO("Starting ", app->name, " ", screen->default_font.desc.name, " (w=", tw->terminal->font->FixedWidth(),
       ", h=", tw->terminal->font->Height(), ", scale=", my_app->downscale_effects, ")");
  return app->Main();
}
