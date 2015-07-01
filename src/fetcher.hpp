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

#ifndef FETCHER_H
#define FETCHER_H

#include <ndn-cxx/face.hpp>
#include "executor.hpp"
#include <boost/intrusive/list.hpp>
#include <boost/date_time/posix_time/posix_time_types.hpp>
#include <set>

namespace ndn {
namespace chronoshare {

class FetchManager;

class Fetcher {
public:
  typedef boost::function<void(ndn::Name& deviceName, ndn::Name& baseName, uint64_t seq,
                               ndn::shared_ptr<ndn::Data> data)> SegmentCallback;
  typedef boost::function<void(ndn::Name& deviceName, ndn::Name& baseName)> FinishCallback;
  typedef boost::function<void(Fetcher&, const ndn::Name& deviceName, const ndn::Name& baseName)>
    OnFetchCompleteCallback;
  typedef boost::function<void(Fetcher&)> OnFetchFailedCallback;

  Fetcher(shared_ptr<ndn::Face> face, ExecutorPtr executor,
          const SegmentCallback& segmentCallback, // callback passed by caller of FetchManager
          const FinishCallback& finishCallback,   // callback passed by caller of FetchManager
          OnFetchCompleteCallback onFetchComplete,
          OnFetchFailedCallback onFetchFailed, // callbacks provided by FetchManager
          const ndn::Name& deviceName, const ndn::Name& name, int64_t minSeqNo, int64_t maxSeqNo,
          boost::posix_time::time_duration timeout =
            boost::posix_time::seconds(30), // this time is not precise, but sets min bound
                                            // actual time depends on how fast Interests timeout
          const ndn::Name& forwardingHint = ndn::Name());
  virtual ~Fetcher();

  inline bool
  IsActive() const;

  inline bool
  IsTimedWait() const
  {
    return m_timedwait;
  }

  void
  RestartPipeline();

  void
  SetForwardingHint(const ndn::Name& forwardingHint);

  const ndn::Name&
  GetForwardingHint() const
  {
    return m_forwardingHint;
  }

  const ndn::Name&
  GetName() const
  {
    return m_name;
  }

  const ndn::Name&
  GetDeviceName() const
  {
    return m_deviceName;
  }

  double
  GetRetryPause() const
  {
    return m_retryPause;
  }

  void
  SetRetryPause(double pause)
  {
    m_retryPause = pause;
  }

  boost::posix_time::ptime
  GetNextScheduledRetry() const
  {
    return m_nextScheduledRetry;
  }

  void
  SetNextScheduledRetry(boost::posix_time::ptime nextScheduledRetry)
  {
    m_nextScheduledRetry = nextScheduledRetry;
  }

private:
  void
  FillPipeline();

  void
  OnData(uint64_t seqno, const ndn::Interest& interest, ndn::Data& data);

  void
  OnData_Execute(uint64_t seqno, const ndn::Interest& interest, ndn::Data& data);

  void
  OnTimeout(uint64_t seqno, const ndn::Interest& interest);

  void
  OnTimeout_Execute(uint64_t seqno, const ndn::Interest& interest);

public:
  boost::intrusive::list_member_hook<> m_managerListHook;

private:
  shared_ptr<ndn::Face> m_face;

  SegmentCallback m_segmentCallback;
  OnFetchCompleteCallback m_onFetchComplete;
  OnFetchFailedCallback m_onFetchFailed;

  FinishCallback m_finishCallback;

  bool m_active;
  bool m_timedwait;

  ndn::Name m_name;
  ndn::Name m_deviceName;
  ndn::Name m_forwardingHint;

  boost::posix_time::time_duration m_maximumNoActivityPeriod;

  int64_t m_minSendSeqNo;
  int64_t m_maxInOrderRecvSeqNo;
  std::set<int64_t> m_outOfOrderRecvSeqNo;
  std::set<int64_t> m_inActivePipeline;

  int64_t m_minSeqNo;
  int64_t m_maxSeqNo;

  uint32_t m_pipeline;
  uint32_t m_activePipeline;

  boost::posix_time::ptime m_lastPositiveActivity;

  double m_retryPause; // pause to stop trying to fetch(for fetch-manager)
  boost::posix_time::ptime m_nextScheduledRetry;

  ExecutorPtr m_executor; // to serialize FillPipeline events

  boost::mutex m_seqNoMutex;
};

typedef boost::error_info<struct tag_errmsg, std::string> errmsg_info_str;

namespace Error {
struct Fetcher : virtual boost::exception, virtual std::exception {
};
}

typedef shared_ptr<Fetcher> FetcherPtr;

bool
Fetcher::IsActive() const
{
  return m_active;
}

} // chronoshare
} // ndn

#endif // FETCHER_H
