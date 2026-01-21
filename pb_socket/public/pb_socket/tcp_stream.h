// Copyright Offene Werkstatt Wädenswil
// SPDX-License-Identifier: MIT

#pragma once

/// @file tcp_stream.h
/// @brief TCP stream interface for pw_rpc communication.
///
/// This provides a pw::stream::ReaderWriter interface over TCP sockets.
/// Used for MACO device to MACO Gateway communication.

#include <cstdint>

#include "pw_status/status.h"
#include "pw_stream/stream.h"

namespace pb::socket {

/// TCP connection state.
enum class TcpState {
  kDisconnected,
  kConnecting,
  kConnected,
  kError,
};

/// Configuration for TCP client connection.
struct TcpConfig {
  /// Server IP address (IPv4 in dotted decimal or hostname)
  const char* host = nullptr;
  /// Server port
  uint16_t port = 0;
  /// Connection timeout in milliseconds
  uint32_t connect_timeout_ms = 10000;
  /// Read timeout in milliseconds (0 = non-blocking)
  uint32_t read_timeout_ms = 0;
};

/// Abstract TCP stream interface.
///
/// Provides pw::stream::NonSeekableReaderWriter over TCP with connection
/// management. Implementations handle platform-specific socket operations.
class TcpStream : public pw::stream::NonSeekableReaderWriter {
 public:
  virtual ~TcpStream() = default;

  /// Connect to the configured server.
  /// @return OkStatus on success, Unavailable if already connected,
  ///         DeadlineExceeded on timeout, FailedPrecondition on error
  virtual pw::Status Connect() = 0;

  /// Disconnect from the server.
  virtual void Disconnect() = 0;

  /// Check if currently connected.
  virtual bool IsConnected() const = 0;

  /// Get current connection state.
  virtual TcpState state() const = 0;

  /// Get last error code (platform-specific).
  virtual int last_error() const = 0;
};

}  // namespace pb::socket
