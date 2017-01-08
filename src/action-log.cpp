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

#include "action-log.hpp"
#include "sync-core.hpp"
#include "core/logging.hpp"

#include <ndn-cxx/util/string-helper.hpp>

namespace ndn {
namespace chronoshare {

INIT_LOGGER("ActionLog")

const std::string INIT_DATABASE = "\
CREATE TABLE ActionLog (                                                \n\
    device_name BLOB NOT NULL,                                          \n\
    seq_no      INTEGER NOT NULL,                                       \n\
                                                                        \n\
    action      CHAR(1) NOT NULL, /* 0 for \"update\", 1 for \"delete\". */ \n\
    filename    TEXT NOT NULL,                                          \n\
    directory   TEXT,                                                   \n\
                                                                        \n\
    version     INTEGER NOT NULL,                                       \n\
    action_timestamp TIMESTAMP NOT NULL,                                \n\
                                                                        \n\
    file_hash   BLOB, /* NULL if action is \"delete\" */                \n\
    file_atime  TIMESTAMP,                                              \n\
    file_mtime  TIMESTAMP,                                              \n\
    file_ctime  TIMESTAMP,                                              \n\
    file_chmod  INTEGER,                                                \n\
    file_seg_num INTEGER, /* NULL if action is \"delete\" */            \n\
                                                                        \n\
    parent_device_name BLOB,                                            \n\
    parent_seq_no      INTEGER,                                         \n\
                                                                        \n\
    action_name	     TEXT,                                              \n\
    action_content_object BLOB,                                         \n\
                                                                        \n\
    PRIMARY KEY (device_name, seq_no)                                   \n\
);                                                                      \n\
                                                                        \n\
CREATE INDEX ActionLog_filename_version ON ActionLog (filename,version);          \n\
CREATE INDEX ActionLog_parent ON ActionLog (parent_device_name, parent_seq_no);   \n\
CREATE INDEX ActionLog_action_name ON ActionLog (action_name);          \n\
CREATE INDEX ActionLog_filename_version_hash ON ActionLog (filename,version,file_hash); \n\
                                                                        \n\
CREATE TRIGGER ActionLogInsert_trigger                                  \n\
    AFTER INSERT ON ActionLog                                           \n\
    FOR EACH ROW                                                        \n\
    WHEN (SELECT device_name                                            \n\
            FROM ActionLog                                              \n\
            WHERE filename=NEW.filename AND                             \n\
                  version > NEW.version) IS NULL AND                    \n\
         (SELECT device_name                                            \n\
            FROM ActionLog                                              \n\
            WHERE filename=NEW.filename AND                             \n\
                  version = NEW.version AND                             \n\
                  device_name > NEW.device_name) IS NULL                \n\
    BEGIN                                                               \n\
        SELECT apply_action (NEW.device_name, NEW.seq_no,               \
                             NEW.action,NEW.filename,NEW.version,NEW.file_hash,     \
                             strftime('%s', NEW.file_atime),strftime('%s', NEW.file_mtime),strftime('%s', NEW.file_ctime), \
                             NEW.file_chmod, NEW.file_seg_num);         \n\
                             /* function that applies action and adds record the FileState */ \n\
    END;                                                                \n\
";

// static void
// xTrace(void*, const char* q)
// {
//   _LOG_TRACE("SQLITE: " << q);
// }

ActionLog::ActionLog(Face& face, const boost::filesystem::path& path, SyncLogPtr syncLog,
                     const std::string& sharedFolder, const std::string& appName,
                     OnFileAddedOrChangedCallback onFileAddedOrChanged,
                     OnFileRemovedCallback onFileRemoved)
  : DbHelper(path / ".chronoshare", "action-log.db")
  , m_syncLog(syncLog)
  // , m_face(face)
  , m_sharedFolderName(sharedFolder)
  , m_appName(appName)
  , m_onFileAddedOrChanged(onFileAddedOrChanged)
  , m_onFileRemoved(onFileRemoved)
{
  sqlite3_exec(m_db, "PRAGMA foreign_keys = OFF", NULL, NULL, NULL);
  _LOG_DEBUG_COND(sqlite3_errcode(m_db) != SQLITE_OK, sqlite3_errmsg(m_db));

  sqlite3_exec(m_db, INIT_DATABASE.c_str(), NULL, NULL, NULL);
  _LOG_DEBUG_COND(sqlite3_errcode(m_db) != SQLITE_OK, sqlite3_errmsg(m_db));

  int res =
    sqlite3_create_function(m_db, "apply_action", -1, SQLITE_ANY, reinterpret_cast<void*>(this),
                            ActionLog::apply_action_xFun, 0, 0);
  if (res != SQLITE_OK) {
    BOOST_THROW_EXCEPTION(Error("Cannot create function ``apply_action''"));
  }

  m_fileState = make_shared<FileState>(path);
}

std::tuple<sqlite3_int64 /*version*/, BufferPtr /*device name*/, sqlite3_int64 /*seq_no*/>
ActionLog::GetLatestActionForFile(const std::string& filename)
{
  // check if something already exists
  sqlite3_stmt* stmt;
  int res = sqlite3_prepare_v2(m_db, "SELECT version,device_name,seq_no,action "
                                     "FROM ActionLog "
                                     "WHERE filename=? ORDER BY version DESC LIMIT 1",
                               -1, &stmt, 0);

  if (res != SQLITE_OK) {
    BOOST_THROW_EXCEPTION(Error("Some error with GetExistingRecord"));
  }

  sqlite3_int64 version = -1;
  BufferPtr parent_device_name;
  sqlite3_int64 parent_seq_no = -1;

  sqlite3_bind_text(stmt, 1, filename.c_str(), filename.size(), SQLITE_STATIC);
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    version = sqlite3_column_int64(stmt, 0);

    if (sqlite3_column_int(stmt, 3) == 0) // prevent "linking" if the file was previously deleted
    {
      parent_device_name =
        std::make_shared<Buffer>(sqlite3_column_blob(stmt, 1), sqlite3_column_bytes(stmt, 1));
      parent_seq_no = sqlite3_column_int64(stmt, 2);
    }
  }

  sqlite3_finalize(stmt);
  return std::make_tuple(version, parent_device_name, parent_seq_no);
}

// local add action. remote action is extracted from content object
ActionItemPtr
ActionLog::AddLocalActionUpdate(const std::string& filename, const Buffer& hash, time_t wtime,
                                int mode, int seg_num)
{
  sqlite3_exec(m_db, "BEGIN TRANSACTION;", 0, 0, 0);

  Block device_name = m_syncLog->GetLocalName().wireEncode();

  sqlite3_int64 seq_no = m_syncLog->GetNextLocalSeqNo();
  sqlite3_int64 version;
  BufferPtr parent_device_name;
  sqlite3_int64 parent_seq_no = -1;

  sqlite3_int64 action_time = std::time(0);

  tie(version, parent_device_name, parent_seq_no) = GetLatestActionForFile(filename);
  version++;

  sqlite3_stmt* stmt;
  int res =
    sqlite3_prepare_v2(m_db,
                       "INSERT INTO ActionLog "
                       "(device_name, seq_no, action, filename, version, action_timestamp, "
                       "file_hash, file_atime, file_mtime, file_ctime, file_chmod, file_seg_num, "
                       "parent_device_name, parent_seq_no, "
                       "action_name, action_content_object) "
                       "VALUES (?, ?, ?, ?, ?, datetime(?, 'unixepoch'),"
                       "        ?, datetime(?, 'unixepoch'), datetime(?, 'unixepoch'), datetime(?, 'unixepoch'), ?,?, "
                       "        ?, ?, "
                       "        ?, ?);",
                       -1, &stmt, 0);

  _LOG_DEBUG_COND(sqlite3_errcode(m_db) != SQLITE_OK, sqlite3_errmsg(m_db));

  if (res != SQLITE_OK) {
    BOOST_THROW_EXCEPTION(Error(sqlite3_errmsg(m_db)));
  }

  sqlite3_bind_blob(stmt, 1, device_name.wire(), device_name.size(), SQLITE_STATIC);
  sqlite3_bind_int64(stmt, 2, seq_no);
  sqlite3_bind_int(stmt, 3, 0);
  sqlite3_bind_text(stmt, 4, filename.c_str(), filename.size(), SQLITE_STATIC);
  sqlite3_bind_int64(stmt, 5, version);
  sqlite3_bind_int64(stmt, 6, action_time);

  sqlite3_bind_blob(stmt, 7, hash.buf(), hash.size(), SQLITE_STATIC);

  // sqlite3_bind_int64(stmt, 8, atime); // NULL
  sqlite3_bind_int64(stmt, 9, wtime);
  // sqlite3_bind_int64(stmt, 10, ctime); // NULL
  sqlite3_bind_int(stmt, 11, mode);
  sqlite3_bind_int(stmt, 12, seg_num);

  if (parent_device_name && parent_seq_no > 0) {
    sqlite3_bind_blob(stmt, 13, parent_device_name->buf(), parent_device_name->size(), SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 14, parent_seq_no);
  }

  ActionItemPtr item = make_shared<ActionItem>();
  item->set_action(ActionItem::UPDATE);
  item->set_filename(filename);
  item->set_version(version);
  item->set_timestamp(action_time);
  item->set_file_hash(hash.buf(), hash.size());
  // item->set_atime(atime);
  item->set_mtime(wtime);
  // item->set_ctime(ctime);
  item->set_mode(mode);
  item->set_seg_num(seg_num);

  if (parent_device_name && parent_seq_no > 0) {
    // cout << Name(*parent_device_name) << endl;

    item->set_parent_device_name(parent_device_name->buf(), parent_device_name->size());
    item->set_parent_seq_no(parent_seq_no);
  }

  // assign name to the action, serialize action, and create content object

  std::string item_msg;
  item->SerializeToString(&item_msg);

  // action name: /<device_name>/<appname>/action/<shared-folder>/<action-seq>

  Name actionName = Name("/");
  actionName.append(m_syncLog->GetLocalName()).append(m_appName).append("action");
  actionName.append(m_sharedFolderName).appendNumber(seq_no);
  _LOG_DEBUG("ActionName: " << actionName);

  shared_ptr<Data> actionData = make_shared<Data>();
  actionData->setName(actionName);
  actionData->setFreshnessPeriod(time::seconds(60));
  actionData->setContent(reinterpret_cast<const uint8_t*>(item_msg.c_str()), item_msg.size());
  m_keyChain.sign(*actionData);

  // _LOG_DEBUG(" >>>>>>> " << Name(namePtr->buf() << " " << namePtr->length());

  sqlite3_bind_blob(stmt, 15, actionName.wireEncode().wire(), actionName.wireEncode().size(),
                    SQLITE_STATIC);
  sqlite3_bind_blob(stmt, 16, actionData->wireEncode().wire(), actionData->wireEncode().size(),
                    SQLITE_STATIC);

  sqlite3_step(stmt);

  _LOG_DEBUG_COND(sqlite3_errcode(m_db) != SQLITE_DONE, sqlite3_errmsg(m_db));

  sqlite3_finalize(stmt);

  // I had a problem including directory_name assignment as part of the initial insert.
  sqlite3_prepare_v2(m_db,
                     "UPDATE ActionLog SET directory=directory_name(filename) WHERE device_name=? AND seq_no=?",
                     -1, &stmt, 0);
  _LOG_DEBUG_COND(sqlite3_errcode(m_db) != SQLITE_OK, sqlite3_errmsg(m_db));

  sqlite3_bind_blob(stmt, 1, device_name.wire(), device_name.size(), SQLITE_STATIC);
  sqlite3_bind_int64(stmt, 2, seq_no);
  sqlite3_step(stmt);
  _LOG_DEBUG_COND(sqlite3_errcode(m_db) != SQLITE_DONE, sqlite3_errmsg(m_db));

  sqlite3_finalize(stmt);

  sqlite3_exec(m_db, "END TRANSACTION;", 0, 0, 0);

  // set complete for local file
  m_fileState->SetFileComplete(filename);

  return item;
}

// void
// ActionLog::AddActionMove(const std::string &oldFile, const std::string &newFile)
// {
//   // not supported yet
//   BOOST_THROW_EXCEPTION(Error("Move operation is not yet supported"));
// }

ActionItemPtr
ActionLog::AddLocalActionDelete(const std::string& filename)
{
  _LOG_DEBUG("Adding local action DELETE");

  sqlite3_exec(m_db, "BEGIN TRANSACTION;", 0, 0, 0);

  const Block device_name = m_syncLog->GetLocalName().wireEncode();
  sqlite3_int64 version;
  BufferPtr parent_device_name;
  sqlite3_int64 parent_seq_no = -1;

  sqlite3_int64 action_time = std::time(0);

  tie(version, parent_device_name, parent_seq_no) = GetLatestActionForFile(filename);
  if (!parent_device_name) // no records exist or file was already deleted
  {
    _LOG_DEBUG("Nothing to delete... [" << filename << "]");

    // just in case, remove data from FileState
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(m_db, "DELETE FROM FileState WHERE filename = ? ", -1, &stmt, 0);
    sqlite3_bind_text(stmt, 1, filename.c_str(), filename.size(), SQLITE_STATIC); // file

    sqlite3_step(stmt);

    _LOG_DEBUG_COND(sqlite3_errcode(m_db) != SQLITE_DONE, sqlite3_errmsg(m_db));

    sqlite3_finalize(stmt);

    sqlite3_exec(m_db, "END TRANSACTION;", 0, 0, 0);
    return ActionItemPtr();
  }
  version++;

  sqlite3_int64 seq_no = m_syncLog->GetNextLocalSeqNo();

  sqlite3_stmt* stmt;
  sqlite3_prepare_v2(m_db, "INSERT INTO ActionLog "
                           "(device_name, seq_no, action, filename, version, action_timestamp, "
                           "parent_device_name, parent_seq_no, "
                           "action_name, action_content_object) "
                           "VALUES(?, ?, ?, ?, ?, datetime(?, 'unixepoch'),"
                           "        ?, ?,"
                           "        ?, ?)",
                     -1, &stmt, 0);

  sqlite3_bind_blob(stmt, 1, device_name.wire(), device_name.size(), SQLITE_STATIC);
  sqlite3_bind_int64(stmt, 2, seq_no);
  sqlite3_bind_int(stmt, 3, 1);
  sqlite3_bind_text(stmt, 4, filename.c_str(), filename.size(), SQLITE_STATIC); // file

  sqlite3_bind_int64(stmt, 5, version);
  sqlite3_bind_int64(stmt, 6, action_time);

  sqlite3_bind_blob(stmt, 7, parent_device_name->buf(), parent_device_name->size(), SQLITE_STATIC);
  sqlite3_bind_int64(stmt, 8, parent_seq_no);

  ActionItemPtr item = make_shared<ActionItem>();
  item->set_action(ActionItem::DELETE);
  item->set_filename(filename);
  item->set_version(version);
  item->set_timestamp(action_time);
  item->set_parent_device_name(parent_device_name->buf(), parent_device_name->size());
  item->set_parent_seq_no(parent_seq_no);

  std::string item_msg;
  item->SerializeToString(&item_msg);

  // action name: /<device_name>/<appname>/action/<shared-folder>/<action-seq>
  Name actionName = Name("/");
  actionName.append(m_syncLog->GetLocalName()).append(m_appName).append("action");
  actionName.append(m_sharedFolderName).appendNumber(seq_no);
  _LOG_DEBUG("ActionName: " << actionName);

  shared_ptr<Data> actionData = make_shared<Data>();
  actionData->setName(actionName);
  actionData->setFreshnessPeriod(time::seconds(60));
  actionData->setContent(reinterpret_cast<const uint8_t*>(item_msg.c_str()), item_msg.size());
  m_keyChain.sign(*actionData);

  sqlite3_bind_blob(stmt, 9, actionName.wireEncode().wire(), actionName.wireEncode().size(),
                    SQLITE_STATIC);
  sqlite3_bind_blob(stmt, 10, actionData->wireEncode().wire(), actionData->wireEncode().size(),
                    SQLITE_STATIC);

  sqlite3_step(stmt);

  _LOG_DEBUG_COND(sqlite3_errcode(m_db) != SQLITE_DONE, sqlite3_errmsg(m_db));

  // cout << Name(parent_device_name) << endl;

  // assign name to the action, serialize action, and create content object

  sqlite3_finalize(stmt);

  // I had a problem including directory_name assignment as part of the initial insert.
  sqlite3_prepare_v2(m_db,
                     "UPDATE ActionLog SET directory=directory_name(filename) WHERE device_name=? AND seq_no=?",
                     -1, &stmt, 0);
  _LOG_DEBUG_COND(sqlite3_errcode(m_db) != SQLITE_OK, sqlite3_errmsg(m_db));

  sqlite3_bind_blob(stmt, 1, device_name.wire(), device_name.size(), SQLITE_STATIC);
  sqlite3_bind_int64(stmt, 2, seq_no);
  sqlite3_step(stmt);
  _LOG_DEBUG_COND(sqlite3_errcode(m_db) != SQLITE_DONE, sqlite3_errmsg(m_db));

  sqlite3_finalize(stmt);

  sqlite3_exec(m_db, "END TRANSACTION;", 0, 0, 0);

  return item;
}

shared_ptr<Data>
ActionLog::LookupActionData(const Name& deviceName, sqlite3_int64 seqno)
{
  sqlite3_stmt* stmt;
  sqlite3_prepare_v2(m_db,
                     "SELECT action_content_object FROM ActionLog WHERE device_name=? AND seq_no=?",
                     -1, &stmt, 0);

  sqlite3_bind_blob(stmt, 1, deviceName.wireEncode().wire(), deviceName.wireEncode().size(),
                    SQLITE_STATIC); // ndn version
  sqlite3_bind_int64(stmt, 2, seqno);

  shared_ptr<Data> retval;

  if (sqlite3_step(stmt) == SQLITE_ROW) {
    // _LOG_DEBUG(sqlite3_column_blob(stmt, 0) << ", " << sqlite3_column_bytes(stmt, 0));
    retval = make_shared<Data>();
    retval->wireDecode(Block(reinterpret_cast<const uint8_t*>(sqlite3_column_blob(stmt, 0)),
                             sqlite3_column_bytes(stmt, 0)));
  }
  else {
    _LOG_TRACE("No action found for deviceName [" << deviceName << "] and seqno:" << seqno);
  }
  // _LOG_DEBUG_COND(sqlite3_errcode(m_db) != SQLITE_OK && sqlite3_errcode(m_db) != SQLITE_ROW,
  // sqlite3_errmsg(m_db));
  sqlite3_finalize(stmt);

  return retval;
}

ActionItemPtr
ActionLog::LookupAction(const Name& deviceName, sqlite3_int64 seqno)
{
  shared_ptr<Data> data = LookupActionData(deviceName, seqno);
  if (!data)
    return ActionItemPtr();

  ActionItemPtr action =
    deserializeMsg<ActionItem>(Buffer(data->getContent().value(), data->getContent().value_size()));
  return action;
}

shared_ptr<Data>
ActionLog::LookupActionData(const Name& actionName)
{
  sqlite3_stmt* stmt;
  sqlite3_prepare_v2(m_db, "SELECT action_content_object FROM ActionLog WHERE action_name=?", -1,
                     &stmt, 0);

  _LOG_DEBUG(actionName);

  _LOG_DEBUG(" LookActionData <<<<<<< " << actionName << " " << actionName.wireEncode().size());

  sqlite3_bind_blob(stmt, 1, actionName.wireEncode().wire(), actionName.wireEncode().size(),
                    SQLITE_STATIC);

  shared_ptr<Data> retval; // = make_shared<Data>();
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    // _LOG_DEBUG(sqlite3_column_blob(stmt, 0) << ", " << sqlite3_column_bytes(stmt, 0));
    retval = make_shared<Data>();
    retval->wireDecode(Block(reinterpret_cast<const uint8_t*>(sqlite3_column_blob(stmt, 0)),
                             sqlite3_column_bytes(stmt, 0)));
  }
  else {
    _LOG_TRACE("No action found for name: " << actionName);
  }
  _LOG_DEBUG_COND(sqlite3_errcode(m_db) != SQLITE_ROW, sqlite3_errmsg(m_db));
  sqlite3_finalize(stmt);

  return retval;
}

ActionItemPtr
ActionLog::LookupAction(const Name& actionName)
{
  shared_ptr<Data> data = LookupActionData(actionName);
  if (!data)
    return ActionItemPtr();

  ActionItemPtr action =
    deserializeMsg<ActionItem>(Buffer(data->getContent().value(), data->getContent().value_size()));

  return action;
}

FileItemPtr
ActionLog::LookupAction(const std::string& filename, sqlite3_int64 version, const Buffer& filehash)
{
  sqlite3_stmt* stmt;
  sqlite3_prepare_v2(m_db,
                     "SELECT device_name, seq_no, strftime('%s', file_mtime), file_chmod, file_seg_num, file_hash "
                     " FROM ActionLog "
                     " WHERE action = 0 AND "
                     "       filename=? AND "
                     "       version=? AND "
                     "       is_prefix(?, file_hash)=1",
                     -1, &stmt, 0);
  _LOG_DEBUG_COND(sqlite3_errcode(m_db) != SQLITE_OK, sqlite3_errmsg(m_db));

  sqlite3_bind_text(stmt, 1, filename.c_str(), filename.size(), SQLITE_STATIC);
  sqlite3_bind_int64(stmt, 2, version);
  sqlite3_bind_blob(stmt, 3, filehash.buf(), filehash.size(), SQLITE_STATIC);

  FileItemPtr fileItem;

  if (sqlite3_step(stmt) == SQLITE_ROW) {
    fileItem = make_shared<FileItem>();
    fileItem->set_filename(filename);
    fileItem->set_device_name(sqlite3_column_blob(stmt, 0), sqlite3_column_bytes(stmt, 0));
    fileItem->set_seq_no(sqlite3_column_int64(stmt, 1));
    fileItem->set_mtime(sqlite3_column_int64(stmt, 2));
    fileItem->set_mode(sqlite3_column_int64(stmt, 3));
    fileItem->set_seg_num(sqlite3_column_int64(stmt, 4));

    fileItem->set_file_hash(sqlite3_column_blob(stmt, 5), sqlite3_column_bytes(stmt, 5));
  }

  _LOG_DEBUG_COND(sqlite3_errcode(m_db) != SQLITE_DONE || sqlite3_errcode(m_db) != SQLITE_ROW ||
                    sqlite3_errcode(m_db) != SQLITE_OK,
                  sqlite3_errmsg(m_db));

  return fileItem;
}


ActionItemPtr
ActionLog::AddRemoteAction(const Name& deviceName, sqlite3_int64 seqno, shared_ptr<Data> actionData)
{
  if (!actionData) {
    _LOG_ERROR("actionData is not valid");
    return ActionItemPtr();
  }
  ActionItemPtr action = deserializeMsg<ActionItem>(
    Buffer(actionData->getContent().value(), actionData->getContent().value_size()));

  if (!action) {
    _LOG_ERROR("action cannot be decoded");
    return ActionItemPtr();
  }

  _LOG_DEBUG("AddRemoteAction: [" << deviceName.toUri() << "] seqno: " << seqno);

  sqlite3_stmt* stmt;
  sqlite3_prepare_v2(m_db,
                     "INSERT INTO ActionLog "
                     "(device_name, seq_no, action, filename, version, action_timestamp, "
                     "file_hash, file_atime, file_mtime, file_ctime, file_chmod, file_seg_num, "
                     "parent_device_name, parent_seq_no, "
                     "action_name, action_content_object) "
                     "VALUES (?, ?, ?, ?, ?, datetime(?, 'unixepoch'),"
                     "        ?, datetime(?, 'unixepoch'), datetime(?, 'unixepoch'), datetime(?, 'unixepoch'), ?,?, "
                     "        ?, ?, "
                     "        ?, ?);",
                     -1, &stmt, 0);
  _LOG_DEBUG_COND(sqlite3_errcode(m_db) != SQLITE_OK, sqlite3_errmsg(m_db));

  sqlite3_bind_blob(stmt, 1, deviceName.wireEncode().wire(), deviceName.wireEncode().size(),
                    SQLITE_STATIC);
  sqlite3_bind_int64(stmt, 2, seqno);

  sqlite3_bind_int(stmt, 3, action->action());
  sqlite3_bind_text(stmt, 4, action->filename().c_str(), action->filename().size(), SQLITE_STATIC);
  sqlite3_bind_int64(stmt, 5, action->version());
  sqlite3_bind_int64(stmt, 6, action->timestamp());

  if (action->action() == ActionItem::UPDATE) {
    sqlite3_bind_blob(stmt, 7, action->file_hash().c_str(), action->file_hash().size(),
                      SQLITE_STATIC);

    // sqlite3_bind_int64(stmt, 8, atime); // NULL
    sqlite3_bind_int64(stmt, 9, action->mtime());
    // sqlite3_bind_int64(stmt, 10, ctime); // NULL

    sqlite3_bind_int(stmt, 11, action->mode());
    sqlite3_bind_int(stmt, 12, action->seg_num());
  }

  if (action->has_parent_device_name()) {
    sqlite3_bind_blob(stmt, 13, action->parent_device_name().c_str(),
                      action->parent_device_name().size(), SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 14, action->parent_seq_no());
  }

  Name actionName = Name(deviceName);
  actionName.append("action").append(m_sharedFolderName).appendNumber(seqno);

  sqlite3_bind_blob(stmt, 15, actionName.wireEncode().wire(), actionName.wireEncode().size(),
                    SQLITE_STATIC);
  sqlite3_bind_blob(stmt, 16, actionData->wireEncode().wire(), actionData->wireEncode().size(),
                    SQLITE_STATIC);
  sqlite3_step(stmt);

  // if action needs to be applied to file state, the trigger will take care of it

  _LOG_DEBUG_COND(sqlite3_errcode(m_db) != SQLITE_DONE, sqlite3_errmsg(m_db));

  sqlite3_finalize(stmt);

  // I had a problem including directory_name assignment as part of the initial insert.
  sqlite3_prepare_v2(m_db,
                     "UPDATE ActionLog SET directory=directory_name(filename) WHERE device_name=? AND seq_no=?",
                     -1, &stmt, 0);
  _LOG_DEBUG_COND(sqlite3_errcode(m_db) != SQLITE_OK, sqlite3_errmsg(m_db));

  sqlite3_bind_blob(stmt, 1, deviceName.wireEncode().wire(), deviceName.wireEncode().size(),
                    SQLITE_STATIC);
  sqlite3_bind_int64(stmt, 2, seqno);
  sqlite3_step(stmt);
  _LOG_DEBUG_COND(sqlite3_errcode(m_db) != SQLITE_DONE, sqlite3_errmsg(m_db));

  sqlite3_finalize(stmt);

  return action;
}

ActionItemPtr
ActionLog::AddRemoteAction(shared_ptr<Data> actionData)
{
  Name name = actionData->getName();
  // action name: /<device_name>/<appname>/action/<shared-folder>/<action-seq>

  uint64_t seqno = name.get(-1).toNumber();
  std::string sharedFolder = name.get(-2).toUri();

  if (sharedFolder != m_sharedFolderName) {
    _LOG_ERROR("Action doesn't belong to this shared folder");
    return ActionItemPtr();
  }

  std::string action = name.get(-3).toUri();

  if (action != "action") {
    _LOG_ERROR("not an action");
    return ActionItemPtr();
  }

  std::string appName = name.get(-4).toUri();
  if (appName != m_appName) {
    _LOG_ERROR("Action doesn't belong to this application");
    return ActionItemPtr();
  }

  Name deviceName = name.getSubName(0, name.size() - 4);

  _LOG_DEBUG("From [" << name << "] extracted deviceName: " << deviceName << ", sharedFolder: "
                      << sharedFolder
                      << ", seqno: "
                      << seqno);

  return AddRemoteAction(deviceName, seqno, actionData);
}

sqlite3_int64
ActionLog::LogSize()
{
  sqlite3_stmt* stmt;
  sqlite3_prepare_v2(m_db, "SELECT count(*) FROM ActionLog", -1, &stmt, 0);

  sqlite3_int64 retval = -1;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    retval = sqlite3_column_int64(stmt, 0);
  }

  return retval;
}

bool
ActionLog::LookupActionsInFolderRecursively(
  const function<void(const Name& name, sqlite3_int64 seq_no, const ActionItem&)>& visitor,
  const std::string& folder, int offset /*=0*/, int limit /*=-1*/)
{
  _LOG_DEBUG("LookupActionsInFolderRecursively: [" << folder << "]");

  if (limit >= 0)
    limit += 1; // to check if there is more data

  sqlite3_stmt* stmt;
  if (folder != "") {
    /// @todo Do something to improve efficiency of this query. Right now it is basically scanning the whole database

    sqlite3_prepare_v2(m_db,
                       "SELECT device_name,seq_no,action,filename,directory,version,strftime('%s', action_timestamp), "
                       "       file_hash,strftime('%s', file_mtime),file_chmod,file_seg_num, "
                       "       parent_device_name,parent_seq_no "
                       "   FROM ActionLog "
                       "   WHERE is_dir_prefix (?, directory)=1 "
                       "   ORDER BY action_timestamp DESC "
                       "   LIMIT ? OFFSET ?",
                       -1, &stmt,
                       0); // there is a small ambiguity with is_prefix matching, but should be ok for now
    _LOG_DEBUG_COND(sqlite3_errcode(m_db) != SQLITE_OK, sqlite3_errmsg(m_db));

    sqlite3_bind_text(stmt, 1, folder.c_str(), folder.size(), SQLITE_STATIC);
    _LOG_DEBUG_COND(sqlite3_errcode(m_db) != SQLITE_OK, sqlite3_errmsg(m_db));

    sqlite3_bind_int(stmt, 2, limit);
    sqlite3_bind_int(stmt, 3, offset);
  }
  else {
    sqlite3_prepare_v2(m_db,
                       "SELECT device_name,seq_no,action,filename,directory,version,strftime('%s', action_timestamp), "
                       "       file_hash,strftime('%s', file_mtime),file_chmod,file_seg_num, "
                       "       parent_device_name,parent_seq_no "
                       "   FROM ActionLog "
                       "   ORDER BY action_timestamp DESC "
                       "   LIMIT ? OFFSET ?",
                       -1, &stmt, 0);
    sqlite3_bind_int(stmt, 1, limit);
    sqlite3_bind_int(stmt, 2, offset);
  }

  _LOG_DEBUG_COND(sqlite3_errcode(m_db) != SQLITE_OK, sqlite3_errmsg(m_db));

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    if (limit == 1)
      break;

    ActionItem action;

    Name device_name(Block(reinterpret_cast<const uint8_t*>(sqlite3_column_blob(stmt, 0)),
                           sqlite3_column_bytes(stmt, 0)));

    sqlite3_int64 seq_no = sqlite3_column_int64(stmt, 1);
    action.set_action(static_cast<ActionItem_ActionType>(sqlite3_column_int(stmt, 2)));
    action.set_filename(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3)),
                        sqlite3_column_bytes(stmt, 3));
    std::string directory(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4)),
                          sqlite3_column_bytes(stmt, 4));
    action.set_version(sqlite3_column_int64(stmt, 5));
    action.set_timestamp(sqlite3_column_int64(stmt, 6));

    if (action.action() == 0) {
      action.set_file_hash(sqlite3_column_blob(stmt, 7), sqlite3_column_bytes(stmt, 7));
      action.set_mtime(sqlite3_column_int(stmt, 8));
      action.set_mode(sqlite3_column_int(stmt, 9));
      action.set_seg_num(sqlite3_column_int64(stmt, 10));
    }
    if (sqlite3_column_bytes(stmt, 11) > 0) {
      action.set_parent_device_name(sqlite3_column_blob(stmt, 11), sqlite3_column_bytes(stmt, 11));
      action.set_parent_seq_no(sqlite3_column_int64(stmt, 12));
    }

    visitor(device_name, seq_no, action);
    limit--;
  }

  _LOG_DEBUG_COND(sqlite3_errcode(m_db) != SQLITE_DONE, sqlite3_errmsg(m_db));

  sqlite3_finalize(stmt);

  return (limit == 1); // more data is available
}

/**
 * @todo Figure out the way to minimize code duplication
 */
bool
ActionLog::LookupActionsForFile(
  const function<void(const Name& name, sqlite3_int64 seq_no, const ActionItem&)>& visitor,
  const std::string& file, int offset /*=0*/, int limit /*=-1*/)
{
  _LOG_DEBUG("LookupActionsInFolderRecursively: [" << file << "]");
  if (file.empty())
    return false;

  if (limit >= 0)
    limit += 1; // to check if there is more data

  sqlite3_stmt* stmt;
  sqlite3_prepare_v2(m_db,
                     "SELECT device_name,seq_no,action,filename,directory,version,strftime('%s', action_timestamp), "
                     "       file_hash,strftime('%s', file_mtime),file_chmod,file_seg_num, "
                     "       parent_device_name,parent_seq_no "
                     "   FROM ActionLog "
                     "   WHERE filename=? "
                     "   ORDER BY action_timestamp DESC "
                     "   LIMIT ? OFFSET ?",
                     -1, &stmt,
                     0); // there is a small ambiguity with is_prefix matching, but should be ok for now
  _LOG_DEBUG_COND(sqlite3_errcode(m_db) != SQLITE_OK, sqlite3_errmsg(m_db));

  sqlite3_bind_text(stmt, 1, file.c_str(), file.size(), SQLITE_STATIC);
  _LOG_DEBUG_COND(sqlite3_errcode(m_db) != SQLITE_OK, sqlite3_errmsg(m_db));

  sqlite3_bind_int(stmt, 2, limit);
  sqlite3_bind_int(stmt, 3, offset);

  _LOG_DEBUG_COND(sqlite3_errcode(m_db) != SQLITE_OK, sqlite3_errmsg(m_db));

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    if (limit == 1)
      break;

    ActionItem action;

    Name device_name(Block(reinterpret_cast<const uint8_t*>(sqlite3_column_blob(stmt, 0)),
                           sqlite3_column_bytes(stmt, 0)));

    sqlite3_int64 seq_no = sqlite3_column_int64(stmt, 1);
    action.set_action(static_cast<ActionItem_ActionType>(sqlite3_column_int(stmt, 2)));
    action.set_filename(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3)),
                        sqlite3_column_bytes(stmt, 3));
    std::string directory(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4)),
                          sqlite3_column_bytes(stmt, 4));
    action.set_version(sqlite3_column_int64(stmt, 5));
    action.set_timestamp(sqlite3_column_int64(stmt, 6));

    if (action.action() == 0) {
      action.set_file_hash(sqlite3_column_blob(stmt, 7), sqlite3_column_bytes(stmt, 7));
      action.set_mtime(sqlite3_column_int(stmt, 8));
      action.set_mode(sqlite3_column_int(stmt, 9));
      action.set_seg_num(sqlite3_column_int64(stmt, 10));
    }
    if (sqlite3_column_bytes(stmt, 11) > 0) {
      action.set_parent_device_name(sqlite3_column_blob(stmt, 11), sqlite3_column_bytes(stmt, 11));
      action.set_parent_seq_no(sqlite3_column_int64(stmt, 12));
    }

    visitor(device_name, seq_no, action);
    limit--;
  }

  _LOG_DEBUG_COND(sqlite3_errcode(m_db) != SQLITE_DONE, sqlite3_errmsg(m_db));

  sqlite3_finalize(stmt);

  return (limit == 1); // more data is available
}

void
ActionLog::LookupRecentFileActions(const function<void(const std::string&, int, int)>& visitor,
                                   int limit)
{
  sqlite3_stmt* stmt;

  sqlite3_prepare_v2(m_db,
                     "SELECT AL.filename, AL.action"
                     "   FROM ActionLog AL"
                     "   JOIN "
                     "   (SELECT filename, MAX(action_timestamp) AS action_timestamp "
                     "       FROM ActionLog "
                     "       GROUP BY filename ) AS GAL"
                     "   ON AL.filename = GAL.filename AND AL.action_timestamp = GAL.action_timestamp "
                     "   ORDER BY AL.action_timestamp DESC "
                     "   LIMIT ?;",
                     -1, &stmt, 0);
  _LOG_DEBUG_COND(sqlite3_errcode(m_db) != SQLITE_OK, sqlite3_errmsg(m_db));
  sqlite3_bind_int(stmt, 1, limit);
  int index = 0;
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    std::string filename(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)),
                         sqlite3_column_bytes(stmt, 0));
    int action = sqlite3_column_int(stmt, 1);
    visitor(filename, action, index);
    index++;
  }

  _LOG_DEBUG_COND(sqlite3_errcode(m_db) != SQLITE_DONE, sqlite3_errmsg(m_db));

  sqlite3_finalize(stmt);
}


///////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////

void
ActionLog::apply_action_xFun(sqlite3_context* context, int argc, sqlite3_value** argv)
{
  ActionLog* the = reinterpret_cast<ActionLog*>(sqlite3_user_data(context));

  if (argc != 11) {
    sqlite3_result_error(context, "``apply_action'' expects 10 arguments", -1);
    return;
  }

  Buffer device_name(sqlite3_value_blob(argv[0]), sqlite3_value_bytes(argv[0]));
  sqlite3_int64 seq_no = sqlite3_value_int64(argv[1]);
  int action = sqlite3_value_int(argv[2]);
  std::string filename = reinterpret_cast<const char*>(sqlite3_value_text(argv[3]));
  sqlite3_int64 version = sqlite3_value_int64(argv[4]);

  _LOG_TRACE("apply_function called with " << argc);
  _LOG_TRACE("device_name: " << Name(Block(reinterpret_cast<const char*>(device_name.buf()),
                                           device_name.size()))
                             << ", action: "
                             << action
                             << ", file: "
                             << filename);

  if (action == 0) // update
  {
    Buffer hash(sqlite3_value_blob(argv[5]), sqlite3_value_bytes(argv[5]));
    time_t atime = static_cast<time_t>(sqlite3_value_int64(argv[6]));
    time_t mtime = static_cast<time_t>(sqlite3_value_int64(argv[7]));
    time_t ctime = static_cast<time_t>(sqlite3_value_int64(argv[8]));
    int mode = sqlite3_value_int(argv[9]);
    int seg_num = sqlite3_value_int(argv[10]);

    _LOG_DEBUG("Update " << filename << " " << atime << " " << mtime << " " << ctime << " "
                         << toHex(hash));

    the->m_fileState->UpdateFile(filename, version, hash, device_name, seq_no, atime, mtime, ctime,
                                 mode, seg_num);

    // no callback here
  }
  else if (action == 1) // delete
  {
    the->m_fileState->DeleteFile(filename);

    the->m_onFileRemoved(filename);
  }

  sqlite3_result_null(context);
}

} // namespace chronoshare
} // namespace ndn
