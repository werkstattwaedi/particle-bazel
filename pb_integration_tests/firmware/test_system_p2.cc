// Copyright Offene Werkstatt Wädenswil
// SPDX-License-Identifier: MIT
//
// P2 implementation of the integration test system.

#include "pb_integration_tests/firmware/test_system.h"

#include <cstddef>

// Pigweed headers first - before Particle headers that define pin macros
#include "pw_channel/stream_channel.h"
#include "pw_log/log.h"
#include "pw_multibuf/simple_allocator.h"
#include "pw_system/io.h"
#include "pw_system/system.h"
#include "pw_thread_particle/options.h"

// Particle HAL headers
#include "pb_log/log_bridge.h"
#include "delay_hal.h"
#include "usb_hal.h"

namespace pb::test {

using pw::channel::StreamChannel;

pw::rpc::Server& GetRpcServer() { return pw::System().rpc_server(); }

void TestSystemInit(pw::Function<void()> init_callback) {
  pb::log::InitLogBridge();

  // Wait up to 10s for USB serial connection (for logs/RPC)
  for (int i = 0; i < 100; i++) {
    if (HAL_USB_USART_Is_Connected(HAL_USB_USART_SERIAL)) {
      break;
    }
    HAL_Delay_Milliseconds(100);
  }

  // Flush any pending data from console that connected before we were ready
  if (HAL_USB_USART_Is_Connected(HAL_USB_USART_SERIAL)) {
    while (HAL_USB_USART_Available_Data(HAL_USB_USART_SERIAL) > 0) {
      HAL_USB_USART_Receive_Data(HAL_USB_USART_SERIAL, false);
    }
    HAL_Delay_Milliseconds(100);
  }

  // Call test-specific initialization
  init_callback();

  // Set up RPC channel over USB serial
  static std::byte channel_buffer[8192];
  static pw::multibuf::SimpleAllocator multibuf_alloc(channel_buffer,
                                                      pw::System().allocator());

  static pw::NoDestructor<StreamChannel> channel(
      pw::system::GetReader(),
      pw::thread::particle::Options()
          .set_name("rx_thread")
          .set_stack_size(4096),
      multibuf_alloc,
      pw::system::GetWriter(),
      pw::thread::particle::Options()
          .set_name("tx_thread")
          .set_stack_size(4096),
      multibuf_alloc);

  PW_LOG_INFO("=== Integration Test System Ready ===");

  pw::system::StartAndClobberTheStack(channel->channel());
  PW_UNREACHABLE;
}

}  // namespace pb::test
