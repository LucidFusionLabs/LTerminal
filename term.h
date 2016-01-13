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
  
template <class TerminalType> struct TerminalWindowT {
  unique_ptr<typename TerminalType::Controller> controller;
  unique_ptr<TerminalType> terminal;
  TerminalWindowT(typename TerminalType::Controller *C) : controller(C) {}

  void Open(int w, int h, int font_size) {
    CHECK(w);
    CHECK(h);
    CHECK(font_size);
    terminal = unique_ptr<TerminalType>(new TerminalType(controller.get(), screen, Fonts::Get(FLAGS_default_font, "", font_size)));
    terminal->Activate();
    terminal->SetDimension(w, h);
    screen->default_textgui = terminal.get();
    CHECK(terminal->font);
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

  virtual void OpenedController() {}
  void OpenController() {
    int fd = controller->Open(terminal.get());
    if (fd != -1) app->scheduler.AddWaitForeverSocket(fd, SocketSet::READABLE, 0);
    if (controller->frame_on_keyboard_input) app->scheduler.AddWaitForeverKeyboard();
    else                                     app->scheduler.DelWaitForeverKeyboard();
    OpenedController();
  }

  int ReadAndUpdateTerminalFramebuffer() {
    StringPiece s = controller->Read();
    if (s.len) terminal->Write(s);
    return s.len;
  }

  void ScrollHistory(bool up_or_down) {
    if (up_or_down) terminal->ScrollUp();
    else            terminal->ScrollDown();
    app->scheduler.Wakeup(0);
  }
};
typedef TerminalWindowT<Terminal> TerminalWindow;

struct NetworkTerminalController : public Terminal::Controller {
  Service *svc=0;
  Connection *conn=0;
  Callback detach_cb;
  string remote, read_buf, ret_buf;
  NetworkTerminalController(Service *s, const string &r=string())
    : svc(s), detach_cb(bind(&NetworkTerminalController::ConnectedCB, this)), remote(r) {}

  virtual int Open(Terminal *term) {
    if (remote.empty()) return -1;
    RunInNetworkThread([=](){
      if (!(conn = svc->Connect(remote, 0, &detach_cb)))
        RunInMainThread(new Callback(bind(&NetworkTerminalController::Dispose, this)));
    });
    return app->network_thread ? -1 : (conn ? conn->socket : -1);
  }

  virtual void Close() {
    if (!conn || conn->state != Connection::Connected) return;
    conn->SetError();
    RunInNetworkThread([=](){ app->network->ConnClose(svc, conn, NULL); });
  }

  virtual void ConnectedCB() {
    if (app->network_thread) app->scheduler.AddWaitForeverSocket(conn->socket, SocketSet::READABLE, 0);
  }

  virtual void ClosedCB(Socket socket) {
    CHECK(conn);
    app->scheduler.DelWaitForeverSocket(socket);
    app->scheduler.Wakeup(0);
    conn = 0;
    Dispose();
  }

  virtual StringPiece Read() {
    if (!conn) return StringPiece();
    if (conn->state == Connection::Connected && NBReadable(conn->socket))
      if (conn->Read() < 0) { ERROR(conn->Name(), ": Read"); auto s=conn->socket; Close(); ClosedCB(s); return StringPiece(); }
    read_buf.append(conn->rb.begin(), conn->rb.size());
    conn->rb.Flush(conn->rb.size());
    swap(read_buf, ret_buf);
    read_buf.clear();
    return ret_buf;
  }

  virtual int Write(const char *b, int l) {
    if (!conn || conn->state != Connection::Connected) return -1;
    if (local_echo) read_buf.append(ReplaceNewlines(string(b, l), "\r\n")); 
    return write(conn->socket, b, l);
  }
};

struct InteractiveTerminalController : public Terminal::Controller {
  Terminal *term=0;
  Shell shell;
  UnbackedTextGUI cmd;
  string buf, read_buf, ret_buf, prompt="> ", header;
  bool blocking=0;
  Terminal::Controller *next_controller=0;
  function<void(Terminal::Controller*, unique_ptr<Terminal::Controller>*)> next_controller_cb;

  map<string, Callback> escapes = {
    { "OA", bind([&] { cmd.HistUp();   ReadCB(StrCat("\x0d", prompt, String::ToUTF8(cmd.cmd_line.Text16()), "\x1b[K")); }) },
    { "OB", bind([&] { cmd.HistDown(); ReadCB(StrCat("\x0d", prompt, String::ToUTF8(cmd.cmd_line.Text16()), "\x1b[K")); }) },
    { "OC", bind([&] { if (cmd.cursor.i.x < cmd.cmd_line.Size()) { ReadCB(string(1, cmd.cmd_line[cmd.cursor.i.x].Id())); cmd.CursorRight(); } }) },
    { "OD", bind([&] { if (cmd.cursor.i.x)                       { ReadCB("\x08");                                       cmd.CursorLeft();  } }) },
  };

  InteractiveTerminalController() : cmd(Fonts::Default()) {
    cmd.runcb = bind(&Shell::Run, &shell, _1);
    cmd.ReadHistory(LFAppDownloadDir(), "shell");
    frame_on_keyboard_input = true;
  }
  virtual ~InteractiveTerminalController() { cmd.WriteHistory(LFAppDownloadDir(), "shell", ""); }

  int Open(Terminal *T) { (term=T)->Write(StrCat(header, prompt)); return -1; }
  StringPiece Read() { swap(read_buf, ret_buf); read_buf.clear(); return ret_buf; }
  void UnBlockWithResponse(const string &t) { if (!(blocking=0)) ReadCB(StrCat(t, "\r\n", prompt)); }

  void ReadCB(const StringPiece &b) {
    if (screen->animating) read_buf.append(b.data(), b.size());
    else if (term) term->Write(b);
  }

  void IOCtlWindowSize(int w, int h) {}
  int Write(const char *b, int l) {
    if (l > 1 && *b == '\x1b') {
      string escape(b+1, l-1);
      if (!FindAndDispatch(escapes, escape)) ERROR("unhandled escape: ", escape);
      return l;
    } else CHECK_EQ(1, l);

    bool cursor_last = cmd.cursor.i.x == cmd.cmd_line.Size();
    if      (*b == '\r') { if (1) { cmd.Enter(); ReadCB("\r\n" + ((!next_controller && !blocking) ? prompt : ""));      } }
    else if (*b == 0x7f) { if (cmd.cursor.i.x) { ReadCB((cursor_last ? "\b \b" : "\x08\x1b[1P"));        cmd.Erase();   } }
    else                 { if (1)              { ReadCB((cursor_last ? "" : "\x1b[1@") + string(1, *b)); cmd.Input(*b); } }

    unique_ptr<Terminal::Controller> self;
    if (auto c = GetThenAssignNull(&next_controller)) next_controller_cb(c, &self);
    return l;
  }
};

}; // namespace LFL
#endif // LFL_TERM_TERM_H__
