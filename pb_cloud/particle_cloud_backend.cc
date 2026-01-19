// Copyright Offene Werkstatt Wädenswil
// SPDX-License-Identifier: MIT

#include "pb_cloud/particle_cloud_backend.h"

#include <array>
#include <cstring>
#include <memory>

#include "pw_assert/check.h"
#include "pw_async2/channel.h"
#include "pw_string/string.h"
#include "spark_wiring_string.h"
#include "system_cloud.h"

#define PW_LOG_MODULE_NAME "pb_cloud"

#include "pw_log/log.h"

namespace pb::cloud {
namespace {

/// Default channel capacity for event buffering.
inline constexpr uint16_t kEventChannelCapacity = 8;

// -- Threading Model --
// Particle Device OS runs all application code and callbacks on the same
// system thread. Event callbacks (OnEventReceived, OnPublishComplete) and
// cloud function invocations are serialized with application code.
// No synchronization is needed for accessing shared state.

/// Particle Cloud backend implementation using spark_* dynalib.
class ParticleCloudBackend : public CloudBackend {
 public:
  ParticleCloudBackend();
  ~ParticleCloudBackend();

  // Function handlers accessible to trampolines (via g_instance).
  std::array<CloudFunction, kMaxCloudFunctions> function_handlers_{};

  PublishFuture Publish(std::string_view name,
                        pw::ConstByteSpan data,
                        const PublishOptions& options) override;

  EventReceiver Subscribe(std::string_view prefix) override;

  pw::Status RegisterFunction(std::string_view name,
                               CloudFunction&& handler) override;

 protected:
  pw::Status DoRegisterVariable(
      std::string_view name,
      const void* data,
      VariableType type,
      std::unique_ptr<void, VariableDeleter> storage) override;

 private:
  EventReceiver CreateSubscriptionReceiver();

  static void OnPublishComplete(int error,
                                const void* data,
                                void* reserved,
                                void* reserved2);

  // Simple EventHandler callback (uses g_instance for routing)
  static void OnEventReceived(const char* event_name, const char* data);

  pw::async2::ValueProvider<pw::Status> publish_provider_;

  // Event channel for subscription
  pw::async2::ChannelStorage<ReceivedEvent, kEventChannelCapacity>
      event_channel_storage_;
  pw::async2::SpscChannelHandle<ReceivedEvent> event_channel_handle_;
  pw::async2::Sender<ReceivedEvent> event_sender_;

  pw::InlineString<kMaxEventNameSize> subscription_prefix_;

  // Variable storage (ownership of CloudVariable containers)
  // Use shared_ptr instead of unique_ptr<void, fn_ptr> to avoid
  // initialization issues with function pointer deleters
  std::array<std::shared_ptr<void>, kMaxCloudVariables> variable_storage_{};
  size_t variable_count_ = 0;

  // Function count (handlers are stored in public function_handlers_)
  size_t function_count_ = 0;
};

/// Global instance pointer for trampoline access.
/// Set during ParticleCloudBackend construction, cleared on destruction.
/// Only one instance is allowed (enforced by PW_CHECK).
ParticleCloudBackend* g_instance = nullptr;

// -- Static Function Trampolines --
// Particle requires raw C function pointers. We use 15 static trampolines
// that dispatch to pw::Function handlers stored in the backend instance.
// These must be defined AFTER the full ParticleCloudBackend class definition
// to avoid incomplete type errors.

/// Static trampoline for slot N - dispatches to instance handler.
/// Note: Particle cloud functions receive a String object (not const char*).
#define DEFINE_TRAMPOLINE(N)                                                 \
  int FunctionTrampoline##N(String arg) {                                    \
    const char* arg_cstr = arg.c_str();                                      \
    PW_LOG_INFO("Trampoline%d called, g_instance=%p, arg=%s",                \
                N, static_cast<void*>(g_instance),                           \
                arg_cstr ? arg_cstr : "(null)");                             \
    if (!g_instance) {                                                       \
      PW_LOG_ERROR("Trampoline%d: g_instance is null!", N);                  \
      return -1;                                                             \
    }                                                                        \
    if (!g_instance->function_handlers_[N]) {                                \
      PW_LOG_ERROR("Trampoline%d: handler is null!", N);                     \
      return -1;                                                             \
    }                                                                        \
    int result = g_instance->function_handlers_[N](                          \
        std::string_view(arg_cstr, arg.length()));                           \
    PW_LOG_INFO("Trampoline%d returned %d", N, result);                      \
    return result;                                                           \
  }

DEFINE_TRAMPOLINE(0)
DEFINE_TRAMPOLINE(1)
DEFINE_TRAMPOLINE(2)
DEFINE_TRAMPOLINE(3)
DEFINE_TRAMPOLINE(4)
DEFINE_TRAMPOLINE(5)
DEFINE_TRAMPOLINE(6)
DEFINE_TRAMPOLINE(7)
DEFINE_TRAMPOLINE(8)
DEFINE_TRAMPOLINE(9)
DEFINE_TRAMPOLINE(10)
DEFINE_TRAMPOLINE(11)
DEFINE_TRAMPOLINE(12)
DEFINE_TRAMPOLINE(13)
DEFINE_TRAMPOLINE(14)

#undef DEFINE_TRAMPOLINE

/// Array of trampoline function pointers for registration with Particle.
/// Type matches user_function_int_str_t: int(String)
constexpr std::array<user_function_int_str_t*, kMaxCloudFunctions> kTrampolines = {
    FunctionTrampoline0,  FunctionTrampoline1,  FunctionTrampoline2,
    FunctionTrampoline3,  FunctionTrampoline4,  FunctionTrampoline5,
    FunctionTrampoline6,  FunctionTrampoline7,  FunctionTrampoline8,
    FunctionTrampoline9,  FunctionTrampoline10, FunctionTrampoline11,
    FunctionTrampoline12, FunctionTrampoline13, FunctionTrampoline14,
};

// -- ParticleCloudBackend Implementation --

ParticleCloudBackend::ParticleCloudBackend() {
  PW_CHECK(g_instance == nullptr,
           "Only one ParticleCloudBackend instance allowed");
  g_instance = this;
  PW_LOG_INFO("ParticleCloudBackend constructed, g_instance=0x%08x",
              reinterpret_cast<unsigned int>(g_instance));

  // Log trampoline addresses for debugging
  PW_LOG_INFO("Trampoline array:");
  for (int i = 0; i < 3; ++i) {
    PW_LOG_INFO("  kTrampolines[%d] = 0x%08x", i,
                reinterpret_cast<unsigned int>(kTrampolines[i]));
  }
}

ParticleCloudBackend::~ParticleCloudBackend() {
  PW_LOG_INFO("ParticleCloudBackend destructing");
  g_instance = nullptr;
}

PublishFuture ParticleCloudBackend::Publish(std::string_view name,
                                            pw::ConstByteSpan data,
                                            const PublishOptions& options) {
  // Build flags
  uint32_t flags = 0;
  if (options.scope == EventScope::kPrivate) {
    flags |= PUBLISH_EVENT_FLAG_PRIVATE;
  }
  if (options.ack == AckMode::kWithAck) {
    flags |= PUBLISH_EVENT_FLAG_WITH_ACK;
  }

  // Copy name to null-terminated string
  pw::InlineString<kMaxEventNameSize> name_str(name);

  // Set up completion handler
  spark_send_event_data extra{};
  extra.size = sizeof(extra);
  extra.data_size = data.size();
  extra.content_type = static_cast<int>(options.content_type);
  extra.handler_callback = &OnPublishComplete;
  extra.handler_data = &publish_provider_;

  PW_LOG_INFO("Publish: name=%s, data_size=%zu, flags=0x%x",
              name_str.c_str(), data.size(), static_cast<unsigned>(flags));

  // Start the publish
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  bool started = spark_send_event(
      name_str.c_str(),
      reinterpret_cast<const char*>(data.data()),
      options.ttl_seconds,
      flags,
      &extra);

  PW_LOG_INFO("Publish: spark_send_event returned %s", started ? "true" : "false");

  if (!started) {
    // Publish failed to start (e.g., not connected)
    publish_provider_.Resolve(pw::Status::Unavailable());
  }

  return publish_provider_.Get();
}

EventReceiver ParticleCloudBackend::Subscribe(std::string_view prefix) {
  // Copy prefix for subscription
  subscription_prefix_ = pw::InlineString<kMaxEventNameSize>(prefix);

  // Check cloud connection status
  bool connected = spark_cloud_flag_connected();
  PW_LOG_INFO("Subscribe: cloud_connected=%s", connected ? "true" : "false");

  PW_LOG_INFO("Subscribe: prefix='%s', handler=%p",
              subscription_prefix_.c_str(),
              reinterpret_cast<void*>(&OnEventReceived));

  // Use simplest form: no handler_data, no extra params
  bool success = spark_subscribe(
      subscription_prefix_.c_str(),
      &OnEventReceived,
      nullptr,     // handler_data - null to use simple EventHandler
      MY_DEVICES,  // deprecated, ignored
      nullptr,     // deprecated, ignored
      nullptr);    // no extra params

  PW_LOG_INFO("Subscribe: spark_subscribe returned %s", success ? "true" : "false");

  if (!success) {
    PW_LOG_ERROR("Failed to subscribe to %s", subscription_prefix_.c_str());
  }

  // Recreate channel to get a fresh receiver
  return CreateSubscriptionReceiver();
}

pw::Status ParticleCloudBackend::RegisterFunction(std::string_view name,
                                                   CloudFunction&& handler) {
  PW_CHECK(function_count_ < kMaxCloudFunctions,
           "Max cloud functions (%d) exceeded", static_cast<int>(kMaxCloudFunctions));

  // Store handler in slot
  size_t slot = function_count_;
  function_handlers_[slot] = std::move(handler);

  // Copy name to null-terminated string
  pw::InlineString<64> name_str(name);

  // Get the trampoline function pointer
  auto trampoline_fn = kTrampolines[slot];

  PW_LOG_INFO("RegisterFunction: name=%s, slot=%d",
              name_str.c_str(), static_cast<int>(slot));
  PW_LOG_INFO("RegisterFunction: trampoline addr=0x%08x",
              reinterpret_cast<unsigned int>(trampoline_fn));
  PW_LOG_INFO("RegisterFunction: handler_set=%s",
              function_handlers_[slot] ? "true" : "false");

  // Verify trampoline is not null
  if (trampoline_fn == nullptr) {
    PW_LOG_ERROR("RegisterFunction: trampoline is NULL!");
    return pw::Status::Internal();
  }

  // Register trampoline with Particle
  // Type already matches p_user_function_int_str_t (pointer to user_function_int_str_t)
  bool success = spark_function(name_str.c_str(), trampoline_fn, nullptr);

  PW_LOG_INFO("RegisterFunction: spark_function returned %s", success ? "true" : "false");

  if (!success) {
    PW_LOG_ERROR("Failed to register function %s", name_str.c_str());
    // Clear the handler if registration failed
    function_handlers_[slot] = nullptr;
    return pw::Status::Internal();
  }

  ++function_count_;
  return pw::OkStatus();
}

pw::Status ParticleCloudBackend::DoRegisterVariable(
    std::string_view name,
    const void* data,
    VariableType type,
    std::unique_ptr<void, VariableDeleter> storage) {
  if (variable_count_ >= kMaxCloudVariables) {
    PW_LOG_ERROR("Max cloud variables reached (%zu)", kMaxCloudVariables);
    return pw::Status::ResourceExhausted();
  }

  // Copy name to null-terminated string
  pw::InlineString<64> name_str(name);

  Spark_Data_TypeDef spark_type;
  switch (type) {
    case VariableType::kBool:
      spark_type = CLOUD_VAR_BOOLEAN;
      break;
    case VariableType::kInt:
      spark_type = CLOUD_VAR_INT;
      break;
    case VariableType::kString:
      spark_type = CLOUD_VAR_STRING;
      break;
    case VariableType::kDouble:
      spark_type = CLOUD_VAR_DOUBLE;
      break;
    default:
      PW_LOG_ERROR("Invalid variable type");
      return pw::Status::InvalidArgument();
  }

  PW_LOG_INFO("RegisterVariable: name=%s, data=%p, type=%d, spark_type=%d",
              name_str.c_str(), data, static_cast<int>(type),
              static_cast<int>(spark_type));

  // Log the actual value for debugging (assuming int for now)
  if (type == VariableType::kInt) {
    PW_LOG_INFO("RegisterVariable: int value = %d",
                *static_cast<const int*>(data));
  }

  bool success = spark_variable(name_str.c_str(), data, spark_type, nullptr);

  PW_LOG_INFO("RegisterVariable: spark_variable returned %s", success ? "true" : "false");

  if (!success) {
    PW_LOG_ERROR("Failed to register variable %s", name_str.c_str());
    return pw::Status::Internal();
  }

  // Store ownership of the variable container
  // Convert unique_ptr to shared_ptr for easier storage
  variable_storage_[variable_count_] =
      std::shared_ptr<void>(storage.release(), storage.get_deleter());
  ++variable_count_;

  PW_LOG_INFO("RegisterVariable: success, variable_count=%d", static_cast<int>(variable_count_));
  return pw::OkStatus();
}

EventReceiver ParticleCloudBackend::CreateSubscriptionReceiver() {
  // Release any existing channel by resetting the handle first
  if (event_sender_.is_open()) {
    event_sender_.Disconnect();
  }
  event_channel_handle_ = {};  // Destroy old handle to release storage

  // Create a fresh channel
  auto [handle, sender, receiver] =
      pw::async2::CreateSpscChannel<ReceivedEvent>(event_channel_storage_);
  event_channel_handle_ = std::move(handle);
  event_sender_ = std::move(sender);
  return std::move(receiver);
}

void ParticleCloudBackend::OnPublishComplete(int error,
                                              const void* /*data*/,
                                              void* callback_data,
                                              void* /*reserved*/) {
  PW_LOG_INFO("OnPublishComplete: error=%d, callback_data=%p", error, callback_data);
  auto* provider =
      static_cast<pw::async2::ValueProvider<pw::Status>*>(callback_data);
  if (provider) {
    provider->Resolve(error == 0 ? pw::OkStatus() : pw::Status::Unknown());
    PW_LOG_INFO("OnPublishComplete: resolved provider");
  } else {
    PW_LOG_ERROR("OnPublishComplete: callback_data is null!");
  }
}

void ParticleCloudBackend::OnEventReceived(const char* event_name,
                                            const char* data) {
  // This is called from the Particle system thread (same as application code).
  // We push events into the channel for the consumer to receive.
  PW_LOG_INFO("OnEventReceived: name=%s, data=%s",
              event_name ? event_name : "(null)",
              data ? data : "(null)");

  if (g_instance == nullptr) {
    PW_LOG_ERROR("OnEventReceived: g_instance is null!");
    return;
  }
  auto* self = g_instance;

  ReceivedEvent event;
  event.name = pw::InlineString<kMaxEventNameSize>(event_name);

  // Copy data (null-terminated string for simple EventHandler)
  size_t data_len = data ? std::strlen(data) : 0;
  size_t copy_len = std::min(data_len, event.data.max_size());
  event.data.resize(copy_len);
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  std::memcpy(event.data.data(),
              reinterpret_cast<const std::byte*>(data),
              copy_len);

  event.content_type = ContentType::kText;

  // Push event into channel (non-blocking)
  if (self->event_sender_.is_open()) {
    auto status = self->event_sender_.TrySend(std::move(event));
    PW_LOG_INFO("OnEventReceived: TrySend status=%d", static_cast<int>(status.code()));
  } else {
    PW_LOG_WARN("OnEventReceived: channel not open, event dropped");
  }
}

// Global instance
ParticleCloudBackend g_particle_cloud_backend;

}  // namespace

CloudBackend& GetParticleCloudBackend() {
  PW_LOG_INFO("GetParticleCloudBackend: returning instance at %p, g_instance=%p",
              static_cast<void*>(&g_particle_cloud_backend),
              static_cast<void*>(g_instance));
  return g_particle_cloud_backend;
}

}  // namespace pb::cloud
