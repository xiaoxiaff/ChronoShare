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
 *         Lijing Wang <wanglj11@mails.tsinghua.edu.cn>
 */

#include "digest-computer.h"
#include "content-server.h"
#include "logging.h"
#include <boost/make_shared.hpp>
#include <utility>
#include "task.h"
#include "periodic-task.h"
#include "simple-interval-generator.h"
#include <boost/lexical_cast.hpp>
#include <boost/tuple/tuple.hpp>
#include <ndn-cxx/face.hpp>

INIT_LOGGER("ContentServer");

using namespace ndn;
using namespace std;
using namespace boost;

static const int DB_CACHE_LIFETIME = 60;

ContentServer::ContentServer(boost::shared_ptr<ndn::Face> face, ActionLogPtr actionLog,
                             const boost::filesystem::path &rootDir,
                             const ndn::Name &userName, const std::string &sharedFolderName,
                             const std::string &appName,
                             int freshness)
  : m_face(face)
  , m_actionLog(actionLog)
  , m_dbFolder(rootDir / ".chronoshare")
  , m_freshness(freshness)
  , m_scheduler(new Scheduler())
  , m_userName(userName)
  , m_sharedFolderName(sharedFolderName)
  , m_appName(appName)
{
//  m_listeningThread = boost::thread(boost::bind(&ContentServer::listen, this));

  m_scheduler->start();
  TaskPtr flushStaleDbCacheTask = boost::make_shared<PeriodicTask>(boost::bind(&ContentServer::flushStaleDbCache, this), "flush-state-db-cache", m_scheduler, boost::make_shared<SimpleIntervalGenerator>(DB_CACHE_LIFETIME));
  m_scheduler->addTask(flushStaleDbCacheTask);
}

ContentServer::~ContentServer()
{
  m_scheduler->shutdown();

  ScopedLock lock(m_mutex);
  for (FilterIdIt it = m_interestFilterIds.begin(); it != m_interestFilterIds.end(); ++it)
  {
    m_face->unsetInterestFilter(it->second);
  }

  m_interestFilterIds.clear();
  
}

void
ContentServer::registerPrefix(const Name &forwardingHint)
{
  // Format for files:   /<forwarding-hint>/<device_name>/<appname>/file/<hash>/<segment>
  // Format for actions: /<forwarding-hint>/<device_name>/<appname>/action/<shared-folder>/<action-seq>

  _LOG_DEBUG(">> content server: register " << forwardingHint);

  ScopedLock lock(m_mutex);
  m_interestFilterIds[forwardingHint]= m_face->setInterestFilter(ndn::InterestFilter(forwardingHint), 
                                                                 boost::bind(&ContentServer::filterAndServe, this, _1, _2),
                                                                 RegisterPrefixSuccessCallback(),
                                                                 RegisterPrefixFailureCallback());

}

void
ContentServer::deregisterPrefix(const Name &forwardingHint)
{
  _LOG_DEBUG("<< content server: deregister " << forwardingHint);
  m_face->unsetInterestFilter(m_interestFilterIds[forwardingHint]);

  ScopedLock lock(m_mutex);
  m_interestFilterIds.erase(forwardingHint);
}


void
ContentServer::filterAndServeImpl(const Name &forwardingHint, const Name &name, const Name &interest)
{
  // interest for files:   /<forwarding-hint>/<device_name>/<appname>/file/<hash>/<segment>
  // interest for actions: /<forwarding-hint>/<device_name>/<appname>/action/<shared-folder>/<action-seq>

  // name for files:   /<device_name>/<appname>/file/<hash>/<segment>
  // name for actions: /<device_name>/<appname>/action/<shared-folder>/<action-seq>

  if (name.size() >= 4 && name.get(-4).toUri() == m_appName)
  {
     string type = name.get(-3).toUri();
     if (type == "file")
     {
        serve_File(forwardingHint, name, interest);
     }
     else if (type == "action")
     {
        string folder = name.get(-2).toUri();
        if (folder == m_sharedFolderName)
        {
           serve_Action(forwardingHint, name, interest);
        }
     }
  }

}

void
ContentServer::filterAndServe(const InterestFilter& interestFilter, const Interest& interestTrue)
{

  Name forwardingHint =  Name(interestFilter);
  Name interest = interestTrue.getName();
  _LOG_DEBUG("I'm serving ForwardingHint: " << forwardingHint << " Interest: " << interest);
  if (forwardingHint.size() > 0 &&
      m_userName.size() >= forwardingHint.size() &&
      m_userName.getSubName(0, forwardingHint.size()) == forwardingHint)
    {
      _LOG_DEBUG("Triggered without Forwardinghint!");
      filterAndServeImpl(Name("/"), interest, interest); // try without forwarding hints
    }

  _LOG_DEBUG("Triggered with Forwardinghint~!");
  filterAndServeImpl(forwardingHint, interest.getSubName(forwardingHint.size()), interest); // always try with hint... :( have to

}

void
ContentServer::serve_Action(const Name &forwardingHint, const Name &name, const Name &interest)
{
  _LOG_DEBUG(">> content server serving ACTION, hint: " << forwardingHint << ", interest: " << interest);
  m_scheduler->scheduleOneTimeTask(m_scheduler, 0, bind(&ContentServer::serve_Action_Execute, this, forwardingHint, name, interest), boost::lexical_cast<string>(name));
  // need to unlock ccnx mutex... or at least don't lock it
}

void
ContentServer::serve_File(const Name &forwardingHint, const Name &name, const Name &interest)
{
  _LOG_DEBUG(">> content server serving FILE, hint: " << forwardingHint << ", interest: " << interest);

  m_scheduler->scheduleOneTimeTask(m_scheduler, 0, bind(&ContentServer::serve_File_Execute, this, forwardingHint, name, interest), boost::lexical_cast<string>(name));
  // need to unlock ccnx mutex... or at least don't lock it
}

void
ContentServer::serve_File_Execute(const Name &forwardingHint, const Name &name, const Name &interest)
{
  // forwardingHint: /<forwarding-hint>
  // interest:       /<forwarding-hint>/<device_name>/<appname>/file/<hash>/<segment>
  // name:           /<device_name>/<appname>/file/<hash>/<segment>

  int64_t segment = name.get(-1).toNumber();
  ndn::Name deviceName = name.getSubName(0, name.size() - 4);
  ndn::Buffer hash(name.get(-2).value(), name.get(-2).value_size());

  _LOG_DEBUG(" server FILE for device: " << deviceName << ", file_hash: " << DigestComputer::shortDigest(hash) << " segment: " << segment);

  string hashStr = DigestComputer::digestToString(hash);

  ObjectDbPtr db;

  ScopedLock(m_dbCacheMutex);
  {
    DbCache::iterator it = m_dbCache.find(hash);
    if (it != m_dbCache.end())
    {
      db = it->second;
    }
    else
    {
      if (ObjectDb::DoesExist(m_dbFolder, deviceName, hashStr)) // this is kind of overkill, as it counts available segments
        {
         db = boost::make_shared<ObjectDb>(m_dbFolder, hashStr);
         m_dbCache.insert(make_pair(hash, db));
        }
      else
        {
          _LOG_ERROR("ObjectDd doesn't exist for device: " << deviceName << ", file_hash: " << DigestComputer::shortDigest(hash));
        }
    }
  }

  if (db)
  {
    ndn::BufferPtr co = db->fetchSegment(deviceName, segment);
    if (co)
      {

        ndn::shared_ptr<ndn::Data> data = ndn::make_shared<ndn::Data>();
        data->setContent(co->buf(), co->size());
        if (forwardingHint.size() == 0)
          {
            _LOG_DEBUG(deviceName << "forwardingHint.size = 0 Name: " << name);
            data->setName(name);
          }
        else
          {
            if (m_freshness > 0)
              {
              	data->setFreshnessPeriod(time::seconds(m_freshness));
              }
            data->setName(interest);
          }
         m_keyChain.sign(*data);
         m_face->put(*data);
         _LOG_DEBUG("Send File Data Done!");
      }
    else
      {
        _LOG_ERROR("ObjectDd exists, but no segment " << segment << " for device: " << deviceName << ", file_hash: " << DigestComputer::shortDigest(hash));
      }

  }
}

void
ContentServer::serve_Action_Execute(const Name &forwardingHint, const Name &name, const Name &interest)
{
  // forwardingHint: /<forwarding-hint>
  // interest:       /<forwarding-hint>/<device_name>/<appname>/action/<shared-folder>/<action-seq>
  // name for actions: /<device_name>/<appname>/action/<shared-folder>/<action-seq>

  int64_t seqno = name.get(-1).toNumber();
  ndn::Name deviceName = name.getSubName(0, name.size() - 4);

  _LOG_DEBUG(" server ACTION for device: " << deviceName << " and seqno: " << seqno);

  ndn::shared_ptr<ndn::Data> data = m_actionLog->LookupActionData(deviceName, seqno);
  if (data)
    {
      if (forwardingHint.size() == 0)
        {
          m_keyChain.sign(*data);
          m_face->put(*data);
        }
      else
        {
          data->setName(interest);
          if (m_freshness > 0)
          {
        	  data->setFreshnessPeriod(time::seconds(m_freshness));
          }
          m_keyChain.sign(*data);
          m_face->put(*data);
        }
    }
  else
    {
      _LOG_ERROR("ACTION not found for device: " << deviceName << " and seqno: " << seqno);
    }
}

void
ContentServer::flushStaleDbCache()
{
  ScopedLock(m_dbCacheMutex);
  DbCache::iterator it = m_dbCache.begin();
  while (it != m_dbCache.end())
  {
    ObjectDbPtr db = it->second;
    if (db->secondsSinceLastUse() >= DB_CACHE_LIFETIME)
    {
      m_dbCache.erase(it++);
    }
    else
    {
      ++it;
    }
  }
}

