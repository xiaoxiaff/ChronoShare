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

#ifndef ACTION_LOG_H
#define ACTION_LOG_H

#include "db-helper.hpp"
#include "file-state.hpp"
#include "sync-log.hpp"
#include "action-item.pb.hpp"
#include "file-item.pb.hpp"
#include <ndn-cxx/face.hpp>
#include <ndn-cxx/security/key-chain.hpp>

#include <boost/tuple/tuple.hpp>

namespace ndn {
namespace chronoshare {

class ActionLog;
typedef shared_ptr<ActionLog> ActionLogPtr;
typedef shared_ptr<ActionItem> ActionItemPtr;

class ActionLog : public DbHelper {
public:
  typedef boost::function<void(std::string /*filename*/, ndn::Name /*device_name*/,
                               sqlite3_int64 /*seq_no*/, ndn::ConstBufferPtr /*hash*/,
                               time_t /*m_time*/, int /*mode*/, int /*seg_num*/)>
    OnFileAddedOrChangedCallback;

  typedef boost::function<void(std::string /*filename*/)> OnFileRemovedCallback;

public:
  ActionLog(shared_ptr<ndn::Face> face, const boost::filesystem::path& path,
            SyncLogPtr syncLog, const std::string& sharedFolder, const std::string& appName,
            OnFileAddedOrChangedCallback onFileAddedOrChanged, OnFileRemovedCallback onFileRemoved);

  virtual ~ActionLog()
  {
  }

  //////////////////////////
  // Local operations     //
  //////////////////////////
  ActionItemPtr
  AddLocalActionUpdate(const std::string& filename, const ndn::Buffer& hash, time_t wtime, int mode,
                       int seg_num);

  // void
  // AddActionMove(const std::string &oldFile, const std::string &newFile);

  ActionItemPtr
  AddLocalActionDelete(const std::string& filename);

  //////////////////////////
  // Remote operations    //
  //////////////////////////

  ActionItemPtr
  AddRemoteAction(const ndn::Name& deviceName, sqlite3_int64 seqno,
                  ndn::shared_ptr<ndn::Data> actionData);

  /**
   * @brief Add remote action using just action's parsed content object
   *
   * This function extracts device name and sequence number from the content object's and calls the
   *overloaded method
   */
  ActionItemPtr
  AddRemoteAction(ndn::shared_ptr<ndn::Data> actionData);

  ///////////////////////////
  // General operations    //
  ///////////////////////////

  ndn::shared_ptr<ndn::Data>
  LookupActionData(const ndn::Name& deviceName, sqlite3_int64 seqno);

  ndn::shared_ptr<ndn::Data>
  LookupActionData(const ndn::Name& actionName);

  ActionItemPtr
  LookupAction(const ndn::Name& deviceName, sqlite3_int64 seqno);

  ActionItemPtr
  LookupAction(const ndn::Name& actionName);

  FileItemPtr
  LookupAction(const std::string& filename, sqlite3_int64 version, const ndn::Buffer& filehash);

  /**
   * @brief Lookup up to [limit] actions starting [offset] in decreasing order(by timestamp) and
   * calling visitor(device_name,seqno,action) for each action
   */
  bool
  LookupActionsInFolderRecursively(
    const boost::function<void(const ndn::Name& name, sqlite3_int64 seq_no, const ActionItem&)>&
      visitor,
    const std::string& folder, int offset = 0, int limit = -1);

  bool
  LookupActionsForFile(const boost::function<void(const ndn::Name& name, sqlite3_int64 seq_no,
                                                  const ActionItem&)>& visitor,
                       const std::string& file, int offset = 0, int limit = -1);

  void
  LookupRecentFileActions(const boost::function<void(const std::string&, int, int)>& visitor,
                          int limit = 5);

  //
  inline FileStatePtr
  GetFileState();

public:
  // for test purposes
  sqlite3_int64
  LogSize();

private:
  boost::tuple<sqlite3_int64 /*version*/, ndn::BufferPtr /*device name*/, sqlite3_int64 /*seq_no*/>
  GetLatestActionForFile(const std::string& filename);

  static void
  apply_action_xFun(sqlite3_context* context, int argc, sqlite3_value** argv);

  //  static shared_ptr<ActionItem>
  //  deserializeActionItem(const ndn::Block &content)
  //  {
  //  	shared_ptr<ActionItem> retval(new ActionItem());
  //  	if (!retval->ParseFromArray(content.value(), content.value_size()))
  //  	{
  //  		return shared_ptr<ActionItem>();
  //  	}
  //  	return retval;
  //  }

private:
  SyncLogPtr m_syncLog;
  FileStatePtr m_fileState;

  shared_ptr<ndn::Face> m_face;
  std::string m_sharedFolderName;
  std::string m_appName;

  OnFileAddedOrChangedCallback m_onFileAddedOrChanged;
  OnFileRemovedCallback m_onFileRemoved;
  ndn::KeyChain m_keyChain;
};

namespace Error {
struct ActionLog : virtual boost::exception, virtual std::exception {
};
}

inline FileStatePtr
ActionLog::GetFileState()
{
  return m_fileState;
}

} // chronoshare
} // ndn

#endif // ACTION_LOG_H
