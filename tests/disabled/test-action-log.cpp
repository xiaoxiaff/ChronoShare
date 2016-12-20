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

#include <boost/test/unit_test.hpp>
#include <boost/lexical_cast.hpp>

#include "logging.hpp"
#include "action-log.hpp"

#include <unistd.h>
#include <iostream>
#include <boost/filesystem.hpp>
#include <boost/make_shared.hpp>
#include <thread>

using namespace std;
using namespace boost;
using namespace ndn;
namespace fs = boost::filesystem;

BOOST_AUTO_TEST_SUITE(TestActionLog)

BOOST_AUTO_TEST_CASE(UpdateAction)
{
  INIT_LOGGERS();

  Name localName("/lijing");

  fs::path tmpdir = fs::unique_path("./Loli_Test");
  std::cout << "tmpdir: " << tmpdir << std::endl;

  if (exists(tmpdir)) {
    remove_all(tmpdir);
  }
  shared_ptr<Face> face = make_shared<Face>();

  SyncLogPtr syncLog = make_shared<SyncLog>(tmpdir, localName);

  ActionLogPtr actionLog =
    make_shared<ActionLog>(face, tmpdir, syncLog, "top-secret", "test-chronoshare",
                                  ActionLog::OnFileAddedOrChangedCallback(),
                                  ActionLog::OnFileRemovedCallback());

  BOOST_CHECK_EQUAL(syncLog->SeqNo(localName), 0);

  BOOST_CHECK_EQUAL(syncLog->LogSize(), 0);
  BOOST_CHECK_EQUAL(actionLog->LogSize(), 0);

  actionLog
    ->AddLocalActionUpdate("file.txt",
                           DigestComputer::digestFromString(
                             "2ff304769cdb0125ac039e6fe7575f8576dceffc62618a431715aaf6eea2bf1c"),
                           std::time(NULL), 0755, 10);

  BOOST_CHECK_EQUAL(syncLog->SeqNo(localName), 1);
  BOOST_CHECK_EQUAL(syncLog->LogSize(), 0);
  BOOST_CHECK_EQUAL(actionLog->LogSize(), 1);

  ndn::ConstBufferPtr hash = syncLog->RememberStateInStateLog();
  BOOST_CHECK_EQUAL(syncLog->LogSize(), 1);
  BOOST_CHECK_EQUAL(DigestComputer::digestToString(*hash),
                    "91a849eede75acd56ae1bcb99e92d8fb28757683bc387dbb0e59c3108fcf4f18");

  ndn::shared_ptr<ndn::Data> data = actionLog->LookupActionData(localName, 0);

  BOOST_CHECK_EQUAL(bool(data), false);

  data = actionLog->LookupActionData(localName, 1);

  BOOST_CHECK_EQUAL(bool(data), true);

  BOOST_CHECK_EQUAL(data->getName(), "/lijing/test-chronoshare/action/top-secret/%01");

  ActionItemPtr action =
    actionLog->LookupAction(Name("/lijing/test-chronoshare/action/top-secret").appendNumber(0));
  BOOST_CHECK_EQUAL((bool)action, false);

  action =
    actionLog->LookupAction(Name("/lijing/test-chronoshare/action/top-secret").appendNumber(1));
  BOOST_CHECK_EQUAL((bool)action, true);

  if (action) {
    BOOST_CHECK_EQUAL(action->version(), 0);
    BOOST_CHECK_EQUAL(action->action(), 0);

    BOOST_CHECK_EQUAL(action->filename(), "file.txt");
    BOOST_CHECK_EQUAL(action->seg_num(), 10);
    BOOST_CHECK_EQUAL(action->file_hash().size(), 32);
    BOOST_CHECK_EQUAL(action->mode(), 0755);

    BOOST_CHECK_EQUAL(action->has_parent_device_name(), false);
    BOOST_CHECK_EQUAL(action->has_parent_seq_no(), false);
  }

  actionLog
    ->AddLocalActionUpdate("file.txt",
                           DigestComputer::digestFromString(
                             "2ff304769cdb0125ac039e6fe7575f8576dceffc62618a431715aaf6eea2bf1c"),
                           std::time(NULL), 0755, 10);
  BOOST_CHECK_EQUAL(syncLog->SeqNo(localName), 2);
  BOOST_CHECK_EQUAL(syncLog->LogSize(), 1);
  BOOST_CHECK_EQUAL(actionLog->LogSize(), 2);

  action = actionLog->LookupAction(Name("/lijing"), 2);
  BOOST_CHECK_EQUAL((bool)action, true);

  if (action) {
    BOOST_CHECK_EQUAL(action->has_parent_device_name(), true);
    BOOST_CHECK_EQUAL(action->has_parent_seq_no(), true);

    BOOST_CHECK_EQUAL(action->parent_seq_no(), 1);
    BOOST_CHECK_EQUAL(action->version(), 1);
  }

  BOOST_CHECK_EQUAL((bool)actionLog->AddRemoteAction(data), true);
  BOOST_CHECK_EQUAL(actionLog->LogSize(), 2);

  // create a real remote action
  ActionItemPtr item = make_shared<ActionItem>();
  item->set_action(ActionItem::UPDATE);
  item->set_filename("file.txt");
  item->set_version(2);
  item->set_timestamp(std::time(NULL));

  string item_msg;
  item->SerializeToString(&item_msg);

  Name actionName = Name("/zhenkai/test/test-chronoshare/action/top-secret").appendNumber(1);

  ndn::shared_ptr<Data> actionData = ndn::make_shared<Data>();
  actionData->setName(actionName);
  actionData->setContent(reinterpret_cast<const uint8_t*>(item_msg.c_str()), item_msg.size());
  ndn::KeyChain m_keyChain;
  m_keyChain.sign(*actionData);

  BOOST_CHECK_EQUAL((bool)actionLog->AddRemoteAction(actionData), true);
  BOOST_CHECK_EQUAL(actionLog->LogSize(), 3);

  if (exists(tmpdir)) {
    std::cout << "Clear ALL from test-action-log" << std::endl;
    face->shutdown();
  }
}

BOOST_AUTO_TEST_CASE(DeleteAction)
{
  INIT_LOGGERS();

  Name localName("/lijing");

  fs::path tmpdir = fs::unique_path("./Loli_Test");
  std::cout << "tmpdir: " << tmpdir << std::endl;

  if (exists(tmpdir)) {
    remove_all(tmpdir);
  }
  shared_ptr<Face> face = make_shared<Face>();

  SyncLogPtr syncLog = make_shared<SyncLog>(tmpdir, localName);

  ActionLogPtr actionLog =
    make_shared<ActionLog>(face, tmpdir, syncLog, "top-secret", "test-chronoshare",
                                  ActionLog::OnFileAddedOrChangedCallback(),
                                  ActionLog::OnFileRemovedCallback());

  BOOST_CHECK_EQUAL(syncLog->SeqNo(localName), 0);

  BOOST_CHECK_EQUAL(syncLog->LogSize(), 0);
  BOOST_CHECK_EQUAL(actionLog->LogSize(), 0);

  actionLog
    ->AddLocalActionUpdate("file.txt",
                           DigestComputer::digestFromString(
                             "2ff304769cdb0125ac039e6fe7575f8576dceffc62618a431715aaf6eea2bf1c"),
                           std::time(NULL), 0755, 10);

  BOOST_CHECK_EQUAL(syncLog->SeqNo(localName), 1);
  BOOST_CHECK_EQUAL(syncLog->LogSize(), 0);
  BOOST_CHECK_EQUAL(actionLog->LogSize(), 1);

  ndn::ConstBufferPtr hash = syncLog->RememberStateInStateLog();
  BOOST_CHECK_EQUAL(syncLog->LogSize(), 1);
  BOOST_CHECK_EQUAL(DigestComputer::digestToString(*hash),
                    "91a849eede75acd56ae1bcb99e92d8fb28757683bc387dbb0e59c3108fcf4f18");

  ndn::shared_ptr<ndn::Data> data = actionLog->LookupActionData(localName, 0);

  BOOST_CHECK_EQUAL(bool(data), false);

  data = actionLog->LookupActionData(localName, 1);

  BOOST_CHECK_EQUAL(bool(data), true);

  BOOST_CHECK_EQUAL(data->getName(), "/lijing/test-chronoshare/action/top-secret/%01");

  ActionItemPtr action =
    actionLog->LookupAction(Name("/lijing/test-chronoshare/action/top-secret").appendNumber(0));
  BOOST_CHECK_EQUAL((bool)action, false);

  action =
    actionLog->LookupAction(Name("/lijing/test-chronoshare/action/top-secret").appendNumber(1));
  BOOST_CHECK_EQUAL((bool)action, true);

  if (action) {
    BOOST_CHECK_EQUAL(action->version(), 0);
    BOOST_CHECK_EQUAL(action->action(), 0);

    BOOST_CHECK_EQUAL(action->filename(), "file.txt");
    BOOST_CHECK_EQUAL(action->seg_num(), 10);
    BOOST_CHECK_EQUAL(action->file_hash().size(), 32);
    BOOST_CHECK_EQUAL(action->mode(), 0755);

    BOOST_CHECK_EQUAL(action->has_parent_device_name(), false);
    BOOST_CHECK_EQUAL(action->has_parent_seq_no(), false);
  }

  actionLog
    ->AddLocalActionUpdate("file.txt",
                           DigestComputer::digestFromString(
                             "2ff304769cdb0125ac039e6fe7575f8576dceffc62618a431715aaf6eea2bf1c"),
                           std::time(NULL), 0755, 10);
  BOOST_CHECK_EQUAL(syncLog->SeqNo(localName), 2);
  BOOST_CHECK_EQUAL(syncLog->LogSize(), 1);
  BOOST_CHECK_EQUAL(actionLog->LogSize(), 2);

  action = actionLog->LookupAction(Name("/lijing"), 2);
  BOOST_CHECK_EQUAL((bool)action, true);

  if (action) {
    BOOST_CHECK_EQUAL(action->has_parent_device_name(), true);
    BOOST_CHECK_EQUAL(action->has_parent_seq_no(), true);

    BOOST_CHECK_EQUAL(action->parent_seq_no(), 1);
    BOOST_CHECK_EQUAL(action->version(), 1);
  }

  BOOST_CHECK_EQUAL((bool)actionLog->AddRemoteAction(data), true);
  BOOST_CHECK_EQUAL(actionLog->LogSize(), 2);

  // create a real remote action
  ActionItemPtr item = make_shared<ActionItem>();
  item->set_action(ActionItem::UPDATE);
  item->set_filename("file.txt");
  item->set_version(2);
  item->set_timestamp(std::time(NULL));

  string item_msg;
  item->SerializeToString(&item_msg);

  Name actionName = Name("/zhenkai/test/test-chronoshare/action/top-secret").appendNumber(1);

  ndn::shared_ptr<Data> actionData = ndn::make_shared<Data>();
  actionData->setName(actionName);
  actionData->setContent(reinterpret_cast<const uint8_t*>(item_msg.c_str()), item_msg.size());
  ndn::KeyChain m_keyChain;
  m_keyChain.sign(*actionData);

  BOOST_CHECK_EQUAL((bool)actionLog->AddRemoteAction(actionData), true);
  BOOST_CHECK_EQUAL(actionLog->LogSize(), 3);

  if (exists(tmpdir)) {
    std::cout << "Clear ALL from test-action-log" << std::endl;
    face->shutdown();
  }
}

BOOST_AUTO_TEST_SUITE_END()

// catch(boost::exception &err)
//   {
//     cout << *boost::get_error_info<errmsg_info_str>(err) << endl;
//   }
