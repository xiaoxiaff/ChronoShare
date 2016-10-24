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

#ifndef CHRONOSHARE_TESTS_TEST_COMMON_HPP
#define CHRONOSHARE_TESTS_TEST_COMMON_HPP

#include "logging.hpp"
#include "dispatcher.hpp"
#include "sync-log.hpp"
#include <ndn-cxx/util/digest.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>

namespace ndn {
namespace chronoshare {

/** \brief convert string to digest
 */
ndn::Buffer 
digestFromString(std::string hash);

/** \brief convert digest to string
 */
std::string
digestToString(const ndn::Buffer &digest);

ndn::ConstBufferPtr
digestFromFile(const boost::filesystem::path& filename);

} // chronoshare
} // ndn

#endif // CHRONOSHARE_TESTS_TEST_COMMON_HPP
