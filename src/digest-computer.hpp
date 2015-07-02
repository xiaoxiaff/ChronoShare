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

#ifndef DIGEST_COMPUTER_H
#define DIGEST_COMPUTER_H

#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <ndn-cxx/util/digest.hpp>

namespace ndn {
namespace chronoshare {

namespace fs = boost::filesystem;
class DigestComputer {
public:

  // static std::string
  // digestToString(const Buffer& digest)
  // {
  //   using namespace CryptoPP;

  //   std::string hash;
  //   StringSource(digest.buf(), digest.size(), true, new HexEncoder(new StringSink(hash), false));
  //   return hash;
  // }

  static Buffer
  digestFromString(const std::string& hash)
  {
    using namespace CryptoPP;

    std::string digestStr;
    StringSource(hash, true, new HexDecoder(new StringSink(digestStr)));
    Buffer
    digest(reinterpret_cast<const uint8_t*>(digestStr.c_str()), digestStr.size());

    return digest;
  }

  // static std::string
  // shortDigest(const Buffer& digest)
  // {
  //   using namespace CryptoPP;

  //   std::string hash;
  //   StringSource(digest.buf(), digest.size(), true, new HexEncoder(new StringSink(hash), false));
  //   return hash.substr(0, 5);
  // }

private:
  util::Sha256 m_digest;
};

} // chronoshare
} // ndn

#endif // DIGEST_COMPUTER_H
