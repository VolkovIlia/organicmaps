#pragma once

#include "traffic/traffic_ml_model.hpp"

#include <memory>
#include <string>

// Forward declarations to avoid TFLite header dependency in header.
namespace tflite
{
class FlatBufferModel;
class Interpreter;
}  // namespace tflite

namespace traffic
{
/// \brief TensorFlow Lite implementation of traffic ML model (Android).
/// Loads a .tflite model file and runs inference using TFLite C++ API.
class TFLiteTrafficMLModel : public ITrafficMLModel
{
public:
  /// \brief Load model from file path.
  /// \param modelPath Path to .tflite model file.
  explicit TFLiteTrafficMLModel(std::string const & modelPath);
  ~TFLiteTrafficMLModel() override;

  // Non-copyable.
  TFLiteTrafficMLModel(TFLiteTrafficMLModel const &) = delete;
  TFLiteTrafficMLModel & operator=(TFLiteTrafficMLModel const &) = delete;

  [[nodiscard]] TrafficMLPrediction Predict(TrafficMLFeatures const & features) const override;
  [[nodiscard]] bool IsReady() const override;
  [[nodiscard]] std::string GetModelInfo() const override;

private:
  std::unique_ptr<tflite::FlatBufferModel> m_model;
  std::unique_ptr<tflite::Interpreter> m_interpreter;
  std::string m_modelPath;
  bool m_isReady = false;
};
}  // namespace traffic
