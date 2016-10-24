/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/**
 * Copyright (c) 2013-2016, Regents of the University of California.
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

#include "fs-watcher.hpp"
#include "test-common.hpp"

#include <boost/bind.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/make_shared.hpp>
#include <boost/test/unit_test.hpp>
#include <boost/thread/thread.hpp>
#include <QtWidgets>
#include <fstream>
#include <iostream>
#include <thread>
#include <set>

namespace ndn {
namespace chronoshare {
namespace tests {

namespace fs = boost::filesystem;

INIT_LOGGER("Test.FsWatcherDelay")

BOOST_AUTO_TEST_SUITE(TestFsWatcherDelay)

void
onChange(const fs::path& file)
{
  std::cerr << "onChange called" << std::endl;
}

void
onDelete(const fs::path& file)
{
  std::cerr << "onDelete called" << std::endl;
}

void
run(fs::path dir, FsWatcher::LocalFile_Change_Callback c, FsWatcher::LocalFile_Change_Callback d)
{
  int x = 0;
  QCoreApplication app(x, 0);
  std::unique_ptr<boost::asio::io_service> ioService;
  ioService.reset(new boost::asio::io_service());
  FsWatcher watcher(*ioService, dir.string().c_str(), c, d);
  app.exec();
  sleep(100);
}

void
SlowWrite(fs::path file)
{
  fs::ofstream off(file, std::ios::out);

  for (int i = 0; i < 10; i++) {
    off << i << std::endl;
    usleep(200000);
  }
}

BOOST_AUTO_TEST_CASE(TestFsWatcherDelay)
{
  fs::path dir = fs::absolute(fs::path("TestFsWatcher"));
  if (fs::exists(dir)) {
    fs::remove_all(dir);
  }

  fs::create_directory(dir);

  FsWatcher::LocalFile_Change_Callback fileChange = boost::bind(onChange, _1);
  FsWatcher::LocalFile_Change_Callback fileDelete = boost::bind(onDelete, _1);

  fs::path file = dir / "test.text";

  std::thread watcherThread(run, dir, fileChange, fileDelete);

  std::thread writeThread(SlowWrite, file);


  usleep(10000000);

  // cleanup
  if (fs::exists(dir)) {
    fs::remove_all(dir);
  }

  usleep(1000000);
}

BOOST_AUTO_TEST_SUITE_END()

} // namespace tests
} // namespace chronoshare
} // namespace ndn