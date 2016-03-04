/*
 * $Id$
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
#include "core/app/ipc.h"

#ifdef __APPLE__
#include <sandbox.h>
#endif

namespace LFL {
unique_ptr<ProcessAPIServer> process_api;
}; // namespace LFL
using namespace LFL;

extern "C" void MyAppInit() {
  app->name = "LTerminalRenderSandbox";
  app->log_pid = true;
#ifdef LFL_DEBUG
  app->logfilename = StrCat(LFAppDownloadDir(), "lterm-render.txt");
#endif
}

extern "C" int MyAppMain(int argc, const char* const* argv) {
  if (app->Create(argc, argv, __FILE__)) return -1;

  int optind = Singleton<FlagMap>::Get()->optind;
  if (optind >= argc) { fprintf(stderr, "Usage: %s [-flags] <socket-name>\n", argv[0]); return -1; }
  // if (app->Init()) return -1;
  screen->gd = static_cast<GraphicsDevice*>(LFAppCreateGraphicsDevice(2));
  app->net = make_unique<Network>();
  (app->asset_loader = make_unique<AssetLoader>())->Init();

  // to cleanup crash leaked shm: for i in $( ipcs -m | grep "^m " | awk '{print $2}' ); do ipcrm -m $i; done
  const string socket_name = StrCat(argv[optind]);
  process_api = make_unique<ProcessAPIServer>();
  process_api->OpenSocket(StrCat(argv[optind]));

#ifdef __APPLE__
  char *sandbox_error=0;
  sandbox_init(kSBXProfilePureComputation, SANDBOX_NAMED, &sandbox_error);
  INFO("render: sandbox init: ", sandbox_error ? sandbox_error : "success");
#endif

  process_api->HandleMessagesLoop();
  INFO("render: exiting");
  delete process_api->conn;
  return 0;
}
