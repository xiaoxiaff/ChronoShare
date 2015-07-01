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

#ifndef SYNC_CORE_H
#define SYNC_CORE_H

#include <ndn-cxx/security/key-chain.hpp>
#include "sync-log.hpp"
#include "scheduler.hpp"
#include "task.hpp"
#include <ndn-cxx/face.hpp>

#include <boost/function.hpp>
#include <boost/iostreams/filter/gzip.hpp>
#include <boost/iostreams/filtering_stream.hpp>
#include <boost/iostreams/device/back_inserter.hpp>
#include <boost/make_shared.hpp>
#include <thread>

namespace ndn {
namespace chronoshare {

// No use this now
template<class Msg>
ndn::BufferPtr
serializeMsg(const Msg& msg)
{
  int size = msg.ByteSize();
  ndn::BufferPtr bytes = std::make_shared<ndn::Buffer>(size);
  msg.SerializeToArray(bytes->buf(), size);
  return bytes;
}

template<class Msg>
shared_ptr<Msg>
deserializeMsg(const ndn::Buffer& bytes)
{
  shared_ptr<Msg> retval(new Msg());
  if (!retval->ParseFromArray(bytes.buf(), bytes.size())) {
    // to indicate an error
    return shared_ptr<Msg>();
  }
  return retval;
}

template<class Msg>
ndn::BufferPtr
serializeGZipMsg(const Msg& msg)
{
  std::vector<char> bytes; // Bytes couldn't work
  {
    boost::iostreams::filtering_ostream out;
    out.push(boost::iostreams::gzip_compressor());    // gzip filter
    out.push(boost::iostreams::back_inserter(bytes)); // back_inserter sink

    msg.SerializeToOstream(&out);
  }
  ndn::BufferPtr uBytes = std::make_shared<ndn::Buffer>(bytes.size());
  memcpy(&(*uBytes)[0], &bytes[0], bytes.size());
  return uBytes;
}

template<class Msg>
shared_ptr<Msg>
deserializeGZipMsg(const ndn::Buffer& bytes)
{
  std::vector<char> sBytes(bytes.size());
  memcpy(&sBytes[0], &bytes[0], bytes.size());
  boost::iostreams::filtering_istream in;
  in.push(boost::iostreams::gzip_decompressor()); // gzip filter
  in.push(boost::make_iterator_range(sBytes));    // source

  shared_ptr<Msg> retval = boost::make_shared<Msg>();
  if (!retval->ParseFromIstream(&in)) {
    // to indicate an error
    return shared_ptr<Msg>();
  }

  return retval;
}

class SyncCore {
public:
  typedef boost::function<void(SyncStateMsgPtr stateMsg)> StateMsgCallback;

  static const int FRESHNESS; // seconds
  static const string RECOVER;
  static const double WAIT;           // seconds;
  static const double RANDOM_PERCENT; // seconds;

public:
  SyncCore(shared_ptr<ndn::Face> face, SyncLogPtr syncLog, const ndn::Name& userName,
           const ndn::Name& localPrefix // routable name used by the local user
           ,
           const ndn::Name& syncPrefix // the prefix for the sync collection
           ,
           const StateMsgCallback& callback // callback when state change is detected
           ,
           long syncInterestInterval = -1);
  ~SyncCore();

  void updateLocalState(sqlite3_int64);

  void
  localStateChanged();

  /**
   * @brief Schedule an event to update local state with a small delay
   *
   * This call is preferred to localStateChanged if many local state updates
   * are anticipated within a short period of time
   */
  void
  localStateChangedDelayed();

  // ------------------ only used in test -------------------------

public:
  ndn::ConstBufferPtr
  root() const
  {
    return m_rootDigest;
  }

  sqlite3_int64
  seq(const ndn::Name& name);

private:
  void
  listen()
  {
    m_face->processEvents();
  }

  void
  onRegisterFailed(const ndn::Name& prefix, const std::string& reason)
  {
    std::cerr << "ERROR: Failed to register prefix \"" << prefix << "\" in local hub's daemon ("
              << reason << ")" << std::endl;
    m_face->shutdown();
  }

  void
  sendSyncInterest();

  void
  recover(ndn::ConstBufferPtr digest);

  void
  handleInterest(const ndn::InterestFilter& filter, const ndn::Interest& interest);

  void
  handleSyncInterest(const ndn::Name& name);

  void
  handleRecoverInterest(const ndn::Name& name);

  void
  handleSyncInterestTimeout(const ndn::Interest& interest);

  void
  handleRecoverInterestTimeout(const ndn::Interest& interest);

  void
  handleSyncData(const ndn::Interest& interest, ndn::Data& data);

  void
  handleRecoverData(const ndn::Interest& interest, ndn::Data& data);

  void
  handleStateData(const ndn::Buffer& content);

  void
  deregister(const ndn::Name& name);

private:
  //  ndn::Face m_face;
  shared_ptr<ndn::Face> m_face;

  SyncLogPtr m_log;
  SchedulerPtr m_scheduler;
  StateMsgCallback m_stateMsgCallback;

  ndn::Name m_syncPrefix;
  ndn::ConstBufferPtr m_rootDigest;

  IntervalGeneratorPtr m_recoverWaitGenerator;

  TaskPtr m_sendSyncInterestTask;

  long m_syncInterestInterval;
  ndn::KeyChain m_keyChain;
  boost::thread m_listeningThread;
  const ndn::RegisteredPrefixId* m_registeredPrefixId;
};

} // chronoshare
} // ndn

#endif // SYNC_CORE_H
