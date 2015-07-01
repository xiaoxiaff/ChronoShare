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

#ifndef FETCH_MANAGER_H
#define FETCH_MANAGER_H

#include "fetcher.hpp"
#include "fetch-task-db.hpp"
#include "scheduler.hpp"
#include "executor.hpp"

#include <boost/exception/all.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/function.hpp>
#include <string>
#include <list>
#include <stdint.h>

namespace ndn {
namespace chronoshare {

class FetchManager {
public:
  enum { PRIORITY_NORMAL, PRIORITY_HIGH };

  typedef boost::function<ndn::Name(const ndn::Name&)> Mapping;
  typedef boost::function<void(ndn::Name& deviceName, ndn::Name& baseName, uint64_t seq,
                               ndn::shared_ptr<ndn::Data> data)> SegmentCallback;
  typedef boost::function<void(ndn::Name& deviceName, ndn::Name& baseName)> FinishCallback;
  FetchManager(shared_ptr<ndn::Face> face, const Mapping& mapping,
               const ndn::Name& broadcastForwardingHint, uint32_t parallelFetches = 3,
               const SegmentCallback& defaultSegmentCallback = SegmentCallback(),
               const FinishCallback& defaultFinishCallback = FinishCallback(),
               const FetchTaskDbPtr& taskDb = FetchTaskDbPtr());
  virtual ~FetchManager();

  void
  Enqueue(const ndn::Name& deviceName, const ndn::Name& baseName,
          const SegmentCallback& segmentCallback, const FinishCallback& finishCallback,
          uint64_t minSeqNo, uint64_t maxSeqNo, int priority = PRIORITY_NORMAL);

  // Enqueue using default callbacks
  void
  Enqueue(const ndn::Name& deviceName, const ndn::Name& baseName, uint64_t minSeqNo,
          uint64_t maxSeqNo, int priority = PRIORITY_NORMAL);

  // only for Fetcher
  inline shared_ptr<ndn::Face>
  GetFace();

private:
  // Fetch Events
  void
  DidDataSegmentFetched(Fetcher& fetcher, uint64_t seqno, const ndn::Name& basename,
                        const ndn::Name& name, ndn::shared_ptr<ndn::Data> data);

  void
  DidNoDataTimeout(Fetcher& fetcher);

  void
  DidFetchComplete(Fetcher& fetcher, const ndn::Name& deviceName, const ndn::Name& baseName);

  void
  ScheduleFetches();

  void
  TimedWait(Fetcher& fetcher);

private:
  shared_ptr<ndn::Face> m_face;
  Mapping m_mapping;

  uint32_t m_maxParallelFetches;
  uint32_t m_currentParallelFetches;
  boost::mutex m_parellelFetchMutex;

  // optimized list structure for fetch queue
  typedef boost::intrusive::member_hook<Fetcher, boost::intrusive::list_member_hook<>,
                                        &Fetcher::m_managerListHook> MemberOption;
  typedef boost::intrusive::list<Fetcher, MemberOption> FetchList;

  FetchList m_fetchList;
  SchedulerPtr m_scheduler;
  ExecutorPtr m_executor;
  TaskPtr m_scheduleFetchesTask;
  SegmentCallback m_defaultSegmentCallback;
  FinishCallback m_defaultFinishCallback;
  FetchTaskDbPtr m_taskDb;

  const ndn::Name m_broadcastHint;
};

shared_ptr<ndn::Face>
FetchManager::GetFace()
{
  return m_face;
}

typedef boost::error_info<struct tag_errmsg, std::string> errmsg_info_str;
namespace Error {
struct FetchManager : virtual boost::exception, virtual std::exception {
};
}

typedef shared_ptr<FetchManager> FetchManagerPtr;

} // chronoshare
} // ndn

#endif // FETCHER_H
