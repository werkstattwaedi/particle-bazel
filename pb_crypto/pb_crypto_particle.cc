// Copyright Offene Werkstatt Wädenswil
// SPDX-License-Identifier: MIT

/// @file pb_crypto_particle.cc
/// @brief Particle Device OS backend for pb_crypto.
///
/// Uses the mbedTLS functions exposed through the crypto dynalib, which may
/// use hardware acceleration on supported platforms (e.g., RTL872x on P2).
///
/// CMAC is implemented on top of AES-ECB since the dynalib doesn't expose
/// the mbedtls_cipher_cmac functions.

#include "pb_crypto/pb_crypto.h"

#include <algorithm>
#include <array>

#include "pw_assert/check.h"

// Particle Device OS mbedTLS headers (routed through dynalib)
#include "mbedtls/aes.h"

namespace pb::crypto {

namespace {

constexpr size_t kBitsPerByte = 8;

// Rb constant for 128-bit CMAC (0x87 in last byte)
constexpr std::array<std::byte, kAesBlockSize> kCmacRb = {
    std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
    std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
    std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
    std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x87}};

/// Left-shift a 16-byte block by 1 bit.
/// Used for CMAC subkey generation.
void LeftShiftBlock(pw::ConstByteSpan in, pw::ByteSpan out) {
  PW_CHECK_INT_EQ(in.size(), kAesBlockSize);
  PW_CHECK_INT_EQ(out.size(), kAesBlockSize);

  uint8_t carry = 0;
  for (int i = kAesBlockSize - 1; i >= 0; --i) {
    uint8_t b = std::to_integer<uint8_t>(in[i]);
    out[i] = static_cast<std::byte>((b << 1) | carry);
    carry = (b >> 7) & 1;
  }
}

/// XOR two 16-byte blocks: out = a ^ b
void XorBlock(pw::ConstByteSpan a, pw::ConstByteSpan b, pw::ByteSpan out) {
  PW_CHECK_INT_EQ(a.size(), kAesBlockSize);
  PW_CHECK_INT_EQ(b.size(), kAesBlockSize);
  PW_CHECK_INT_EQ(out.size(), kAesBlockSize);

  for (size_t i = 0; i < kAesBlockSize; ++i) {
    out[i] = a[i] ^ b[i];  // std::byte supports ^ directly
  }
}

/// Generate CMAC subkeys K1 and K2 from the cipher key.
/// Based on RFC 4493 Section 2.3.
pw::Status GenerateCmacSubkeys(mbedtls_aes_context* aes,
                               pw::ByteSpan k1,
                               pw::ByteSpan k2) {
  PW_CHECK_INT_EQ(k1.size(), kAesBlockSize);
  PW_CHECK_INT_EQ(k2.size(), kAesBlockSize);

  // L = AES(K, 0^128)
  std::array<std::byte, kAesBlockSize> zero_block = {};
  std::array<std::byte, kAesBlockSize> L;

  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  auto* in = reinterpret_cast<const unsigned char*>(zero_block.data());
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  auto* out = reinterpret_cast<unsigned char*>(L.data());

  if (mbedtls_aes_crypt_ecb(aes, MBEDTLS_AES_ENCRYPT, in, out) != 0) {
    return pw::Status::Internal();
  }

  // K1 = L << 1, XOR with Rb if MSB(L) = 1
  LeftShiftBlock(L, k1);
  if ((std::to_integer<uint8_t>(L[0]) & 0x80) != 0) {
    XorBlock(k1, kCmacRb, k1);
  }

  // K2 = K1 << 1, XOR with Rb if MSB(K1) = 1
  LeftShiftBlock(k1, k2);
  if ((std::to_integer<uint8_t>(k1[0]) & 0x80) != 0) {
    XorBlock(k2, kCmacRb, k2);
  }

  return pw::OkStatus();
}

}  // namespace

pw::Status AesCbcEncrypt(pw::ConstByteSpan key,
                         pw::ConstByteSpan iv,
                         pw::ConstByteSpan plaintext,
                         pw::ByteSpan ciphertext) {
  if (key.size() != kAesKeySize) {
    return pw::Status::InvalidArgument();
  }
  if (iv.size() != kAesBlockSize) {
    return pw::Status::InvalidArgument();
  }
  if (plaintext.size() % kAesBlockSize != 0) {
    return pw::Status::InvalidArgument();
  }
  if (ciphertext.size() < plaintext.size()) {
    return pw::Status::ResourceExhausted();
  }

  mbedtls_aes_context aes;
  mbedtls_aes_init(&aes);

  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  auto* key_data = reinterpret_cast<const unsigned char*>(key.data());
  if (mbedtls_aes_setkey_enc(&aes, key_data, kAesKeySize * kBitsPerByte) != 0) {
    mbedtls_aes_free(&aes);
    return pw::Status::Internal();
  }

  // mbedtls_aes_crypt_cbc modifies IV in place, so make a copy
  std::array<unsigned char, kAesBlockSize> iv_copy;
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  std::copy(reinterpret_cast<const unsigned char*>(iv.data()),
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
            reinterpret_cast<const unsigned char*>(iv.data()) + kAesBlockSize,
            iv_copy.begin());

  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  auto* in = reinterpret_cast<const unsigned char*>(plaintext.data());
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  auto* out = reinterpret_cast<unsigned char*>(ciphertext.data());

  if (mbedtls_aes_crypt_cbc(&aes,
                            MBEDTLS_AES_ENCRYPT,
                            plaintext.size(),
                            iv_copy.data(),
                            in,
                            out) != 0) {
    mbedtls_aes_free(&aes);
    return pw::Status::Internal();
  }

  mbedtls_aes_free(&aes);
  return pw::OkStatus();
}

pw::Status AesCbcDecrypt(pw::ConstByteSpan key,
                         pw::ConstByteSpan iv,
                         pw::ConstByteSpan ciphertext,
                         pw::ByteSpan plaintext) {
  if (key.size() != kAesKeySize) {
    return pw::Status::InvalidArgument();
  }
  if (iv.size() != kAesBlockSize) {
    return pw::Status::InvalidArgument();
  }
  if (ciphertext.size() % kAesBlockSize != 0) {
    return pw::Status::InvalidArgument();
  }
  if (plaintext.size() < ciphertext.size()) {
    return pw::Status::ResourceExhausted();
  }

  mbedtls_aes_context aes;
  mbedtls_aes_init(&aes);

  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  auto* key_data = reinterpret_cast<const unsigned char*>(key.data());
  if (mbedtls_aes_setkey_dec(&aes, key_data, kAesKeySize * kBitsPerByte) != 0) {
    mbedtls_aes_free(&aes);
    return pw::Status::Internal();
  }

  // mbedtls_aes_crypt_cbc modifies IV in place, so make a copy
  std::array<unsigned char, kAesBlockSize> iv_copy;
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  std::copy(reinterpret_cast<const unsigned char*>(iv.data()),
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
            reinterpret_cast<const unsigned char*>(iv.data()) + kAesBlockSize,
            iv_copy.begin());

  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  auto* in = reinterpret_cast<const unsigned char*>(ciphertext.data());
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  auto* out = reinterpret_cast<unsigned char*>(plaintext.data());

  if (mbedtls_aes_crypt_cbc(&aes,
                            MBEDTLS_AES_DECRYPT,
                            ciphertext.size(),
                            iv_copy.data(),
                            in,
                            out) != 0) {
    mbedtls_aes_free(&aes);
    return pw::Status::Internal();
  }

  mbedtls_aes_free(&aes);
  return pw::OkStatus();
}

pw::Status AesCmac(pw::ConstByteSpan key,
                   pw::ConstByteSpan data,
                   pw::ByteSpan mac) {
  if (key.size() != kAesKeySize) {
    return pw::Status::InvalidArgument();
  }
  if (mac.size() < kAesBlockSize) {
    return pw::Status::ResourceExhausted();
  }

  mbedtls_aes_context aes;
  mbedtls_aes_init(&aes);

  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  auto* key_data = reinterpret_cast<const unsigned char*>(key.data());
  if (mbedtls_aes_setkey_enc(&aes, key_data, kAesKeySize * kBitsPerByte) != 0) {
    mbedtls_aes_free(&aes);
    return pw::Status::Internal();
  }

  // Generate subkeys K1 and K2
  std::array<std::byte, kAesBlockSize> k1;
  std::array<std::byte, kAesBlockSize> k2;
  pw::Status subkey_status = GenerateCmacSubkeys(&aes, k1, k2);
  if (!subkey_status.ok()) {
    mbedtls_aes_free(&aes);
    return subkey_status;
  }

  // Calculate number of blocks
  size_t n = (data.size() + kAesBlockSize - 1) / kAesBlockSize;
  if (n == 0) {
    n = 1;  // At least one block for empty message
  }

  bool complete_block = (data.size() % kAesBlockSize == 0) && !data.empty();

  // Prepare the last block M_n
  std::array<std::byte, kAesBlockSize> last_block;
  if (complete_block) {
    // M_n = M_last XOR K1
    size_t last_block_start = (n - 1) * kAesBlockSize;
    std::copy(data.begin() + last_block_start,
              data.begin() + last_block_start + kAesBlockSize,
              last_block.begin());
    XorBlock(last_block, k1, last_block);
  } else {
    // Pad with 10*: copy remaining bytes, add 0x80, fill with 0x00
    size_t last_block_start = (n - 1) * kAesBlockSize;
    size_t remaining = data.size() - last_block_start;
    std::fill(last_block.begin(), last_block.end(), std::byte{0x00});
    if (remaining > 0) {
      std::copy(data.begin() + last_block_start,
                data.end(),
                last_block.begin());
    }
    last_block[remaining] = std::byte{0x80};
    // M_n = padded(M_last) XOR K2
    XorBlock(last_block, k2, last_block);
  }

  // CBC-MAC: X = 0, for each block: X = AES(K, X XOR M_i)
  std::array<std::byte, kAesBlockSize> x = {};

  // Process all blocks except the last
  for (size_t i = 0; i < n - 1; ++i) {
    std::array<std::byte, kAesBlockSize> block;
    std::copy(data.begin() + i * kAesBlockSize,
              data.begin() + (i + 1) * kAesBlockSize,
              block.begin());
    XorBlock(x, block, x);

    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    auto* in = reinterpret_cast<const unsigned char*>(x.data());
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    auto* out = reinterpret_cast<unsigned char*>(x.data());

    if (mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_ENCRYPT, in, out) != 0) {
      mbedtls_aes_free(&aes);
      return pw::Status::Internal();
    }
  }

  // Process the last block (already XORed with K1 or K2)
  XorBlock(x, last_block, x);

  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  auto* in = reinterpret_cast<const unsigned char*>(x.data());
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  auto* out = reinterpret_cast<unsigned char*>(mac.data());

  if (mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_ENCRYPT, in, out) != 0) {
    mbedtls_aes_free(&aes);
    return pw::Status::Internal();
  }

  mbedtls_aes_free(&aes);
  return pw::OkStatus();
}

}  // namespace pb::crypto
