/* -*- Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil -*- */
/*
 * Copyright(c) 2013 University of California, Los Angeles
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
 * Author: Zhenkai Zhu <zhenkai@cs.ucla.edu>
 *         Alexander Afanasyev <alexander.afanasyev@ucla.edu>
 *	       Lijing Wang <wanglj11@mails.tsinghua.edu.cn>
 */

#include "sync-core.h"
#include "sync-state-helper.h"
#include "logging.h"
#include "random-interval-generator.h"
#include "simple-interval-generator.h"
#include "periodic-task.h"
#include "digest-computer.h"

#include <boost/lexical_cast.hpp>

INIT_LOGGER("Sync.Core");

const int SyncCore::FRESHNESS = 2;
const string SyncCore::RECOVER = "RECOVER";
const double SyncCore::WAIT = 0.05;
const double SyncCore::RANDOM_PERCENT = 0.5;

const std::string SYNC_INTEREST_TAG = "send-sync-interest";
const std::string SYNC_INTEREST_TAG2 = "send-sync-interest2";

const std::string LOCAL_STATE_CHANGE_DELAYED_TAG = "local-state-changed";

using namespace std;
using namespace boost;
using namespace ndn;


SyncCore::SyncCore(boost::shared_ptr<Face> face, SyncLogPtr syncLog, const Name &userName, const Name &localPrefix, const Name &syncPrefix,
                   const StateMsgCallback &callback, long syncInterestInterval/*= -1.0*/)
  : m_face(face)
  , m_log(syncLog)
  , m_scheduler(new Scheduler())
  , m_stateMsgCallback(callback)
  , m_syncPrefix(syncPrefix)
  , m_recoverWaitGenerator(new RandomIntervalGenerator(WAIT, RANDOM_PERCENT, RandomIntervalGenerator::UP))
  , m_syncInterestInterval(syncInterestInterval)
{
  m_rootDigest = m_log->RememberStateInStateLog();

//  m_face->setInterestFilter(m_syncPrefix, boost::bind(&SyncCore::handleInterest, this, _1));
  m_registeredPrefixId = m_face->setInterestFilter(m_syncPrefix,
                                                   boost::bind(&SyncCore::handleInterest, this, _1, _2),
                                                   RegisterPrefixSuccessCallback(),
                                                   boost::bind(&SyncCore::onRegisterFailed, this, _1, _2));

  // m_face start listening
//  m_listeningThread = boost::thread(boost::bind(&SyncCore::listen, this));
  // m_log->initYP(m_yp);
  m_log->UpdateLocalLocator(localPrefix);

  m_scheduler->start();

  double interval =(m_syncInterestInterval > 0 && m_syncInterestInterval < 30) ? m_syncInterestInterval : 4;
  m_sendSyncInterestTask = boost::make_shared<PeriodicTask>(bind(&SyncCore::sendSyncInterest, this), SYNC_INTEREST_TAG, m_scheduler, boost::make_shared<SimpleIntervalGenerator>(interval));
  // sendSyncInterest();
  Scheduler::scheduleOneTimeTask(m_scheduler, 0.1, bind(&SyncCore::sendSyncInterest, this), SYNC_INTEREST_TAG2);
}

SyncCore::~SyncCore()
{
  m_scheduler->shutdown();
//  m_face->shutdown();
//  m_listeningThread.detach();
  // need to "deregister" closures
  m_face->unsetInterestFilter(m_registeredPrefixId);

}

void
SyncCore::updateLocalState(sqlite3_int64 seqno)
{
  m_log->UpdateLocalSeqNo(seqno);
  localStateChanged();
}

void
SyncCore::localStateChanged()
{
  ndn::ConstBufferPtr oldDigest = m_rootDigest;
  m_rootDigest = m_log->RememberStateInStateLog();

  _LOG_DEBUG("[" << m_log->GetLocalName() << "] localStateChanged ");
  _LOG_TRACE("[" << m_log->GetLocalName() << "] publishes: oldDigest--" << DigestComputer::shortDigest(*oldDigest) << " newDigest--" << DigestComputer::shortDigest(*m_rootDigest));

  SyncStateMsgPtr msg = m_log->FindStateDifferences(*oldDigest, *m_rootDigest);

  // reply sync Interest with oldDigest as last component

  Name syncName(m_syncPrefix);
  syncName.appendImplicitSha256Digest(oldDigest);

  BufferPtr syncData = serializeGZipMsg(*msg);

  // Create Data packet
  ndn::shared_ptr<Data> data = ndn::make_shared<Data>();
  data->setName(syncName);
  data->setFreshnessPeriod(time::seconds(FRESHNESS));
  data->setContent(reinterpret_cast<const uint8_t*>(syncData->buf()), syncData->size());
  m_keyChain.sign(*data);
  m_face->put(*data);

  _LOG_TRACE(msg);

  m_scheduler->deleteTask(SYNC_INTEREST_TAG2);
  // no hurry in sending out new Sync Interest; if others send the new Sync Interest first, no problem, we know the new root digest already;
  // this is trying to avoid the situation that the order of SyncData and new Sync Interest gets reversed at receivers
  Scheduler::scheduleOneTimeTask(m_scheduler, 0.05,
                                  bind(&SyncCore::sendSyncInterest, this),
                                  SYNC_INTEREST_TAG2);

  //sendSyncInterest();
}

void
SyncCore::localStateChangedDelayed()
{
  // many calls to localStateChangedDelayed within 0.5 second will be suppressed to one localStateChanged calls
  Scheduler::scheduleOneTimeTask(m_scheduler, 0.5,
                                  bind(&SyncCore::localStateChanged, this),
                                  LOCAL_STATE_CHANGE_DELAYED_TAG);
}

// ------------------------------------------------------------------------------------ send & handle interest

void
SyncCore::sendSyncInterest()
{
  Name syncInterest(m_syncPrefix);
//  syncInterest.append(ndn::name::Component(*m_rootDigest));
  syncInterest.appendImplicitSha256Digest(m_rootDigest);

  _LOG_DEBUG("[" << m_log->GetLocalName() << "] >>> send SYNC Interest for " << DigestComputer::shortDigest(*m_rootDigest) << ": " << syncInterest.toUri());

  Interest interest(syncInterest);
  if (m_syncInterestInterval > 0 && m_syncInterestInterval < 30)
  {
    interest.setInterestLifetime(time::seconds(m_syncInterestInterval));
  }

  m_face->expressInterest(interest,
		                      boost::bind(&SyncCore::handleSyncData, this, _1, _2),
		                      boost::bind(&SyncCore::handleSyncInterestTimeout, this, _1));

  // if there is a pending syncSyncInterest task, reschedule it to be m_syncInterestInterval seconds from now
  // if no such task exists, it will be added
  m_scheduler->rescheduleTask(m_sendSyncInterestTask);
}

void
SyncCore::recover(ndn::ConstBufferPtr digest)
{
  if (!(*digest == *m_rootDigest) && m_log->LookupSyncLog(*digest) <= 0)
  {
    _LOG_TRACE(m_log->GetLocalName() << ", Recover for received_Digest " << DigestComputer::shortDigest(*digest));
    // unfortunately we still don't recognize this digest
    // append the unknown digest
    Name recoverInterest(m_syncPrefix);
    recoverInterest.append(RECOVER).appendImplicitSha256Digest(digest);

    _LOG_DEBUG("[" << m_log->GetLocalName() << "] >>> send RECOVER Interests for " << DigestComputer::shortDigest(*digest));

    m_face->expressInterest(recoverInterest,
    		boost::bind(&SyncCore::handleRecoverData, this, _1, _2),
    		boost::bind(&SyncCore::handleRecoverInterestTimeout, this, _1));
  }
  else
  {
    // we already learned the digest; cheers!
  }
}

void
SyncCore::handleInterest(const InterestFilter& filter, const Interest& interest)
{
  ndn::Name name = interest.getName();
  _LOG_DEBUG("[" << m_log->GetLocalName() << "] <<<< handleInterest with Name: " << name);
  int size = name.size();
  int prefixSize = m_syncPrefix.size();
  if (size == prefixSize + 1)
  {
    // this is normal sync interest
    handleSyncInterest(name);
  }
  else if (size == prefixSize + 2 && name.get(m_syncPrefix.size()).toUri() == RECOVER)
  {
    // this is recovery interest
    handleRecoverInterest(name);
  }
}

void
SyncCore::handleSyncInterest(const Name &name)
{
  _LOG_DEBUG("[" << m_log->GetLocalName() << "] <<<<< handle SYNC Interest with Name: " << name);

  ndn::ConstBufferPtr digest = ndn::make_shared<ndn::Buffer>(name.get(-1).value(), name.get(-1).value_size());
  if (*digest == *m_rootDigest)
  {
    // we have the same digest; nothing needs to be done
    _LOG_TRACE("same as root digest: " << DigestComputer::shortDigest(*digest));
    return;
  }
  else if (m_log->LookupSyncLog(*digest) > 0)
  {
    // we know something more
    _LOG_TRACE("found digest in sync log");
    SyncStateMsgPtr msg = m_log->FindStateDifferences(*digest, *m_rootDigest);

    BufferPtr syncData = serializeGZipMsg(*msg);
    ndn::shared_ptr<Data> data = ndn::make_shared<Data>();
    data->setName(name);
    data->setFreshnessPeriod(time::seconds(FRESHNESS));
    data->setContent(reinterpret_cast<const uint8_t*>(syncData->buf()), syncData->size());
    m_keyChain.sign(*data);
    m_face->put(*data);

    _LOG_TRACE(m_log->GetLocalName() << " publishes: " << DigestComputer::shortDigest(*digest) << " my_rootDigest:" << DigestComputer::shortDigest(*m_rootDigest));
    _LOG_TRACE(msg);
  }
  else
  {
    // we don't recognize the digest, send recover Interest if still don't know the digest after a randomized wait period
    double wait = m_recoverWaitGenerator->nextInterval();
    _LOG_TRACE(m_log->GetLocalName() << ", my_rootDigest: " << DigestComputer::shortDigest(*m_rootDigest) << ", received_Digest: " << DigestComputer::shortDigest(*digest));
    _LOG_TRACE("recover task scheduled after wait: " << wait);

    Scheduler::scheduleOneTimeTask(m_scheduler,
                                    wait, boost::bind(&SyncCore::recover, this, digest),
                                    "r-"+DigestComputer::digestToString(*digest));
  }
}

void
SyncCore::handleRecoverInterest(const Name &name)
{
  _LOG_DEBUG("[" << m_log->GetLocalName() << "] <<<<< handle RECOVER Interest with name " << name);

  ndn::ConstBufferPtr digest = ndn::make_shared<ndn::Buffer>(name.get(-1).value(), name.get(-1).value_size());
  // this is the digest unkonwn to the sender of the interest
  _LOG_DEBUG("rootDigest: " << DigestComputer::shortDigest(*digest));
  if (m_log->LookupSyncLog(*digest) > 0)
  {
     _LOG_DEBUG("Find in our sync_log! " << DigestComputer::shortDigest(*digest));
    // we know the digest, should reply everything and the newest thing, but not the digest!!! This is important
    unsigned char _origin = 0;
    ndn::BufferPtr origin = ndn::make_shared<ndn::Buffer>(_origin);
//    std::cout << "size of origin " << origin->size() << std::endl;
    SyncStateMsgPtr msg = m_log->FindStateDifferences(*origin, *m_rootDigest);

    BufferPtr syncData = serializeGZipMsg(*msg);
    ndn::shared_ptr<Data> data = ndn::make_shared<Data>();
    data->setName(name);
    data->setFreshnessPeriod(time::seconds(FRESHNESS));
    data->setContent(reinterpret_cast<const uint8_t*>(syncData->buf()), syncData->size());
    m_keyChain.sign(*data);
    m_face->put(*data);

    _LOG_TRACE("[" << m_log->GetLocalName() << "] publishes " << DigestComputer::shortDigest(*digest)
                   << " FindStateDifferences(0, m_rootDigest/" << DigestComputer::shortDigest(*m_rootDigest) << ")");
    _LOG_TRACE(msg);
  }
  else
    {
      // we don't recognize this digest, can not help
      _LOG_DEBUG("we don't recognize this digest, can not help");
    }
}


void
SyncCore::handleSyncInterestTimeout(const Interest &interest)
{
  // sync interest will be resent by scheduler
}

void
SyncCore::handleRecoverInterestTimeout(const Interest &interest)
{
  // We do not re-express recovery interest for now
  // if difference is not resolved, the sync interest will trigger
  // recovery anyway; so it's not so important to have recovery interest
  // re-expressed
}

void
SyncCore::handleSyncData(const Interest &interest, Data &data)
{
  _LOG_DEBUG("[" << m_log->GetLocalName() << "] <<<<< receive SYNC DATA with interest: " << interest.toUri());

  const Block &content = data.getContent();
  // suppress recover in interest - data out of order case
  if (data.getContent().value() && content.size() > 0)
    {
      handleStateData(ndn::Buffer(content.value(), content.value_size()));
    }
  else
    {
      _LOG_ERROR("Got sync DATA with empty content");
    }

  // resume outstanding sync interest
  // sendSyncInterest();

  m_scheduler->deleteTask(SYNC_INTEREST_TAG2);
  Scheduler::scheduleOneTimeTask(m_scheduler, 0,
                                  bind(&SyncCore::sendSyncInterest, this),
                                  SYNC_INTEREST_TAG2);
}

void
SyncCore::handleRecoverData(const Interest &interest, Data &data)
{
  _LOG_DEBUG("[" << m_log->GetLocalName() << "] <<<<< receive RECOVER DATA with interest: " << interest.toUri());
  //cout << "handle recover data" << end;
  const Block &content = data.getContent();
  if (content.value() && content.size() > 0)
    {
      handleStateData(Buffer(content.value(), content.value_size()));
    }
  else
    {
      _LOG_ERROR("Got recovery DATA with empty content");
    }

  // sendSyncInterest();
  m_scheduler->deleteTask(SYNC_INTEREST_TAG2);
  Scheduler::scheduleOneTimeTask(m_scheduler, 0,
                                  bind(&SyncCore::sendSyncInterest, this),
                                  SYNC_INTEREST_TAG2);
}

void
SyncCore::handleStateData(const Buffer &content)
{
  _LOG_DEBUG("handleStateData Begin");
  SyncStateMsgPtr msg = deserializeGZipMsg<SyncStateMsg>(content);
  if (!(msg))
  {
    // ignore misformed SyncData
    _LOG_ERROR("Misformed SyncData");
    return;
  }

  _LOG_TRACE("[" << m_log->GetLocalName() << "]" << " receives Msg ");
  _LOG_TRACE(msg);
  int size = msg->state_size();
  int index = 0;
  while (index < size)
  {
    SyncState state = msg->state(index);
    string devStr = state.name();
    Name deviceName(ndn::Block((const unsigned char *)devStr.c_str(), devStr.size()));
//    cout << "Got Name: " << deviceName;
    if (state.type() == SyncState::UPDATE)
    {
      sqlite3_int64 seqno = state.seq();
//      cout << ", Got seq: " << seqno << endl;
      m_log->UpdateDeviceSeqNo(deviceName, seqno);
      if (state.has_locator())
      {
        string locStr = state.locator();
        Name locatorName(ndn::Block((const unsigned char *)locStr.c_str(), locStr.size()));
//        cout << ", Got loc: " << locatorName << endl;
        m_log->UpdateLocator(deviceName, locatorName);

        _LOG_TRACE("self: " << m_log->GetLocalName() << ", device: " << deviceName << " < == > " << locatorName);
      }
    }
    else
    {
      _LOG_ERROR("Receive SYNC DELETE, but we don't support it yet");
      deregister(deviceName);
    }
    index++;
  }

  // find the actuall difference and invoke callback on the actual difference
  ndn::ConstBufferPtr oldDigest = m_rootDigest;
  m_rootDigest = m_log->RememberStateInStateLog();
  // get diff with both new SeqNo and old SeqNo
  SyncStateMsgPtr diff = m_log->FindStateDifferences(*oldDigest, *m_rootDigest, true);

  if (diff->state_size() > 0)
  {
    m_stateMsgCallback(diff);
  }
}

void
SyncCore::deregister(const Name &name)
{
  // handle deregistering
  // TODO deregister device_name?
}

sqlite3_int64
SyncCore::seq(const Name &name)
{
  return m_log->SeqNo(name);
}
