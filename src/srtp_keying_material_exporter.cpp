#include "sorac/srtp_keying_material_exporter.hpp"

// libdatachannel::impl
#include <impl/dtlstransport.hpp>
#include <impl/peerconnection.hpp>

// plog
#include <plog/Log.h>

namespace sorac {

using PeerConnection_impl = rtc::impl_ptr<rtc::impl::PeerConnection> (
    rtc::CheshireCat<rtc::impl::PeerConnection>::*)();
static PeerConnection_impl impl;
template <PeerConnection_impl P>
struct PeerConnection_impl_Init {
  inline static auto dummy = impl = P;
};
template struct PeerConnection_impl_Init<
    &rtc::CheshireCat<rtc::impl::PeerConnection>::impl>;

struct DtlsTransportHack : rtc::impl::DtlsTransport {
#if USE_GNUTLS
  using rtc::impl::DtlsTransport::mSession;

#elif USE_MBEDTLS
  using rtc::impl::DtlsTransport::mMasterSecret;
  using rtc::impl::DtlsTransport::mRandBytes;
  using rtc::impl::DtlsTransport::mSsl;
  using rtc::impl::DtlsTransport::mTlsProfile;

#else  // OPENSSL
  using rtc::impl::DtlsTransport::mSsl;

#endif
};

std::optional<KeyingMaterial> ExportSrtpKeyingMaterial(
    std::shared_ptr<rtc::PeerConnection> pc) {
  auto dtls = static_cast<DtlsTransportHack*>(
      ((rtc::CheshireCat<rtc::impl::PeerConnection>*)(pc.get())->*impl)()
          ->getDtlsTransport()
          .get());

#if USE_MBEDTLS
  PLOG_INFO << "Deriving SRTP keying material (Mbed TLS)";

  mbedtls_dtls_srtp_info srtpInfo;
  mbedtls_ssl_get_dtls_srtp_negotiation_result(&dtls->mSsl, &srtpInfo);
  if (srtpInfo.MBEDTLS_PRIVATE(chosen_dtls_srtp_profile) !=
      MBEDTLS_TLS_SRTP_AES128_CM_HMAC_SHA1_80) {
    PLOG_ERROR << "Failed to get SRTP profile";
    return std::nullopt;
  }

  const srtp_profile_t srtpProfile = srtp_profile_aes128_cm_sha1_80;
  const size_t keySize = SRTP_AES_128_KEY_LEN;
  const size_t saltSize = SRTP_SALT_LEN;
  const size_t keySizeWithSalt = SRTP_AES_ICM_128_KEY_LEN_WSALT;

  if (dtls->mTlsProfile == MBEDTLS_SSL_TLS_PRF_NONE) {
    PLOG_ERROR << "TLS PRF type is not set";
    return std::nullopt;
  }

  // The extractor provides the client write master key, the server write master key, the client
  // write master salt and the server write master salt in that order.
  const std::string label = "EXTRACTOR-dtls_srtp";
  const size_t materialLen = keySizeWithSalt * 2;
  std::vector<unsigned char> material(materialLen);

  if (mbedtls_ssl_tls_prf(
          dtls->mTlsProfile,
          reinterpret_cast<const unsigned char*>(dtls->mMasterSecret), 48,
          label.c_str(),
          reinterpret_cast<const unsigned char*>(dtls->mRandBytes), 64,
          material.data(), materialLen) != 0) {
    PLOG_ERROR << "Failed to derive SRTP keys";
    return std::nullopt;
  }

  // Order is client key, server key, client salt, and server salt
  const unsigned char* clientKey = material.data();
  const unsigned char* serverKey = clientKey + keySize;
  const unsigned char* clientSalt = serverKey + keySize;
  const unsigned char* serverSalt = clientSalt + saltSize;
#endif

  KeyingMaterial km;
  km.client_write_key.assign(clientKey, clientKey + keySize);
  km.server_write_key.assign(serverKey, serverKey + keySize);
  km.client_write_salt.assign(clientSalt, clientSalt + saltSize);
  km.server_write_salt.assign(serverSalt, serverSalt + saltSize);
  return km;
}

}  // namespace sorac
