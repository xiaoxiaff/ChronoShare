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

#include "state-server.hpp"
#include "core/logging.hpp"

#include <ndn-cxx/util/digest.hpp>
#include <ndn-cxx/util/string-helper.hpp>

#include <boost/asio/io_service.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/filesystem/fstream.hpp>

namespace ndn {
namespace chronoshare {

INIT_LOGGER("StateServer")

namespace fs = boost::filesystem;

StateServer::StateServer(Face& face, ActionLogPtr actionLog,
                         const fs::path& rootDir, const Name& userName,
                         const std::string& sharedFolderName, const std::string& appName,
                         ObjectManager& objectManager, time::milliseconds freshness)
  : m_face(face)
  , m_actionLog(actionLog)
  , m_objectManager(objectManager)
  , m_rootDir(rootDir)
  , m_freshness(freshness)
  , m_userName(userName)
  , m_sharedFolderName(sharedFolderName)
  , m_appName(appName)
  , m_ioService(m_face.getIoService())
{
  // may be later /localhop should be replaced with /%C1.M.S.localhost

  // <PREFIX_INFO> = /localhop/<user's-device-name>/"chronoshare"/"info"
  m_PREFIX_INFO = Name("/localhop");
  m_PREFIX_INFO.append(m_userName).append("chronoshare").append(m_sharedFolderName).append("info");

  // <PREFIX_CMD> = /localhop/<user's-device-name>/"chronoshare"/"cmd"
  m_PREFIX_CMD = Name("/localhop");
  m_PREFIX_CMD.append(m_userName).append("chronoshare").append(m_sharedFolderName).append("cmd");

  registerPrefixes();
}

StateServer::~StateServer()
{
  deregisterPrefixes();
}

void
StateServer::registerPrefixes()
{
  // currently supporting limited number of command.
  // will be extended to support all planned commands later

  // <PREFIX_INFO>/"actions"/"all"/<segment>  get list of all actions
  Name actionsFolder = Name(m_PREFIX_INFO);
  actionsFolder.append("actions").append("folder");
  actionsFolderId =
    m_face.setInterestFilter(InterestFilter(actionsFolder),
                             bind(&StateServer::info_actions_folder, this, _1, _2),
                             RegisterPrefixSuccessCallback(), RegisterPrefixFailureCallback());

  _LOG_DEBUG("Register Prefix: " << actionsFolder);

  Name actionsFile = Name(m_PREFIX_INFO);
  actionsFile.append("actions").append("file");
  actionsFileId =
    m_face.setInterestFilter(InterestFilter(actionsFile),
                             bind(&StateServer::info_actions_file, this, _1, _2),
                             RegisterPrefixSuccessCallback(), RegisterPrefixFailureCallback());

  _LOG_DEBUG("Register Prefix: " << actionsFile);

  // <PREFIX_INFO>/"filestate"/"all"/<segment>
  Name filesFolder = Name(m_PREFIX_INFO);
  filesFolder.append("files").append("folder");
  filesFolderId =
    m_face.setInterestFilter(InterestFilter(filesFolder),
                             bind(&StateServer::info_files_folder, this, _1, _2),
                             RegisterPrefixSuccessCallback(), RegisterPrefixFailureCallback());

  _LOG_DEBUG("Register Prefix: " << filesFolder);

  // <PREFIX_CMD>/"restore"/"file"/<one-component-relative-file-name>/<version>/<file-hash>
  Name restoreFile = Name(m_PREFIX_CMD);
  restoreFile.append("restore").append("file");
  restoreFileId =
    m_face.setInterestFilter(InterestFilter(restoreFile),
                             bind(&StateServer::cmd_restore_file, this, _1, _2),
                             RegisterPrefixSuccessCallback(), RegisterPrefixFailureCallback());

  _LOG_DEBUG("Register Prefix: " << restoreFile);
}

void
StateServer::deregisterPrefixes()
{
  m_face.unsetInterestFilter(actionsFolderId);
  m_face.unsetInterestFilter(actionsFileId);
  m_face.unsetInterestFilter(filesFolderId);
  m_face.unsetInterestFilter(restoreFileId);
}

void
StateServer::formatActionJson(json_spirit::Array& actions, const Name& name,
                              sqlite3_int64 seq_no, const ActionItem& action)
{
  /*
   *      {
   *          "id": {
   *              "userName": "<NDN-NAME-OF-THE-USER>",
   *              "seqNo": "<SEQ_NO_OF_THE_ACTION>"
   *          },
   *          "timestamp": "<ACTION-TIMESTAMP>",
   *          "filename": "<FILENAME>",
   *
   *          "action": "UPDATE | DELETE",
   *
   *          // only if update
   *          "update": {
   *              "hash": "<FILE-HASH>",
   *              "timestamp": "<FILE-TIMESTAMP>",
   *              "chmod": "<FILE-MODE>",
   *              "segNum": "<NUMBER-OF-SEGMENTS(~file size)>"
   *          },
   *
   *          // if parent_device_name is set
   *          "parentId": {
   *              "userName": "<NDN-NAME-OF-THE-USER>",
   *              "seqNo": "<SEQ_NO_OF_THE_ACTION>"
   *          }
   *      }
   */

  using namespace json_spirit;
  using namespace boost::posix_time;

  Object json;
  Object id;

  id.push_back(Pair("userName", boost::lexical_cast<std::string>(name)));
  id.push_back(Pair("seqNo", static_cast<int64_t>(seq_no)));

  json.push_back(Pair("id", id));

  json.push_back(Pair("timestamp", to_iso_extended_string(from_time_t(action.timestamp()))));
  json.push_back(Pair("filename", action.filename()));
  json.push_back(Pair("version", action.version()));
  json.push_back(Pair("action", (action.action() == 0) ? "UPDATE" : "DELETE"));

  if (action.action() == 0) {
    Object update;
    const Buffer hash(action.file_hash().c_str(), action.file_hash().size());
    update.push_back(Pair("hash", toHex(hash)));
    update.push_back(Pair("timestamp", to_iso_extended_string(from_time_t(action.mtime()))));

    std::ostringstream chmod;
    chmod << std::setbase(8) << std::setfill('0') << std::setw(4) << action.mode();
    update.push_back(Pair("chmod", chmod.str()));

    update.push_back(Pair("segNum", action.seg_num()));
    json.push_back(Pair("update", update));
  }

  if (action.has_parent_device_name()) {
    Object parentId;
    Name parent_device_name(action.parent_device_name());
    id.push_back(Pair("userName", boost::lexical_cast<std::string>(parent_device_name)));
    id.push_back(Pair("seqNo", action.parent_seq_no()));

    json.push_back(Pair("parentId", parentId));
  }

  actions.push_back(json);
}

void
StateServer::info_actions_folder(const InterestFilter& interesFilter, const Interest& interestTrue)
{
  Name interest = interestTrue.getName();
  _LOG_DEBUG(">> info_actions_folder: " << interest);
  if (interest.size() - m_PREFIX_INFO.size() != 3 && interest.size() - m_PREFIX_INFO.size() != 4) {
    _LOG_DEBUG("Invalid interest: " << interest);
    return;
  }

  m_ioService.post(bind(&StateServer::info_actions_fileOrFolder_Execute, this, interest, true));
}

void
StateServer::info_actions_file(const InterestFilter& interesFilter, const Interest& interestTrue)
{
  Name interest = interestTrue.getName();
  if (interest.size() - m_PREFIX_INFO.size() != 3 && interest.size() - m_PREFIX_INFO.size() != 4) {
    _LOG_DEBUG("Invalid interest: " << interest);
    return;
  }

  _LOG_DEBUG(">> info_actions_file: " << interest);
  m_ioService.post(bind(&StateServer::info_actions_fileOrFolder_Execute, this, interest, false));
}

void
StateServer::info_actions_fileOrFolder_Execute(const Name& interest, bool isFolder /* = true*/)
{
  // <PREFIX_INFO>/"actions"/"folder|file"/<folder|file>/<offset>  get list of all actions
  if (interest.size() < 1) {
    // ignore any unexpected interests and errors
    _LOG_ERROR("empty interest name");
    return;
  }
  uint64_t offset = interest.get(-1).toNumber();

  /// @todo !!! add security checking

  std::string fileOrFolderName;
  if (interest.size() - m_PREFIX_INFO.size() == 4)
    fileOrFolderName = interest.get(-2).toUri();
  else // == 3
    fileOrFolderName = "";
  /*
   * {
   *    "actions": [
   *         ...
   *    ],
   *
   *    // only if there are more actions available
   *    "more": "<NDN-NAME-OF-NEXT-SEGMENT-OF-ACTION>"
   * }
   */

  _LOG_DEBUG("info_actions_fileOrFolder_Execute! offset: " << offset);
  using namespace json_spirit;
  Object json;

  Array actions;
  bool more;
  if (isFolder) {
    more =
      m_actionLog->LookupActionsInFolderRecursively(bind(StateServer::formatActionJson,
                                                                boost::ref(actions), _1, _2, _3),
                                                    fileOrFolderName, offset * 10, 10);
  }
  else {
    more = m_actionLog->LookupActionsForFile(bind(StateServer::formatActionJson,
                                                         boost::ref(actions), _1, _2, _3),
                                             fileOrFolderName, offset * 10, 10);
  }

  json.push_back(Pair("actions", actions));

  if (more) {
    json.push_back(Pair("more", boost::lexical_cast<std::string>(offset + 1)));
    // Name more = Name(interest.getPartialName(0, interest.size() - 1))(offset + 1);
    // json.push_back(Pair("more", boost::lexical_cast<std::string>(more)));
  }

  std::ostringstream os;
  write_stream(Value(json), os, pretty_print | raw_utf8);

  shared_ptr<Data> data = make_shared<Data>();
  data->setName(interest);
  data->setFreshnessPeriod(m_freshness);
  data->setContent(reinterpret_cast<const uint8_t*>(os.str().c_str()), os.str().size());
  m_keyChain.sign(*data);
  m_face.put(*data);
}

void
StateServer::formatFilestateJson(json_spirit::Array& files, const FileItem& file)
{
  /**
   *   {
   *      "filestate": [
   *      {
   *          "filename": "<FILENAME>",
   *          "owner": {
   *              "userName": "<NDN-NAME-OF-THE-USER>",
   *              "seqNo": "<SEQ_NO_OF_THE_ACTION>"
   *          },
   *
   *          "hash": "<FILE-HASH>",
   *          "timestamp": "<FILE-TIMESTAMP>",
   *          "chmod": "<FILE-MODE>",
   *          "segNum": "<NUMBER-OF-SEGMENTS(~file size)>"
   *      }, ...,
   *      ]
   *
   *      // only if there are more actions available
   *      "more": "<NDN-NAME-OF-NEXT-SEGMENT-OF-FILESTATE>"
   *   }
   */
  using namespace json_spirit;
  using namespace boost::posix_time;

  Object json;

  json.push_back(Pair("filename", file.filename()));
  json.push_back(Pair("version", file.version()));
  {
    Object owner;
    Name device_name(file.device_name());
    owner.push_back(Pair("userName", boost::lexical_cast<std::string>(device_name)));
    owner.push_back(Pair("seqNo", file.seq_no()));

    json.push_back(Pair("owner", owner));
  }

  json.push_back(Pair("hash", toHex(reinterpret_cast<const uint8_t*>(file.file_hash().data()),
                                    file.file_hash().size())));
  json.push_back(Pair("timestamp", to_iso_extended_string(from_time_t(file.mtime()))));

  std::ostringstream chmod;
  chmod << std::setbase(8) << std::setfill('0') << std::setw(4) << file.mode();
  json.push_back(Pair("chmod", chmod.str()));

  json.push_back(Pair("segNum", file.seg_num()));

  files.push_back(json);
}

void
debugFileState(const FileItem& file)
{
  std::cout << file.filename() << std::endl;
}

void
StateServer::info_files_folder(const InterestFilter& interesFilter, const Interest& interestTrue)
{
  Name interest = interestTrue.getName();
  if (interest.size() - m_PREFIX_INFO.size() != 3 && interest.size() - m_PREFIX_INFO.size() != 4) {
    _LOG_DEBUG("Invalid interest: " << interest << ", " << interest.size() - m_PREFIX_INFO.size());
    return;
  }

  _LOG_DEBUG(">> info_files_folder: " << interest);
  m_ioService.post(bind(&StateServer::info_files_folder_Execute, this, interest));
}

void
StateServer::info_files_folder_Execute(const Name& interest)
{
  // <PREFIX_INFO>/"filestate"/"folder"/<one-component-relative-folder-name>/<offset>
  if (interest.size() < 1) {
    // ignore any unexpected interests and errors
    _LOG_ERROR("empty interest name");
    return;
  }
  uint64_t offset = interest.get(-1).toNumber();

  // /// @todo !!! add security checking

  std::string folder;
  if (interest.size() - m_PREFIX_INFO.size() == 4)
    folder = interest.get(-2).toUri();
  else // == 3
    folder = "";

  /*
   *{
   *  "files": [
   *       ...
   *  ],
   *
   *  // only if there are more actions available
   *  "more": "<NDN-NAME-OF-NEXT-SEGMENT-OF-ACTION>"
   *}
   */

  using namespace json_spirit;
  Object json;

  Array files;
  bool more = m_actionLog->GetFileState()
                ->LookupFilesInFolderRecursively(bind(StateServer::formatFilestateJson,
                                                             boost::ref(files), _1),
                                                 folder, offset * 10, 10);

  json.push_back(Pair("files", files));

  if (more) {
    json.push_back(Pair("more", boost::lexical_cast<std::string>(offset + 1)));
    // Name more = Name(interest.getPartialName(0, interest.size() - 1))(offset + 1);
    // json.push_back(Pair("more", boost::lexical_cast<std::string>(more)));
  }

  std::ostringstream os;
  write_stream(Value(json), os, pretty_print | raw_utf8);

  shared_ptr<Data> data = make_shared<Data>();
  data->setName(interest);
  data->setFreshnessPeriod(m_freshness);
  data->setContent(reinterpret_cast<const uint8_t*>(os.str().c_str()), os.str().size());
  m_keyChain.sign(*data);
  m_face.put(*data);
}

void
StateServer::cmd_restore_file(const InterestFilter& interesFilter, const Interest& interestTrue)
{
  Name interest = interestTrue.getName();
  if (interest.size() - m_PREFIX_CMD.size() != 4 && interest.size() - m_PREFIX_CMD.size() != 5) {
    _LOG_DEBUG("Invalid interest: " << interest);
    return;
  }

  _LOG_DEBUG(">> cmd_restore_file: " << interest);
  m_ioService.post(bind(&StateServer::cmd_restore_file_Execute, this, interest));
}

void
StateServer::cmd_restore_file_Execute(const Name& interest)
{
  // <PREFIX_CMD>/"restore"/"file"/<one-component-relative-file-name>/<version>/<file-hash>

  /// @todo !!! add security checking

  FileItemPtr file;

  if (interest.size() - m_PREFIX_CMD.size() == 5) {
    const Buffer hash(interest.get(-1).value(), interest.get(-1).value_size());
    uint64_t version = interest.get(-2).toNumber();
    std::string filename = interest.get(-3).toUri(); // should be safe even with full relative path

    _LOG_DEBUG("filename: " << filename << " version: " << version);

    file = m_actionLog->LookupAction(filename, version, hash);

    if (!file) {
      _LOG_ERROR("Requested file is not found: [" << filename << "] version [" << version
                                                  << "] hash ["
                                                  << toHex(hash) << "]");
    }
  }
  else {
    uint64_t version = interest.get(-1).toNumber();
    std::string filename = interest.get(-2).toUri();
    file = m_actionLog->LookupAction(filename, version, Buffer(0, 0));
    if (!file) {
      _LOG_ERROR("Requested file is not found: [" << filename << "] version [" << version << "]");
    }
  }

  if (!file) {
    shared_ptr<Data> data = make_shared<Data>();
    data->setName(interest);
    data->setFreshnessPeriod(m_freshness);
    std::string msg = "FAIL: Requested file is not found";
    data->setContent(reinterpret_cast<const uint8_t*>(msg.c_str()), msg.size());
    m_keyChain.sign(*data);
    m_face.put(*data);
    return;
  }

  const Buffer hash(file->file_hash().c_str(), file->file_hash().size());

  ///////////////////
  // now the magic //
  ///////////////////

  fs::path filePath = m_rootDir / file->filename();
  Name deviceName(Block((const unsigned char*)file->device_name().c_str(), file->device_name().size()));

  _LOG_DEBUG("filePath" << filePath << " deviceName " << deviceName);

  try {
    if (fs::exists(filePath) && fs::last_write_time(filePath) == file->mtime()
#if BOOST_VERSION >= 104900
        && fs::status(filePath).permissions() == static_cast<fs::perms>(file->mode())
#endif
        ) {
      fs::ifstream input(filePath, std::ios::in | std::ios::binary);
      if (*util::Sha256(input).computeDigest() == hash) {
        shared_ptr<Data> data = make_shared<Data>();
        data->setName(interest);
        data->setFreshnessPeriod(m_freshness);
        std::string msg = "OK: File already exists";
        data->setContent(reinterpret_cast<const uint8_t*>(msg.c_str()), msg.size());
        m_keyChain.sign(*data);
        m_face.put(*data);
        _LOG_DEBUG("Asking to assemble a file, but file already exists on a filesystem");
        return;
      }
    }
  }
  catch (fs::filesystem_error& error) {
    shared_ptr<Data> data = make_shared<Data>();
    data->setName(interest);
    data->setFreshnessPeriod(m_freshness);
    std::string msg = "FAIL: File operation failed";
    data->setContent(reinterpret_cast<const uint8_t*>(msg.c_str()), msg.size());
    m_keyChain.sign(*data);
    m_face.put(*data);
    _LOG_ERROR("File operations failed on [" << filePath << "](ignoring)");
  }

  _LOG_TRACE("Restoring file [" << filePath << "]"
                                << " deviceName " << deviceName);
  if (m_objectManager.objectsToLocalFile(deviceName, hash, filePath)) {
    last_write_time(filePath, file->mtime());
#if BOOST_VERSION >= 104900
    permissions(filePath, static_cast<fs::perms>(file->mode()));
#endif
    shared_ptr<Data> data = make_shared<Data>();
    data->setName(interest);
    data->setFreshnessPeriod(m_freshness);
    std::string msg = "OK";
    data->setContent(reinterpret_cast<const uint8_t*>(msg.c_str()), msg.size());
    m_keyChain.sign(*data);
    m_face.put(*data);
    _LOG_DEBUG("Restoring file successfully!");
  }
  else {
    shared_ptr<Data> data = make_shared<Data>();
    data->setName(interest);
    data->setFreshnessPeriod(m_freshness);
    std::string msg = "FAIL: Unknown error while restoring file";
    data->setContent(reinterpret_cast<const uint8_t*>(msg.c_str()), msg.size());
    m_keyChain.sign(*data);
    m_face.put(*data);
  }
}

} // chronoshare
} // ndn
