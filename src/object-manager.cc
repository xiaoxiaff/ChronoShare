/* -*- Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil -*- */
/*
 * Copyright(c) 2012 University of California, Los Angeles
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

#include "object-manager.h"
#include "object-db.h"
#include "logging.h"

#include <sys/stat.h>

#include <fstream>
#include <boost/lexical_cast.hpp>
#include <boost/throw_exception.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/make_shared.hpp>

INIT_LOGGER("Object.Manager");

using namespace ndn;
using namespace boost;
using namespace std;
namespace fs = boost::filesystem;

const int MAX_FILE_SEGMENT_SIZE = 1024;

ObjectManager::ObjectManager(boost::shared_ptr<ndn::Face> face, const fs::path &folder, const std::string &appName)
  : m_face(face)
  , m_folder(folder / ".chronoshare")
  , m_appName(appName)
{
  fs::create_directories(m_folder);
}

ObjectManager::~ObjectManager()
{
}

ndn::ConstBufferPtr
ObjectManager::fromFile(const fs::path &filename) 
{
  m_digest.reset();
  fs::ifstream iff(filename, std::ios::in | std::ios::binary);
  while (iff.good())
  {
    char buf[1024];
    iff.read(buf, 1024);
    m_digest.update(reinterpret_cast<const uint8_t*>(&buf), iff.gcount());
  }
  return m_digest.computeDigest();
}
// /<devicename>/<appname>/file/<hash>/<segment>
boost::tuple<ndn::ConstBufferPtr /*object-db name*/, size_t /* number of segments*/>
ObjectManager::localFileToObjects(const fs::path &file, const ndn::Name &deviceName)
{
  ndn::ConstBufferPtr fileHash = fromFile(file);
  _LOG_DEBUG("fileHash size " << fileHash->size() << " fileHash content " << m_digest.toString());

  _LOG_DEBUG("file " << file);
  ObjectDb fileDb(m_folder, m_digest.toString());

  fs::ifstream iff(file, std::ios::in | std::ios::binary);
  sqlite3_int64 segment = 0;
  while (iff.good() && !iff.eof())
    {
      char buf[MAX_FILE_SEGMENT_SIZE];
      iff.read(buf, MAX_FILE_SEGMENT_SIZE);
      if (iff.gcount() == 0)
        {
          // stupid streams...
          break;
        }

      ndn::Name name = ndn::Name("/");
//      name.append(deviceName).append(m_appName).append("file").append(reinterpret_cast<const uint8_t*>(fileHash->GetHash()), fileHash->GetHashBytes()).appendNumber(segment);
      name.append(deviceName).append(m_appName).append("file").append(ndn::name::Component(*fileHash)).appendNumber(segment);

      // cout << *fileHash << endl;
      // cout << name << endl;
      //_LOG_DEBUG("Read " << iff.gcount() << " from " << file << " for segment " << segment);

      boost::shared_ptr<Data> data = boost::make_shared<Data>();
      data->setName(name);
      data->setFreshnessPeriod(time::seconds(60));
      data->setContent(reinterpret_cast<const uint8_t*>(&buf), iff.gcount());
      m_keyChain.sign(*data);
      m_face->put(*data);

      fileDb.saveContentObject(deviceName, segment, data->wireEncode());


      segment ++;
    }
  if (segment == 0) // handle empty files
    {
      ndn::Name name = ndn::Name("/");
      name.append(m_appName);
      name.append("file").append(ndn::name::Component(*fileHash)).append(deviceName).appendNumber(0);

      boost::shared_ptr<Data> data = boost::make_shared<Data>();
      data->setName(name);
      data->setFreshnessPeriod(time::seconds(0));
      data->setContent(0, 0);
      m_keyChain.sign(*data);
      m_face->put(*data);

      fileDb.saveContentObject(deviceName, 0, data->wireEncode());

      segment ++;
    }

  return boost::make_tuple(fileHash, segment);
}

bool
ObjectManager::objectsToLocalFile(/*in*/const ndn::Name &deviceName, /*in*/const ndn::Buffer &fileHash, /*out*/ const fs::path &file)
{
  string hashStr = hashToString(fileHash);
  if (!ObjectDb::DoesExist(m_folder, deviceName, hashStr))
    {
      _LOG_ERROR("ObjectDb for [" << m_folder << ", " << deviceName << ", " << hashStr << "] does not exist or not all segments are available");
      return false;
    }

  if (!exists(file.parent_path()))
    {
      create_directories(file.parent_path());
    }

  fs::ofstream off(file, std::ios::out | std::ios::binary);
  ObjectDb fileDb(m_folder, hashStr);

  sqlite3_int64 segment = 0;
  ndn::BufferPtr bytes = fileDb.fetchSegment(deviceName, 0);
  while (bytes)
    {

      if (bytes->buf())
        {
          off.write(reinterpret_cast<const char*>(bytes->buf()), bytes->size());
        }

      segment ++;
      bytes = fileDb.fetchSegment(deviceName, segment);
    }

  // permission and timestamp should be assigned somewhere else(ObjectManager has no idea about that)

  return true;
}

std::string
ObjectManager::hashToString(const ndn::Buffer &digest)
{
  using namespace CryptoPP;

  std::string hash;
  StringSource(digest.buf(), digest.size(), true,
               new HexEncoder(new StringSink(hash), false));
  return hash;
}
