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

#ifndef LFL_TERM_TERM_MOBILE_H__
#define LFL_TERM_TERM_MOBILE_H__
namespace LFL {
  
struct MyHostRecord {
  int remote_id=0, cred_id=0;
  const LTerminal::Remote *host=0;
  const LTerminal::Credential *cred=0;

  MyHostRecord() {}
  MyHostRecord(SQLiteIdValueStore *cred_db, int cred_id) { LoadCredential(cred_db, cred_id); }
  MyHostRecord(SQLiteIdValueStore *remote_db, SQLiteIdValueStore *cred_db, int remote_id) {
    LoadRemote(remote_db, remote_id);
    if (host->credential() && host->credential()->db() == LTerminal::CredentialDBType_Table) 
      LoadCredential(cred_db, host->credential()->id());
  }

  void LoadRemote(SQLiteIdValueStore *remote_db, int id) {
    host = flatbuffers::GetRoot<LTerminal::Remote>(FindRefOrDie(remote_db->data, (remote_id = id)).data());
  }

  void LoadCredential(SQLiteIdValueStore *cred_db, int id) {
    cred = flatbuffers::GetRoot<LTerminal::Credential>(FindRefOrDie(cred_db->data, (cred_id = id)).data());
  }

  string Username() const { return host && host->username() ? host->username()->data() : ""; }
  string Displayname() const { return host && host->displayname() ? host->displayname()->data() : ""; }
  LTerminal::CredentialType Credtype() const { return cred ? cred->type() : LTerminal::CredentialType_Ask; }

  string Hostname() const {
    if (!host || !host->hostport()) return "";
    string ret(host->hostport()->data());
    return ret.substr(0, ret.find(":"));
  }

  int Port(int default_port) const { 
    if (!host || !host->hostport()) return default_port;
    if (const char *colon = strchr(host->hostport()->data(), ':')) return atoi(colon+1);
    return default_port;
  }

  string Creddata() const {
    return cred && cred->data() ? string(MakeSigned(cred->data()->data()), cred->data()->size()) : "";
  }

  string Password() const {
    return cred && cred->type() == LTerminal::CredentialType_Password ? Creddata() : "";
  }

  string Keyname() const {
    if (!cred || cred->type() != LTerminal::CredentialType_PEM || !cred->displayname()) return "";
    return cred->displayname()->data();
  }
};

}; // namespace LFL
#endif // LFL_TERM_TERM_MOBILE_H__
