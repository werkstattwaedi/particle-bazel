// Copyright Offene Werkstatt Wädenswil
// SPDX-License-Identifier: MIT

#pragma once

/// @file particle_cloud_backend.h
/// @brief Particle Cloud backend for P2 devices.
///
/// This header provides access to the Particle Cloud backend implementation
/// for use with dependency injection.
///
/// Usage:
/// @code
/// // In application main (P2):
/// auto& cloud = pb::cloud::GetParticleCloudBackend();
/// MyComponent component(cloud);
/// @endcode

#include "pb_cloud/cloud_backend.h"

namespace pb::cloud {

/// Get the Particle Cloud backend instance.
///
/// Returns a reference to the global ParticleCloudBackend singleton.
/// This function is only available on P2 devices.
///
/// @return Reference to the ParticleCloudBackend instance
CloudBackend& GetParticleCloudBackend();

}  // namespace pb::cloud
