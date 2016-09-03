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
  using Dialog::Dialog;

  virtual bool Animating() const = 0;
  virtual int ReadAndUpdateTerminalFramebuffer() = 0;
  virtual bool ControllerReadableCB() { ReadAndUpdateTerminalFramebuffer(); return true; }
  virtual void SetFontSize(int) = 0;
  virtual void ScrollUp() = 0;
  virtual void ScrollDown() = 0;
};

struct TerminalControllerInterface : public Terminal::Controller {
  TerminalTabInterface *parent;
  TerminalControllerInterface(TerminalTabInterface *P) : parent(P) {}
};
  
struct NetworkTerminalController : public TerminalControllerInterface {
  Service *svc=0;
  Connection *conn=0;
  Callback detach_cb, close_cb, success_cb;
  string remote, read_buf, ret_buf;
  NetworkTerminalController(TerminalTabInterface *p, Service *s, const string &r, const Callback &ccb) :
    TerminalControllerInterface(p), svc(s), detach_cb(bind(&NetworkTerminalController::ConnectedCB, this)),
    close_cb(ccb), remote(r) {}
  virtual ~NetworkTerminalController() { if (conn) app->scheduler.DelMainWaitSocket(parent->root, conn->socket); }

  virtual int Open(TextArea *t) {
    if (remote.empty()) return -1;
    t->Write(StrCat("Connecting to ", remote, "\r\n"));
    app->RunInNetworkThread([=](){
      if (!(conn = svc->Connect(remote, 0, &detach_cb)))
        if (app->network_thread) app->RunInMainThread([=](){ Close(); }); });
    return app->network_thread ? -1 : (conn ? conn->socket : -1);
  }

  virtual void Close() {
    if (!conn || conn->state != Connection::Connected) return;
    if (app->network_thread) app->scheduler.DelMainWaitSocket(parent->root, conn->socket);
    app->net->ConnCloseDetached(svc, conn);
    conn = 0;
    close_cb();
  }

  virtual void ConnectedCB() {
    if (app->network_thread) app->scheduler.AddMainWaitSocket
      (parent->root, conn->socket, SocketSet::READABLE, bind(&TerminalTabInterface::ControllerReadableCB, parent));
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

struct InteractiveTerminalController : public TerminalControllerInterface {
  TextArea *term=0;
  Shell shell;
  UnbackedTextBox cmd;
  string buf, prompt="> ", header;
  bool blocking=0, done=0;
  unordered_map<string, Callback> escapes = {
    { "OA", bind([&] { cmd.HistUp();   WriteText(StrCat("\x0d", prompt, String::ToUTF8(cmd.cmd_line.Text16()), "\x1b[K")); }) },
    { "OB", bind([&] { cmd.HistDown(); WriteText(StrCat("\x0d", prompt, String::ToUTF8(cmd.cmd_line.Text16()), "\x1b[K")); }) },
    { "OC", bind([&] { if (cmd.cursor.i.x < cmd.cmd_line.Size()) { WriteText(string(1, cmd.cmd_line[cmd.cursor.i.x].Id())); cmd.CursorRight(); } }) },
    { "OD", bind([&] { if (cmd.cursor.i.x)                       { WriteText("\x08");                                       cmd.CursorLeft();  } }) },
  };

  InteractiveTerminalController(TerminalTabInterface *p) :
    TerminalControllerInterface(p), cmd(FontDesc::Default()) {
    cmd.runcb = bind(&Shell::Run, &shell, _1);
    cmd.ReadHistory(app->savedir, "shell");
    frame_on_keyboard_input = true;
  }
  virtual ~InteractiveTerminalController() { cmd.WriteHistory(app->savedir, "shell", ""); }

  StringPiece Read() { return ""; }
  int Open(TextArea *T) { (term=T)->Write(StrCat(header, prompt)); return -1; }
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

  void WriteText(const StringPiece &b) { if (term) term->Write(b); }
};

template <class TerminalType> struct TerminalTabT : public TerminalTabInterface {
  TerminalType *terminal;
  unique_ptr<FlatFile> record;

  TerminalTabT(Window *W, TerminalType *t) : TerminalTabInterface(W, 1.0, 1.0), terminal(t) {
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
  virtual void TakeFocus()  { terminal->Activate(); }
  virtual void LoseFocus()  { terminal->Deactivate(); }
  virtual void ScrollUp()   { terminal->ScrollUp(); }
  virtual void ScrollDown() { terminal->ScrollDown(); }

  void ChangeController(unique_ptr<Terminal::Controller> new_controller) {
    if (auto ic = dynamic_cast<InteractiveTerminalController*>(controller.get())) ic->done = true;
    controller.swap(last_controller);
    controller = move(new_controller);
    terminal->sink = controller.get();
    int fd = controller ? controller->Open(terminal) : -1;
    if (fd != -1) app->scheduler.AddMainWaitSocket
      (root, fd, SocketSet::READABLE, bind(&TerminalTabInterface::ControllerReadableCB, this));
    if (controller && controller->frame_on_keyboard_input) app->scheduler.AddMainWaitKeyboard(root);
    else                                                   app->scheduler.DelMainWaitKeyboard(root);
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
};
typedef TerminalTabT<Terminal> TerminalTab;

}; // namespace LFL
#endif // LFL_TERM_TERM_H__
