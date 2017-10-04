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

#include "fetch-task-db.hpp"

#include "test-common.hpp"

namespace ndn {
namespace chronoshare {
namespace tests {

namespace fs = boost::filesystem;

_LOG_INIT(Test.FetchTaskDb);

BOOST_AUTO_TEST_SUITE(TestFetchTaskDb)

class Checker
{
public:
  Checker(const Name& deviceName, const Name& baseName, uint64_t minSeqNo, uint64_t maxSeqNo, int priority)
    : m_deviceName(deviceName)
    , m_baseName(baseName)
    , m_minSeqNo(minSeqNo)
    , m_maxSeqNo(maxSeqNo)
    , m_priority(priority)
  {
  }

  Checker(const Checker& other)
    : m_deviceName(other.m_deviceName)
    , m_baseName(other.m_baseName)
    , m_minSeqNo(other.m_minSeqNo)
    , m_maxSeqNo(other.m_maxSeqNo)
    , m_priority(other.m_priority)
  {
  }

  bool
  operator==(const Checker& other)
  {
    return m_deviceName == other.m_deviceName && m_baseName == other.m_baseName &&
           m_minSeqNo == other.m_minSeqNo && m_maxSeqNo == other.m_maxSeqNo &&
           m_priority == other.m_priority;
  }

  void
  show()
  {
    std::cout << m_deviceName << ", " << m_baseName << ", " << m_minSeqNo << ", " << m_maxSeqNo << ", "
              << m_priority << std::endl;
  }

  Name m_deviceName;
  Name m_baseName;
  uint64_t m_minSeqNo;
  uint64_t m_maxSeqNo;
  int m_priority;
};

std::map<Name, Checker> checkers;
int g_counter = 0;

void
getChecker(const Name& deviceName, const Name& baseName, uint64_t minSeqNo, uint64_t maxSeqNo,
           int priority)
{
  _LOG_DEBUG("deviceName: " << deviceName << " baseName ");
  Checker checker(deviceName, baseName, minSeqNo, maxSeqNo, priority);
  g_counter++;
  Name whole(checker.m_deviceName);
  whole.append(checker.m_baseName);
  if (checkers.find(whole) != checkers.end()) {
    BOOST_FAIL("duplicated checkers");
  }

  checkers.insert(std::make_pair(whole, checker));
}

BOOST_AUTO_TEST_CASE(FetchTaskDbTest)
{
  fs::path tmpdir = fs::unique_path(UNIT_TEST_CONFIG_PATH) / "TaskDbTest";
  if (exists(tmpdir)) {
    remove_all(tmpdir);
  }

  fs::create_directories(tmpdir / ".chronoshare");

  FetchTaskDbPtr db = make_shared<FetchTaskDb>(tmpdir, "test");

  std::map<Name, Checker> m1;
  g_counter = 0;

  checkers.clear();

  Name deviceNamePrefix("/device");
  Name baseNamePrefix("/device/base");

  // add 10 tasks
  for (uint64_t i = 0; i < 10; i++) {
    Name d = deviceNamePrefix;
    Name b = baseNamePrefix;
    Checker c(d.appendNumber(i), b.appendNumber(i), i, 11, 1);
    m1.insert(std::make_pair(d.append(b), c));
    db->addTask(c.m_deviceName, c.m_baseName, c.m_minSeqNo, c.m_maxSeqNo, c.m_priority);
  }

  // delete the latter 5
  for (uint64_t i = 5; i < 10; i++) {
    Name d = deviceNamePrefix;
    Name b = baseNamePrefix;
    d.appendNumber(i);
    b.appendNumber(i);
    db->deleteTask(d, b);
  }

  // add back 3 to 7, 3 and 4 should not be added twice

  for (uint64_t i = 3; i < 8; i++) {
    Name d = deviceNamePrefix;
    Name b = baseNamePrefix;
    Checker c(d.appendNumber(i), b.appendNumber(i), i, 11, 1);
    db->addTask(c.m_deviceName, c.m_baseName, c.m_minSeqNo, c.m_maxSeqNo, c.m_priority);
  }

  db->foreachTask(bind(getChecker, _1, _2, _3, _4, _5));

  BOOST_CHECK_EQUAL(g_counter, 8);

  std::map<Name, Checker>::iterator it = checkers.begin();
  while (it != checkers.end()) {
    _LOG_DEBUG("first -> " << it->first);
    std::map<Name, Checker>::iterator mt = m1.find(it->first);
    if (mt == m1.end()) {
      BOOST_FAIL("unknown task found");
    }
    else {
      Checker c1 = it->second;
      Checker c2 = mt->second;
      BOOST_CHECK(c1 == c2);
      if (!(c1 == c2)) {
        std::cout << "C1: " << std::endl;
        c1.show();
        std::cout << "C2: " << std::endl;
        c2.show();
      }
    }
    ++it;
  }
}

BOOST_AUTO_TEST_SUITE_END()

} // namespace tests
} // namespace chronoshare
} // namespace ndn
