/* -*- Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil -*- */
/*
 * Copyright(c) 2012 University of California, Los Angeles
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
 * Author: Alexander Afanasyev <lijingander.afanasyev@ucla.edu>
 *	   Zhenkai Zhu <zhenkai@cs.ucla.edu>
 */

#include <boost/test/unit_test.hpp>
#include <boost/lexical_cast.hpp>

#include "digest-computer.h"
#include "logging.h"
#include <unistd.h>
#include "action-log.h"
#include <iostream>
#include <boost/filesystem.hpp>

using namespace std;
using namespace boost;
using namespace ndn;
namespace fs = boost::filesystem;

// namespace chronochat {
// namespace tests {

INIT_LOGGER("Test.SyncLog");

BOOST_AUTO_TEST_SUITE(TestSyncLog)

BOOST_AUTO_TEST_CASE(BasicDatabaseTest)
{
  INIT_LOGGERS();

  //  fs::path tmpdir = fs::unique_path(fs::temp_directory_path() / "%%%%-%%%%-%%%%-%%%%");
  fs::path tmpdir = fs::unique_path("./Loli_Test");
  std::cout << "tmpdir " << tmpdir << std::endl;

  if (exists(tmpdir)) {
    remove_all(tmpdir);
  }

  SyncLog db(tmpdir, Name("/lijing"));

  ndn::ConstBufferPtr hash = db.RememberStateInStateLog();
  // should be empty

  _LOG_DEBUG("hashToString " << DigestComputer::digestToString(*hash));
  BOOST_CHECK_EQUAL(DigestComputer::digestToString(*hash),
                    "94d988a90c6a3d0f74624368be65e5369ddddb3444841fad4ef41f674b937f26");

  db.UpdateDeviceSeqNo(Name("/lijing"), 1);
  hash = db.RememberStateInStateLog();

  BOOST_CHECK_EQUAL(DigestComputer::digestToString(*hash),
                    "91a849eede75acd56ae1bcb99e92d8fb28757683bc387dbb0e59c3108fcf4f18");

  db.UpdateDeviceSeqNo(Name("/lijing"), 2);
  hash = db.RememberStateInStateLog();
  BOOST_CHECK_EQUAL(DigestComputer::digestToString(*hash),
                    "d2dfeda56ed98c0e17d455a859bc8c3b9e31c85c138c280a8badab4fc551f282");

  db.UpdateDeviceSeqNo(Name("/lijing"), 2);
  hash = db.RememberStateInStateLog();
  BOOST_CHECK_EQUAL(DigestComputer::digestToString(*hash),
                    "d2dfeda56ed98c0e17d455a859bc8c3b9e31c85c138c280a8badab4fc551f282");

  db.UpdateDeviceSeqNo(Name("/lijing"), 1);
  hash = db.RememberStateInStateLog();
  BOOST_CHECK_EQUAL(DigestComputer::digestToString(*hash),
                    "d2dfeda56ed98c0e17d455a859bc8c3b9e31c85c138c280a8badab4fc551f282");

  db.UpdateLocator(Name("/lijing"), Name("/hawaii"));

  BOOST_CHECK_EQUAL(db.LookupLocator(Name("/lijing")), Name("/hawaii"));

  SyncStateMsgPtr msg =
    db.FindStateDifferences("00",
                            "95284d3132a7a88b85c5141ca63efa68b7a7daf37315def69e296a0c24692833");
  BOOST_CHECK_EQUAL(msg->state_size(), 0);

  msg = db.FindStateDifferences("00",
                                "d2dfeda56ed98c0e17d455a859bc8c3b9e31c85c138c280a8badab4fc551f282");
  BOOST_CHECK_EQUAL(msg->state_size(), 1);
  BOOST_CHECK_EQUAL(msg->state(0).type(), SyncState::UPDATE);
  BOOST_CHECK_EQUAL(msg->state(0).seq(), 2);

  msg = db.FindStateDifferences("d2dfeda56ed98c0e17d455a859bc8c3b9e31c85c138c280a8badab4fc551f282",
                                "00");
  BOOST_CHECK_EQUAL(msg->state_size(), 1);
  BOOST_CHECK_EQUAL(msg->state(0).type(), SyncState::DELETE);

  msg = db.FindStateDifferences("94d988a90c6a3d0f74624368be65e5369ddddb3444841fad4ef41f674b937f26",
                                "d2dfeda56ed98c0e17d455a859bc8c3b9e31c85c138c280a8badab4fc551f282");
  BOOST_CHECK_EQUAL(msg->state_size(), 1);
  BOOST_CHECK_EQUAL(msg->state(0).type(), SyncState::UPDATE);
  BOOST_CHECK_EQUAL(msg->state(0).seq(), 2);

  msg = db.FindStateDifferences("d2dfeda56ed98c0e17d455a859bc8c3b9e31c85c138c280a8badab4fc551f282",
                                "94d988a90c6a3d0f74624368be65e5369ddddb3444841fad4ef41f674b937f26");
  BOOST_CHECK_EQUAL(msg->state_size(), 1);
  BOOST_CHECK_EQUAL(msg->state(0).type(), SyncState::UPDATE);
  BOOST_CHECK_EQUAL(msg->state(0).seq(), 0);

  db.UpdateDeviceSeqNo(Name("/shuai"), 1);
  hash = db.RememberStateInStateLog();
  BOOST_CHECK_EQUAL(DigestComputer::digestToString(*hash),
                    "602ff1878fc394b90e4a0e90c7409ea4b8ee8aa40169801d62f838470551db7c");

  msg = db.FindStateDifferences("00",
                                "602ff1878fc394b90e4a0e90c7409ea4b8ee8aa40169801d62f838470551db7c");
  BOOST_CHECK_EQUAL(msg->state_size(), 2);
  BOOST_CHECK_EQUAL(msg->state(0).type(), SyncState::UPDATE);
  BOOST_CHECK_EQUAL(msg->state(0).seq(), 2);

  BOOST_CHECK_EQUAL(msg->state(1).type(), SyncState::UPDATE);
  BOOST_CHECK_EQUAL(msg->state(1).seq(), 1);
}

BOOST_AUTO_TEST_SUITE_END()
//} // namespace tests
//} // namespace chronochat
