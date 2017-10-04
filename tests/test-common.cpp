/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/**
 * Copyright (c) 2014-2017, Regents of the University of California,
 *                          Arizona Board of Regents,
 *                          Colorado State University,
 *                          University Pierre & Marie Curie, Sorbonne University,
 *                          Washington University in St. Louis,
 *                          Beijing Institute of Technology,
 *                          The University of Memphis.
 *
 * This file, originally written as part of NFD (Named Data Networking Forwarding Daemon),
 * is a part of ChronoShare, a decentralized file sharing application over NDN.
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

#include <ndn-cxx/util/sha256.hpp>
#include <ndn-cxx/security/signature-sha256-with-rsa.hpp>

namespace ndn {
namespace chronoshare {
namespace tests {

UnitTestTimeFixture::UnitTestTimeFixture()
  : steadyClock(make_shared<time::UnitTestSteadyClock>())
  , systemClock(make_shared<time::UnitTestSystemClock>())
{
  time::setCustomClocks(steadyClock, systemClock);
}

UnitTestTimeFixture::~UnitTestTimeFixture()
{
  time::setCustomClocks(nullptr, nullptr);
}

void
UnitTestTimeFixture::advanceClocks(const time::nanoseconds& tick, size_t nTicks)
{
  this->advanceClocks(tick, tick * nTicks);
}

void
UnitTestTimeFixture::advanceClocks(const time::nanoseconds& tick, const time::nanoseconds& total)
{
  BOOST_ASSERT(tick > time::nanoseconds::zero());
  BOOST_ASSERT(total >= time::nanoseconds::zero());

  time::nanoseconds remaining = total;
  while (remaining > time::nanoseconds::zero()) {
    if (remaining >= tick) {
      steadyClock->advance(tick);
      systemClock->advance(tick);
      remaining -= tick;
    }
    else {
      steadyClock->advance(remaining);
      systemClock->advance(remaining);
      remaining = time::nanoseconds::zero();
    }

    if (m_io.stopped())
      m_io.reset();
    m_io.poll();
  }
}

shared_ptr<Interest>
makeInterest(const Name& name, uint32_t nonce)
{
  auto interest = make_shared<Interest>(name);
  if (nonce != 0) {
    interest->setNonce(nonce);
  }
  return interest;
}

shared_ptr<Data>
makeData(const Name& name)
{
  auto data = make_shared<Data>(name);
  return signData(data);
}

Data&
signData(Data& data)
{
  ndn::SignatureSha256WithRsa fakeSignature;
  fakeSignature.setValue(ndn::encoding::makeEmptyBlock(tlv::SignatureValue));
  data.setSignature(fakeSignature);
  data.wireEncode();

  return data;
}

lp::Nack
makeNack(Interest interest, lp::NackReason reason)
{
  lp::Nack nack(std::move(interest));
  nack.setReason(reason);
  return nack;
}

lp::Nack
makeNack(const Name& name, uint32_t nonce, lp::NackReason reason)
{
  Interest interest(name);
  interest.setNonce(nonce);
  return makeNack(std::move(interest), reason);
}

ConstBufferPtr
digestFromFile(const boost::filesystem::path& filename)
{
  boost::filesystem::ifstream iff(filename, std::ios::in | std::ios::binary);
  util::Sha256 digest(iff);
  return digest.computeDigest();
}

} // namespace tests
} // namespace chronoshare
} // namespace ndn
