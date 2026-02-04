#pragma once

#include "traffic/traffic_ml_model.hpp"

#include <memory>
#include <string>

namespace traffic
{
/// \brief CoreML implementation of traffic ML model (iOS/macOS).
/// Loads a compiled .mlmodelc from the app bundle and runs inference.
class CoreMLTrafficMLModel : public ITrafficMLModel
{
public:
  /// \brief Load model by name from app bundle.
  /// \param modelName Model name without extension (e.g., "TrafficModel").
  explicit CoreMLTrafficMLModel(std::string const & modelName);
  ~CoreMLTrafficMLModel() override;

  // Non-copyable.
  CoreMLTrafficMLModel(CoreMLTrafficMLModel const &) = delete;
  CoreMLTrafficMLModel & operator=(CoreMLTrafficMLModel const &) = delete;

  [[nodiscard]] TrafficMLPrediction Predict(TrafficMLFeatures const & features) const override;
  [[nodiscard]] bool IsReady() const override;
  [[nodiscard]] std::string GetModelInfo() const override;

private:
  // pImpl idiom to hide Objective-C types from C++ header.
  class Impl;
  std::unique_ptr<Impl> m_impl;
};
}  // namespace traffic
