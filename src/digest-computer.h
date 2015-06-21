#ifndef DIGEST_COMPUTER_H
#define DIGEST_COMPUTER_H

#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <ndn-cxx/util/digest.hpp>

namespace fs = boost::filesystem;
class DigestComputer 
{
 public:

  DigestComputer() 
  {
  }

  ~DigestComputer() 
  {
  }

  mutable ndn::util::Sha256 m_digest;

  ndn::ConstBufferPtr
  digestFromFile(const boost::filesystem::path &filename) 
  {
    m_digest.reset();
    boost::filesystem::ifstream iff(filename, std::ios::in | std::ios::binary);
    while (iff.good())
    {
      char buf[1024];
      iff.read(buf, 1024);
      m_digest.update(reinterpret_cast<const uint8_t*>(&buf), iff.gcount());
    }
    return m_digest.computeDigest();
  }

  ndn::ConstBufferPtr
  computeRootDigest(ndn::Block &block, uint64_t seq_no)
  {
    m_digest.reset();
    m_digest << block << seq_no;
    return m_digest.computeDigest();
  }

  static std::string
  digestToString(const ndn::Buffer &digest) {
    using namespace CryptoPP;

    std::string hash;
    StringSource(digest.buf(), digest.size(), true,
                 new HexEncoder(new StringSink(hash), false));
    return hash;
  }

  static std::string
  shortDigest(const ndn::Buffer &digest) {
    using namespace CryptoPP;

    std::string hash;
    StringSource(digest.buf(), digest.size(), true,
                 new HexEncoder(new StringSink(hash), false));
    return hash.substr(0, 5);
  }

};
#endif //DIGEST_COMPUTER_H
