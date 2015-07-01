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
 * Author: Alexander Afanasyev <alexander.afanasyev@ucla.edu>
 *	   Zhenkai Zhu <zhenkai@cs.ucla.edu>
 */

#include "fetch-manager.h"
#include "fetcher.h"
#include <boost/test/unit_test.hpp>
#include <boost/make_shared.hpp>
#include "logging.h"
#include <ndn-cxx/security/key-chain.hpp>

INIT_LOGGER("Test.FetchManager");

using namespace ndn;
using namespace std;
using namespace boost;

BOOST_AUTO_TEST_SUITE(TestFetchManager)

struct FetcherTestData {
  set<uint64_t> recvData;
  set<uint64_t> recvContent;

  set<Name> differentNames;
  set<Name> segmentNames;

  bool m_done;
  bool m_failed;

  FetcherTestData()
    : m_done(false)
    , m_failed(false)
  {
  }

  void
  on_Data(const Interest& interest, const Data& data)
  {
    std::cout << data << std::endl;
  }

  void
  on_Timeout(const Interest& interest)
  {
    std::cout << "Timeout " << interest << std::endl;
  }

  void
  listen(boost::shared_ptr<ndn::Face> face)
  {
    //    face->registerPrefix(Name("/base"),
    //                         RegisterPrefixSuccessCallback(),
    //                         RegisterPrefixFailureCallback());

    face->setInterestFilter("/base", bind(&FetcherTestData::onInterest, this, _1, _2),
                            RegisterPrefixSuccessCallback(),
                            bind(&FetcherTestData::onRegisterFailed, this, _1, _2));
    std::cout << "Set Filter OK!" << std::endl;
    face->processEvents();
  }

  void
  onInterest(const InterestFilter& filter, const Interest& interest)
  {
    std::cout << "<< I: " << interest << std::endl;
  }

  void
  onRegisterFailed(const Name& prefix, const std::string& reason)
  {
    std::cerr << "ERROR: Failed to register prefix \"" << prefix << "\" in local hub's daemon ("
              << reason << ")" << std::endl;
  }

  void
  onData(const ndn::Name& deviceName, const ndn::Name& basename, uint64_t seqno,
         ndn::shared_ptr<ndn::Data> data)
  {
    _LOG_TRACE("onData: " << seqno);

    recvData.insert(seqno);
    differentNames.insert(basename);
    Name name = basename;
    name.appendNumber(seqno);
    segmentNames.insert(name);

    ndn::Block block = data->getContent();

    if (block.value_size() == sizeof(int)) {
      recvContent.insert(*reinterpret_cast<const int*>(block.value()));
    }

    cout << "onData Called!! <<< " << basename << ", " << name << ", " << seqno << endl;
  }

  void
  finish(const ndn::Name& deviceName, const ndn::Name& baseName)
  {
  }

  void
  onComplete(Fetcher& fetcher)
  {
    m_done = true;
    // cout << "Done" << endl;
  }

  void
  onFail(Fetcher& fetcher)
  {
    m_failed = true;
    // cout << "Failed" << endl;
  }
};

BOOST_AUTO_TEST_CASE(TestFetcher)
{
  INIT_LOGGERS();

  boost::shared_ptr<ndn::Face> face = boost::make_shared<ndn::Face>();

  Name baseName("/base");
  Name deviceName("/device");
  ndn::KeyChain keyChain;

  FetcherTestData ftData;

  boost::thread m_listeningThread(boost::bind(&FetcherTestData::listen, ftData, face));

  /* publish seqnos:  0, 1, 2, 3, 4, 5, 6, 7, 8, 9, <gap 5>, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24,
   * <gap 1>, 26 */
  // this will allow us to test our pipeline of 6

  for (int i = 0; i < 10; i++) {
    ndn::shared_ptr<Data> data = ndn::make_shared<Data>();
    Name name(baseName);
    name.appendNumber(i);
    std::cout << "Put name " << name << std::endl;
    data->setName(name);
    data->setFreshnessPeriod(time::seconds(30));
    data->setContent(reinterpret_cast<const unsigned char*>(&i), sizeof(int));
    keyChain.sign(*data);
    face->put(*data);
    std::cout << "Put data i---" << i << std::endl;
  }

  for (int i = 15; i < 25; i++) {
    ndn::shared_ptr<Data> data = ndn::make_shared<Data>();
    Name name(baseName);
    name.appendNumber(i);
    data->setName(name);
    std::cout << "Put name " << name << std::endl;
    data->setFreshnessPeriod(time::seconds(30));
    data->setContent(reinterpret_cast<const unsigned char*>(&i), sizeof(int));
    keyChain.sign(*data);
    face->put(*data);
    std::cout << "Put data i---" << i << std::endl;
  }

  int oneMore = 26;

  {
    ndn::shared_ptr<Data> data = ndn::make_shared<Data>();
    Name name(baseName);
    name.appendNumber(oneMore);
    data->setName(name);
    std::cout << "Put name " << name << std::endl;
    data->setFreshnessPeriod(time::seconds(30));
    data->setContent(reinterpret_cast<const unsigned char*>(&oneMore), sizeof(int));
    keyChain.sign(*data);
    face->put(*data);
    std::cout << "Put data oneMore---" << oneMore << std::endl;
  }

  std::cout << "baseName " << baseName << std::endl;

  ndn::Interest interest(ndn::Name("/base").appendNumber(0));
  face->expressInterest(interest, boost::bind(&FetcherTestData::on_Data, &ftData, _1, _2),
                        boost::bind(&FetcherTestData::on_Timeout, &ftData, _1));

  std::cout << "Express Interest " << interest << std::endl;
  ExecutorPtr executor = boost::make_shared<Executor>(1);
  executor->start();

  Fetcher fetcher(face, executor, bind(&FetcherTestData::onData, &ftData, _1, _2, _3, _4),
                  bind(&FetcherTestData::finish, &ftData, _1, _2),
                  bind(&FetcherTestData::onComplete, &ftData, _1),
                  bind(&FetcherTestData::onFail, &ftData, _1), deviceName, Name("/base"), 0, 26,
                  boost::posix_time::seconds(5)); // this time is not precise

  BOOST_CHECK_EQUAL(fetcher.IsActive(), false);
  fetcher.RestartPipeline();
  BOOST_CHECK_EQUAL(fetcher.IsActive(), true);

  usleep(7000000);
  BOOST_CHECK_EQUAL(ftData.m_failed, true);
  BOOST_CHECK_EQUAL(ftData.differentNames.size(), 1);
  BOOST_CHECK_EQUAL(ftData.segmentNames.size(), 20);
  BOOST_CHECK_EQUAL(ftData.recvData.size(), 20);
  BOOST_CHECK_EQUAL(ftData.recvContent.size(), 20);

  {
    ostringstream recvData;
    for (set<uint64_t>::iterator i = ftData.recvData.begin(); i != ftData.recvData.end(); i++)
      recvData << *i << ", ";

    ostringstream recvContent;
    for (set<uint64_t>::iterator i = ftData.recvContent.begin(); i != ftData.recvContent.end(); i++)
      recvContent << *i << ", ";

    BOOST_CHECK_EQUAL(recvData.str(),
                      "0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, ");
    BOOST_CHECK_EQUAL(recvData.str(), recvContent.str());
  }

  BOOST_CHECK_EQUAL(fetcher.IsActive(), false);
  fetcher.RestartPipeline();
  BOOST_CHECK_EQUAL(fetcher.IsActive(), true);

  usleep(7000000);
  BOOST_CHECK_EQUAL(ftData.m_failed, true);

  // publishing missing pieces
  for (int i = 0; i < 27; i++) {
    ndn::shared_ptr<Data> data = ndn::make_shared<Data>();
    Name name(baseName);
    name.appendNumber(i);
    data->setName(name);
    data->setFreshnessPeriod(time::seconds(1));
    data->setContent(reinterpret_cast<const unsigned char*>(&i), sizeof(int));
    keyChain.sign(*data);
    face->put(*data);
  }
  BOOST_CHECK_EQUAL(fetcher.IsActive(), false);
  fetcher.RestartPipeline();
  BOOST_CHECK_EQUAL(fetcher.IsActive(), true);

  usleep(1000000);
  BOOST_CHECK_EQUAL(ftData.m_done, true);

  {
    ostringstream recvData;
    for (set<uint64_t>::iterator i = ftData.recvData.begin(); i != ftData.recvData.end(); i++)
      recvData << *i << ", ";

    ostringstream recvContent;
    for (set<uint64_t>::iterator i = ftData.recvContent.begin(); i != ftData.recvContent.end(); i++)
      recvContent << *i << ", ";

    BOOST_CHECK_EQUAL(recvData.str(), "0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, "
                                      "17, 18, 19, 20, 21, 22, 23, 24, 25, 26, ");
    BOOST_CHECK_EQUAL(recvData.str(), recvContent.str());
  }

  executor->shutdown();
  face->shutdown();
}

// BOOST_AUTO_TEST_CASE(ndnWrapperSelector)
// {

//   Closure closure(bind(ftDataCallback, _1, _2), bind(timeout, _1));

//   Selectors selectors;
//   selectors.interestLifetime(1);

//   string n1 = "/random/01";
//   c1->sendInterest(Name(n1), closure, selectors);
//   sleep(2);
//   c2->publishData(Name(n1),(const unsigned char *)n1.c_str(), n1.size(), 4);
//   usleep(100000);
//   BOOST_CHECK_EQUAL(g_timeout_counter, 1);
//   BOOST_CHECK_EQUAL(g_ftDataCallback_counter, 0);

//   string n2 = "/random/02";
//   selectors.interestLifetime(2);
//   c1->sendInterest(Name(n2), closure, selectors);
//   sleep(1);
//   c2->publishData(Name(n2),(const unsigned char *)n2.c_str(), n2.size(), 4);
//   usleep(100000);
//   BOOST_CHECK_EQUAL(g_timeout_counter, 1);
//   BOOST_CHECK_EQUAL(g_ftDataCallback_counter, 1);

//   // reset
//   g_ftDataCallback_counter = 0;
//   g_timeout_counter = 0;
// }

BOOST_AUTO_TEST_SUITE_END()
