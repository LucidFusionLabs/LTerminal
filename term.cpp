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
#include "core/app/net/rfb.h"
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

#if defined(__APPLE__) && !defined(LFL_MOBILE) && !defined(LFL_QT)
#define LFL_TERMINAL_JOIN_READS
#endif

namespace LFL {
#ifdef LFL_CRYPTO
DEFINE_string(ssh,             "",     "SSH to host");
DEFINE_string(login,           "",     "SSH user");
DEFINE_string(keyfile,         "",     "SSH private key");
DEFINE_bool  (compress,        false,  "SSH compression");
DEFINE_bool  (forward_agent,   false,  "SSH agent forwarding");
DEFINE_string(forward_local,   "",     "Forward local_port:remote_host:remote_port");
DEFINE_string(forward_remote,  "",     "Forward remote_port:local_host:local_port");
DEFINE_string(keygen,          "",     "Generate key");
DEFINE_int   (bits,            0,      "Generate key bits");      
#endif
DEFINE_bool  (interpreter,     false,  "Launch interpreter instead of shell");
DEFINE_string(term,            "",     "TERM var");
DEFINE_string(telnet,          "",     "Telnet to host");
DEFINE_string(vnc,             "",     "VNC to host");
DEFINE_string(command,         "",     "Execute initial command");
DEFINE_string(screenshot,      "",     "Screenshot and exit");
DEFINE_string(record,          "",     "Record session to file");
DEFINE_string(playback,        "",     "Playback recorded session file");
DEFINE_bool  (draw_fps,        false,  "Draw FPS");
DEFINE_bool  (resize_grid,     true,   "Resize window in glyph bound increments");
DEFINE_FLAG(dim, point, point(80,25),  "Initial terminal dimensions");
extern FlagOfType<bool> FLAGS_enable_network_;

#ifdef LFL_CRYPTO
static bool ParsePortForward(const string &text, vector<SSHClient::Params::Forward> *out) {
  vector<string> v;
  Split(text, isint<':'>, &v);
  if (v.size() != 3) return false;
  int port1 = atoi(v[0]), port2 = atoi(v[2]);
  if (port1 <= 0 || port2 <= 0) return false;
  out->push_back(SSHClient::Params::Forward{ port1, v[1], port2 });
  return true;
}
#endif

template <class X> struct TerminalWindowInterface : public GUI {
  TabbedDialog<X> tabs;
  TerminalWindowInterface(Window *W) : GUI(W), tabs(this) {}
  virtual void UpdateTargetFPS() = 0;
  virtual X *AddRFBTab(RFBClient::Params p, string) = 0;
};

struct MyTerminalMenus;
struct MyTerminalTab;
struct MyTerminalWindow;
inline MyTerminalWindow *GetActiveWindow() {
  if (auto w = app->focused) return w->GetOwnGUI<MyTerminalWindow>(0);
  else                       return nullptr;
}

struct MyAppState {
  unordered_map<string, Shader> shader_map;
  unique_ptr<Browser> image_browser;
  unique_ptr<SystemAlertView> passphrase_alert, passphraseconfirm_alert, passphrasefailed_alert, keypastefailed_alert;
  unique_ptr<SystemMenuView> edit_menu, view_menu, toys_menu;
  unique_ptr<MyTerminalMenus> menus;
  int new_win_width = FLAGS_dim.x*Fonts::InitFontWidth(), new_win_height = FLAGS_dim.y*Fonts::InitFontHeight();
  int downscale_effects = 1;
  virtual ~MyAppState();
} *my_app = nullptr;

struct PlaybackTerminalController : public TerminalControllerInterface {
  unique_ptr<FlatFile> playback;
  PlaybackTerminalController(TerminalTabInterface *p, unique_ptr<FlatFile> f) :
    TerminalControllerInterface(p), playback(move(f)) {}
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

struct PTYTerminalController : public TerminalControllerInterface {
  int fd = -1;
  ProcessPipe process;
  ReadBuffer read_buf;
  PTYTerminalController(TerminalTabInterface *p) : TerminalControllerInterface(p), read_buf(65536) {}
  virtual ~PTYTerminalController() {
    if (process.in) app->scheduler.DelMainWaitSocket(app->focused, fileno(process.in));
  }

  int Open(TextArea*) {
    if (FLAGS_term.empty()) setenv("TERM", (FLAGS_term = "xterm").c_str(), 1);
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
    if (NBRead(fd, &read_buf.data) < 0) { ERROR("PTYTerminalController read"); Close(); return ""; }
    return read_buf.data;
  }

  void Close() {
    if (process.in) {
      app->scheduler.DelMainWaitSocket(app->focused, fileno(process.in));
      process.Close();
    }
  }
};
#endif

#ifdef LFL_CRYPTO
struct SSHTerminalController : public NetworkTerminalController {
  typedef function<void(int, const string&)> SavehostCB;
  SSHClient::Params params;
  StringCB metakey_cb;
  SavehostCB savehost_cb;
  shared_ptr<SSHClient::Identity> identity;
  string fingerprint, password;
  int fingerprint_type=0;
  unordered_set<Socket> forward_fd;

  SSHTerminalController(TerminalTabInterface *p, Service *s, SSHClient::Params a, const Callback &ccb) :
    NetworkTerminalController(p, s, a.hostport, ccb), params(move(a)) {
    for (auto &f : params.forward_local) ForwardLocalPort(f.local, f.host, f.port);
  }

  virtual ~SSHTerminalController() {
    for (auto &fd : forward_fd) {
      app->scheduler.DelMainWaitSocket(parent->root, fd);
      SystemNetwork::CloseSocket(fd);
    }
  }

  int Open(TextArea *t) {
    Terminal *term = dynamic_cast<Terminal*>(t);
    SSHReadCB(0, StrCat("Connecting to ", params.user, "@", params.hostport, "\r\n"));
    app->RunInNetworkThread([=](){
      success_cb = bind(&SSHTerminalController::SSHLoginCB, this, term);
      conn = SSHClient::Open(params, SSHClient::ResponseCB
                             (bind(&SSHTerminalController::SSHReadCB, this, _1, _2)), &detach_cb, &success_cb);
      if (!conn) { app->RunInMainThread(bind(&NetworkTerminalController::Dispose, this)); return; }
      SSHClient::SetTerminalWindowSize(conn, term->term_width, term->term_height);
      SSHClient::SetCredentialCB(conn, bind(&SSHTerminalController::FingerprintCB, this, _1, _2),
                                 bind(&SSHTerminalController::LoadIdentityCB, this, _1),
                                 bind(&SSHTerminalController::LoadPasswordCB, this, _1));
    });
    return -1;
  }

  bool FingerprintCB(int hostkey_type, const StringPiece &in) {
    string type = SSH::Key::Name((fingerprint_type = hostkey_type));
    fingerprint = in.str();
    INFO(params.hostport, ": fingerprint: ", type, " ", "MD5", HexEscape(Crypto::MD5(fingerprint), ":"));
    return true;
  }

  bool LoadPasswordCB(string *out) {
    if (password.size()) { out->clear(); swap(*out, password); return true; }
    return false;
  }

  bool LoadIdentityCB(shared_ptr<SSHClient::Identity> *out) {
    if (identity) {
      *out = identity;
      return true;
    } else if (!FLAGS_keyfile.empty()) {
      INFO("Load keyfile ", FLAGS_keyfile);
      *out = make_shared<SSHClient::Identity>();
      if (!Crypto::ParsePEM(&LocalFile::FileContents(FLAGS_keyfile)[0], &(*out)->rsa, &(*out)->dsa, &(*out)->ec, &(*out)->ed25519,
                            [=](string v) { return my_app->passphrase_alert->RunModal(v); })) { (*out).reset(); return false; }
      return true;
    }
    return false;
  }

  void SSHLoginCB(Terminal *term) {
    SSHReadCB(0, "Connected.\r\n");
    if (savehost_cb) savehost_cb(fingerprint_type, fingerprint);
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
      if (metakey_cb) metakey_cb("ctrl");
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

  bool ForwardLocalPort(int port, const string &remote_h, int remote_p) {
    Socket fd = SystemNetwork::Listen(Protocol::TCP, IPV4::Parse("127.0.0.1"), port, 1, false);
    if (fd == InvalidSocket) return ERRORv(false, "listen ", port);
    app->scheduler.AddMainWaitSocket
      (parent->root, fd, SocketSet::READABLE, bind(&SSHTerminalController ::LocalForwardAcceptCB, this, fd, remote_h, remote_p));
    forward_fd.insert(fd);
    return true;
  }

  bool LocalForwardAcceptCB(Socket listen_fd, const string &remote_h, int remote_p) {
    IPV4::Addr accept_addr = 0;
    int accept_port = 0;
    Socket fd = SystemNetwork::Accept(listen_fd, &accept_addr, &accept_port);
    if (!conn || conn->state != Connection::Connected) { SystemNetwork::CloseSocket(fd); return ERRORv(false, "no conn"); }
    SSHClient::Channel *chan = SSHClient::OpenTCPChannel
      (conn, IPV4::Text(accept_addr), accept_port, remote_h, remote_p,
       bind(&SSHTerminalController::LocalForwardRemoteReadCB, this, fd, _1, _2, _3));
    if (!chan) { SystemNetwork::CloseSocket(fd); return ERRORv(false, "open chan"); } 
    app->scheduler.AddMainWaitSocket
      (parent->root, fd, SocketSet::READABLE, bind(&SSHTerminalController::LocalForwardLocalReadCB, this, fd, chan));
    forward_fd.insert(fd);
    return false;
  }

  bool LocalForwardLocalReadCB(Socket fd, SSHClient::Channel *chan) {
    string buf(4096, 0);
    int l = ::recv(fd, &buf[0], buf.size(), 0);
    if (l <= 0) return ERRORv(false, "recv");
    buf.resize(l);
    if (!chan->opened) chan->buf.append(buf);
    else if (!SSHClient::WriteToChannel(conn, chan, buf)) ERROR("write");
    return false;
  }

  int LocalForwardRemoteReadCB(Socket fd, Connection*, SSHClient::Channel *chan, const StringPiece &b) {
    if (!chan->opened) {
      app->scheduler.DelMainWaitSocket(parent->root, fd);
      SystemNetwork::CloseSocket(fd);
      forward_fd.erase(fd);
    } else if (!b.len) {
      if (chan->buf.size()) {
        if (!SSHClient::WriteToChannel(conn, chan, chan->buf)) return ERRORv(-1, "write");
        chan->buf.clear();
      }
    } else {
      if (::send(fd, b.data(), b.size(), 0) != b.size()) return ERRORv(-1, "write");
    }
    return 0;
  }
};
#endif

struct ShellTerminalController : public InteractiveTerminalController {
  string ssh_usage="\r\nusage: ssh -l user host[:port]";
  StringCB telnet_cb;
  function<void(RFBClient::Params)> vnc_cb;
#ifdef LFL_CRYPTO
  function<void(SSHClient::Params)> ssh_cb;
#endif

  ShellTerminalController(TerminalTabInterface *p, const string &hdr, StringCB tcb, StringVecCB ecb) :
    InteractiveTerminalController(p), telnet_cb(move(tcb)) {
    header = StrCat(hdr, "LTerminal 1.0", ssh_usage, "\r\n\r\n");
#ifdef LFL_CRYPTO
    shell.Add("ssh",      bind(&ShellTerminalController::MySSHCmd,      this, _1));
#endif
    shell.Add("vnc",      bind(&ShellTerminalController::MyVNCCmd,      this, _1));
    shell.Add("telnet",   bind(&ShellTerminalController::MyTelnetCmd,   this, _1));
    shell.Add("nslookup", bind(&ShellTerminalController::MyNSLookupCmd, this, _1));
    shell.Add("help",     bind(&ShellTerminalController::MyHelpCmd,     this, _1));
    shell.Add("exit",     move(ecb));
  }

#ifdef LFL_CRYPTO
  void MySSHCmd(const vector<string> &arg) {
    string host, login;
    ParseHostAndLogin(arg, &host, &login);
    if (login.empty() || host.empty()) { if (term) term->Write(ssh_usage); }
    else ssh_cb(SSHClient::Params{host, login, FLAGS_term, FLAGS_command, FLAGS_compress,
                FLAGS_forward_agent, false});
  }
#endif

  void MyVNCCmd(const vector<string> &arg) {
    string host, login;
    ParseHostAndLogin(arg, &host, &login);
    if (login.empty() || host.empty()) { if (term) term->Write("\r\nusage: vnc -l user host[:port]"); }
    else vnc_cb(RFBClient::Params{host, login});
  }

  void MyTelnetCmd(const vector<string> &arg) {
    if (arg.empty()) { if (term) term->Write("\r\nusage: telnet host"); }
    else telnet_cb(arg[0]);
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
              "* vnc -l user host[:port]\r\n"
              "* telnet host[:port]\r\n"
              "* nslookup host\r\n");
  }

  static void ParseHostAndLogin(const vector<string> &arg, string *host, string *login) {
    int ind = 0;
    for (; ind < arg.size() && arg[ind][0] == '-'; ind += 2) if (arg[ind] == "-l") *login = arg[ind+1];
    if (ind < arg.size()) *host = arg[ind];
  }
};

struct MyTerminalTab : public TerminalTab {
  TerminalWindowInterface<TerminalTabInterface> *parent;
  Shader *activeshader = &app->shaders->shader_default;
  Time join_read_interval = Time(100), refresh_interval = Time(33);
  int join_read_pending = 0;

  virtual ~MyTerminalTab() { root->DelGUI(terminal); }
  MyTerminalTab(Window *W, TerminalWindowInterface<TerminalTabInterface> *P) :
    TerminalTab(W, W->AddGUI(make_unique<Terminal>(nullptr, W, W->default_font, FLAGS_dim))), parent(P) {
    terminal->new_link_cb      = bind(&MyTerminalTab::NewLinkCB,   this, _1);
    terminal->hover_control_cb = bind(&MyTerminalTab::HoverLinkCB, this, _1);
    if (terminal->bg_color) W->gd->clear_color = *terminal->bg_color;
  }
  
  bool Animating() const { return activeshader != &app->shaders->shader_default; }

  void Draw() {
    Box draw_box = root->Box();
    bool effects = root->animating, downscale = effects && my_app->downscale_effects > 1;
    if (downscale) root->gd->RestoreViewport(DrawMode::_2D);
    terminal->CheckResized(draw_box);
    if (downscale) {
      float scale = activeshader->scale = 1.0 / my_app->downscale_effects;
      draw_box.y *= scale;
      draw_box.h -= terminal->extra_height * scale;
    } else root->gd->DrawMode(DrawMode::_2D);

    root->gd->DisableBlend();
    terminal->Draw(draw_box, downscale ? Terminal::DrawFlag::DrawCursor : Terminal::DrawFlag::Default,
                   effects ? activeshader : NULL);
    if (effects) root->gd->UseShader(0);
  }

  void SetFontSize(int n) {
    bool drew = false;
    root->default_font.desc.size = n;
    CHECK((terminal->style.font = root->default_font.Load()));
    int font_width  = terminal->style.font->FixedWidth(), new_width  = font_width  * terminal->term_width;
    int font_height = terminal->style.font->Height(),     new_height = font_height * terminal->term_height;
    if (FLAGS_resize_grid) root->SetResizeIncrements(font_width, font_height);
    if (new_width != root->width || new_height != root->height) drew = root->Reshape(new_width, new_height);
    if (!drew) terminal->Redraw(true, true);
    INFO("Font: ", app->fonts->DefaultFontEngine()->DebugString(terminal->style.font));
  }

  void UsePlaybackTerminalController(unique_ptr<FlatFile> f) {
    title = "Playback";
    ChangeController(make_unique<PlaybackTerminalController>(this, move(f)));
  }

  void UseShellTerminalController(const string &m) {
    title = "Interactive Shell";
    auto c = make_unique<ShellTerminalController>
      (this, m, [=](const string &h){ UseTelnetTerminalController(h); },
       [=](const StringVec&) { closed_cb(); });
#ifdef LFL_CRYPTO
    c->ssh_cb = [=](SSHClient::Params p){ UseSSHTerminalController(move(p)); };
#endif
    c->vnc_cb = [=](RFBClient::Params p){ parent->AddRFBTab(move(p), ""); };
    ChangeController(move(c));
  }

#ifdef LFL_CRYPTO
  void UseSSHTerminalController(SSHClient::Params params, const string &pw="", const string &pem="", StringCB metakey_cb=StringCB(),
                                SSHTerminalController::SavehostCB savehost_cb=SSHTerminalController::SavehostCB()) {
    title = StrCat("SSH ", params.user, "@", params.hostport);
    bool close_on_disconn = params.close_on_disconnect;
    auto ssh =
      make_unique<SSHTerminalController>
      (this, app->net->tcp_client.get(), move(params), 
       close_on_disconn ? closed_cb : [=](){ UseShellTerminalController("\r\nsession ended.\r\n\r\n\r\n"); });
    ssh->metakey_cb = move(metakey_cb);
    ssh->savehost_cb = move(savehost_cb);
    if (pw.size()) ssh->password = pw;
    if (pem.size()) {
      ssh->identity = make_shared<SSHClient::Identity>();
      Crypto::ParsePEM(pem.data(), &ssh->identity->rsa, &ssh->identity->dsa, &ssh->identity->ec, &ssh->identity->ed25519);
    }
    ChangeController(move(ssh));
  }
#endif

  void UseTelnetTerminalController(const string &hostport) {
    title = StrCat("Telnet ", hostport);
    ChangeController(make_unique<NetworkTerminalController>(this, app->net->tcp_client.get(), hostport,
                                                            [=](){ UseShellTerminalController("\r\nsession ended.\r\n\r\n\r\n"); }));
  }

  void UseDefaultTerminalController() {
#if defined(WIN32) || defined(LFL_MOBILE)
    UseShellTerminalController("");
#else
    ChangeController(make_unique<PTYTerminalController>(this));
#endif
  }

  void UseInitialTerminalController() {
    if      (FLAGS_playback.size()) return UsePlaybackTerminalController(make_unique<FlatFile>(FLAGS_playback));
    else if (FLAGS_interpreter)     return UseShellTerminalController("");
#ifdef LFL_CRYPTO
    else if (FLAGS_ssh.size()) {
      SSHClient::Params params{FLAGS_ssh, FLAGS_login, FLAGS_term, FLAGS_command, FLAGS_compress,
        FLAGS_forward_agent, 0};
      if (FLAGS_forward_local .size()) ParsePortForward(FLAGS_forward_local,  &params.forward_local);
      if (FLAGS_forward_remote.size()) ParsePortForward(FLAGS_forward_remote, &params.forward_remote);
      return UseSSHTerminalController(params);
    }
#endif
    else if (FLAGS_telnet.size()) return UseTelnetTerminalController(FLAGS_telnet);
    else                          return UseDefaultTerminalController();
  }

  void OpenedController() {
    if (FLAGS_command.size()) CHECK_EQ(FLAGS_command.size()+1, controller->Write(StrCat(FLAGS_command, "\n")));
  }

  bool ControllerReadableCB() {
    int read_size = ReadAndUpdateTerminalFramebuffer();
    if (!parent->root->animating) {
#ifdef LFL_TERMINAL_JOIN_READS
      if (read_size) {
        int *pending = &join_read_pending;
        bool join_read = read_size > 255;
        if (join_read) { if (1            && ++(*pending)) { if (app->scheduler.WakeupIn(parent->root, join_read_interval)) return false; } }
        else           { if ((*pending)<1 && ++(*pending)) { if (app->scheduler.WakeupIn(parent->root, refresh_interval))   return false; } }
        *pending = 0;
      }
#endif
    }
    return parent->tabs.top == this;
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
    root->gd->EnableBlend();
    root->gd->SetColor(Color::white - Color::Alpha(0.25));
    GraphicsContext::DrawTexturedBox1
      (root->gd, Box::DelBorder(root->Box(), root->width*.2, root->height*.2), tex->coord);
    root->gd->ClearDeferred();
  }

  void ChangeFont(const StringVec &arg) {
    if (arg.size() < 2) return app->ShowSystemFontChooser
      (root->default_font.desc, bind(&MyTerminalTab::ChangeFont, this, _1));
    if (arg.size() > 2) FLAGS_font_flag = atoi(arg[2]);
    root->default_font.desc.name = arg[0];
    SetFontSize(atof(arg[1]));
    app->scheduler.Wakeup(root);
  }

  void ChangeColors(const string &colors_name) {
    if      (colors_name == "vga")             terminal->ChangeColors(Singleton<Terminal::StandardVGAColors>   ::Get());
    else if (colors_name == "solarized_dark")  terminal->ChangeColors(Singleton<Terminal::SolarizedDarkColors> ::Get());
    else if (colors_name == "solarized_light") terminal->ChangeColors(Singleton<Terminal::SolarizedLightColors>::Get());
    if (terminal->bg_color) root->gd->clear_color = *terminal->bg_color;
    app->scheduler.Wakeup(root);
  }

  void ChangeShader(const string &shader_name) {
    auto shader = my_app->shader_map.find(shader_name);
    bool found = shader != my_app->shader_map.end();
    if (found && !shader->second.ID) Shader::CreateShaderToy(shader_name, Asset::FileContents(StrCat(shader_name, ".frag")), &shader->second);
    activeshader = found ? &shader->second : &app->shaders->shader_default;
    parent->UpdateTargetFPS();
  }

  void ShowEffectsControls() { 
    root->shell->Run("slider shadertoy_blend 1.0 0.01");
    app->scheduler.Wakeup(root);
  }
};

struct RFBTerminalController : public NetworkTerminalController, public KeyboardController, public MouseController {
  RFBClient::Params params;
  FrameBuffer *fb;
  string password;
  RFBTerminalController(TerminalTabInterface *p, Service *s, RFBClient::Params a, const Callback &ccb, FrameBuffer *f) :
    NetworkTerminalController(p, s, a.hostport, ccb), params(move(a)), fb(f) {}

  int Open(TextArea*) {
    INFO("Connecting to vnc://", params.user, "@", params.hostport);
    app->RunInNetworkThread([=](){
      success_cb = bind(&RFBTerminalController::RFBLoginCB, this);
      conn = RFBClient::Open(params, bind(&RFBTerminalController::LoadPasswordCB, this, _1), 
                             bind(&RFBTerminalController::RFBUpdateCB, this, _1, _2, _3, _4),
                             bind(&RFBTerminalController::RFBCopyCB, this, _1, _2, _3),
                             &detach_cb, &success_cb);
      if (!conn) { app->RunInMainThread(bind(&NetworkTerminalController::Dispose, this)); return; }
    });
    return -1;
  }

  StringPiece Read() {
    if (conn && conn->state == Connection::Connected && NBReadable(conn->socket)) {
      if (conn->Read() < 0)                                 { ERROR(conn->Name(), ": Read");       Close(); return ""; }
      if (conn->rb.size() && conn->handler->Read(conn) < 0) { ERROR(conn->Name(), ": query read"); Close(); return ""; }
    }
    return StringPiece();
  }

  int SendKeyEvent(InputEvent::Id event, bool down) {
    if (conn && conn->state == Connection::Connected)
      RFBClient::WriteKeyEvent(conn, InputEvent::GetKey(event), down);
    return 1;
  }

  int SendMouseEvent(InputEvent::Id id, const point &p, int down, int flag) {
    uint8_t buttons = app->input->MouseButton1Down() | app->input->MouseButton2Down()<<2;
    if (conn && conn->state == Connection::Connected)
      RFBClient::WritePointerEvent(conn, float(p.x) / parent->root->width * fb->width,
                                   (1.0 - float(p.y) / parent->root->height) * fb->height, buttons);
    return 1;
  }

  bool LoadPasswordCB(string *out) {
#ifdef LFL_MOBILE
    *out = move(password);
    return true;
#else
    *out = my_app->passphrase_alert->RunModal("");
    return true;
#endif
  }

  void RFBLoginCB() {}

  void RFBUpdateCB(Connection *c, const Box &b, int pf, const StringPiece &data) {
    if (!data.buf) {
      CHECK_EQ(0, b.x);
      CHECK_EQ(0, b.y);
      fb->Create(b.w, b.h, FrameBuffer::Flag::CreateTexture | FrameBuffer::Flag::ReleaseFB);
    } else fb->tex.UpdateGL(MakeUnsigned(data.buf), b, pf, Texture::Flag::FlipY); 
  }

  void RFBCopyCB(Connection *c, const Box &b, point copy_from) {
    fb->Attach();
    fb->tex.Bind();
    fb->gd->CopyTexSubImage2D(fb->tex.GLTexType(), 0, b.x, fb->tex.height - b.y - b.h,
                              copy_from.x, copy_from.y, b.w, b.h);
    fb->Release();
  }
};

struct MyRFBTab : public TerminalTabInterface {
  TerminalWindowInterface<TerminalTabInterface> *parent;
  FrameBuffer fb;
  RFBTerminalController *rfb;

  MyRFBTab(Window *W, TerminalWindowInterface<TerminalTabInterface> *P, RFBClient::Params a, string pw) :
    TerminalTabInterface(W, 1.0, 1.0), parent(P), fb(root->gd) {
    title = StrCat("VNC: ", a.hostport);
    auto c = make_unique<RFBTerminalController>(this, app->net->tcp_client.get(), move(a),
                                                [=](){ closed_cb(); }, &fb);
    rfb = c.get();
    rfb->password = move(pw);
    (controller = move(c))->Open(nullptr);
  }

  bool                Animating() const   { return false; }
  MouseController    *GetMouseTarget()    { return rfb; }
  KeyboardController *GetKeyboardTarget() { return rfb; }
  void SetFontSize(int) {}
  void ScrollDown() {}
  void ScrollUp() {}

  void Draw() {
    GraphicsContext gc(root->gd);
    Box draw_box = root->Box();
    root->gd->DisableBlend();
    fb.tex.DrawCrimped(root->gd, draw_box, 1, 0, 0);
  }

  int ReadAndUpdateTerminalFramebuffer() {
    if (!controller) return 0;
    return controller->Read().len;
  }
};

struct MyTerminalWindow : public TerminalWindowInterface<TerminalTabInterface> {
  MyTerminalWindow(Window *W) : TerminalWindowInterface(W) {}
  virtual ~MyTerminalWindow() { for (auto t : tabs.tabs) delete t; }

  MyTerminalTab *AddTerminalTab();
  TerminalTabInterface *AddRFBTab(RFBClient::Params p, string);
  void InitTab(TerminalTabInterface*);

  void CloseActiveTab() {
    TerminalTabInterface *tab = tabs.top;
    if (!tab) return;
    tab->deleted_cb();
  }

  int Frame(Window *W, unsigned clicks, int flag) {
#ifdef LFL_TERMINAL_JOIN_READS
    app->scheduler.ClearWakeupIn(root);
#endif
    if (tabs.top) tabs.top->Draw();
    W->DrawDialogs();
    if (FLAGS_draw_fps) W->default_font->Draw(StringPrintf("FPS = %.2f", app->FPS()), point(W->width*.85, 0));
    if (FLAGS_screenshot.size()) ONCE(W->shell->screenshot(vector<string>(1, FLAGS_screenshot)); app->run=0;);
    return 0;
  }

  void ConsoleAnimatingCB() { 
    UpdateTargetFPS();
    if (!root->console || !root->console->animating) {
      if ((root->console && root->console->Active()) || tabs.top->controller->frame_on_keyboard_input) app->scheduler.AddMainWaitKeyboard(root);
      else                                                                                             app->scheduler.DelMainWaitKeyboard(root);
    }
  }

  void UpdateTargetFPS() {
    bool animating = tabs.top->Animating() || (root->console && root->console->animating);
    app->scheduler.SetAnimating(root, animating);
    if (my_app->downscale_effects) app->SetDownScale(animating);
  }

  void ShowTransparencyControls() {
    SliderDialog::UpdatedCB cb(bind([=](Widget::Slider *s){ root->SetTransparency(s->Percent()); }, _1));
    root->AddDialog(make_unique<SliderDialog>(root, "window transparency", cb, 0, 1.0, .025));
    app->scheduler.Wakeup(root);
  }
};

inline TerminalTabInterface *GetActiveTab() { return GetActiveWindow()->tabs.top; }
inline MyTerminalTab *GetActiveTerminalTab() { return dynamic_cast<MyTerminalTab*>(GetActiveTab()); }

#ifdef LFL_MOBILE
}; // namespace LFL
#include "term_menu.h"
#include "term_menu.cpp"
namespace LFL {
#else
struct MyTerminalMenus { int unused; };
#endif

MyAppState::~MyAppState() {}
  
MyTerminalTab *MyTerminalWindow::AddTerminalTab() {
  auto t = new MyTerminalTab(root, this);
#ifdef LFL_MOBILE
  t->terminal->line_fb.align_top_or_bot = t->terminal->cmd_fb.align_top_or_bot = true;
#endif
  InitTab(t);
  return t;
}

TerminalTabInterface *MyTerminalWindow::AddRFBTab(RFBClient::Params p, string pw) {
  auto t = new MyRFBTab(root, this, move(p), move(pw));
  InitTab(t);
  return t;
}

void MyTerminalWindow::InitTab(TerminalTabInterface *t) {
#ifdef LFL_MOBILE
  t->closed_cb = [=]() {
    t->deleted_cb();
    if (!tabs.top) {
      my_app->menus->keyboard_toolbar->Show(false);
      my_app->menus->hosts.view->DelNavigationButton(HAlign::Left);
      my_app->menus->hosts_nav->Show(true);
    }
  };
#else
  t->closed_cb = [](){ LFAppShutdown(); };
#endif
  t->deleted_cb = [=](){ tabs.DelTab(t); app->RunInMainThread([=]{ delete t; }); };
  tabs.AddTab(t);
}

void MyWindowInit(Window *W) {
  W->width = my_app->new_win_width;
  W->height = my_app->new_win_height;
  W->caption = app->name;
}

void MyWindowStart(Window *W) {
  CHECK(W->gd->have_framebuffer);
  CHECK_EQ(0, W->NewGUI());
  auto tw = W->ReplaceGUI(0, make_unique<MyTerminalWindow>(W));
  if (FLAGS_console) W->InitConsole(bind(&MyTerminalWindow::ConsoleAnimatingCB, tw));
  W->frame_cb = bind(&MyTerminalWindow::Frame, tw, _1, _2, _3);
  W->default_controller = [=]() -> MouseController* { if (auto t = GetActiveTab()) return t->GetMouseTarget();    return nullptr; };
  W->default_textbox = [=]() -> KeyboardController* { if (auto t = GetActiveTab()) return t->GetKeyboardTarget(); return nullptr; };
  W->shell = make_unique<Shell>(W);
  if (my_app->image_browser) W->shell->AddBrowserCommands(my_app->image_browser.get());

#ifndef LFL_MOBILE                                                                                 
  app->scheduler.AddMainWaitMouse(W);
  TerminalTabInterface *t = nullptr;
  ONCE_ELSE({ if (FLAGS_vnc.size()) t = tw->AddRFBTab(RFBClient::Params{FLAGS_vnc, FLAGS_login}, "");
              else { auto tt = tw->AddTerminalTab(); tt->UseInitialTerminalController(); t=tt; }
              },   { auto tt = tw->AddTerminalTab(); tt->UseDefaultTerminalController(); t=tt; });
  if (FLAGS_resize_grid)
    if (auto tt = dynamic_cast<MyTerminalTab*>(t))
      W->SetResizeIncrements(tt->terminal->style.font->FixedWidth(),
                             tt->terminal->style.font->Height());

  BindMap *binds = W->AddInputController(make_unique<BindMap>());
#ifndef WIN32
  binds->Add('n',       Key::Modifier::Cmd, Bind::CB(bind(&Application::CreateNewWindow, app)));
#endif                  
  binds->Add('t',       Key::Modifier::Cmd, Bind::CB(bind([=](){ tw->AddTerminalTab()->UseDefaultTerminalController(); })));
  binds->Add('w',       Key::Modifier::Cmd, Bind::CB(bind([=](){ tw->CloseActiveTab();      app->scheduler.Wakeup(W); })));
  binds->Add(']',       Key::Modifier::Cmd, Bind::CB(bind([=](){ tw->tabs.SelectNextTab();  app->scheduler.Wakeup(W); })));
  binds->Add('[',       Key::Modifier::Cmd, Bind::CB(bind([=](){ tw->tabs.SelectPrevTab();  app->scheduler.Wakeup(W); })));
  binds->Add(Key::Up,   Key::Modifier::Cmd, Bind::CB(bind([=](){ t->ScrollUp();             app->scheduler.Wakeup(W); })));
  binds->Add(Key::Down, Key::Modifier::Cmd, Bind::CB(bind([=](){ t->ScrollDown();           app->scheduler.Wakeup(W); })));
  binds->Add('=',       Key::Modifier::Cmd, Bind::CB(bind([=](){ t->SetFontSize(W->default_font.desc.size + 1); })));
  binds->Add('-',       Key::Modifier::Cmd, Bind::CB(bind([=](){ t->SetFontSize(W->default_font.desc.size - 1); })));
  binds->Add('6',       Key::Modifier::Cmd, Bind::CB(bind([=](){ W->shell->console(vector<string>()); })));
#endif
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
  my_app = new MyAppState();
  app->focused = new Window();
  app->name = "LTerminal";
  app->exit_cb = []() { delete my_app; };
  app->window_closed_cb = MyWindowClosed;
  app->window_start_cb = MyWindowStart;
  app->window_init_cb = MyWindowInit;
  app->window_init_cb(app->focused);
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
#ifdef WIN32
  app->input->paste_bind = Bind(Mouse::Button::_2);
#endif

  my_app->image_browser = make_unique<Browser>();
  my_app->passphrase_alert = make_unique<SystemAlertView>(AlertItemVec{
    { "style", "pwinput" }, { "Passphrase", "Passphrase" }, { "Cancel", "" }, { "Continue", "" } });
  my_app->passphraseconfirm_alert = make_unique<SystemAlertView>(AlertItemVec{
    { "style", "pwinput" }, { "Passphrase", "Confirm Passphrase" }, { "Cancel", "" }, { "Continue", "" } });
  my_app->passphrasefailed_alert = make_unique<SystemAlertView>(AlertItemVec{
    { "style", "" }, { "Invalid passphrase", "Passphrase failed" }, { "", "" }, { "Continue", "" } });
  my_app->keypastefailed_alert = make_unique<SystemAlertView>(AlertItemVec{
    { "style", "" }, { "Paste key failed", "Load key failed" }, { "", "" }, { "Continue", "" } });
#ifndef LFL_MOBILE
  my_app->edit_menu = SystemMenuView::CreateEditMenu(vector<MenuItem>());
  my_app->view_menu = make_unique<SystemMenuView>("View", MenuItemVec{
#ifdef __APPLE__
    MenuItem{ "=", "Zoom In" },
    MenuItem{ "-", "Zoom Out" },
#endif
    MenuItem{ "", "Fonts",        [=](){ if (auto t = GetActiveTerminalTab()) t->ChangeFont(StringVec()); } },
    MenuItem{ "", "Transparency", [=](){ if (auto w = GetActiveWindow()) w->ShowTransparencyControls(); } },
    MenuItem{ "", "VGA Colors",             [=](){ if (auto t = GetActiveTerminalTab()) t->ChangeColors("vga");             } },
    MenuItem{ "", "Solarized Dark Colors",  [=](){ if (auto t = GetActiveTerminalTab()) t->ChangeColors("solarized_dark");  } },
    MenuItem{ "", "Solarized Light Colors", [=](){ if (auto t = GetActiveTerminalTab()) t->ChangeColors("solarized_light"); } }
  });
  if (FLAGS_term.empty()) FLAGS_term = BlankNull(getenv("TERM"));
#endif

  my_app->toys_menu = make_unique<SystemMenuView>("Toys", vector<MenuItem>{
    MenuItem{ "", "None",         [=](){ if (auto t = GetActiveTerminalTab()) t->ChangeShader("none");     } },
    MenuItem{ "", "Warper",       [=](){ if (auto t = GetActiveTerminalTab()) t->ChangeShader("warper");   } },
    MenuItem{ "", "Water",        [=](){ if (auto t = GetActiveTerminalTab()) t->ChangeShader("water");    } },
    MenuItem{ "", "Twistery",     [=](){ if (auto t = GetActiveTerminalTab()) t->ChangeShader("twistery"); } },
    MenuItem{ "", "Fire",         [=](){ if (auto t = GetActiveTerminalTab()) t->ChangeShader("fire");     } },
    MenuItem{ "", "Waves",        [=](){ if (auto t = GetActiveTerminalTab()) t->ChangeShader("waves");    } },
    MenuItem{ "", "Emboss",       [=](){ if (auto t = GetActiveTerminalTab()) t->ChangeShader("emboss");   } },
    MenuItem{ "", "Stormy",       [=](){ if (auto t = GetActiveTerminalTab()) t->ChangeShader("stormy");   } },
    MenuItem{ "", "Alien",        [=](){ if (auto t = GetActiveTerminalTab()) t->ChangeShader("alien");    } },
    MenuItem{ "", "Fractal",      [=](){ if (auto t = GetActiveTerminalTab()) t->ChangeShader("fractal");  } },
    MenuItem{ "", "Darkly",       [=](){ if (auto t = GetActiveTerminalTab()) t->ChangeShader("darkly");   } },
    MenuItem{ "", "<separator>" },
    MenuItem{ "", "Controls",     [=](){ if (auto t = GetActiveTerminalTab()) t->ShowEffectsControls(); } } });

  MakeValueTuple(&my_app->shader_map, "warper", "water", "twistery");
  MakeValueTuple(&my_app->shader_map, "warper", "water", "twistery");
  MakeValueTuple(&my_app->shader_map, "fire",   "waves", "emboss");
  MakeValueTuple(&my_app->shader_map, "stormy", "alien", "fractal");
  MakeValueTuple(&my_app->shader_map, "darkly");

#ifdef LFL_CRYPTO
  Crypto::PublicKeyInit();
  if (FLAGS_keygen.size()) {
    string pw = my_app->passphrase_alert->RunModal(""), fn="identity", pubkey, privkey;
    if (!Crypto::GenerateKey(FLAGS_keygen, FLAGS_bits, pw, "", &pubkey, &privkey))
      return ERRORv(-1, "keygen ", FLAGS_keygen, " bits=", FLAGS_bits, ": failed");
    LocalFile::WriteFile(fn, privkey);
    LocalFile::WriteFile(StrCat(fn, ".pub"), pubkey);
    INFO("Wrote ", fn, " and ", fn, ".pub");
    return 1;
  }
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

  app->StartNewWindow(app->focused);
#ifdef LFL_MOBILE
  my_app->menus = make_unique<MyTerminalMenus>();
  my_app->menus->hosts_nav->Show(true);
#else
  auto tw = GetActiveWindow();
  if (auto t = dynamic_cast<MyTerminalTab*>(tw->tabs.top)) {
    my_app->new_win_width  = t->terminal->style.font->FixedWidth() * t->terminal->term_width;
    my_app->new_win_height = t->terminal->style.font->Height()     * t->terminal->term_height;
    if (FLAGS_record.size()) t->record = make_unique<FlatFile>(FLAGS_record);
    t->terminal->Draw(app->focused->Box());
    INFO("Starting ", app->name, " ", app->focused->default_font.desc.name, " (w=", t->terminal->style.font->FixedWidth(),
         ", h=", t->terminal->style.font->Height(), ", scale=", my_app->downscale_effects, ")");
  }
#endif

  return app->Main();
}
