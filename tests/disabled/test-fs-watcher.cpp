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

#include "fs-watcher.hpp"
#include <boost/make_shared.hpp>
#include <boost/filesystem.hpp>
#include <boost/test/unit_test.hpp>
#include <boost/thread/thread.hpp>
#include <boost/bind.hpp>
#include <boost/lexical_cast.hpp>
#include <fstream>
#include <thread>
#include <set>
#include <QtGui>
// TODO Segmentation Fault and Other Error Exits No time Fix yet


using namespace std;
namespace fs = boost::filesystem;


namespace ndn {
namespace chronoshare {

BOOST_AUTO_TEST_SUITE(TestFsWatchers)

void
onChange(set<string>& files, const fs::path& file)
{
  files.insert(file.string());
}

void
onDelete(set<string>& files, const fs::path& file)
{
  files.erase(file.string());
}

void
create_file(const fs::path& ph, const std::string& contents)
{
  std::ofstream f(ph.string().c_str());
  if (!f) {
    abort();
  }
  if (!contents.empty()) {
    f << contents;
  }
}

void
run(fs::path dir, ndn::chronoshare::FsWatcher::LocalFile_Change_Callback c, ndn::chronoshare::FsWatcher::LocalFile_Change_Callback d)
{
  int x = 0;
  QCoreApplication app(x, 0);
  std::unique_ptr<boost::asio::io_service> ioService;
  ioService.reset(new boost::asio::io_service());
  FsWatcher watcher(*ioService,dir.string().c_str(), c, d);
  app.exec();
  sleep(100);
}

BOOST_AUTO_TEST_CASE(TestFsWatcher)
{
  fs::path dir = fs::absolute(fs::path("TestFsWatcher"));
  if (fs::exists(dir)) {
    fs::remove_all(dir);
  }

  fs::create_directory(dir);

  set<string> files;

  ndn::chronoshare::FsWatcher::LocalFile_Change_Callback fileChange = boost::bind(onChange, std::ref(files), _1);
  ndn::chronoshare::FsWatcher::LocalFile_Change_Callback fileDelete = boost::bind(onDelete, std::ref(files), _1);

  std::thread workThread(run, dir, fileChange, fileDelete);
  // FsWatcher watcher(dir.string().c_str(), fileChange, fileDelete);

  // ============ check create file detection ================
  create_file(dir / "test.txt", "hello");
  // have to at least wait 0.5 seconds
  usleep(600000);
  // test.txt
  BOOST_CHECK_EQUAL(files.size(), 1);
  BOOST_CHECK(files.find("test.txt") != files.end());

  // =========== check create a bunch of files in sub dir =============
  fs::path subdir = dir / "sub";
  fs::create_directory(subdir);
  for (int i = 0; i < 10; i++) {
    string filename = boost::lexical_cast<string>(i);
    create_file(subdir / filename.c_str(), boost::lexical_cast<string>(i));
  }
  // have to at least wait 0.5 * 2 seconds
  usleep(1100000);
  // test.txt
  // sub/0..9
  BOOST_CHECK_EQUAL(files.size(), 11);
  for (int i = 0; i < 10; i++) {
    string filename = boost::lexical_cast<string>(i);
    BOOST_CHECK(files.find("sub/" + filename) != files.end());
  }

  // ============== check copy directory with files to two levels of sub dirs =================
  fs::create_directory(dir / "sub1");
  fs::path subdir1 = dir / "sub1" / "sub2";
  fs::copy_directory(subdir, subdir1);
  for (int i = 0; i < 5; i++) {
    string filename = boost::lexical_cast<string>(i);
    fs::copy(subdir / filename.c_str(), subdir1 / filename.c_str());
  }
  // have to at least wait 0.5 * 2 seconds
  usleep(1100000);
  // test.txt
  // sub/0..9
  // sub1/sub2/0..4
  BOOST_CHECK_EQUAL(files.size(), 16);
  for (int i = 0; i < 5; i++) {
    string filename = boost::lexical_cast<string>(i);
    BOOST_CHECK(files.find("sub1/sub2/" + filename) != files.end());
  }

  // =============== check remove files =========================
  for (int i = 0; i < 7; i++) {
    string filename = boost::lexical_cast<string>(i);
    fs::remove(subdir / filename.c_str());
  }
  usleep(1100000);
  // test.txt
  // sub/7..9
  // sub1/sub2/0..4
  BOOST_CHECK_EQUAL(files.size(), 9);
  for (int i = 0; i < 10; i++) {
    string filename = boost::lexical_cast<string>(i);
    if (i < 7)
      BOOST_CHECK(files.find("sub/" + filename) == files.end());
    else
      BOOST_CHECK(files.find("sub/" + filename) != files.end());
  }

  // =================== check remove files again, remove the whole dir this time
  // ===================
  // before remove check
  for (int i = 0; i < 5; i++) {
    string filename = boost::lexical_cast<string>(i);
    BOOST_CHECK(files.find("sub1/sub2/" + filename) != files.end());
  }
  fs::remove_all(subdir1);
  usleep(1100000);
  BOOST_CHECK_EQUAL(files.size(), 4);
  // test.txt
  // sub/7..9
  for (int i = 0; i < 5; i++) {
    string filename = boost::lexical_cast<string>(i);
    BOOST_CHECK(files.find("sub1/sub2/" + filename) == files.end());
  }

  // =================== check rename files =======================
  for (int i = 7; i < 10; i++) {
    string filename = boost::lexical_cast<string>(i);
    fs::rename(subdir / filename.c_str(), dir / filename.c_str());
  }
  usleep(1100000);
  // test.txt
  // 7
  // 8
  // 9
  // sub
  BOOST_CHECK_EQUAL(files.size(), 4);
  for (int i = 7; i < 10; i++) {
    string filename = boost::lexical_cast<string>(i);
    BOOST_CHECK(files.find("sub/" + filename) == files.end());
    BOOST_CHECK(files.find(filename) != files.end());
  }

  create_file(dir / "add-removal-check.txt", "add-removal-check");
  usleep(1200000);
  BOOST_CHECK(files.find("add-removal-check.txt") != files.end());

  fs::remove(dir / "add-removal-check.txt");
  usleep(1200000);
  BOOST_CHECK(files.find("add-removal-check.txt") == files.end());

  create_file(dir / "add-removal-check.txt", "add-removal-check");
  usleep(1200000);
  BOOST_CHECK(files.find("add-removal-check.txt") != files.end());

  fs::remove(dir / "add-removal-check.txt");
  usleep(1200000);
  BOOST_CHECK(files.find("add-removal-check.txt") == files.end());

  create_file(dir / "add-removal-check.txt", "add-removal-check");
  usleep(1200000);
  BOOST_CHECK(files.find("add-removal-check.txt") != files.end());

  fs::remove(dir / "add-removal-check.txt");
  usleep(1200000);
  BOOST_CHECK(files.find("add-removal-check.txt") == files.end());

  // cleanup
  if (fs::exists(dir)) {
    std::cout << "Cleaning all" << std::endl;
    fs::remove_all(dir);
  }
}

BOOST_AUTO_TEST_SUITE_END()

} // chronoshare
} // ndn