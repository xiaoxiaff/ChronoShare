/* -*- Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil -*- */
/*
 * Copyright (c) 2012-2013 University of California, Los Angeles
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

#include "action-log.h"
#include <action-item.pb.h>

using namespace boost;
using namespace std;

ActionLog::ActionLog (const std::string &path, const std::string &localName)
  : SyncLog (path, localName)
{
}

tuple<sqlite3_int64, sqlite3_int64, sqlite3_int64, string>
ActionLog::GetExistingRecord (const std::string &filename)
{
  // check if something already exists
  sqlite3_stmt *stmt;
  int res = sqlite3_prepare_v2 (m_db, "SELECT a.version,a.device_id,a.seq_no,a.action,s.device_name "
                                "FROM ActionLog a JOIN SyncNodes s ON s.device_id = a.device_id "
                                "WHERE filename=? ORDER BY a.version DESC,a.device_id DESC LIMIT 1", -1, &stmt, 0);

  if (res != SQLITE_OK)
    {
      BOOST_THROW_EXCEPTION (Error::Db ()
                             << errmsg_info_str ("Some error with GetExistingRecord"));
    }

  // with parent with version number + 1
  sqlite3_int64 version = 0;
  sqlite3_int64 parent_device_id = -1;
  sqlite3_int64 parent_seq_no = -1;
  string parent_device_name;
  
  sqlite3_bind_text (stmt, 1, filename.c_str (), filename.size (), SQLITE_STATIC);
  if (sqlite3_step (stmt) == SQLITE_ROW) 
    {
      version = sqlite3_column_int64 (stmt, 0) + 1;

      if (sqlite3_column_int (stmt, 3) == 0) // prevent "linking" if the file was previously deleted
        {
          parent_device_id = sqlite3_column_int64 (stmt, 1);
          parent_seq_no = sqlite3_column_int64 (stmt, 2);
          parent_device_name = string(reinterpret_cast<const char*> (sqlite3_column_text (stmt, 4)));
        }
    }
  
  sqlite3_finalize (stmt);
  return make_tuple (version, parent_device_id, parent_seq_no, parent_device_name);
}

// local add action. remote action is extracted from content object
void
ActionLog::AddActionUpdate (const std::string &filename,
                            const Hash &hash,
                            time_t atime, time_t mtime, time_t ctime,
                            int mode)
{
  sqlite3_exec (m_db, "BEGIN TRANSACTION;", 0,0,0);
  
  sqlite3_int64 seq_no = GetNextLocalSeqNo ();
  sqlite3_int64 version = 0;
  sqlite3_int64 parent_device_id = -1;
  string        parent_device_name;
  sqlite3_int64 parent_seq_no = -1;

  sqlite3_int64 action_time = time (0);
  
  tie (version, parent_device_id, parent_seq_no, parent_device_name) = GetExistingRecord (filename);
  
  sqlite3_stmt *stmt;
  int res = sqlite3_prepare_v2 (m_db, "INSERT INTO ActionLog "
                                "(device_id, seq_no, action, filename, version, action_timestamp, "
                                "file_hash, file_atime, file_mtime, file_ctime, file_chmod, "
                                "parent_device_id, parent_seq_no) "
                                "VALUES (?, ?, ?, ?, ?, datetime(?, 'unixepoch'),"
                                "        ?, datetime(?, 'unixepoch'), datetime(?, 'unixepoch'), datetime(?, 'unixepoch'), ?,"
                                "        ?, ?);", -1, &stmt, 0);

  // cout << "INSERT INTO ActionLog "
  //                               "(device_id, seq_no, action, filename, version, action_timestamp, "
  //                               "file_hash, file_atime, file_mtime, file_ctime, file_chmod, "
  //                               "parent_device_id, parent_seq_no) "
  //                               "VALUES (?, ?, ?, ?, ?, datetime(?, 'unixepoch'),"
  //                               "        ?, datetime(?, 'unixepoch'), datetime(?, 'unixepoch'), datetime(?, 'unixepoch'), ?,"
  //   "        ?, ?)" << endl;
  
  if (res != SQLITE_OK)
    {
      BOOST_THROW_EXCEPTION (Error::Db ()
                             << errmsg_info_str (sqlite3_errmsg (m_db))
                             );
                             // << errmsg_info_str ("Some error with prepare AddActionUpdate"));
    }

  
  sqlite3_bind_int64 (stmt, 1, m_localDeviceId);
  sqlite3_bind_int64 (stmt, 2, seq_no);
  sqlite3_bind_int   (stmt, 3, 0);
  sqlite3_bind_text  (stmt, 4, filename.c_str (), filename.size (), SQLITE_TRANSIENT);
  sqlite3_bind_int64 (stmt, 5, version);
  sqlite3_bind_int64 (stmt, 6, action_time);
  
  sqlite3_bind_blob  (stmt, 7, hash.GetHash (), hash.GetHashBytes (), SQLITE_TRANSIENT);
  
  sqlite3_bind_int64 (stmt, 8, atime);
  sqlite3_bind_int64 (stmt, 9, mtime);
  sqlite3_bind_int64 (stmt, 10, ctime);
  sqlite3_bind_int   (stmt, 11, mode);

  if (parent_device_id > 0 && parent_seq_no > 0)
    {
      sqlite3_bind_int64 (stmt, 12, parent_device_id);
      sqlite3_bind_int64 (stmt, 13, parent_seq_no);
    }
  else
    {
      sqlite3_bind_null (stmt, 12);
      sqlite3_bind_null (stmt, 13);
    }
  
  sqlite3_step (stmt);

  // missing part: creating ContentObject for the action !!!

  ActionItem item;
  item.set_action (ActionItem::UPDATE);
  item.set_filename (filename);
  item.set_version (version);
  item.set_timestamp (action_time);
  item.set_file_hash (hash.GetHash (), hash.GetHashBytes ());
  item.set_atime (atime);
  item.set_mtime (mtime);
  item.set_ctime (ctime);
  item.set_mode (mode);

  if (parent_device_id > 0 && parent_seq_no > 0)
    {
      item.set_parent_device_name (parent_device_name);
      item.set_parent_seq_no (parent_seq_no);
    }

  // assign name to the action, serialize action, and create content object
  
  sqlite3_finalize (stmt); 
                          
  sqlite3_exec (m_db, "END TRANSACTION;", 0,0,0);
}

void
ActionLog::AddActionMove (const std::string &oldFile, const std::string &newFile)
{
  // not supported yet
  BOOST_THROW_EXCEPTION (Error::Db ()
                         << errmsg_info_str ("Move operation is not yet supported"));
}

void
ActionLog::AddActionDelete (const std::string &filename)
{
  sqlite3_exec (m_db, "BEGIN TRANSACTION;", 0,0,0);
  
  sqlite3_int64 version = 0;
  sqlite3_int64 parent_device_id = -1;
  string        parent_device_name;
  sqlite3_int64 parent_seq_no = -1;

  sqlite3_int64 action_time = time (0);
  
  tie (version, parent_device_id, parent_seq_no, parent_device_name) = GetExistingRecord (filename);
  if (parent_device_id < 0) // no records exist or file was already deleted
    {
      sqlite3_exec (m_db, "END TRANSACTION;", 0,0,0);  
      return;
    }

  sqlite3_int64 seq_no = GetNextLocalSeqNo ();

  sqlite3_stmt *stmt;
  sqlite3_prepare_v2 (m_db, "INSERT INTO ActionLog "
                      "(device_id, seq_no, action, filename, version, action_timestamp, "
                      "parent_device_id, parent_seq_no) "
                      "VALUES (?, ?, ?, ?, ?, datetime(?, 'unixepoch'),"
                      "        ?, ?)", -1, &stmt, 0);

  sqlite3_bind_int64 (stmt, 1, m_localDeviceId);
  sqlite3_bind_int64 (stmt, 2, seq_no);
  sqlite3_bind_int   (stmt, 3, 1);
  sqlite3_bind_text  (stmt, 4, filename.c_str (), filename.size (), SQLITE_TRANSIENT);
  sqlite3_bind_int64 (stmt, 5, version);
  sqlite3_bind_int64 (stmt, 6, action_time);

  sqlite3_bind_int64 (stmt, 7, parent_device_id);
  sqlite3_bind_int64 (stmt, 8, parent_seq_no);
  
  sqlite3_step (stmt);

  // missing part: creating ContentObject for the action !!!

  ActionItem item;
  item.set_action (ActionItem::UPDATE);
  item.set_filename (filename);
  item.set_version (version);
  item.set_timestamp (action_time);
  item.set_parent_device_name (parent_device_name);
  item.set_parent_seq_no (parent_seq_no);

  // assign name to the action, serialize action, and create content object
  
  sqlite3_finalize (stmt); 
  
  sqlite3_exec (m_db, "END TRANSACTION;", 0,0,0);    
}