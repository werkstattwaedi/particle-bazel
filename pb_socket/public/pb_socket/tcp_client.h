// Copyright Offene Werkstatt Wädenswil
// SPDX-License-Identifier: MIT

#pragma once

/// @file tcp_client.h
/// @brief TCP client implementation using Particle Device OS sockets.
///
/// This provides a TCP stream for MACO device to MACO Gateway communication
/// using the Particle Device OS socket API.

#include "pb_socket/tcp_stream.h"

namespace pb::socket {

/// TCP client implementation using Particle Device OS sockets.
///
/// Usage:
/// @code
///   pb::socket::TcpConfig config{
///       .host = "192.168.1.100",
///       .port = 5000,
///       .connect_timeout_ms = 10000,
///   };
///   pb::socket::ParticleTcpClient tcp(config);
///
///   if (tcp.Connect().ok()) {
///     // Use tcp as pw::stream::ReaderWriter
///     std::byte buffer[64];
///     tcp.Write(pw::bytes::Array<0x7E, 0x00>());
///     auto result = tcp.Read(buffer);
///   }
///   tcp.Disconnect();
/// @endcode
class ParticleTcpClient : public TcpStream {
 public:
  /// Construct TCP client with configuration.
  explicit ParticleTcpClient(const TcpConfig& config);

  /// Destructor - closes socket if open.
  ~ParticleTcpClient() override;

  // Non-copyable
  ParticleTcpClient(const ParticleTcpClient&) = delete;
  ParticleTcpClient& operator=(const ParticleTcpClient&) = delete;

  // TcpStream interface
  pw::Status Connect() override;
  void Disconnect() override;
  bool IsConnected() const override;
  TcpState state() const override { return state_; }
  int last_error() const override { return last_error_; }

 private:
  pw::StatusWithSize DoRead(pw::ByteSpan dest) override;
  pw::Status DoWrite(pw::ConstByteSpan data) override;

  TcpConfig config_;
  int socket_fd_ = -1;
  TcpState state_ = TcpState::kDisconnected;
  int last_error_ = 0;
};

}  // namespace pb::socket
