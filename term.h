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

#ifndef LFL_TERM_TERM_H__
#define LFL_TERM_TERM_H__
namespace LFL {

struct TerminalTabInterface : public Dialog {
  string title;
  Callback closed_cb;
  unique_ptr<Terminal::Controller> controller, last_controller;
  Shader *activeshader = &app->shaders->shader_default;
  int connected_host_id=0;
  TerminalTabInterface(Window *W, float w, float h, int flag, int host_id) : Dialog(W,w,h,flag), connected_host_id(host_id) {}

  virtual int ReadAndUpdateTerminalFramebuffer() = 0;
  virtual bool ControllerReadableCB() { ReadAndUpdateTerminalFramebuffer(); return true; }
  virtual void SetFontSize(int) = 0;
  virtual void ScrollUp() = 0;
  virtual void ScrollDown() = 0;
  virtual void UpdateTargetFPS() = 0;
  virtual MouseController *GetMouseTarget() = 0;
  virtual KeyboardController *GetKeyboardTarget() = 0;
  virtual Box GetLastDrawBox() = 0;
  virtual void DrawBox(GraphicsDevice*, Box draw_box, bool check_resized) = 0;
  virtual void LoseFocus() { root->active_textbox = root->default_textbox(); root->active_controller = root->default_controller(); }
  virtual void TakeFocus() { root->active_textbox = GetKeyboardTarget();     root->active_controller = GetMouseTarget(); UpdateControllerWait(); }
  virtual void UpdateControllerWait() {
    if (controller && controller->frame_on_keyboard_input) app->scheduler.AddMainWaitKeyboard(root);
    else                                                   app->scheduler.DelMainWaitKeyboard(root);
  }

  virtual bool Animating() const { return Effects(); }
  virtual bool Effects() const { return activeshader != &app->shaders->shader_default; }
  virtual void ChangeShader(const string &shader_name) {}

  virtual void ShowEffectsControls() { 
    root->shell->Run("slider shadertoy_blend 1.0 0.01");
    app->scheduler.Wakeup(root);
  }

  virtual int PrepareEffects(Box *draw_box, int downscale_effects, int extra_height=0) {
    if (root->gd->attached_framebuffer) return 0;
    if (!root->animating) { root->gd->DrawMode(DrawMode::_2D); return 0; }
    if (Effects() && downscale_effects > 1) {
      root->gd->RestoreViewport(DrawMode::_2D);
      float scale = activeshader->scale = 1.0 / downscale_effects;
      draw_box->y *= scale;
      draw_box->h -= extra_height * scale;
    }
    return downscale_effects;
  }
};

struct TerminalControllerInterface : public Terminal::Controller {
  TerminalTabInterface *parent;
  StringCB metakey_cb;
  TerminalControllerInterface(TerminalTabInterface *P) : parent(P) {}

#ifndef LFL_TERMINAL_MENUS
  StringPiece GetMetaModified(const StringPiece &b, char*) { return b; }
#else
  StringPiece GetMetaModified(const StringPiece &b, char *buf) {
    if (b.size() == 1 && ctrl_down && !(ctrl_down = false)) {
      if (metakey_cb) metakey_cb("ctrl");
      return StringPiece(&(buf[0] = Key::CtrlModified(*MakeUnsigned(b.data()))), 1);
    } else return b;
  }
#endif
};
  
struct NetworkTerminalController : public TerminalControllerInterface {
  Connection *conn=0;
  Connection::CB detach_cb;
  Callback close_cb, success_cb;
  string remote, read_buf, ret_buf;
  bool background_services=true, success_on_connect=false;
  NetworkTerminalController(TerminalTabInterface *p, const string &r, const Callback &ccb) :
    TerminalControllerInterface(p), detach_cb(bind(&NetworkTerminalController::ConnectedCB, this)),
    close_cb(ccb), remote(r) {}
  virtual ~NetworkTerminalController() { close_cb=Callback(); Close(); }

  virtual Socket Open(TextArea *t) {
    if (remote.empty()) return InvalidSocket;
    t->Write(StrCat("Connecting to ", remote, "\r\n"));
    app->RunInNetworkThread([=](){
      if (!(conn = app->ConnectTCP(remote, 0, &detach_cb, background_services)))
        if (app->network_thread) app->RunInMainThread([=](){ Close(); }); });
    return app->network_thread ? InvalidSocket : (conn ? conn->GetSocket() : InvalidSocket);
  }

  virtual void Close() {
    if (!conn || conn->state != Connection::Connected) return;
    conn->RemoveFromMainWait(parent->root);
    conn->Close();
    conn = 0;
    if (close_cb) close_cb();
  }

  virtual void ConnectedCB() {
    conn->AddToMainWait(parent->root, bind(&TerminalTabInterface::ControllerReadableCB, parent));
    if (success_on_connect && success_cb) success_cb();
  }

  virtual StringPiece Read() {
    if (!conn || conn->state != Connection::Connected) return StringPiece();
    if (conn->Read() < 0) { ERROR(conn->Name(), ": Read"); Close(); return StringPiece(); }
    read_buf.append(conn->rb.begin(), conn->rb.size());
    conn->rb.Flush(conn->rb.size());
    swap(read_buf, ret_buf);
    read_buf.clear();
    return ret_buf;
  }

  virtual int Write(const StringPiece &in) {
    char buf[1];
    if (!conn || conn->state != Connection::Connected) return -1;
    StringPiece b = GetMetaModified(in, buf);
    return conn->WriteFlush(b.data(), b.size());
  }
};

struct InteractiveTerminalController : public TerminalControllerInterface {
  TextArea *term=0;
  Shell shell;
  UnbackedTextBox cmd;
  string buf, prompt="> ", header;
  bool blocking=0, done=0;
  char enter_char = '\r';
  unordered_map<string, Callback> escapes = {
    { "OA", bind([&] { cmd.HistUp();   WriteText(StrCat("\x0d", prompt, String::ToUTF8(cmd.cmd_line.Text16()), "\x1b[K")); }) },
    { "OB", bind([&] { cmd.HistDown(); WriteText(StrCat("\x0d", prompt, String::ToUTF8(cmd.cmd_line.Text16()), "\x1b[K")); }) },
    { "OC", bind([&] { if (cmd.cursor.i.x < cmd.cmd_line.Size()) { WriteText(string(1, cmd.cmd_line[cmd.cursor.i.x].Id())); cmd.CursorRight(); } }) },
    { "OD", bind([&] { if (cmd.cursor.i.x)                       { WriteText("\x08");                                       cmd.CursorLeft();  } }) },
  };

  InteractiveTerminalController(TerminalTabInterface *p) :
    TerminalControllerInterface(p), shell(nullptr), cmd(FontDesc::Default()) {
    cmd.runcb = bind(&Shell::Run, &shell, _1);
    cmd.ReadHistory(app->savedir, "shell");
    frame_on_keyboard_input = true;
  }
  virtual ~InteractiveTerminalController() { cmd.WriteHistory(app->savedir, "shell", ""); }

  StringPiece Read() { return ""; }
  Socket Open(TextArea *T) { (term=T)->Write(StrCat(header, prompt)); return InvalidSocket; }
  void UnBlockWithResponse(const string &t) { blocking=0; WriteText(StrCat(t, "\r\n", prompt)); }

  void IOCtlWindowSize(int w, int h) {}
  int Write(const StringPiece &buf) {
    int l = buf.size();
    const char *b = buf.data();

    if (l > 1 && *b == '\x1b') {
      string escape(b+1, l-1);
      if (!FindAndDispatch(escapes, escape)) ERROR("unhandled escape: ", escape);
      return l;
    } else CHECK_EQ(1, l);

    bool cursor_last = cmd.cursor.i.x == cmd.cmd_line.Size();
    if      (*b == enter_char) { if (1) { cmd.Enter(); WriteText("\r\n" + ((!done && !blocking) ? prompt : "")); } }
    else if (*b == 0x7f)       { if (cmd.cursor.i.x) { WriteText((cursor_last ? "\b \b" : "\x08\x1b[1P"));        cmd.Erase();   } }
    else                       { if (1)              { WriteText((cursor_last ? "" : "\x1b[1@") + string(1, *b)); cmd.Input(*b); } }
    return l;
  }

  void WriteText(const StringPiece &b) { if (term) term->Write(b); }
};

struct PlaybackTerminalController : public TerminalControllerInterface {
  unique_ptr<FlatFile> playback;
  PlaybackTerminalController(TerminalTabInterface *p, unique_ptr<FlatFile> f) :
    TerminalControllerInterface(p), playback(move(f)) {}
  Socket Open(TextArea*) { return InvalidSocket; }
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

#ifdef LFL_PTY
struct ReadBuffer {
  int size;
  Time stamp;
  string data;
  ReadBuffer(int S=0) : size(S), stamp(Now()), data(S, 0) {}
  void Reset() { stamp=Now(); data.resize(size); }
};

struct PTYTerminalController : public TerminalControllerInterface {
  Socket fd = -1;
  ProcessPipe process;
  ReadBuffer read_buf;
  PTYTerminalController(TerminalTabInterface *p) : TerminalControllerInterface(p), read_buf(65536) {}
  virtual ~PTYTerminalController() {
    if (process.in) app->scheduler.DelMainWaitSocket(app->focused, fileno(process.in));
  }

  Socket Open(TextArea*) {
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
#endif // LFL_PTY

#ifdef LFL_CRYPTO
struct SSHTerminalController : public NetworkTerminalController {
  typedef function<void(int, const string&)> SavehostCB;
  SSHClient::Params params;
  SavehostCB savehost_cb;
  Connection::CB remote_forward_detach_cb;
  SSHClient::FingerprintCB fingerprint_cb;
  SSHClient::LoadIdentityCB identity_cb;
  string fingerprint, password;
  int fingerprint_type=0;
  unordered_set<Socket> forward_fd;
  SystemAlertView *passphrase_alert=0;

  SSHTerminalController(TerminalTabInterface *p, SSHClient::Params a, const Callback &ccb) :
    NetworkTerminalController(p, a.hostport, ccb), params(move(a)),
    remote_forward_detach_cb(bind(&SSHTerminalController::RemotePortForwardConnectCB, this, _1)) {
    for (auto &f : params.forward_local) ForwardLocalPort(f.port, f.target_host, f.target_port);
  }

  virtual ~SSHTerminalController() {
    for (auto &fd : forward_fd) {
      app->scheduler.DelMainWaitSocket(parent->root, fd);
      SystemNetwork::CloseSocket(fd);
    }
  }

  Socket Open(TextArea *t) {
    Terminal *term = dynamic_cast<Terminal*>(t);
    SSHReadCB(0, StrCat("Connecting to ", params.user, "@", params.hostport, "\r\n"));
    params.background_services = background_services;
    app->RunInNetworkThread([=](){
      success_cb = bind(&SSHTerminalController::SSHLoginCB, this, term);
      conn = SSHClient::Open(params, SSHClient::ResponseCB
                             (bind(&SSHTerminalController::SSHReadCB, this, _1, _2)), &detach_cb, &success_cb);
      if (!conn) { app->RunInMainThread(bind(&NetworkTerminalController::Dispose, this)); return; }
      SSHClient::SetTerminalWindowSize(conn, term->term_width, term->term_height);
      SSHClient::SetCredentialCB(conn, bind(&SSHTerminalController::FingerprintCB, this, _1, _2),
                                 bind(&SSHTerminalController::LoadIdentityCB, this, _1),
                                 bind(&SSHTerminalController::LoadPasswordCB, this, _1));
      SSHClient::SetRemoteForwardCB(conn, bind(&SSHTerminalController::RemotePortForwardAcceptCB, this, _1, _2, _3, _4, _5));
    });
    return InvalidSocket;
  }

  bool FingerprintCB(int hostkey_type, const StringPiece &in) {
    string type = SSH::Key::Name((fingerprint_type = hostkey_type));
    fingerprint = in.str();
    INFO(params.hostport, ": fingerprint: ", type, " ", "MD5", HexEscape(Crypto::MD5(fingerprint), ":"));
    return fingerprint_cb ? fingerprint_cb(fingerprint_type, fingerprint) : true;
  }

  bool LoadIdentityCB(shared_ptr<SSHClient::Identity> *out) { return identity_cb ? identity_cb(out) : true; }
  bool LoadPasswordCB(string *out) {
    if (password.size()) { out->clear(); swap(*out, password); return true; }
    else {
      if (passphrase_alert) passphrase_alert->ShowCB
        ("Password", "Password", "", [=](const string &pw){ SSHClient::WritePassword(conn, pw); });
      return false;
    } 
  }

  void SSHLoginCB(Terminal *term) {
    SSHReadCB(0, "Connected.\r\n");
    if (savehost_cb) savehost_cb(fingerprint_type, fingerprint);
  }

  void SSHReadCB(Connection *c, const StringPiece &b) { 
    if (b.empty()) Close();
    else read_buf.append(b.data(), b.size());
  }

  void IOCtlWindowSize(int w, int h) {
    if (conn && conn->handler) SSHClient::SetTerminalWindowSize(conn, w, h);
  }

  int Write(const StringPiece &in) {
    char buf[1];
    if (!conn || conn->state != Connection::Connected) return -1;
    StringPiece b = GetMetaModified(in, buf);
    return SSHClient::WriteChannelData(conn, b);
  }

  StringPiece Read() {
    if (conn && conn->state == Connection::Connected) {
      if (conn->Read() < 0)                                 { ERROR(conn->Name(), ": Read");       Close(); return ""; }
      if (conn->rb.size() && conn->handler->Read(conn) < 0) { ERROR(conn->Name(), ": query read"); Close(); return ""; }
    }
    swap(read_buf, ret_buf);
    read_buf.clear();
    return ret_buf;
  }

  bool ForwardLocalPort(int port, const string &target_h, int target_p) {
    Socket fd = SystemNetwork::Listen(Protocol::TCP, IPV4::Parse("127.0.0.1"), port, 1, false);
    if (fd == InvalidSocket) return ERRORv(false, "listen ", port);
    app->scheduler.AddMainWaitSocket
      (parent->root, fd, SocketSet::READABLE, bind(&SSHTerminalController::LocalPortForwardAcceptCB, this, fd, target_h, target_p));
    forward_fd.insert(fd);
    return true;
  }

  bool LocalPortForwardAcceptCB(Socket listen_fd, const string &target_h, int target_p) {
    int accept_port = 0;
    IPV4::Addr accept_addr = 0;
    Socket fd = SystemNetwork::Accept(listen_fd, &accept_addr, &accept_port);
    if (!conn || conn->state != Connection::Connected) { SystemNetwork::CloseSocket(fd); return ERRORv(false, "no conn"); }
    SSHClient::Channel *chan = SSHClient::OpenTCPChannel
      (conn, IPV4::Text(accept_addr), accept_port, target_h, target_p,
       bind(&SSHTerminalController::PortForwardRemoteReadCB, this, fd, _1, _2, _3));
    if (!chan) { SystemNetwork::CloseSocket(fd); return ERRORv(false, "open chan"); } 
    app->scheduler.AddMainWaitSocket
      (parent->root, fd, SocketSet::READABLE, bind(&SSHTerminalController::PortForwardLocalReadCB, this, fd, chan));
    forward_fd.insert(fd);
    return false;
  }

  void RemotePortForwardAcceptCB(SSHClient::Channel *chan, const string &target_h, int target_p,
                                 const string &local_h, int local_p) {
    INFO("Forwarding ", local_h, ":", local_p, " -> ", target_h, ":", target_p);
    chan->cb = bind(&SSHTerminalController::PortForwardRemoteReadCB, this, 0, _1, _2, _3);
    app->RunInNetworkThread([=](){
      if (auto c = app->ConnectTCP(target_h, target_p, &remote_forward_detach_cb, false)) c->data = chan; 
    });
  }

  void RemotePortForwardConnectCB(Connection *c) {
    unique_ptr<SocketConnection> conn(dynamic_cast<SocketConnection*>(c));
    Socket fd = conn->socket;
    auto chan = static_cast<SSHClient::Channel*>(conn->data);
    chan->cb = bind(&SSHTerminalController::PortForwardRemoteReadCB, this, fd, _1, _2, _3);
    app->scheduler.AddMainWaitSocket
      (parent->root, fd, SocketSet::READABLE, bind(&SSHTerminalController::PortForwardLocalReadCB, this, fd, chan));
    forward_fd.insert(fd);
    if (chan->buf.size()) {
      if (::send(fd, chan->buf.data(), chan->buf.size(), 0) != chan->buf.size()) PortForwardLocalCloseCB(fd, chan);
      chan->buf.clear();
    }
  }

  bool PortForwardLocalReadCB(Socket fd, SSHClient::Channel *chan) {
    string buf(4096, 0);
    int l = ::recv(fd, &buf[0], buf.size(), 0);
    if (l <= 0) { PortForwardLocalCloseCB(fd, chan); return false; }
    buf.resize(l);
    if (!chan->opened) chan->buf.append(buf);
    else if (!SSHClient::WriteToChannel(conn, chan, buf)) ERROR(conn->Name(), ": write");
    return false;
  }

  void PortForwardLocalCloseCB(Socket fd, SSHClient::Channel *chan) {
    if (!SSHClient::CloseChannel(conn, chan)) ERROR(conn->Name(), ": write");
    app->scheduler.DelMainWaitSocket(parent->root, fd);
    SystemNetwork::CloseSocket(fd);
    forward_fd.erase(fd);
  }

  int PortForwardRemoteReadCB(Socket fd, Connection*, SSHClient::Channel *chan, const StringPiece &b) {
    if (!chan->opened) PortForwardRemoteCloseCB(fd, chan);
    else if (!b.len) {
      if (chan->buf.size()) {
        if (!SSHClient::WriteToChannel(conn, chan, chan->buf)) return ERRORv(0, conn->Name(), ": write");
        chan->buf.clear();
      }
    } else {
      if (!fd) chan->buf.append(b.str());
      else if (::send(fd, b.data(), b.size(), 0) != b.size()) PortForwardLocalCloseCB(fd, chan);
    }
    return 0;
  }

  void PortForwardRemoteCloseCB(Socket fd, SSHClient::Channel *chan) {
    auto it = forward_fd.find(fd);
    if (it == forward_fd.end()) return;
    app->scheduler.DelMainWaitSocket(parent->root, fd);
    SystemNetwork::CloseSocket(fd);
    forward_fd.erase(it);
  }
};
#endif

#ifdef LFL_RFB
struct RFBTerminalController : public NetworkTerminalController, public KeyboardController, public MouseController {
  RFBClient::Params params;
  FrameBuffer *fb;
  Box viewport;
  string password;
  Callback savehost_cb;
  SystemAlertView *passphrase_alert=0;
  bool zoom_x_dir=0, zoom_y_dir;
  Box zoom_start_viewport;
  v2 zoom_last;
  RFBTerminalController(TerminalTabInterface *p, RFBClient::Params a, const Callback &ccb, FrameBuffer *f) :
    NetworkTerminalController(p, a.hostport, ccb), params(move(a)), fb(f) {}

  template <class X> X MouseToFramebufferCoords(const X &p) const {
    return X(viewport.x +        float(p.x)                   / parent->root->width   * viewport.w,
             viewport.y + (1.0 - float(p.y - parent->root->y) / parent->root->height) * viewport.h);
  }

  Socket Open(TextArea*) override {
    INFO("Connecting to vnc://", params.hostport);
    params.background_services = background_services;
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

  StringPiece Read() override {
    if (conn && conn->state == Connection::Connected) {
      if (conn->Read() < 0)                                 { ERROR(conn->Name(), ": Read");       Close(); return ""; }
      if (conn->rb.size() && conn->handler->Read(conn) < 0) { ERROR(conn->Name(), ": query read"); Close(); return ""; }
    }
    return StringPiece();
  }

  int SendKeyEvent(InputEvent::Id event, bool down) override {
    if (conn && conn->state == Connection::Connected)
      RFBClient::SendKeyEvent(conn, InputEvent::GetKey(event), down);
    return 1;
  }

  bool AddViewportOffset(const point &d) {
    bool ret = false;
    viewport += d;
    if      (viewport.x < 0)                           { ret = true; viewport.x = 0; }
    else if (viewport.x + viewport.w > fb->tex.width)  { ret = true; viewport.x = fb->tex.width - viewport.w; }
    if      (viewport.y < 0)                           { ret = true; viewport.y = 0; }
    else if (viewport.y + viewport.h > fb->tex.height) { ret = true; viewport.y = fb->tex.height - viewport.h; }
    return ret;
  }

  int SendMouseEvent(InputEvent::Id id, const point &p, const point &d, int down, int flag) override {
    uint8_t buttons = app->input->MouseButton1Down() | app->input->MouseButton2Down()<<2;
    if (down && id == Mouse::Event::Motion) AddViewportOffset(point(-d.x, d.y));
    if (conn && conn->state == Connection::Connected) {
      point fp = MouseToFramebufferCoords(p);
      RFBClient::SendPointerEvent(conn, fp.x, fp.y, buttons);
    }
    return 1;
  }

  int SendWheelEvent(InputEvent::Id id, const v2 &p, const v2 &d, bool begin) override {
    if (id == Mouse::Event::Wheel) AddViewportOffset(point(-p.x, p.y));
    else if (id == Mouse::Event::Zoom) {
      if (begin) {
        zoom_x_dir = d.x > 1;
        zoom_y_dir = d.y > 1;
        zoom_start_viewport = viewport;
      } else {
        if (zoom_x_dir) { if (d.x < zoom_last.x) return 0; }
        else            { if (d.x > zoom_last.x) return 0; }
        if (zoom_y_dir) { if (d.y < zoom_last.y) return 0; }
        else            { if (d.y > zoom_last.y) return 0; }
      }
      viewport.w = Clamp<float>(zoom_start_viewport.w * d.x, 256, fb->tex.width);
      viewport.h = Clamp<float>(zoom_start_viewport.h * d.y, 256, fb->tex.height);
      viewport.x = p.x - viewport.w/2;
      viewport.y = p.y - viewport.h/2;
      zoom_last = d;
      AddViewportOffset(point());
    }
    return 1;
  }

  bool LoadPasswordCB(string *out) {
    if (password.size()) {
      *out = move(password);
      return true;
    } else {
      passphrase_alert->ShowCB("Password", "Password", "",
                               [=](const string &pw){ RFBClient::SendChallengeResponse(conn, pw); });
      return false;
    }
  }

  void RFBLoginCB() { if (savehost_cb) savehost_cb(); }

  void RFBUpdateCB(Connection *c, const Box &b, int pf, const StringPiece &data) {
    if (!data.buf) {
      if (b.w && b.h) {
        CHECK_EQ(0, b.x);
        CHECK_EQ(0, b.y);
        viewport = b;
        fb->Create(b.w, b.h, FrameBuffer::Flag::CreateTexture | FrameBuffer::Flag::ReleaseFB);
      }
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
#endif // LFL_RFB

struct ShellTerminalController : public InteractiveTerminalController {
  string ssh_usage="\r\nusage: ssh -l user host[:port]", ssh_term, discon_msg;
  StringCB telnet_cb;
#ifdef LFL_CRYPTO
  function<void(SSHClient::Params)> ssh_cb;
#endif
#ifdef LFL_RFB
  function<void(RFBClient::Params)> vnc_cb;
#endif
  Callback reconnect_cb;

  ShellTerminalController(TerminalTabInterface *p, const string &msg, StringCB tcb, StringVecCB ecb, Callback rcb, bool commands) :
    InteractiveTerminalController(p), discon_msg(msg), telnet_cb(move(tcb)), reconnect_cb(move(rcb)) {
    if (!commands) { prompt.clear(); return; }
    header = StrCat("LTerminal 1.0", ssh_usage, "\r\n\r\n");
#ifdef LFL_CRYPTO
    shell.Add("ssh",      bind(&ShellTerminalController::MySSHCmd,      this, _1));
#endif
#ifdef LFL_RFB
    shell.Add("vnc",      bind(&ShellTerminalController::MyVNCCmd,      this, _1));
#endif
    shell.Add("telnet",   bind(&ShellTerminalController::MyTelnetCmd,   this, _1));
    shell.Add("nslookup", bind(&ShellTerminalController::MyNSLookupCmd, this, _1));
    shell.Add("help",     bind(&ShellTerminalController::MyHelpCmd,     this, _1));
    shell.Add("exit",     move(ecb));
  }

  Socket Open(TextArea *ta) override {
    if (auto t = dynamic_cast<Terminal*>(ta)) {
      t->last_fb = 0;
      if (t->line_fb.w && t->line_fb.h) t->SetScrollRegion(1, t->term_height, true);
      if (discon_msg.size()) t->Write(discon_msg);
      if (reconnect_cb) {
        Callback r_cb;
        swap(r_cb, reconnect_cb);
        string text ="[Reconnect]";
        t->Write(StrCat("\r\n\x08\x1b[1;31m", text, "\x08\x1b[0m"));
        auto l = t->GetCursorLine();
        t->AddUrlBox(l, 0, l, text.size()-1, "", move(r_cb));
        t->Write("\r\n\r\n");
      }
    } else if (discon_msg.size()) ta->Write(discon_msg);
    return InteractiveTerminalController::Open(ta);
  }

#ifdef LFL_CRYPTO
  void MySSHCmd(const vector<string> &arg) {
    string host, login;
    ParseHostAndLogin(arg, &host, &login);
    if (login.empty() || host.empty()) { if (term) term->Write(ssh_usage); }
    else ssh_cb(SSHClient::Params{host, login, ssh_term, "", false, false, false});
  }
#endif

#ifdef LFL_RFB
  void MyVNCCmd(const vector<string> &arg) {
    string host, login;
    ParseHostAndLogin(arg, &host, &login);
    if (login.empty() || host.empty()) { if (term) term->Write("\r\nusage: vnc -l user host[:port]"); }
    else vnc_cb(RFBClient::Params{host});
  }
#endif

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

struct BufferedShellTerminalController : public ShellTerminalController {
  using ShellTerminalController::ShellTerminalController;
  int Write(const StringPiece &text) {
    for (const char *b = text.data(); !text.Done(b); ++b)
      ShellTerminalController::Write(StringPiece(b, 1));
    return text.size();
  }
};

template <class TerminalType> struct TerminalTabT : public TerminalTabInterface {
  TerminalType *terminal;
  GUI scrollbar_gui;
  Widget::Slider scrollbar;
  unique_ptr<FlatFile> record;

  TerminalTabT(Window *W, TerminalType *t, int host_id) :
    TerminalTabInterface(W, 1.0, 1.0, 0, host_id), terminal(t), scrollbar_gui(W), scrollbar(&scrollbar_gui) {
    scrollbar.arrows = false;
#ifdef FUZZ_DEBUG
    for (int i=0; i<256; i++) {
      INFO("fuzz i = ", i);
      for (int j=0; j<256; j++)
        for (int k=0; k<256; k++)
          terminal->Write(string(1, i), 1, 1);
    }
    terminal->Newline(1);
    terminal->Write("Hello world.", 1, 1);
#endif
  }

  virtual ~TerminalTabT() {}
  virtual void OpenedController() {}
  virtual void ScrollUp()   { terminal->ScrollUp(); }
  virtual void ScrollDown() { terminal->ScrollDown(); }
  virtual MouseController    *GetMouseTarget()    { return &terminal->mouse; }
  virtual KeyboardController *GetKeyboardTarget() { return terminal; }
  virtual Box                 GetLastDrawBox()    { return Box(terminal->line_fb.w, terminal->term_height * terminal->style.font->Height()); }
  
  void ChangeController(unique_ptr<Terminal::Controller> new_controller) {
    if (auto ic = dynamic_cast<InteractiveTerminalController*>(controller.get())) ic->done = true;
    controller.swap(last_controller);
    controller = move(new_controller);
    terminal->sink = controller.get();
    Socket fd = controller ? controller->Open(terminal) : InvalidSocket;
    app->scheduler.AddMainWaitSocket
      (root, fd, SocketSet::READABLE, bind(&TerminalTabInterface::ControllerReadableCB, this));
    UpdateControllerWait();
    if (controller) OpenedController();
  }

  int ReadAndUpdateTerminalFramebuffer() {
    if (!controller) return 0;
    StringPiece s = controller->Read();
    if (s.len) {
      terminal->Write(s);
#ifdef LFL_FLATBUFFERS
      if (record) record->Add
        (MakeFlatBufferOfType
         (LTerminal::RecordLog, LTerminal::CreateRecordLog(fb, (Now() - app->time_started).count(), fb.CreateVector(MakeUnsigned(s.buf), s.len))));
#endif
    }
    return s.len;
  }

  void DrawScrollBar(const Box &draw_box) {
    if (Changed(&scrollbar_gui.box, draw_box)) {
      scrollbar_gui.ClearGUI();
      scrollbar.LayoutAttached(draw_box.RelativeCoordinatesBox()); 
    }
    scrollbar.scrolled = 1.0 - terminal->v_scrolled;
    scrollbar.Update(true);
    scrollbar_gui.Draw();
  }
};
typedef TerminalTabT<Terminal> TerminalTab;

template <class X> struct TerminalWindowInterface : public GUI {
  TabbedDialog<X> tabs;
  TerminalWindowInterface(Window *W) : GUI(W), tabs(this) {}
  virtual void UpdateTargetFPS() = 0;
#ifdef LFL_RFB
  virtual X *AddRFBTab(int host_id, RFBClient::Params p, string, Callback savehost_cb=Callback()) = 0;
#endif
};

}; // namespace LFL
#endif // LFL_TERM_TERM_H__
