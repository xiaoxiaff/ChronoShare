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

#include "logging.hpp"
#include "object-manager.hpp"

#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>

#include <boost/test/unit_test.hpp>
#include <unistd.h>
#include <boost/make_shared.hpp>
#include <iostream>
#include <iterator>
#include <thread>

INIT_LOGGER("Test.ObjectManager")

using namespace std;
using namespace boost;
namespace fs = boost::filesystem;


namespace ndn {
namespace chronoshare {

BOOST_AUTO_TEST_SUITE(TestObjectManager)

BOOST_AUTO_TEST_CASE(ObjectManagerTest)
{
  INIT_LOGGERS();

  fs::path tmpdir = fs::unique_path(fs::temp_directory_path() / "%%%%-%%%%-%%%%-%%%%");
  _LOG_DEBUG("tmpdir: " << tmpdir);
  Name deviceName("/device");

  shared_ptr<Face> face = make_shared<Face>();

  ObjectManager manager(*face, tmpdir, "test-chronoshare");

  auto hash_segments = manager.localFileToObjects(fs::path("test") / "test-object-manager.cc", deviceName);

  BOOST_CHECK_EQUAL(std::get<1>(hash_segments), 3);

  bool ok = manager.objectsToLocalFile(deviceName, *std::get<0>(hash_segments), tmpdir / "test.cc");
  BOOST_CHECK_EQUAL(ok, true);

  {
    fs::ifstream origFile(fs::path("test") / "test-object-manager.cc");
    fs::ifstream newFile(tmpdir / "test.cc");

    istream_iterator<char> eof, origFileI(origFile), newFileI(newFile);

    BOOST_CHECK_EQUAL_COLLECTIONS(origFileI, eof, newFileI, eof);
  }

  if (exists(tmpdir)) {
    std::cout << "Clear ALLLLLLLL" << std::endl;
    face->shutdown();

    remove_all(tmpdir);
  }
}

BOOST_AUTO_TEST_SUITE_END()

} // chronoshare
} // ndn