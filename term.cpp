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

#include "lfapp/lfapp.h"
#include "lfapp/dom.h"
#include "lfapp/css.h"
#include "lfapp/flow.h"
#include "lfapp/gui.h"
#include "lfapp/ipc.h"
#include "lfapp/resolver.h"
#include "lfapp/browser.h"
#include "web/html.h"
#include "web/document.h"
#include "term.h"

#ifndef WIN32
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <termios.h>
#endif

namespace LFL {
DEFINE_bool  (draw_fps,    false,  "Draw FPS");
DEFINE_bool  (interpreter, false,  "Launch interpreter instead of shell");
DEFINE_string(login,       "",     "SSH user");
DEFINE_string(ssh,         "",     "SSH to host");
DEFINE_string(command,     "",     "Execute initial command");
DEFINE_string(screenshot,  "",     "Screenshot and exit");
extern FlagOfType<bool> FLAGS_lfapp_network_;

BindMap *binds;
unordered_map<string, Shader> shader_map;
Browser *image_browser;
int new_win_width = 80*Video::InitFontWidth(), new_win_height = 25*Video::InitFontHeight(), downscale_effects = 1;

void MyNewLinkCB(const shared_ptr<TextGUI::Link> &link) {
  const char *args = FindChar(link->link.c_str() + 6, isint2<'?', ':'>);
  string image_url(link->link, 0, args ? args - link->link.c_str() : string::npos);
  printf("NewLinKCB %s\n", image_url.c_str());
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
    app->network_thread->Write(new Callback([=]() { link->image = image_browser->doc.parser->OpenImage(image_url); }));
}

void MyHoverLinkCB(TextGUI::Link *link) {
  Texture *tex = link ? link->image.get() : 0;
  if (!tex) return;
  tex->Bind();
  screen->gd->EnableBlend();
  screen->gd->SetColor(Color::white - Color::Alpha(0.25));
  Box::DelBorder(screen->Box(), screen->width*.2, screen->height*.2).Draw(tex->coord);
  screen->gd->ClearDeferred();
}

struct MyTerminalController {
  static void ChangeCurrent(Terminal::Controller *new_controller_in, unique_ptr<Terminal::Controller> *old_controller_out);
  static Terminal::Controller *NewDefaultTerminalController();
  static Terminal::Controller *NewShellTerminalController(const string &m);  
  static Terminal::Controller *NewSSHTerminalController();
  static Terminal::Controller *NewTelnetTerminalController(const string &hostport);
  static void InitSSHTerminalController() {
    ONCE({ if (app->network_thread) RunInNetworkThread([=]() { app->network->Enable(Singleton<SSHClient>::Get()); }); });
  }
};

struct MyNetworkTerminalController : public NetworkTerminalController {
  using NetworkTerminalController::NetworkTerminalController;
  void Dispose() {
    unique_ptr<Terminal::Controller> self;
    MyTerminalController::ChangeCurrent
      (MyTerminalController::NewShellTerminalController("\r\nsession ended.\r\n\r\n\r\n"), &self);
  }
};

struct MyTerminalWindow : public TerminalWindow {
  Shader *activeshader;
  FrameBuffer *effects_buffer=0;
  int font_size, join_read_pending=0;

  MyTerminalWindow(Terminal::Controller *C) :
    TerminalWindow(C), activeshader(&app->video->shader_default), font_size(FLAGS_default_font_size) {}

  void Open() {
    TerminalWindow::Open(80, 25, font_size);
    terminal->new_link_cb = MyNewLinkCB;
    terminal->hover_link_cb = MyHoverLinkCB;
  }

  void OpenedController() {
    if (FLAGS_command.size()) CHECK_EQ(FLAGS_command.size()+1, controller->Write(StrCat(FLAGS_command, "\n")));
  }

  bool CustomShader() const { return activeshader != &app->video->shader_default; }
  void UpdateTargetFPS() { app->scheduler.SetAnimating(CustomShader() || (screen->lfapp_console && screen->lfapp_console->animating)); }
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
  virtual ~PTYTerminalController() { if (process.in) app->scheduler.DelWaitForeverSocket(fileno(process.in)); }

  int Open(Terminal*) {
    setenv("TERM", "xterm", 1);
    string shell = BlankNull(getenv("SHELL")), lang = BlankNull(getenv("LANG"));
    if (shell.empty()) setenv("SHELL", (shell = "/bin/bash").c_str(), 1);
    if (lang .empty()) setenv("LANG", "en_US.UTF-8", 1);
    const char *av[] = { shell.c_str(), 0 };
    CHECK_EQ(process.OpenPTY(av, app->startdir.c_str()), 0);
    return (fd = fileno(process.out));
  }

  StringPiece Read() {
    if (!process.in) return StringPiece();
    read_buf.Reset();
    NBRead(fd, &read_buf.data);
    return read_buf.data;
  }

  int Write(const StringPiece &b) { return write(fd, b.data(), b.size()); }
  void IOCtlWindowSize(int w, int h) {
    struct winsize ws;
    memzero(ws);
    ws.ws_col = w;
    ws.ws_row = h;
    ioctl(fd, TIOCSWINSZ, &ws);
  }
};
#endif

struct SSHTerminalController : public MyNetworkTerminalController {
  SSHTerminalController() : MyNetworkTerminalController(Singleton<SSHClient>::Get()) {}

  int Open(Terminal *term) {
    RunInNetworkThread([=](){
      conn = Singleton<SSHClient>::Get()->Open
      (FLAGS_ssh, SSHClient::ResponseCB(bind(&SSHTerminalController::SSHReadCB, this, _1, _2)), &detach_cb);
      if (!conn) { Dispose(); return; }
      SSHClient::SetTerminalWindowSize(conn, term->term_width, term->term_height);
      SSHClient::SetUser(conn, FLAGS_login);
#ifdef LFL_MOBILE
      SSHClient::SetPasswordCB(conn, Vault::LoadPassword, Vault::SavePassword);
#endif
    });
    return -1;
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

  void SSHReadCB(Connection *c, const StringPiece &b) { 
    if (b.empty()) ClosedCB(conn->socket);
    else read_buf.append(b.data(), b.size());
  }

  void IOCtlWindowSize(int w, int h) { if (conn) SSHClient::SetTerminalWindowSize(conn, w, h); }
  int Write(const StringPiece &b) {
#ifdef LFL_MOBILE
    char buf[1];
    if (b.size() == 1 && ctrl_down && !(ctrl_down = false)) {
      TouchDevice::ToggleToolbarButton("ctrl");
      b = &(buf[0] = Key::CtrlModified(*reinterpret_cast<const unsigned char *>(b.data())));
    }
#endif
    if (!conn || conn->state != Connection::Connected) return -1;
    return SSHClient::WriteChannelData(conn, b);
  }
};

struct ShellTerminalController : public InteractiveTerminalController {
  string ssh_usage="\r\nusage: ssh -l user host[:port]";

  ShellTerminalController(const string &hdr) {
    header = StrCat(hdr, "LTerminal 1.0", ssh_usage, "\r\n\r\n");
    shell.command.push_back(Shell::Command("ssh",      bind(&ShellTerminalController::MySSHCmd,      this, _1)));
    shell.command.push_back(Shell::Command("telnet",   bind(&ShellTerminalController::MyTelnetCmd,   this, _1)));
    shell.command.push_back(Shell::Command("nslookup", bind(&ShellTerminalController::MyNSLookupCmd, this, _1)));
    shell.command.push_back(Shell::Command("help",     bind(&ShellTerminalController::MyHelpCmd,     this, _1)));
    next_controller_cb = bind(MyTerminalController::ChangeCurrent, _1, _2);
  }

  void MySSHCmd(const vector<string> &arg) {
    int ind = 0;
    for (; ind < arg.size() && arg[ind][0] == '-'; ind += 2) if (arg[ind] == "-l") FLAGS_login = arg[ind+1];
    if (ind < arg.size()) FLAGS_ssh = arg[ind];
    if (FLAGS_login.empty() || FLAGS_ssh.empty()) { if (term) term->Write(ssh_usage); }
    else CheckNullAssign(&next_controller, MyTerminalController::NewSSHTerminalController());
  }

  void MyTelnetCmd(const vector<string> &arg) {
    if (arg.empty()) { if (term) term->Write("\r\nusage: telnet host"); }
    else CheckNullAssign(&next_controller, MyTerminalController::NewTelnetTerminalController(arg[0]));
  }

  void MyNSLookupCmd(const vector<string> &arg) {
    if (arg.empty() || !app->network_thread) return;
    if ((blocking = 1)) app->network_thread->Write(new Callback(bind(&ShellTerminalController::MyNetworkThreadNSLookup, this, arg[0])));
  }

  void MyNetworkThreadNSLookup(const string &host) {
    Singleton<Resolver>::Get()->NSLookup(host, bind(&ShellTerminalController::MyNetworkThreadNSLookupResponse, this, host, _1, _2));
  }

  void MyNetworkThreadNSLookupResponse(const string &host, IPV4::Addr ipv4_addr, DNS::Response*) {
    RunInMainThread(new Callback(bind(&ShellTerminalController::UnBlockWithResponse, this, StrCat("host = ", IPV4::Text(ipv4_addr)))));
  }

  void MyHelpCmd(const vector<string> &arg) {
    ReadCB("\r\n\r\nLTerminal interpreter commands:\r\n\r\n"
           "* ssh -l user host[:port]\r\n"
           "* telnet host[:port]\r\n"
           "* nslookup host\r\n");
  }
};

void MyTerminalController::ChangeCurrent(Terminal::Controller *new_controller_in, unique_ptr<Terminal::Controller> *old_controller_out) {
  auto tw = static_cast<MyTerminalWindow*>(screen->user1);
  tw->terminal->sink = new_controller_in;
  tw->controller.swap(*old_controller_out);
  tw->controller = unique_ptr<Terminal::Controller>(new_controller_in);
  tw->OpenController();
}

Terminal::Controller *MyTerminalController::NewSSHTerminalController()                  { InitSSHTerminalController(); return new SSHTerminalController(); }
Terminal::Controller *MyTerminalController::NewShellTerminalController(const string &m) { InitSSHTerminalController(); return new ShellTerminalController(m); }
Terminal::Controller *MyTerminalController::NewTelnetTerminalController(const string &h) { return new NetworkTerminalController(Singleton<HTTPClient>::Get(), h); }

Terminal::Controller *MyTerminalController::NewDefaultTerminalController() {
#if defined(WIN32) || defined(LFL_MOBILE)
  return NewShellTerminalController("");
#else
  return new PTYTerminalController();
#endif
}

int Frame(Window *W, unsigned clicks, int flag) {
  static const Time join_read_interval(100), refresh_interval(33);
  MyTerminalWindow *tw = (MyTerminalWindow*)W->user1;
  bool effects = screen->animating, downscale = effects && downscale_effects > 1;
  Box draw_box = screen->Box();

  if (downscale) W->gd->RestoreViewport(DrawMode::_2D);
  tw->terminal->CheckResized(draw_box);
  int read_size = tw->ReadAndUpdateTerminalFramebuffer();
  if (downscale) {
    float scale = tw->activeshader->scale = 1.0 / downscale_effects;
    W->gd->ViewPort(Box(screen->width*scale, screen->height*scale));
  }

  if (!effects) {
#if defined(__APPLE__) && !defined(LFL_MOBILE) && !defined(LFL_QT)
    if (read_size && !(flag & LFApp::Frame::DontSkip)) {
      int *pending = &tw->join_read_pending;
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
  tw->terminal->Draw(draw_box, Terminal::DrawFlag::Default, effects ? tw->activeshader : NULL);
  if (effects) screen->gd->UseShader(0);

  W->DrawDialogs();
  if (FLAGS_draw_fps) Fonts::Default()->Draw(StringPrintf("FPS = %.2f", FPS()), point(W->width*.85, 0));
  if (FLAGS_screenshot.size()) ONCE(app->shell.screenshot(vector<string>(1, FLAGS_screenshot)); app->run=0;);
  return 0;
}

void SetFontSize(int n) {
  MyTerminalWindow *tw = (MyTerminalWindow*)screen->user1;
  tw->font_size = n;
  CHECK((tw->terminal->font = Fonts::Get(FLAGS_default_font, "", tw->font_size, Color::white, Color::clear, FLAGS_default_font_flag)));
  int font_width  = tw->terminal->font->FixedWidth(), new_width  = font_width  * tw->terminal->term_width;
  int font_height = tw->terminal->font->Height(),     new_height = font_height * tw->terminal->term_height;
  if (new_width != screen->width || new_height != screen->height) screen->Reshape(new_width, new_height);
  else                                                            tw->terminal->Redraw();
  screen->SetResizeIncrements(font_width, font_height);
  INFO("Font: ", Fonts::DefaultFontEngine()->DebugString(tw->terminal->font));
}

void MyConsoleAnimating(Window *W) { 
  MyTerminalWindow *tw = (MyTerminalWindow*)screen->user1;
  tw->UpdateTargetFPS();
  if (!screen->lfapp_console || !screen->lfapp_console->animating) {
    if ((screen->lfapp_console && screen->lfapp_console->Active()) || tw->controller->frame_on_keyboard_input) app->scheduler.AddWaitForeverKeyboard();
    else                                                                                                       app->scheduler.DelWaitForeverKeyboard();
  }
}

void MyIncreaseFontCmd(const vector<string>&) { SetFontSize(((MyTerminalWindow*)screen->user1)->font_size + 1); }
void MyDecreaseFontCmd(const vector<string>&) { SetFontSize(((MyTerminalWindow*)screen->user1)->font_size - 1); }

void MyColorsCmd(const vector<string> &arg) {
  string colors_name = arg.size() ? arg[0] : "";
  MyTerminalWindow *tw = (MyTerminalWindow*)screen->user1;
  if      (colors_name == "vga")             tw->terminal->ChangeColors(Singleton<Terminal::StandardVGAColors>   ::Get());
  else if (colors_name == "solarized_dark")  tw->terminal->ChangeColors(Singleton<Terminal::SolarizedDarkColors> ::Get());
  else if (colors_name == "solarized_light") tw->terminal->ChangeColors(Singleton<Terminal::SolarizedLightColors>::Get());
  app->scheduler.Wakeup(0);
}

void MyShaderCmd(const vector<string> &arg) {
  string shader_name = arg.size() ? arg[0] : "";
  MyTerminalWindow *tw = (MyTerminalWindow*)screen->user1;
  if (tw->effects_buffer) {
    tw->effects_buffer->tex.ClearGL();
    Replace(&tw->effects_buffer, (FrameBuffer*)0);
  }
  auto shader = shader_map.find(shader_name);
  bool found = shader != shader_map.end();
  if (found && !shader->second.ID) Shader::CreateShaderToy(shader_name, Asset::FileContents(StrCat(shader_name, ".glsl")), &shader->second);
  tw->activeshader = found ? &shader->second : &app->video->shader_default;
  tw->UpdateTargetFPS();
}

void MyEffectsControlsCmd(const vector<string>&) { 
  app->shell.Run("slider shadertoy_blend 1.0 0.01");
  app->scheduler.Wakeup(0);
}

void MyTransparencyControlsCmd(const vector<string>&) {
  static SliderDialog::UpdatedCB cb = SliderDialog::UpdatedCB(bind([=](Widget::Slider *s){ Window::Get()->SetTransparency(s->Percent()); }, _1));
  new SliderDialog("window transparency", cb, 0, 1.0, .025);
  app->scheduler.Wakeup(0);
}

void MyChooseFontCmd(const vector<string> &arg) {
  MyTerminalWindow *tw = static_cast<MyTerminalWindow*>(screen->user1);
  if (arg.size() < 2) return app->LaunchNativeFontChooser(FontDesc(FLAGS_default_font, "", tw->font_size), "choosefont");
  if (arg.size() > 2) FLAGS_default_font_flag = atoi(arg[2]);
  FLAGS_default_font = arg[0];
  SetFontSize(atof(arg[1]));
  app->scheduler.Wakeup(0);
}

#ifdef LFL_MOBILE
void MyMobileMenuCmd(const vector<string> &arg) {
  MyShaderCmd(vector<string>{"none"});
  if (arg.size()) app->LaunchNativeMenu(arg[0]);
}
void MyMobileKeyPressCmd(const vector<string> &arg) {
  MyTerminalWindow *tw = static_cast<MyTerminalWindow*>(screen->user1);
  static map<string, Callback> keys = {
    { "left",   bind([=]{ tw->terminal->CursorLeft();  if (tw->controller->FrameOnKeyboardInput()) app->scheduler.Wakeup(0); }) },
    { "right",  bind([=]{ tw->terminal->CursorRight(); if (tw->controller->FrameOnKeyboardInput()) app->scheduler.Wakeup(0); }) },
    { "up",     bind([=]{ tw->terminal->HistUp();      if (tw->controller->FrameOnKeyboardInput()) app->scheduler.Wakeup(0); }) },
    { "down",   bind([=]{ tw->terminal->HistDown();    if (tw->controller->FrameOnKeyboardInput()) app->scheduler.Wakeup(0); }) },
    { "pgup",   bind([=]{ tw->terminal->PageUp();      if (tw->controller->FrameOnKeyboardInput()) app->scheduler.Wakeup(0); }) },
    { "pgdown", bind([=]{ tw->terminal->PageDown();    if (tw->controller->FrameOnKeyboardInput()) app->scheduler.Wakeup(0); }) } };
  if (arg.size()) FindAndDispatch(keys, arg[0]);
}
void MyMobileKeyToggleCmd(const vector<string> &arg) {
  static map<string, Callback> keys = {
    { "ctrl", bind([&]{ static_cast<MyTerminalWindow*>(screen->user1)->controller->ctrl_down = !ssh->ctrl_down; }) } };
  if (arg.size()) FindAndDispatch(keys, arg[0]);
}
void MyMobileCloseCmd(const vector<string> &arg) { static_cast<MyTerminalWindow*>(screen->user1)->controller->Close(); }
#endif

void MyWindowInitCB(Window *W) {
  W->width = new_win_width;
  W->height = new_win_height;
  W->caption = app->name;
  W->frame_cb = Frame;
  W->binds = binds;
}

void MyWindowStartCB(Window *W) {
  auto tw = (MyTerminalWindow*)W->user1;
  tw->Open();
  W->SetResizeIncrements(tw->terminal->font->FixedWidth(), tw->terminal->font->Height());
  if (W->lfapp_console) W->lfapp_console->animating_cb = bind(&MyConsoleAnimating, screen);
}

void MyWindowCloneCB(Window *W) {
  if (FLAGS_lfapp_console) W->InitLFAppConsole();
  W->user1 = new MyTerminalWindow(MyTerminalController::NewDefaultTerminalController());
  W->input_bind.push_back(W->binds);
  MyWindowStartCB(W);
}

void MyWindowClosedCB(Window *W) {
  delete (MyTerminalWindow*)W->user1;
  delete W;
}

}; // naemspace LFL
using namespace LFL;

extern "C" void LFAppCreateCB() {
  app->name = "LTerminal";
#ifdef LFL_DEBUG
  app->logfilename = StrCat(LFAppDownloadDir(), "lterm.txt");
#endif
  binds = new BindMap();
  MyWindowInitCB(screen);
  FLAGS_lfapp_video = FLAGS_lfapp_input = 1;
#ifdef LFL_MOBILE
  downscale_effects = TouchDevice::SetExtraScale(true);
#endif
}

extern "C" int main(int argc, const char *argv[]) {
  if (app->Create(argc, argv, __FILE__, LFAppCreateCB)) { app->Free(); return -1; }
  bool start_network_thread = !(FLAGS_lfapp_network_.override && !FLAGS_lfapp_network);
  app->splash_color = &Singleton<Terminal::SolarizedDarkColors>::Get()->c[Terminal::Colors::bg_index];

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

  if (app->Init()) { app->Free(); return -1; }
  CHECK(app->video->opengl_framebuffer);
  app->window_init_cb = MyWindowInitCB;
  app->window_closed_cb = MyWindowClosedCB;
  app->shell.command.push_back(Shell::Command("colors", bind(&MyColorsCmd, _1)));
  app->shell.command.push_back(Shell::Command("shader", bind(&MyShaderCmd, _1)));
  if (start_network_thread) {
    app->network = new Network();
#if !defined(LFL_MOBILE)
    app->render_process = new ProcessAPIClient();
    app->render_process->StartServerProcess(StrCat(app->bindir, "lterm-render-sandbox", LocalFile::ExecutableSuffix));
#endif
    CHECK(app->CreateNetworkThread(false, true));
  }

  app->create_win_f = bind(&Application::CreateNewWindow, app, &MyWindowCloneCB);
#ifdef WIN32
  app->input.paste_bind = Bind(Mouse::Button::_2);
#else
  binds->Add(Bind('n',       Key::Modifier::Cmd, Bind::CB(app->create_win_f)));
#endif                       
  binds->Add(Bind('=',       Key::Modifier::Cmd, Bind::CB(bind(&MyIncreaseFontCmd, vector<string>()))));
  binds->Add(Bind('-',       Key::Modifier::Cmd, Bind::CB(bind(&MyDecreaseFontCmd, vector<string>()))));
  binds->Add(Bind('6',       Key::Modifier::Cmd, Bind::CB(bind([&](){ app->shell.console(vector<string>()); }))));
  binds->Add(Bind(Key::Up,   Key::Modifier::Cmd, Bind::CB(bind([&](){ if (screen->user1) static_cast<MyTerminalWindow*>(screen->user1)->ScrollHistory(1); }))));
  binds->Add(Bind(Key::Down, Key::Modifier::Cmd, Bind::CB(bind([&](){ if (screen->user1) static_cast<MyTerminalWindow*>(screen->user1)->ScrollHistory(0); }))));

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

  shader_map["warper"];  shader_map["water"];  shader_map["twistery"];
  shader_map["fire"];    shader_map["waves"];  shader_map["emboss"];
  shader_map["stormy"];  shader_map["alien"];  shader_map["fractal"];
  shader_map["darkly"];

  Terminal::Controller *controller = 0;
  if      (FLAGS_interpreter)  controller = MyTerminalController::NewShellTerminalController("");
  else if (!FLAGS_ssh.empty()) controller = MyTerminalController::NewSSHTerminalController();
  else                         controller = MyTerminalController::NewDefaultTerminalController();
  image_browser = new Browser();
  MyTerminalWindow *tw = new MyTerminalWindow(controller);
  screen->user1 = tw;
  MyWindowStartCB(screen);
  SetFontSize(tw->font_size);
  new_win_width  = tw->terminal->font->FixedWidth() * tw->terminal->term_width;
  new_win_height = tw->terminal->font->Height()     * tw->terminal->term_height;
  tw->terminal->Draw(screen->Box());

  app->shell.command.push_back(Shell::Command("fxctl",        bind(&MyEffectsControlsCmd,      _1)));
  app->shell.command.push_back(Shell::Command("transparency", bind(&MyTransparencyControlsCmd, _1)));
  app->shell.command.push_back(Shell::Command("choosefont",   bind(&MyChooseFontCmd,           _1)));
#ifdef LFL_MOBILE                                                                            
  app->shell.command.push_back(Shell::Command("menu",         bind(&MyMobileMenuCmd,           _1)));
  app->shell.command.push_back(Shell::Command("keypress",     bind(&MyMobileKeyPressCmd,       _1)));
  app->shell.command.push_back(Shell::Command("togglekey",    bind(&MyMobileKeyToggleCmd,      _1)));
  app->shell.command.push_back(Shell::Command("close",        bind(&MyMobileCloseCmd,          _1)));
  vector<pair<string,string>> toolbar_menu = { { "fx", "menu Effects" }, { "ctrl", "togglekey ctrl" },
    { "\U000025C0", "keypress left" }, { "\U000025B6", "keypress right" }, { "\U000025B2", "keypress up" }, 
    { "\U000025BC", "keypress down" }, { "\U000023EB", "keypress pgup" }, { "\U000023EC", "keypress pgdown" }, 
    { "quit", "close" } };
  TouchDevice::CloseKeyboardAfterReturn(false);
  TouchDevice::OpenKeyboard();
  TouchDevice::AddToolbar(toolbar_menu);
#endif
  INFO("Starting ", app->name, " ", FLAGS_default_font, " (w=", tw->terminal->font->FixedWidth(),
       ", h=", tw->terminal->font->Height(), ", scale=", downscale_effects, ")");

  return app->Main();
}
