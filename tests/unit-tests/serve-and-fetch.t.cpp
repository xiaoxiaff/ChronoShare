/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/**
 * Copyright (c) 2013-2017, Regents of the University of California.
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

#include "content-server.hpp"
#include "fetch-manager.hpp"
#include "object-db.hpp"
#include "object-manager.hpp"
#include <boost/filesystem.hpp>
#include <boost/make_shared.hpp>
#include <boost/test/unit_test.hpp>
#include <boost/thread/condition_variable.hpp>
#include <boost/thread/locks.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/thread/thread_time.hpp>
#include <ctime>
#include <stdio.h>

#include "test-common.hpp"


using namespace std;
using namespace boost;
using namespace boost::filesystem;


namespace ndn {
namespace chronoshare {
namespace tests {

INIT_LOGGER("Test.ServerAndFetch")

class TestServerAndFetchFixture : public IdentityManagementTimeFixture
{
public:
  void
  advanceClocks()
  {
    for (int i = 0; i < 100; ++i) {
      usleep(10000);
      IdentityManagementTimeFixture::advanceClocks(time::milliseconds(10), 1);
    }
  }
};

BOOST_FIXTURE_TEST_SUITE(TestServeAndFetch, TestServerAndFetchFixture)

path root("test-server-and-fetch");
path filePath = root / "random-file";
unsigned char magic = 'm';
int repeat = 1024 * 40;
boost::mutex mut;
boost::condition_variable cond;
bool finished;
int ack;

void
setup()
{
  if (exists(root)) {
    remove_all(root);
  }

  create_directory(root);

  // create file
  FILE* fp = fopen(filePath.string().c_str(), "w");
  for (int i = 0; i < repeat; i++) {
    fwrite(&magic, 1, sizeof(magic), fp);
  }
  fclose(fp);

  ack = 0;
  finished = false;
}

void
teardown()
{
  if (exists(root)) {
    remove_all(root);
  }

  ack = 0;
  finished = false;
}

Name
simpleMap(const Name& deviceName)
{
  return Name("/local");
}

void
segmentCallback(const Name& deviceName, const Name& baseName, uint64_t seq,
                ndn::shared_ptr<ndn::Data> data)
{
  ack++;
  ndn::Block block = data->getContent();

  int size = block.value_size();
  const uint8_t* co = block.value();
  for (int i = 0; i < size; i++) {
    BOOST_CHECK_EQUAL(co[i], magic);
  }

  _LOG_DEBUG("I'm called!!! segmentCallback!! size " << size << " ack times:" << ack);
}

void
finishCallback(Name& deviceName, Name& baseName)
{
  BOOST_CHECK_EQUAL(ack, repeat / 1024);
  boost::unique_lock<boost::mutex> lock(mut);
  finished = true;
  cond.notify_one();
}

BOOST_AUTO_TEST_CASE(TestServeAndFetch)
{
  _LOG_DEBUG("Setting up test environment ...");
  setup();

  shared_ptr<Face> face_serve = make_shared<Face>(this->m_io);
  this->advanceClocks();
  shared_ptr<Face> face_fetch = make_shared<Face>(this->m_io);

  Name deviceName("/test/device");
  Name localPrefix("/local");
  Name broadcastPrefix("/broadcast");

  const string APPNAME = "test-chronoshare";

  time_t start = std::time(NULL);
  _LOG_DEBUG("At time " << start << ", publish local file to database, this is extremely slow ...");
  // publish file to db
  ObjectManager om(*face_serve, root, APPNAME);
  auto pub = om.localFileToObjects(filePath, deviceName);
  time_t end = std::time(NULL);
  _LOG_DEBUG("At time " << end << ", publish finally finished, used " << end - start
                        << " seconds ...");

  ActionLogPtr dummyLog;
  KeyChain keyChain;
  ContentServer server(*face_serve, dummyLog, root, deviceName, "pentagon's secrets", APPNAME, keyChain, 5);
  server.registerPrefix(localPrefix);
  server.registerPrefix(broadcastPrefix);

  FetchManager fm(*face_fetch, bind(simpleMap, _1), Name("/local/broadcast"));
  ConstBufferPtr hash = std::get<0>(pub);
  Name baseName = Name(deviceName);
  baseName.append(APPNAME).append("file").appendImplicitSha256Digest(hash);

  fm.Enqueue(deviceName, baseName, bind(segmentCallback, _1, _2, _3, _4),
             bind(finishCallback, _1, _2), 0, std::get<1>(pub) - 1);

  this->advanceClocks();
  boost::unique_lock<boost::mutex> lock(mut);
  system_time timeout = get_system_time() + posix_time::milliseconds(5000);
  while (!finished) {
    if (!cond.timed_wait(lock, timeout)) {
      BOOST_FAIL("Fetching has not finished after 5 seconds");
      break;
    }
  }

  _LOG_DEBUG("Finish");
  this->advanceClocks();

  teardown();
}

BOOST_AUTO_TEST_SUITE_END()

} // namespace tests
} // namespace chronoshare
} // namespace ndn
