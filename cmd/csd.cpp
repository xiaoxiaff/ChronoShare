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

#include <boost/asio/io_service.hpp>

#include <thread>

namespace ndn {
namespace chronoshare {

class Runner : public QObject
{
Q_OBJECT
public:
};

int
main(int argc, char* argv[])
{
  INIT_LOGGERS();

  QCoreApplication app(argc, argv);
  // Runner runner;
  // QObject::connect(&runner, SIGNAL(done()), &app, SLOT(quit()));

  if (argc != 4) {
    std::cerr << "Usage: ./csd <username> <shared-folder> <path>" << std::endl;
    return 1;
  }

  std::string username = argv[1];
  std::string sharedFolder = argv[2];
  std::string path = argv[3];

  std::cout << "Starting ChronoShare for [" << username << "] shared-folder [" << sharedFolder
       << "] at [" << path << "]" << std::endl;

  boost::asio::io_service ioService;
  QApplication::setEventDispatcher(new QAsioEventDispatcher(ioService));
  Face face(ioService);

  Dispatcher dispatcher(username, sharedFolder, path, face);

  // std::thread ioThread([&ioService, &app] {
  //     try {
  //       ioService.run();
  //     }
  //     catch (boost::exception& e) {
  //       if (&dynamic_cast<std::exception&>(e) != nullptr) {
  //         std::cerr << "ERROR: " << dynamic_cast<std::exception&>(e).what() << std::endl;
  //       }
  //       std::cerr << boost::diagnostic_information(e, true) << std::endl;
  //     }
  //     catch (std::exception& e) {
  //       std::cerr << "ERROR: " << e.what() << std::endl;
  //     }
  //   });

  FsWatcher watcher(ioService, path.c_str(),
                    bind(&Dispatcher::Did_LocalFile_AddOrModify, &dispatcher, _1),
                    bind(&Dispatcher::Did_LocalFile_Delete, &dispatcher, _1));

  try {
    return app.exec();
  }
  catch (boost::exception& e) {
    if (&dynamic_cast<std::exception&>(e) != nullptr) {
      std::cerr << "ERROR: " << dynamic_cast<std::exception&>(e).what() << std::endl;
    }
    std::cerr << boost::diagnostic_information(e, true) << std::endl;
  }
  catch (std::exception& e) {
    std::cerr << "ERROR: " << e.what() << std::endl;
  }

  return retval;
}

} // chronoshare
} // ndn

int
main(int argc, char* argv[])
{
  return ndn::chronoshare::main(argc, argv);
}
