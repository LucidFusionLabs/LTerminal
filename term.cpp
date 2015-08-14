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
#include "crawler/html.h"
#include "crawler/document.h"

#ifndef WIN32
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <termios.h>
#endif

namespace LFL {
#ifdef LFL_MOBILE
DEFINE_int   (peak_fps,    30,     "Peak FPS");
#else                      
DEFINE_int   (peak_fps,    60,     "Peak FPS");
#endif                     
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
NetworkThread *network_thread;
ProcessAPIClient *render_process;
#if defined(WIN32)
int new_win_width = 80*8, new_win_height = 25*17;
#elif defined(__APPLE__)
int new_win_width = 80*9,    new_win_height = 25*20;
#else
int new_win_width = 80*10,    new_win_height = 25*18;
#endif
int downscale_effects = 1;

void MyNewLinkCB(const shared_ptr<TextGUI::Link> &link) {
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
  if (render_process && render_process->conn)
    network_thread->Write(new Callback([=]() { link->image = image_browser->doc.parser->OpenImage(image_url); }));
}

void MyHoverLinkCB(TextGUI::Link *link) {
  Texture *tex = link ? link->image.get() : 0;
  if (!tex) return;
  tex->Bind();
  screen->gd->EnableBlend();
  screen->gd->SetColor(Color::white - Color::Alpha(0.25));
  Box::DelBorder(screen->Box(), screen->width*.2, screen->height*.2).Draw(tex->coord);
}

struct MyTerminalController : public ByteSink {
  virtual ~MyTerminalController() {}
  virtual int Open(Terminal*) = 0;
  virtual StringPiece Read() = 0;
  virtual void Close() {}
  virtual bool FrameOnKeyboardInput() const { return false; }
  static void ChangeCurrent(MyTerminalController *new_controller_in, unique_ptr<MyTerminalController> *old_controller_out);
  static MyTerminalController *NewDefaultTerminalController();
  static MyTerminalController *NewShellTerminalController(const string &m);  
  static MyTerminalController *NewSSHTerminalController();
  static void InitSSHTerminalController() {
    ONCE({ if (network_thread) network_thread->Write(new Callback([=]() { app->network.Enable(Singleton<SSHClient>::Get()); })); });
  }
};

struct MyTerminalWindow {
  unique_ptr<MyTerminalController> controller;
  unique_ptr<Terminal> terminal;
  Shader *activeshader;
  int font_size, join_read_pending=0;
  bool effects_mode=0, read_pending=0, effects_init=0;
  FrameBuffer *effects_buffer=0;
  MyTerminalWindow(MyTerminalController *C) :
    controller(C), activeshader(&app->video.shader_default), font_size(FLAGS_default_font_size) {}

  void Open() {
    terminal = unique_ptr<Terminal>(new Terminal(controller.get(), screen, Fonts::Get(FLAGS_default_font, "", font_size)));
    terminal->new_link_cb = MyNewLinkCB;
    terminal->hover_link_cb = MyHoverLinkCB;
    terminal->active = true;
    terminal->SetDimension(80, 25);
#ifdef FUZZ_DEBUG
    for (int i=0; i<256; i++) {
      INFO("fuzz i = ", i);
      for (int j=0; j<256; j++)
        for (int k=0; k<256; k++)
          terminal->Write(string(1, i), 1, 1);
    }
    terminal->Newline(1);
    terminal->Write("Hello world.", 1, 1);
#else
    app->scheduler.AddWaitForeverMouse();
    OpenController();
#endif
  }
  void OpenController() {
    int fd = controller->Open(terminal.get());
    if (fd != -1) app->scheduler.AddWaitForeverSocket(fd, SocketSet::READABLE, 0);
    if (int len = FLAGS_command.size()) CHECK_EQ(len+1, controller->Write(StrCat(FLAGS_command, "\n").data(), len+1));
    if (controller->FrameOnKeyboardInput()) app->scheduler.AddWaitForeverKeyboard();
    else                                    app->scheduler.DelWaitForeverKeyboard();
  }
  int ReadAndUpdateTerminalFramebuffer() {
    StringPiece s = controller->Read();
    if (s.len) { terminal->Write(s); read_pending = 1; }
    return s.len;
  }
  void UpdateTargetFPS() {
    effects_mode = CustomShader() || (screen->lfapp_console && screen->lfapp_console->animating);
    int target_fps = effects_mode ? FLAGS_peak_fps : 0;
    if (target_fps != screen->target_fps) {
      app->scheduler.UpdateTargetFPS(target_fps);
      app->scheduler.Wakeup(0);
    }
  }
  bool CustomShader() const { return activeshader != &app->video.shader_default; }
  void ScrollHistory(bool up_or_down) {
    if (up_or_down) terminal->ScrollUp();
    else            terminal->ScrollDown();
    app->scheduler.Wakeup(0);
  }
};

#ifndef WIN32
struct PTYTerminalController : public MyTerminalController {
  int fd = -1;
  ProcessPipe process;
  ReadBuffer read_buf;
  PTYTerminalController() : read_buf(65536) {}
  virtual ~PTYTerminalController() { if (process.in) app->scheduler.DelWaitForeverSocket(fileno(process.in)); }

  int Write(const char *b, int l) { return write(fd, b, l); }
  void IOCtlWindowSize(int w, int h) {
    struct winsize ws;
    memzero(ws);
    ws.ws_col = w;
    ws.ws_row = h;
    ioctl(fd, TIOCSWINSZ, &ws);
  }
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
};
#endif

struct SSHTerminalController : public MyTerminalController {
  bool ctrl_down=0;
  Connection *conn=0;
  Callback connected_cb;
  string read_buf, ret_buf, ended_msg="\r\nSSH session ended.\r\n\r\n\r\n";
  SSHTerminalController() : connected_cb(bind(&SSHTerminalController::ConnectedCB, this)) {}

  void IOCtlWindowSize(int w, int h) { if (conn) SSHClient::SetTerminalWindowSize(conn, w, h); }
  void ConnectedCB() { app->scheduler.AddWaitForeverSocket(conn->socket, SocketSet::READABLE, 0); }
  void ClosedCB() {
    CHECK(conn);
    app->scheduler.DelWaitForeverSocket(conn->socket);
    app->scheduler.Wakeup(0);
    conn = 0;
    unique_ptr<MyTerminalController> self;
    MyTerminalController::ChangeCurrent(MyTerminalController::NewShellTerminalController(ended_msg), &self);
  }
  void ReadCB(Connection *c, const StringPiece &b) { 
    if (b.empty()) ClosedCB();
    else read_buf.append(b.data(), b.size());
  }
  StringPiece Read() {
    if (conn && conn->state == Connection::Connected && NBReadable(conn->socket)) {
      if (conn->Read() < 0)                          { ERROR(conn->Name(), ": Read");       Close(); return ""; }
      if (conn->rl && conn->handler->Read(conn) < 0) { ERROR(conn->Name(), ": query read"); Close(); return ""; }
    }
    swap(read_buf, ret_buf);
    read_buf.clear();
    return ret_buf;
  }
  int Write(const char *b, int l) {
#ifdef LFL_MOBILE
    char buf[1];
    if (l == 1 && ctrl_down && !(ctrl_down = false)) {
      TouchDevice::ToggleToolbarButton("ctrl");
      b = &(buf[0] = Key::CtrlModified(*reinterpret_cast<const unsigned char *>(b)));
    }
#endif
    if (!conn || conn->state != Connection::Connected) return -1;
    return SSHClient::WriteChannelData(conn, StringPiece(b, l));
  }
  int Open(Terminal *term) {
    network_thread->Write
      (new Callback([&,term](){
                    conn = Singleton<SSHClient>::Get()->Open
                    (FLAGS_ssh, SSHClient::ResponseCB(bind(&SSHTerminalController::ReadCB, this, _1, _2)), &connected_cb);
                    if (conn) SSHClient::SetTerminalWindowSize(conn, term->term_width, term->term_height);
                    if (conn) SSHClient::SetUser(conn, FLAGS_login);
#ifdef LFL_MOBILE
                    if (conn) SSHClient::SetPasswordCB(conn, Vault::LoadPassword, Vault::SavePassword);
#endif
                    }));
    return -1;
  }
  void Close() {
    if (!conn || conn->state != Connection::Connected) return;
    conn->SetError();
    network_thread->Write(new Callback([=](){ app->network.ConnClose(Singleton<SSHClient>::Get(), conn, NULL); }));
  }
};

struct ShellTerminalController : public MyTerminalController {
  Terminal *term=0;
  bool done=0, blocking=0;
  Shell shell;
  UnbackedTextGUI cmd;
  string buf, read_buf, ret_buf, prompt="> ", ssh_usage="\r\nusage: ssh -l user host[:port]", header;
  map<string, Callback> escapes = {
    { "OA", bind([&] { cmd.HistUp();   ReadCB(StrCat("\x0d", prompt, String::ToUTF8(cmd.cmd_line.Text16()), "\x1b[K")); }) },
    { "OB", bind([&] { cmd.HistDown(); ReadCB(StrCat("\x0d", prompt, String::ToUTF8(cmd.cmd_line.Text16()), "\x1b[K")); }) },
    { "OC", bind([&] { if (cmd.cursor.i.x < cmd.cmd_line.Size()) { ReadCB(string(1, cmd.cmd_line[cmd.cursor.i.x].Id())); cmd.CursorRight(); } }) },
    { "OD", bind([&] { if (cmd.cursor.i.x)                       { ReadCB("\x08");                                       cmd.CursorLeft();  } }) },
  };

  ShellTerminalController(const string &hdr) : header(StrCat(hdr, "LTerminal 1.0", ssh_usage, "\r\n\r\n")), cmd(Fonts::Default()) {
    shell.command.push_back(Shell::Command("ssh",      bind(&ShellTerminalController::MySSHCmd,      this, _1)));
    shell.command.push_back(Shell::Command("nslookup", bind(&ShellTerminalController::MyNSLookupCmd, this, _1)));
    shell.command.push_back(Shell::Command("help",     bind(&ShellTerminalController::MyHelpCmd,     this, _1)));
    cmd.runcb = bind(&Shell::Run, shell, _1);
    cmd.ReadHistory(LFAppDownloadDir(), "shell");
  }
  virtual ~ShellTerminalController() { cmd.WriteHistory(LFAppDownloadDir(), "shell", ""); }

  void MySSHCmd(const vector<string> &arg) {
    int ind = 0;
    for (; ind < arg.size() && arg[ind][0] == '-'; ind += 2) if (arg[ind] == "-l") FLAGS_login = arg[ind+1];
    if (ind < arg.size()) FLAGS_ssh = arg[ind];
    if (FLAGS_login.empty() || FLAGS_ssh.empty()) { if (term) term->Write(ssh_usage); }
    else done = 1;
  }
  void MyNSLookupCmd(const vector<string> &arg) {
    if (arg.empty() || !network_thread) return;
    if ((blocking = 1)) network_thread->Write(new Callback(bind(&ShellTerminalController::MyNetworkThreadNSLookup, this, arg[0])));
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
           "* nslookup host\r\n");
  }

  void UnBlockWithResponse(const string &t) { if (!(blocking=0)) ReadCB(StrCat(t, "\r\n", prompt)); }
  bool FrameOnKeyboardInput() const { return true; }
  void IOCtlWindowSize(int w, int h) {}
  int Open(Terminal *T) { (term=T)->Write(StrCat(header, prompt)); return -1; }
  StringPiece Read() { swap(read_buf, ret_buf); read_buf.clear(); return ret_buf; }
  void ReadCB(const StringPiece &b) {
    MyTerminalWindow *tw = (MyTerminalWindow*)screen->user1;
    if (tw->effects_mode) read_buf.append(b.data(), b.size());
    else if (term) term->Write(b);
  }
  int Write(const char *b, int l) {
    if (l > 1 && *b == '\x1b') {
      string escape(b+1, l-1);
      if (!FindAndDispatch(escapes, escape)) ERROR("unhandled escape: ", escape);
      return l;
    } else CHECK_EQ(1, l);

    bool cursor_last = cmd.cursor.i.x == cmd.cmd_line.Size();
    if      (*b == '\r') { if (1) { cmd.Enter(); ReadCB("\r\n" + ((!done && !blocking) ? prompt : ""));                 } }
    else if (*b == 0x7f) { if (cmd.cursor.i.x) { ReadCB((cursor_last ? "\b \b" : "\x08\x1b[1P"));        cmd.Erase();   } }
    else                 { if (1)              { ReadCB((cursor_last ? "" : "\x1b[1@") + string(1, *b)); cmd.Input(*b); } }

    unique_ptr<MyTerminalController> self;
    if (done) MyTerminalController::ChangeCurrent(MyTerminalController::NewSSHTerminalController(), &self);
    return l;
  }
};

void MyTerminalController::ChangeCurrent(MyTerminalController *new_controller_in, unique_ptr<MyTerminalController> *old_controller_out) {
  MyTerminalWindow *tw = (MyTerminalWindow*)screen->user1;
  tw->terminal->sink = new_controller_in;
  tw->controller.swap(*old_controller_out);
  tw->controller = unique_ptr<MyTerminalController>(new_controller_in);
  tw->OpenController();
}

MyTerminalController *MyTerminalController::NewSSHTerminalController()                  { InitSSHTerminalController(); return new SSHTerminalController(); }
MyTerminalController *MyTerminalController::NewShellTerminalController(const string &m) { InitSSHTerminalController(); return new ShellTerminalController(m); }
MyTerminalController *MyTerminalController::NewDefaultTerminalController() {
#if defined(WIN32) || defined(LFL_MOBILE)
  return NewShellTerminalController("");
#else
  return new PTYTerminalController();
#endif
}

int Frame(Window *W, unsigned clicks, unsigned mic_samples, bool cam_sample, int flag) {
  static const Time join_read_interval(100), refresh_interval(33);
  MyTerminalWindow *tw = (MyTerminalWindow*)W->user1;
  bool effects = tw->effects_mode, downscale = effects && downscale_effects > 1;
  Box draw_box = screen->Box();

  if (downscale) W->gd->RestoreViewport(DrawMode::_2D);
  tw->terminal->CheckResized(draw_box);
  int read_size = tw->ReadAndUpdateTerminalFramebuffer();
  if (downscale) {
    float scale = tw->activeshader->scale = 1.0 / downscale_effects;
    W->gd->ViewPort(Box(screen->width*scale, screen->height*scale));
  }

#if defined(__APPLE__) && !defined(LFL_MOBILE) && !defined(LFL_QT)
  if (!effects) {
    if (read_size && !(flag & LFApp::Frame::DontSkip)) {
      int *pending = &tw->join_read_pending;
      bool join_read = read_size > 255;
      if (join_read) { if (1            && ++(*pending)) { if (app->scheduler.WakeupIn(0, join_read_interval)) return -1; } }
      else           { if ((*pending)<1 && ++(*pending)) { if (app->scheduler.WakeupIn(0,   refresh_interval)) return -1; } }
    }
    app->scheduler.ClearWakeupIn();
  } else tw->read_pending = read_size;
#endif

  W->gd->DrawMode(DrawMode::_2D);
  W->gd->DisableBlend();
  tw->terminal->Draw(draw_box, true, effects ? tw->activeshader : NULL);
  if (effects) screen->gd->UseShader(0);

  W->DrawDialogs();
  if (FLAGS_draw_fps) Fonts::Default()->Draw(StringPrintf("FPS = %.2f", FPS()), point(W->width*.85, 0));
  if (FLAGS_screenshot.size()) ONCE(app->shell.screenshot(vector<string>(1, FLAGS_screenshot)); app->run=0;);
  tw->join_read_pending = tw->read_pending = 0;
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
    if ((screen->lfapp_console && screen->lfapp_console->active) || tw->controller->FrameOnKeyboardInput()) app->scheduler.AddWaitForeverKeyboard();
    else                                                                                                    app->scheduler.DelWaitForeverKeyboard();
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
  tw->activeshader = found ? &shader->second : &app->video.shader_default;
  tw->UpdateTargetFPS();
}
void MyEffectsControlsCmd(const vector<string>&) { 
  app->shell.Run("slider shadertoy_blend 1.0 0.01");
  app->scheduler.Wakeup(0);
}
void MyTransparencyControlsCmd(const vector<string>&) {
  static SliderDialog::UpdatedCB cb = SliderDialog::UpdatedCB(bind([=](Widget::Scrollbar *s){ Window::Get()->SetTransparency(s->Percent()); }, _1));
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
    { "ctrl", bind([&]{
      MyTerminalWindow *w = static_cast<MyTerminalWindow*>(screen->user1);
      if (auto ssh = dynamic_cast<SSHTerminalController*>(w->controller.get())) ssh->ctrl_down = !ssh->ctrl_down; }) } };
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
  // app->logfilename = StrCat(LFAppDownloadDir(), "lterm.txt");
  binds = new BindMap();
  MyWindowInitCB(screen);
  FLAGS_target_fps = 0;
  FLAGS_lfapp_video = FLAGS_lfapp_input = 1;
#ifdef LFL_MOBILE
  downscale_effects = TouchDevice::SetExtraScale(true);
#endif
}

extern "C" int main(int argc, const char *argv[]) {
  if (app->Create(argc, argv, __FILE__, LFAppCreateCB)) { app->Free(); return -1; }
  bool start_network_thread = !(FLAGS_lfapp_network_.override && !FLAGS_lfapp_network);
  app->video.splash_color = &Singleton<Terminal::SolarizedDarkColors>::Get()->c[Terminal::Colors::bg_index];

#ifdef WIN32
  Asset::cache["MenuAtlas,0,0,0,0,0.0000.glyphs.matrix"] = app->LoadResource(200);
  Asset::cache["MenuAtlas,0,0,0,0,0.0000.png"]           = app->LoadResource(201);
  Asset::cache["lfapp_vertex.glsl"]                      = app->LoadResource(202);
  Asset::cache["lfapp_pixel.glsl"]                       = app->LoadResource(203);
  Asset::cache["alien.glsl"]                             = app->LoadResource(204);
  Asset::cache["emboss.glsl"]                            = app->LoadResource(205);
  Asset::cache["fire.glsl"]                              = app->LoadResource(206);
  Asset::cache["fractal.glsl"]                           = app->LoadResource(207);
  Asset::cache["shrooms.glsl"]                           = app->LoadResource(208);
  Asset::cache["stormy.glsl"]                            = app->LoadResource(209);
  Asset::cache["twistery.glsl"]                          = app->LoadResource(210);
  Asset::cache["warper.glsl"]                            = app->LoadResource(211);
  Asset::cache["water.glsl"]                             = app->LoadResource(212);
  Asset::cache["waves.glsl"]                             = app->LoadResource(213);
  if (FLAGS_lfapp_console) {
    Asset::cache["VeraMoBd.ttf,32,255,255,255,4.0000.glyphs.matrix"] = app->LoadResource(214);
    Asset::cache["VeraMoBd.ttf,32,255,255,255,4.0000.png"]           = app->LoadResource(215);
  }
#endif

  if (app->Init()) { app->Free(); return -1; }
  CHECK(app->video.opengl_framebuffer);
  app->window_init_cb = MyWindowInitCB;
  app->window_closed_cb = MyWindowClosedCB;
  app->shell.command.push_back(Shell::Command("colors", bind(&MyColorsCmd, _1)));
  app->shell.command.push_back(Shell::Command("shader", bind(&MyShaderCmd, _1)));
  if (start_network_thread) {
#if !defined(LFL_MOBILE)
    render_process = new ProcessAPIClient();
    render_process->StartServer(StrCat(app->bindir, "lterm-render-sandbox", LocalFile::ExecutableSuffix));
#endif
    CHECK((network_thread = app->CreateNetworkThread(false)));
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
  app->AddNativeMenu("View", view_menu);
#endif

  vector<MenuItem> effects_menu{
    MenuItem{ "", "None",     "shader none"     }, MenuItem{ "", "Warper", "shader warper" }, MenuItem{ "", "Water", "shader water" },
    MenuItem{ "", "Twistery", "shader twistery" }, MenuItem{ "", "Fire",   "shader fire"   }, MenuItem{ "", "Waves", "shader waves" },
    MenuItem{ "", "Emboss",   "shader emboss"   }, MenuItem{ "", "Stormy", "shader stormy" }, MenuItem{ "", "Alien", "shader alien" },
    MenuItem{ "", "Fractal",  "shader fractal"  }, MenuItem{ "", "Shrooms", "shader shrooms" },
    MenuItem{ "", "<seperator>", "" }, MenuItem{ "", "Controls", "fxctl" } };
  app->AddNativeMenu("Effects", effects_menu);

  shader_map["warper"];  shader_map["water"];  shader_map["twistery"];
  shader_map["fire"];    shader_map["waves"];  shader_map["emboss"];
  shader_map["stormy"];  shader_map["alien"];  shader_map["fractal"];
  shader_map["shrooms"];

  MyTerminalController *controller = 0;
  if      (FLAGS_interpreter)  controller = MyTerminalController::NewShellTerminalController("");
  else if (!FLAGS_ssh.empty()) controller = MyTerminalController::NewSSHTerminalController();
  else                         controller = MyTerminalController::NewDefaultTerminalController();
  image_browser = new Browser();
  image_browser->doc.parser->render_process = render_process;
  MyTerminalWindow *tw = new MyTerminalWindow(controller);
  screen->user1 = tw;
  MyWindowStartCB(screen);
  SetFontSize(tw->font_size);
  new_win_width  = tw->terminal->font->FixedWidth() * tw->terminal->term_width;
  new_win_height = tw->terminal->font->Height()     * tw->terminal->term_height;
  tw->terminal->Draw(screen->Box(), false);

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

  app->scheduler.Start();
  return app->Main();
}
