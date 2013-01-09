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

#ifndef ACTION_LOG_H
#define ACTION_LOG_H

#include "sync-log.h"
#include <boost/tuple/tuple.hpp>
#include <ccnx-wrapper.h>

class ActionLog;
typedef boost::shared_ptr<ActionLog> ActionLogPtr;

class ActionLog : public SyncLog
{
public:
  ActionLog (Ccnx::CcnxWrapperPtr ccnx, const boost::filesystem::path &path,
             const std::string &localName, const std::string &sharedFolder);

  void
  AddActionUpdate (const std::string &filename,
                   const Hash &hash,
                   time_t wtime,
                   int mode);

  void
  AddActionMove (const std::string &oldFile, const std::string &newFile);
  
  void
  AddActionDelete (const std::string &filename);

private:
  boost::tuple<sqlite3_int64, sqlite3_int64, sqlite3_int64, std::string>
  GetExistingRecord (const std::string &filename);

  static void
  apply_action_xFun (sqlite3_context *context, int argc, sqlite3_value **argv);
  
private:
  Ccnx::CcnxWrapperPtr m_ccnx;
  Ccnx::Name m_sharedFolderName;
};

#endif // ACTION_LOG_H
