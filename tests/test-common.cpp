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

#include "test-common.hpp"

namespace ndn {
namespace chronoshare {

ndn::Buffer 
digestFromString(std::string hash) {
  using namespace CryptoPP;
   
  std::string digestStr;
  StringSource(hash, true,
                new HexDecoder(new StringSink(digestStr)));
  ndn::Buffer digest(reinterpret_cast<const uint8_t*>(digestStr.c_str()), digestStr.size());

  return digest;
}
std::string
digestToString(const ndn::Buffer &digest) {
  using namespace CryptoPP;

  std::string hash;
  StringSource(digest.buf(), digest.size(), true,
               new HexEncoder(new StringSink(hash), false));
  return hash;
}

ndn::ConstBufferPtr
digestFromFile(const boost::filesystem::path& filename)
{
  util::Sha256 m_digest;
  boost::filesystem::ifstream iff(filename, std::ios::in | std::ios::binary);
  while (iff.good()) {
    char buf[1024];
    iff.read(buf, 1024);
    m_digest.update(reinterpret_cast<const uint8_t*>(&buf), iff.gcount());
  }
  return m_digest.computeDigest();
}

} // chronoshare
} // ndn