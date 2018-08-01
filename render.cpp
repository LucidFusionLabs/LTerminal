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
Application *app;
unique_ptr<ProcessAPIServer> process_api;

extern "C" LFApp *MyAppCreate(int argc, const char* const* argv) {
  app = make_unique<Application>(argc, argv).release();
  app->focused = app->framework->ConstructWindow(app).release();
  app->name = "LTerminalRenderSandbox";
  app->log_pid = true;
  return app;
}

extern "C" int MyAppMain(LFApp*) {
  if (app->Create(__FILE__)) return -1;
  int optind = Singleton<FlagMap>::Get()->optind;
  if (optind >= app->argc) { fprintf(stderr, "Usage: %s [-flags] <socket-name>\n", app->argv[0]); return -1; }
  app->focused->gd = GraphicsDevice::Create(app->focused, app->shaders.get()).release();
  app->net = make_unique<SocketServices>(app, app);
  (app->asset_loader = make_unique<AssetLoader>(app))->Init();

  const string socket_name = StrCat(app->argv[optind]);
  process_api = make_unique<ProcessAPIServer>(app, app, app->input.get(), app->net.get(), app);
  process_api->OpenSocket(StrCat(app->argv[optind]));

#ifdef __APPLE__
  char *sandbox_error=0;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
  sandbox_init(kSBXProfilePureComputation, SANDBOX_NAMED, &sandbox_error);
#pragma clang diagnostic pop
  INFO("render: sandbox init: ", sandbox_error ? sandbox_error : "success");
#endif

  process_api->HandleMessagesLoop();
  INFO("render: exiting");
  delete process_api->conn;
  return 0;
}

}; // namespace LFL
