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

#include "face-service.hpp"
#include "core/logging.hpp"
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/thread/thread.hpp> 


namespace ndn {
namespace chronoshare {

using namespace ndn::chronoshare;

INIT_LOGGER("FaceService")

FaceService::FaceService(Face& face)
  : m_face(face)
{
}

FaceService::~FaceService()
{
  handle_stop();
}

void
FaceService::run()
{ 
  try {
    m_face.processEvents();
  }
  catch (...) {
    _LOG_DEBUG("error while connecting to the forwarder");
    boost::this_thread::sleep(boost::posix_time::milliseconds(RECONNECTION_TIME));
    _LOG_DEBUG("reconnect to the forwarder");
    run();
  }
}

void
FaceService::handle_stop()
{
  m_face.shutdown();
}

} // chronoshare
} // ndn
