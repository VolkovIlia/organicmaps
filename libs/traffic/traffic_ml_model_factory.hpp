#pragma once

#include "traffic/traffic_ml_model.hpp"

#include <memory>
#include <string>

namespace traffic
{
/// \brief Factory to create platform-appropriate ML model.
/// Returns TFLite on Android, CoreML on iOS, Stub on desktop.
/// \param modelPath Path to model file (Android) or model name without extension (iOS).
[[nodiscard]] std::unique_ptr<ITrafficMLModel> CreateTrafficMLModel(std::string const & modelPath);

/// \brief Check if real ML model is available on current platform.
/// \return true on Android and iOS, false on desktop.
[[nodiscard]] bool IsPlatformMLAvailable();
}  // namespace traffic
