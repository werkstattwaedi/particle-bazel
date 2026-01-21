// Copyright Offene Werkstatt Wädenswil
// SPDX-License-Identifier: MIT

#include "pb_socket/tcp_client.h"

#include <cerrno>
#include <cstring>

#include "inet_hal_posix.h"
#include "netdb_hal.h"
#include "pw_log/log.h"
#include "socket_hal_posix.h"

namespace pb::socket {

ParticleTcpClient::ParticleTcpClient(const TcpConfig& config) : config_(config) {}

ParticleTcpClient::~ParticleTcpClient() { Disconnect(); }

pw::Status ParticleTcpClient::Connect() {
  if (state_ == TcpState::kConnected) {
    return pw::Status::FailedPrecondition();
  }

  state_ = TcpState::kConnecting;

  // Create socket
  socket_fd_ = sock_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (socket_fd_ < 0) {
    last_error_ = errno;
    state_ = TcpState::kError;
    PW_LOG_ERROR("Failed to create socket: %d", last_error_);
    return pw::Status::Internal();
  }

  // Set socket options
  int flag = 1;
  sock_setsockopt(socket_fd_, SOL_SOCKET, SO_KEEPALIVE, &flag, sizeof(flag));

  // Set send/receive timeouts if specified
  if (config_.read_timeout_ms > 0) {
    struct timeval tv;
    tv.tv_sec = config_.read_timeout_ms / 1000;
    tv.tv_usec = (config_.read_timeout_ms % 1000) * 1000;
    sock_setsockopt(socket_fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  }

  // Resolve hostname to IP address
  struct sockaddr_in server_addr;
  std::memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = inet_htons(config_.port);

  // Try parsing as IP address first
  if (inet_inet_pton(AF_INET, config_.host, &server_addr.sin_addr) != 1) {
    // Not an IP address - resolve hostname
    struct addrinfo hints;
    struct addrinfo* result = nullptr;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    int err = netdb_getaddrinfo(config_.host, nullptr, &hints, &result);
    if (err != 0 || result == nullptr) {
      last_error_ = err;
      state_ = TcpState::kError;
      sock_close(socket_fd_);
      socket_fd_ = -1;
      PW_LOG_ERROR("Failed to resolve hostname '%s': %d", config_.host, err);
      return pw::Status::NotFound();
    }

    // Use the first result
    auto* addr = reinterpret_cast<struct sockaddr_in*>(result->ai_addr);
    server_addr.sin_addr = addr->sin_addr;
    netdb_freeaddrinfo(result);
  }

  // Connect (with timeout handled via poll)
  // Set non-blocking for connection timeout handling
  int flags = sock_fcntl(socket_fd_, F_GETFL, 0);
  sock_fcntl(socket_fd_, F_SETFL, flags | O_NONBLOCK);

  int ret = sock_connect(
      socket_fd_,
      reinterpret_cast<struct sockaddr*>(&server_addr),
      sizeof(server_addr));

  if (ret < 0 && errno != EINPROGRESS) {
    last_error_ = errno;
    state_ = TcpState::kError;
    sock_close(socket_fd_);
    socket_fd_ = -1;
    PW_LOG_ERROR("Failed to connect: %d", last_error_);
    return pw::Status::Unavailable();
  }

  // Wait for connection with timeout
  struct pollfd pfd;
  pfd.fd = socket_fd_;
  pfd.events = POLLOUT;
  pfd.revents = 0;

  ret = sock_poll(&pfd, 1, static_cast<int>(config_.connect_timeout_ms));
  if (ret <= 0) {
    last_error_ = (ret == 0) ? ETIMEDOUT : errno;
    state_ = TcpState::kError;
    sock_close(socket_fd_);
    socket_fd_ = -1;
    PW_LOG_ERROR("Connection timeout or error: %d", last_error_);
    return pw::Status::DeadlineExceeded();
  }

  // Check for connection errors
  int socket_error = 0;
  socklen_t len = sizeof(socket_error);
  sock_getsockopt(socket_fd_, SOL_SOCKET, SO_ERROR, &socket_error, &len);
  if (socket_error != 0) {
    last_error_ = socket_error;
    state_ = TcpState::kError;
    sock_close(socket_fd_);
    socket_fd_ = -1;
    PW_LOG_ERROR("Connection failed: %d", last_error_);
    return pw::Status::Unavailable();
  }

  // Restore blocking mode
  sock_fcntl(socket_fd_, F_SETFL, flags);

  state_ = TcpState::kConnected;
  PW_LOG_INFO("Connected to %s:%u", config_.host, config_.port);
  return pw::OkStatus();
}

void ParticleTcpClient::Disconnect() {
  if (socket_fd_ >= 0) {
    sock_shutdown(socket_fd_, SHUT_RDWR);
    sock_close(socket_fd_);
    socket_fd_ = -1;
  }
  state_ = TcpState::kDisconnected;
}

bool ParticleTcpClient::IsConnected() const {
  return state_ == TcpState::kConnected && socket_fd_ >= 0;
}

pw::StatusWithSize ParticleTcpClient::DoRead(pw::ByteSpan dest) {
  if (!IsConnected()) {
    return pw::StatusWithSize::FailedPrecondition();
  }

  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  ssize_t bytes_read = sock_recv(
      socket_fd_, reinterpret_cast<void*>(dest.data()), dest.size(), 0);

  if (bytes_read < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      // No data available (non-blocking)
      return pw::StatusWithSize(0);
    }
    last_error_ = errno;
    state_ = TcpState::kError;
    PW_LOG_ERROR("Read error: %d", last_error_);
    return pw::StatusWithSize::Internal();
  }

  if (bytes_read == 0) {
    // Connection closed by peer
    state_ = TcpState::kDisconnected;
    return pw::StatusWithSize::OutOfRange();
  }

  return pw::StatusWithSize(static_cast<size_t>(bytes_read));
}

pw::Status ParticleTcpClient::DoWrite(pw::ConstByteSpan data) {
  if (!IsConnected()) {
    return pw::Status::FailedPrecondition();
  }

  size_t total_sent = 0;
  while (total_sent < data.size()) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    ssize_t bytes_sent = sock_send(
        socket_fd_,
        reinterpret_cast<const void*>(data.data() + total_sent),
        data.size() - total_sent,
        0);

    if (bytes_sent < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        // Would block - poll for write ready
        struct pollfd pfd;
        pfd.fd = socket_fd_;
        pfd.events = POLLOUT;
        int ret = sock_poll(&pfd, 1, 1000);  // 1 second timeout
        if (ret <= 0) {
          last_error_ = errno;
          state_ = TcpState::kError;
          return pw::Status::DeadlineExceeded();
        }
        continue;
      }
      last_error_ = errno;
      state_ = TcpState::kError;
      PW_LOG_ERROR("Write error: %d", last_error_);
      return pw::Status::Internal();
    }

    total_sent += static_cast<size_t>(bytes_sent);
  }

  return pw::OkStatus();
}

}  // namespace pb::socket
