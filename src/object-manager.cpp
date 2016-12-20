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

#include "object-manager.hpp"
#include "object-db.hpp"
#include "core/logging.hpp"

#include <sys/stat.h>

#include <boost/lexical_cast.hpp>
#include <boost/filesystem/fstream.hpp>

#include <ndn-cxx/util/string-helper.hpp>

namespace ndn {
namespace chronoshare {

INIT_LOGGER("Object.Manager")

namespace fs = boost::filesystem;
using util::Sha256;

const int MAX_FILE_SEGMENT_SIZE = 1024;

ObjectManager::ObjectManager(Face& face, const fs::path& folder,
                             const std::string& appName)
  : m_face(face)
  , m_folder(folder / ".chronoshare")
  , m_appName(appName)
{
  fs::create_directories(m_folder);
}

ObjectManager::~ObjectManager()
{
}

// /<devicename>/<appname>/file/<hash>/<segment>
std::tuple<ConstBufferPtr /*object-db name*/, size_t /* number of segments*/>
ObjectManager::localFileToObjects(const fs::path& file, const Name& deviceName)
{
  fs::ifstream input1(file, std::ios::in | std::ios::binary);
  Sha256 fileHash(input1);
  _LOG_DEBUG("fileHash content " << fileHash.toString());

  _LOG_DEBUG("file " << file);
  ObjectDb fileDb(m_folder, fileHash.toString());

  fs::ifstream iff(file, std::ios::in | std::ios::binary);
  sqlite3_int64 segment = 0;
  while (iff.good() && !iff.eof()) {
    char buf[MAX_FILE_SEGMENT_SIZE];
    iff.read(buf, MAX_FILE_SEGMENT_SIZE);
    if (iff.gcount() == 0) {
      // stupid streams...
      break;
    }

    Name name = Name("/");
    name.append(deviceName)
      .append(m_appName)
      .append("file")
      .appendImplicitSha256Digest(fileHash.computeDigest())
      .appendNumber(segment);
    _LOG_DEBUG("publish Data Name: " << name.toUri());

    // cout << *fileHash << endl;
    // cout << name << endl;
    //_LOG_DEBUG("Read " << iff.gcount() << " from " << file << " for segment " << segment);

    shared_ptr<Data> data = make_shared<Data>();
    data->setName(name);
    data->setFreshnessPeriod(time::seconds(60));
    data->setContent(reinterpret_cast<const uint8_t*>(&buf), iff.gcount());
    m_keyChain.sign(*data);
    m_face.put(*data);

    fileDb.saveContentObject(deviceName, segment, *data);

    segment++;
  }
  if (segment == 0) // handle empty files
  {
    Name name = Name("/");
    name.append(m_appName)
      .append("file")
      .appendImplicitSha256Digest(fileHash.computeDigest())
      .append(deviceName)
      .appendNumber(0);

    shared_ptr<Data> data = make_shared<Data>();
    data->setName(name);
    data->setFreshnessPeriod(time::seconds(0));
    data->setContent(0, 0);
    m_keyChain.sign(*data);
    m_face.put(*data);

    fileDb.saveContentObject(deviceName, 0, *data);

    segment++;
  }

  return std::make_tuple(fileHash.computeDigest(), segment);
}

bool
ObjectManager::objectsToLocalFile(/*in*/const Name& deviceName,
                                  /*in*/const Buffer& fileHash, /*out*/const fs::path& file)
{
  std::string hashStr = toHex(fileHash);
  if (!ObjectDb::DoesExist(m_folder, deviceName, hashStr)) {
    _LOG_ERROR("ObjectDb for [" << m_folder << ", " << deviceName << ", " << hashStr
                                << "] does not exist or not all segments are available");
    return false;
  }

  if (!exists(file.parent_path())) {
    create_directories(file.parent_path());
  }

  fs::ofstream off(file, std::ios::out | std::ios::binary);
  ObjectDb fileDb(m_folder, hashStr);

  sqlite3_int64 segment = 0;
  BufferPtr bytes = fileDb.fetchSegment(deviceName, 0);
  while (bytes) {

    if (bytes->buf()) {
      off.write(reinterpret_cast<const char*>(bytes->buf()), bytes->size());
    }

    segment++;
    bytes = fileDb.fetchSegment(deviceName, segment);
  }

  // permission and timestamp should be assigned somewhere else(ObjectManager has no idea about
  // that)

  return true;
}

} // chronoshare
} // ndn
