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
  
struct NetworkTerminalController : public Terminal::Controller {
  Service *svc=0;
  Connection *conn=0;
  Callback detach_cb, close_cb;
  string remote, read_buf, ret_buf;
  NetworkTerminalController(Service *s, const string &r, const Callback &ccb)
    : svc(s), detach_cb(bind(&NetworkTerminalController::ConnectedCB, this)), close_cb(ccb), remote(r) {}
  virtual ~NetworkTerminalController() { if (conn) app->scheduler.DelFrameWaitSocket(screen, conn->socket); }

  virtual int Open(TextArea*) {
    if (remote.empty()) return -1;
    app->RunInNetworkThread([=](){
      if (!(conn = svc->Connect(remote, 0, &detach_cb)))
        if (app->network_thread) app->RunInMainThread([=](){ Close(); }); });
    return app->network_thread ? -1 : (conn ? conn->socket : -1);
  }

  virtual void Close() {
    if (!conn || conn->state != Connection::Connected) return;
    if (app->network_thread) app->scheduler.DelFrameWaitSocket(screen, conn->socket);
    app->net->ConnCloseDetached(svc, conn);
    conn = 0;
    close_cb();
  }

  virtual void ConnectedCB() {
    if (app->network_thread) app->scheduler.AddFrameWaitSocket(screen, conn->socket, SocketSet::READABLE);
  }

  virtual StringPiece Read() {
    if (!conn || conn->state != Connection::Connected || !NBReadable(conn->socket)) return StringPiece();
    if (conn->Read() < 0) { ERROR(conn->Name(), ": Read"); Close(); return StringPiece(); }
    read_buf.append(conn->rb.begin(), conn->rb.size());
    conn->rb.Flush(conn->rb.size());
    swap(read_buf, ret_buf);
    read_buf.clear();
    return ret_buf;
  }

  virtual int Write(const StringPiece &b) {
    if (!conn || conn->state != Connection::Connected) return -1;
    return conn->WriteFlush(b.data(), b.size());
  }
};

struct InteractiveTerminalController : public Terminal::Controller {
  TextArea *term=0;
  Shell shell;
  UnbackedTextBox cmd;
  string buf, read_buf, ret_buf, prompt="> ", header;
  bool blocking=0, done=0;
  unordered_map<string, Callback> escapes = {
    { "OA", bind([&] { cmd.HistUp();   WriteText(StrCat("\x0d", prompt, String::ToUTF8(cmd.cmd_line.Text16()), "\x1b[K")); }) },
    { "OB", bind([&] { cmd.HistDown(); WriteText(StrCat("\x0d", prompt, String::ToUTF8(cmd.cmd_line.Text16()), "\x1b[K")); }) },
    { "OC", bind([&] { if (cmd.cursor.i.x < cmd.cmd_line.Size()) { WriteText(string(1, cmd.cmd_line[cmd.cursor.i.x].Id())); cmd.CursorRight(); } }) },
    { "OD", bind([&] { if (cmd.cursor.i.x)                       { WriteText("\x08");                                       cmd.CursorLeft();  } }) },
  };

  InteractiveTerminalController() : cmd(FontDesc::Default()) {
    cmd.runcb = bind(&Shell::Run, &shell, _1);
    cmd.ReadHistory(app->savedir, "shell");
    frame_on_keyboard_input = true;
  }
  virtual ~InteractiveTerminalController() { cmd.WriteHistory(app->savedir, "shell", ""); }

  int Open(TextArea *T) { (term=T)->Write(StrCat(header, prompt)); return -1; }
  StringPiece Read() { swap(read_buf, ret_buf); read_buf.clear(); return ret_buf; }
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
    if      (*b == '\r') { if (1) { cmd.Enter(); WriteText("\r\n" + ((!done && !blocking) ? prompt : "")); } }
    else if (*b == 0x7f) { if (cmd.cursor.i.x) { WriteText((cursor_last ? "\b \b" : "\x08\x1b[1P"));        cmd.Erase();   } }
    else                 { if (1)              { WriteText((cursor_last ? "" : "\x1b[1@") + string(1, *b)); cmd.Input(*b); } }
    return l;
  }

  void WriteText(const StringPiece &b) {
    if (screen->animating) read_buf.append(b.data(), b.size());
    else if (term) term->Write(b);
  }
};

template <class TerminalType> struct TerminalWindowT {
  TerminalType *terminal;
  unique_ptr<Terminal::Controller> controller, last_controller;
  unique_ptr<FlatFile> record;

  TerminalWindowT(TerminalType *t) : terminal(t) {
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

  virtual ~TerminalWindowT() {}
  virtual void OpenedController() {}

  void ChangeController(unique_ptr<Terminal::Controller> new_controller) {
    if (auto ic = dynamic_cast<InteractiveTerminalController*>(controller.get())) ic->done = true;
    controller.swap(last_controller);
    controller = move(new_controller);
    terminal->sink = controller.get();
    int fd = controller->Open(terminal);
    if (fd != -1) app->scheduler.AddFrameWaitSocket(screen, fd, SocketSet::READABLE);
    if (controller->frame_on_keyboard_input) app->scheduler.AddFrameWaitKeyboard(screen);
    else                                     app->scheduler.DelFrameWaitKeyboard(screen);
    OpenedController();
  }

  int ReadAndUpdateTerminalFramebuffer() {
    StringPiece s = controller->Read();
    if (s.len) {
      terminal->Write(s);
      if (record) record->Add
        (MakeIPC(RecordLog, (Now() - app->time_started).count(), fb.CreateVector(MakeUnsigned(s.buf), s.len)));
    }
    return s.len;
  }
};
typedef TerminalWindowT<Terminal> TerminalWindow;

}; // namespace LFL
#endif // LFL_TERM_TERM_H__
