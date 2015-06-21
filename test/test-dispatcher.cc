/* -*- Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil -*- */
/*
 * Copyright (c) 2012 University of California, Los Angeles
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Author:  Zhenkai Zhu <zhenkai@cs.ucla.edu>
 *          Alexander Afanasyev <alexander.afanasyev@ucla.edu>
 *          Lijing Wang <wanglj11@mails.tsinghua.edu.cn>
 */

#include "logging.h"
#include "dispatcher.h"
#include <boost/test/unit_test.hpp>
#include <boost/make_shared.hpp>
#include <boost/filesystem.hpp>
#include <fstream>
#include <cassert>

using namespace ndn;
using namespace std;
using namespace boost;
namespace fs = boost::filesystem;

INIT_LOGGER ("Test.Dispatcher");

BOOST_AUTO_TEST_SUITE(TestDispatcher)


void cleanDir(fs::path dir)
{
  if (fs::exists(dir))
  {
    fs::remove_all(dir);
  }
}

void checkRoots(ndn::ConstBufferPtr root1, ndn::ConstBufferPtr root2)
{
  BOOST_CHECK_EQUAL(DigestComputer::digestToString(*root1), DigestComputer::digestToString(*root2));
}

BOOST_AUTO_TEST_CASE(DispatcherTest)
{
  INIT_LOGGERS ();

  fs::path dir1("./TestDispatcher/test-white-house");
  fs::path dir2("./TestDispatcher/test-black-house");

  string user1 = "/obamaa";
  string user2 = "/romney";

  string folder = "who-is-president";

  boost::shared_ptr<Face> face1 = boost::make_shared<Face>();
  usleep(100);
  boost::shared_ptr<Face> face2 = boost::make_shared<Face>();
  usleep(100);

  cleanDir(dir1);
  cleanDir(dir2);

  Dispatcher d1(user1, folder, dir1, face1, false);
  usleep(100);
  Dispatcher d2(user2, folder, dir2, face2, false);

  usleep(14900000);

  _LOG_DEBUG ("checking obama vs romney");
  checkRoots(d1.SyncRoot(), d2.SyncRoot());

  fs::path filename("a_letter_to_romney.txt");
  string words = "I'm the new socialist President. You are not!";

  fs::path abf = dir1 / filename;

  ofstream ofs;
  ofs.open(abf.string().c_str());
  for (int i = 0; i < 5000; i ++)
  {
    ofs << words;
  }
  ofs.close();

  d1.Did_LocalFile_AddOrModify(filename);

  sleep(5);

  fs::path ef = dir2 / filename;
  BOOST_REQUIRE_MESSAGE(fs::exists(ef), user1 << " failed to notify " << user2 << " about " << filename.string());
  BOOST_CHECK_EQUAL(fs::file_size(abf), fs::file_size(ef));
  DigestComputer digestComputer1;
  DigestComputer digestComputer2;
  ConstBufferPtr fileHash1 = digestComputer1.digestFromFile(abf);
  ConstBufferPtr fileHash2 = digestComputer2.digestFromFile(ef);

  checkRoots(fileHash1, fileHash2);

//  cleanDir(dir1);
//  cleanDir(dir2);
}

BOOST_AUTO_TEST_SUITE_END()
