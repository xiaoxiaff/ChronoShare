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

#include "sync-core.hpp"
#include "logging.hpp"
#include <boost/make_shared.hpp>
#include <boost/test/unit_test.hpp>
#include <boost/iostreams/filter/gzip.hpp>
#include <boost/iostreams/filtering_stream.hpp>
#include <boost/iostreams/device/back_inserter.hpp>
#include <boost/range/iterator_range.hpp>
#include <boost/make_shared.hpp>

INIT_LOGGER("Test.protobuf")

using namespace std;
using namespace boost;

namespace ndn {
namespace chronoshare {

BOOST_AUTO_TEST_SUITE(ProtobufTests)

BOOST_AUTO_TEST_CASE(TestGzipProtobuf)
{
  SyncStateMsgPtr msg = make_shared<SyncStateMsg>();

  SyncState* state = msg->add_state();
  state->set_type(SyncState::UPDATE);
  state->set_seq(100);
  char x[100] = {'a'};
  state->set_locator(&x[0], sizeof(x));
  state->set_name(&x[0], sizeof(x));

  ndn::ConstBufferPtr bb = serializeMsg<SyncStateMsg>(*msg);

  ndn::ConstBufferPtr cb = serializeGZipMsg<SyncStateMsg>(*msg);
  BOOST_CHECK(cb->size() < bb->size());
  cout << cb->size() << ", " << bb->size() << endl;

  SyncStateMsgPtr msg1 = deserializeGZipMsg<SyncStateMsg>(*cb);

  BOOST_REQUIRE(msg1->state_size() == 1);

  SyncState state1 = msg1->state(0);
  BOOST_CHECK_EQUAL(state->seq(), state1.seq());
  BOOST_CHECK_EQUAL(state->type(), state1.type());
  string sx(x, 100);
  BOOST_CHECK_EQUAL(sx, state1.name());
  BOOST_CHECK_EQUAL(sx, state1.locator());
}

BOOST_AUTO_TEST_SUITE_END()

} // chronoshare
} // ndn