/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/**
 * Copyright (c) 2013-2015 Regents of the University of California.
 *
 * This file is part of ChronoShare, a decentralized file sharing application over NDN.
 *
 * ChronoShare is free software: you can redistribute it and/or modify it under the terms
 * of the GNU General Public License as published by the Free Software Foundation, either
 * version 3 of the License, or (at your option) any later version.
 *
 * ChronoShare is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE.  See the GNU General Public License for more details.
 *
 * You should have received copies of the GNU General Public License along with
 * ChronoShare, e.g., in COPYING.md file.  If not, see <http://www.gnu.org/licenses/>.
 *
 * See AUTHORS.md for complete list of ChronoShare authors and contributors.
 */

#include <QtCore>

#include "dispatcher.hpp"
#include "logging.hpp"
#include "fs-watcher.hpp"

namespace ndn {
namespace chronoshare {

int
main(int argc, char* argv[])
{
  INIT_LOGGERS();

  QCoreApplication app(argc, argv);

  if (argc != 4) {
    cerr << "Usage: ./csd <username> <shared-folder> <path>" << endl;
    return 1;
  }

  string username = argv[1];
  string sharedFolder = argv[2];
  string path = argv[3];

  cout << "Starting ChronoShare for [" << username << "] shared-folder [" << sharedFolder
       << "] at [" << path << "]" << endl;

  Dispatcher dispatcher(username, sharedFolder, path, make_shared<Face>());

  FsWatcher watcher(path.c_str(), bind(&Dispatcher::Did_LocalFile_AddOrModify, &dispatcher, _1),
                    bind(&Dispatcher::Did_LocalFile_Delete, &dispatcher, _1));

  return app.exec();
}

} // chronoshare
} // ndn

int
main(int argc, char* argv[])
{
  return ndn::chronoshare::main(argc, argv);
}
