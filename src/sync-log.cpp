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

#include "sync-log.hpp"
#include "core/logging.hpp"

#include <ndn-cxx/util/sqlite3-statement.hpp>
#include <ndn-cxx/util/string-helper.hpp>

namespace ndn {
namespace chronoshare {

using util::Sqlite3Statement;

INIT_LOGGER("Sync.Log");

// static void
// xTrace(void*, const char* q)
// {
//   cout << q << endl;
// }

const std::string INIT_DATABASE = "\
CREATE TABLE                                                    \n\
    SyncNodes(                                                  \n\
        device_id       INTEGER PRIMARY KEY AUTOINCREMENT,      \n\
        device_name     BLOB NOT NULL,                          \n\
        description     TEXT,                                   \n\
        seq_no          INTEGER NOT NULL,                       \n\
        last_known_locator  BLOB,                               \n\
        last_update     TIMESTAMP                               \n\
    );                                                          \n\
                                                                \n\
CREATE TRIGGER SyncNodesUpdater_trigger                                \n\
    BEFORE INSERT ON SyncNodes                                         \n\
    FOR EACH ROW                                                       \n\
    WHEN (SELECT device_id                                             \n\
             FROM SyncNodes                                            \n\
             WHERE device_name=NEW.device_name)                        \n\
         IS NOT NULL                                                   \n\
    BEGIN                                                              \n\
        UPDATE SyncNodes                                               \n\
            SET seq_no=max(seq_no,NEW.seq_no)                          \n\
            WHERE device_name=NEW.device_name;                         \n\
        SELECT RAISE(IGNORE);                                          \n\
    END;                                                               \n\
                                                                       \n\
CREATE INDEX SyncNodes_device_name ON SyncNodes (device_name);         \n\
                                                                       \n\
CREATE TABLE SyncLog(                                                  \n\
        state_id    INTEGER PRIMARY KEY AUTOINCREMENT,                 \n\
        state_hash  BLOB NOT NULL UNIQUE,                              \n\
        last_update TIMESTAMP NOT NULL                                 \n\
    );                                                                 \n\
                                                                       \n\
CREATE TABLE                                                            \n\
    SyncStateNodes(                                                     \n\
        id          INTEGER PRIMARY KEY AUTOINCREMENT,                  \n\
        state_id    INTEGER NOT NULL                                    \n\
            REFERENCES SyncLog (state_id) ON UPDATE CASCADE ON DELETE CASCADE, \n\
        device_id   INTEGER NOT NULL                                    \n\
            REFERENCES SyncNodes (device_id) ON UPDATE CASCADE ON DELETE CASCADE, \n\
        seq_no      INTEGER NOT NULL                                    \n\
    );                                                                  \n\
                                                                        \n\
CREATE INDEX SyncStateNodes_device_id ON SyncStateNodes (device_id);    \n\
CREATE INDEX SyncStateNodes_state_id  ON SyncStateNodes (state_id);     \n\
CREATE INDEX SyncStateNodes_seq_no    ON SyncStateNodes (seq_no);       \n\
                                                                        \n\
CREATE TRIGGER SyncLogGuard_trigger                                     \n\
    BEFORE INSERT ON SyncLog                                            \n\
    FOR EACH ROW                                                        \n\
    WHEN (SELECT state_hash                                             \n\
            FROM SyncLog                                                \n\
            WHERE state_hash=NEW.state_hash)                            \n\
        IS NOT NULL                                                     \n\
    BEGIN                                                               \n\
        DELETE FROM SyncLog WHERE state_hash=NEW.state_hash;            \n\
    END;                                                                \n\
";

SyncLog::SyncLog(const boost::filesystem::path& path, const Name& localName)
  : DbHelper(path / ".chronoshare", "sync-log.db")
  , m_localName(localName)
{
  sqlite3_exec(m_db, INIT_DATABASE.c_str(), NULL, NULL, NULL);
  _LOG_DEBUG_COND(sqlite3_errcode(m_db) != SQLITE_OK, "DB Constructer: " << sqlite3_errmsg(m_db));

  UpdateDeviceSeqNo(localName, 0);

  Sqlite3Statement stmt(m_db, "SELECT device_id, seq_no FROM SyncNodes WHERE device_name=?");
  stmt.bind(1, m_localName.wireEncode(), SQLITE_STATIC);

  if (sqlite3_step(stmt) == SQLITE_ROW) {
    m_localDeviceId = stmt.getInt(0);
  }
  else {
    BOOST_THROW_EXCEPTION(Error("Impossible thing in SyncLog::SyncLog"));
  }
}

sqlite3_int64
SyncLog::GetNextLocalSeqNo()
{
  Sqlite3Statement stmt_seq(m_db, "SELECT seq_no FROM SyncNodes WHERE device_id = ?");
  stmt_seq.bind(1, m_localDeviceId);

  if (sqlite3_step(stmt_seq) != SQLITE_ROW) {
    BOOST_THROW_EXCEPTION(Error("Impossible thing in SyncLog::GetNextLocalSeqNo"));
  }

  _LOG_DEBUG_COND(sqlite3_errcode(m_db) != SQLITE_DONE,
                  "DB GetNextLocalSeqNo: " << sqlite3_errmsg(m_db));

  sqlite3_int64 seq_no = stmt_seq.getInt(0) + 1;

  UpdateDeviceSeqNo(m_localDeviceId, seq_no);

  return seq_no;
}

ConstBufferPtr
SyncLog::RememberStateInStateLog()
{
  WriteLock lock(m_stateUpdateMutex);

  int res = sqlite3_exec(m_db, "BEGIN TRANSACTION;", 0, 0, 0);

  res += sqlite3_exec(m_db, "\
INSERT INTO SyncLog                                                \
    (state_hash, last_update)                                      \
    SELECT                                                         \
       hash(device_name, seq_no), datetime('now')                  \
    FROM (SELECT * FROM SyncNodes                                  \
              ORDER BY device_name);                               \
",
                      0, 0, 0);

  _LOG_DEBUG_COND(sqlite3_errcode(m_db) != SQLITE_OK, "DbError: " << sqlite3_errmsg(m_db));

  if (res != SQLITE_OK) {
    sqlite3_exec(m_db, "ROLLBACK TRANSACTION;", 0, 0, 0);
    BOOST_THROW_EXCEPTION(Error(sqlite3_errmsg(m_db)));
  }

  sqlite3_int64 rowId = sqlite3_last_insert_rowid(m_db);

  sqlite3_stmt* insertStmt;
  res += sqlite3_prepare(m_db, "\
INSERT INTO SyncStateNodes                              \
      (state_id, device_id, seq_no)                     \
      SELECT ?, device_id, seq_no                       \
            FROM SyncNodes;                             \
",
                         -1, &insertStmt, 0);

  res += sqlite3_bind_int64(insertStmt, 1, rowId);
  sqlite3_step(insertStmt);

  _LOG_DEBUG_COND(sqlite3_errcode(m_db) != SQLITE_DONE, "DbError: " << sqlite3_errmsg(m_db));
  if (res != SQLITE_OK) {
    sqlite3_exec(m_db, "ROLLBACK TRANSACTION;", 0, 0, 0);
    BOOST_THROW_EXCEPTION(Error(sqlite3_errmsg(m_db)));
  }
  sqlite3_finalize(insertStmt);

  sqlite3_stmt* getHashStmt;
  res += sqlite3_prepare(m_db, "\
SELECT state_hash FROM SyncLog WHERE state_id = ?\
",
                         -1, &getHashStmt, 0);
  res += sqlite3_bind_int64(getHashStmt, 1, rowId);

  BufferPtr retval;
  int stepRes = sqlite3_step(getHashStmt);
  if (stepRes == SQLITE_ROW) {
    retval = make_shared<Buffer>(static_cast<const uint8_t*>(sqlite3_column_blob(getHashStmt, 0)),
                                 sqlite3_column_bytes(getHashStmt, 0));
  }
  else {
    sqlite3_exec(m_db, "ROLLBACK TRANSACTION;", 0, 0, 0);

    _LOG_ERROR("DbError: " << sqlite3_errmsg(m_db));
    BOOST_THROW_EXCEPTION(Error("Not a valid hash in rememberStateInStateLog"));
  }
  sqlite3_finalize(getHashStmt);
  res += sqlite3_exec(m_db, "COMMIT;", 0, 0, 0);

  if (res != SQLITE_OK) {
    sqlite3_exec(m_db, "ROLLBACK TRANSACTION;", 0, 0, 0);
    BOOST_THROW_EXCEPTION(Error("Some error with rememberStateInStateLog"));
  }

  return retval;
}

sqlite3_int64
SyncLog::LookupSyncLog(const std::string& stateHash)
{
  return LookupSyncLog(*fromHex(stateHash));
}

sqlite3_int64
SyncLog::LookupSyncLog(const Buffer& stateHash)
{
  sqlite3_stmt* stmt;
  int res = sqlite3_prepare(m_db, "SELECT state_id FROM SyncLog WHERE state_hash = ?", -1, &stmt, 0);

  if (res != SQLITE_OK) {
    BOOST_THROW_EXCEPTION(Error("Cannot prepare statement"));
  }

  res = sqlite3_bind_blob(stmt, 1, stateHash.buf(), stateHash.size(), SQLITE_STATIC);
  if (res != SQLITE_OK) {
    BOOST_THROW_EXCEPTION(Error("Cannot bind"));
  }

  sqlite3_int64 row = 0; // something bad

  if (sqlite3_step(stmt) == SQLITE_ROW) {
    row = sqlite3_column_int64(stmt, 0);
  }

  sqlite3_finalize(stmt);

  return row;
}

void
SyncLog::UpdateDeviceSeqNo(const Name& name, sqlite3_int64 seqNo)
{
  sqlite3_stmt* stmt;
  // update is performed using trigger
  int res =
    sqlite3_prepare(m_db, "INSERT INTO SyncNodes (device_name, seq_no) VALUES (?,?);", -1, &stmt, 0);

  res +=
    sqlite3_bind_blob(stmt, 1, name.wireEncode().wire(), name.wireEncode().size(), SQLITE_STATIC);
  res += sqlite3_bind_int64(stmt, 2, seqNo);
  sqlite3_step(stmt);

  if (res != SQLITE_OK) {
    BOOST_THROW_EXCEPTION(Error("Some error with UpdateDeviceSeqNo(name)"));
  }
  sqlite3_finalize(stmt);
}

void
SyncLog::UpdateLocalSeqNo(sqlite3_int64 seqNo)
{
  return UpdateDeviceSeqNo(m_localDeviceId, seqNo);
}

void
SyncLog::UpdateDeviceSeqNo(sqlite3_int64 deviceId, sqlite3_int64 seqNo)
{
  sqlite3_stmt* stmt;
  // update is performed using trigger
  int res = sqlite3_prepare(m_db, "UPDATE SyncNodes SET seq_no=MAX(seq_no,?) WHERE device_id=?;",
                            -1, &stmt, 0);

  res += sqlite3_bind_int64(stmt, 1, seqNo);
  res += sqlite3_bind_int64(stmt, 2, deviceId);
  sqlite3_step(stmt);

  if (res != SQLITE_OK) {
    BOOST_THROW_EXCEPTION(Error("Some error with UpdateDeviceSeqNo(id)"));
  }

  _LOG_DEBUG_COND(sqlite3_errcode(m_db) != SQLITE_OK,
                  "DB UpdateDeviceSeqNo: " << sqlite3_errmsg(m_db));

  sqlite3_finalize(stmt);
}

Name
SyncLog::LookupLocator(const Name& deviceName)
{
  sqlite3_stmt* stmt;
  sqlite3_prepare_v2(m_db, "SELECT last_known_locator FROM SyncNodes WHERE device_name=?;", -1,
                     &stmt, 0);
  sqlite3_bind_blob(stmt, 1, deviceName.wireEncode().wire(), deviceName.wireEncode().size(),
                    SQLITE_STATIC);
  int res = sqlite3_step(stmt);
  Name locator;
  switch (res) {
    case SQLITE_ROW: {
      locator = Name(Block(sqlite3_column_blob(stmt, 0), sqlite3_column_bytes(stmt, 0)));
    }
    case SQLITE_DONE:
      break;
    default:
      BOOST_THROW_EXCEPTION(Error("Error in LookupLocator()"));
  }

  sqlite3_finalize(stmt);

  return locator;
}

Name
SyncLog::LookupLocalLocator()
{
  return LookupLocator(m_localName);
}

void
SyncLog::UpdateLocator(const Name& deviceName, const Name& locator)
{
  sqlite3_stmt* stmt;
  sqlite3_prepare_v2(m_db, "UPDATE SyncNodes SET last_known_locator=?,last_update=datetime('now', "
                           "'localtime') WHERE device_name=?;",
                     -1, &stmt, 0);

  sqlite3_bind_blob(stmt, 1, locator.wireEncode().wire(), locator.wireEncode().size(), SQLITE_STATIC);
  sqlite3_bind_blob(stmt, 2, deviceName.wireEncode().wire(), deviceName.wireEncode().size(),
                    SQLITE_STATIC);

  int res = sqlite3_step(stmt);

  if (res != SQLITE_OK && res != SQLITE_DONE) {
    BOOST_THROW_EXCEPTION(Error("Error in UpdateLoactor()"));
  }

  sqlite3_finalize(stmt);
}

void
SyncLog::UpdateLocalLocator(const Name& forwardingHint)
{
  return UpdateLocator(m_localName, forwardingHint);
}

SyncStateMsgPtr
SyncLog::FindStateDifferences(const std::string& oldHash, const std::string& newHash,
                              bool includeOldSeq)
{
  return FindStateDifferences(*fromHex(oldHash), *fromHex(newHash), includeOldSeq);
}

SyncStateMsgPtr
SyncLog::FindStateDifferences(const Buffer& oldHash, const Buffer& newHash, bool includeOldSeq)
{
  sqlite3_stmt* stmt;

  int res = sqlite3_prepare_v2(m_db, "\
SELECT sn.device_name, sn.last_known_locator, s_old.seq_no, s_new.seq_no\
    FROM (SELECT *                                                      \
            FROM SyncStateNodes                                         \
            WHERE state_id=(SELECT state_id                             \
                                FROM SyncLog                            \
                                WHERE state_hash=:old_hash)) s_old      \
    LEFT JOIN (SELECT *                                                 \
                FROM SyncStateNodes                                     \
                WHERE state_id=(SELECT state_id                         \
                                    FROM SyncLog                        \
                                    WHERE state_hash=:new_hash)) s_new  \
                                                                        \
        ON s_old.device_id = s_new.device_id                            \
    JOIN SyncNodes sn ON sn.device_id = s_old.device_id                 \
                                                                        \
    WHERE s_new.seq_no IS NULL OR                                       \
          s_old.seq_no != s_new.seq_no                                  \
                                                                        \
UNION ALL                                                               \
                                                                        \
SELECT sn.device_name, sn.last_known_locator, s_old.seq_no, s_new.seq_no\
    FROM (SELECT *                                                      \
            FROM SyncStateNodes                                         \
            WHERE state_id=(SELECT state_id                             \
                                FROM SyncLog                            \
                                WHERE state_hash=:new_hash )) s_new     \
    LEFT JOIN (SELECT *                                                 \
                FROM SyncStateNodes                                     \
                WHERE state_id=(SELECT state_id                         \
                                    FROM SyncLog                        \
                                    WHERE state_hash=:old_hash)) s_old  \
                                                                        \
        ON s_old.device_id = s_new.device_id                            \
    JOIN SyncNodes sn ON sn.device_id = s_new.device_id                 \
                                                                        \
    WHERE s_old.seq_no IS NULL                                          \
",
                               -1, &stmt, 0);

  if (res != SQLITE_OK) {
    BOOST_THROW_EXCEPTION(Error("Some error with FindStateDifferences"));
  }

  res += sqlite3_bind_blob(stmt, 1, oldHash.buf(), oldHash.size(), SQLITE_STATIC);
  res += sqlite3_bind_blob(stmt, 2, newHash.buf(), newHash.size(), SQLITE_STATIC);

  SyncStateMsgPtr msg = make_shared<SyncStateMsg>();

  // sqlite3_trace(m_db, xTrace, NULL);

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    SyncState* state = msg->add_state();

    // set name
    state->set_name(reinterpret_cast<const char*>(sqlite3_column_blob(stmt, 0)),
                    sqlite3_column_bytes(stmt, 0));

    // locator is optional, so must check if it is null
    if (sqlite3_column_type(stmt, 1) == SQLITE_BLOB) {
      state->set_locator(reinterpret_cast<const char*>(sqlite3_column_blob(stmt, 1)),
                         sqlite3_column_bytes(stmt, 1));
    }

    // set old seq
    if (includeOldSeq) {
      if (sqlite3_column_type(stmt, 2) == SQLITE_NULL) {
        // old seq is zero; we always have an initial action of zero seq
        // other's do not need to fetch this action
        state->set_old_seq(0);
      }
      else {
        sqlite3_int64 oldSeqNo = sqlite3_column_int64(stmt, 2);
        state->set_old_seq(oldSeqNo);
      }
    }

    // set new seq
    if (sqlite3_column_type(stmt, 3) == SQLITE_NULL) {
      state->set_type(SyncState::DELETE);
    }
    else {
      sqlite3_int64 newSeqNo = sqlite3_column_int64(stmt, 3);
      state->set_type(SyncState::UPDATE);
      state->set_seq(newSeqNo);
    }

    // std::cout << sqlite3_column_text (stmt, 0) <<
    //   ": from "  << sqlite3_column_int64 (stmt, 1) <<
    //   " to "     << sqlite3_column_int64 (stmt, 2) <<
    //   std::endl;
  }
  sqlite3_finalize(stmt);

  // sqlite3_trace(m_db, NULL, NULL);

  return msg;
}

sqlite3_int64
SyncLog::SeqNo(const Name& name)
{
  sqlite3_stmt* stmt;
  sqlite3_int64 seq = -1;
  sqlite3_prepare_v2(m_db, "SELECT seq_no FROM SyncNodes WHERE device_name=?;", -1, &stmt, 0);

  sqlite3_bind_blob(stmt, 1, name.wireEncode().wire(), name.wireEncode().size(), SQLITE_STATIC);

  if (sqlite3_step(stmt) == SQLITE_ROW) {
    seq = sqlite3_column_int64(stmt, 0);
  }

  return seq;
}

sqlite3_int64
SyncLog::LogSize()
{
  sqlite3_stmt* stmt;
  sqlite3_prepare_v2(m_db, "SELECT count(*) FROM SyncLog", -1, &stmt, 0);

  sqlite3_int64 retval = -1;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    retval = sqlite3_column_int64(stmt, 0);
  }

  return retval;
}

} // namespace chronoshare
} // namespace ndn
