#include "traffic/tflite_traffic_ml_model.hpp"

#include "traffic/traffic_ml_model.hpp"

#include "base/logging.hpp"

#include "tensorflow/lite/interpreter.h"
#include "tensorflow/lite/kernels/register.h"
#include "tensorflow/lite/model.h"

#include <algorithm>

namespace traffic
{
TFLiteTrafficMLModel::TFLiteTrafficMLModel(std::string const & modelPath)
  : m_modelPath(modelPath)
{
  m_model = tflite::FlatBufferModel::BuildFromFile(modelPath.c_str());
  if (!m_model)
  {
    LOG(LWARNING, ("Failed to load TFLite model from:", modelPath));
    return;
  }

  tflite::ops::builtin::BuiltinOpResolver resolver;
  tflite::InterpreterBuilder builder(*m_model, resolver);
  if (builder(&m_interpreter) != kTfLiteOk || !m_interpreter)
  {
    LOG(LWARNING, ("Failed to build TFLite interpreter"));
    return;
  }

  if (m_interpreter->AllocateTensors() != kTfLiteOk)
  {
    LOG(LWARNING, ("Failed to allocate TFLite tensors"));
    return;
  }

  m_isReady = true;
  LOG(LINFO, ("TFLite traffic model loaded:", modelPath));
}

TFLiteTrafficMLModel::~TFLiteTrafficMLModel() = default;

TrafficMLPrediction TFLiteTrafficMLModel::Predict(TrafficMLFeatures const & features) const
{
  TrafficMLPrediction result;
  if (!m_isReady)
    return result;

  auto const inputVec = TrafficMLFeatureBuilder::ToVector(features);
  if (inputVec.size() != TrafficMLFeatureBuilder::kInputSize)
    return result;

  // Copy input to tensor.
  float * inputTensor = m_interpreter->typed_input_tensor<float>(0);
  if (!inputTensor)
    return result;

  std::copy(inputVec.begin(), inputVec.end(), inputTensor);

  // Run inference.
  if (m_interpreter->Invoke() != kTfLiteOk)
    return result;

  // Get output (speed multiplier, confidence).
  float const * output = m_interpreter->typed_output_tensor<float>(0);
  if (!output)
    return result;

  result.m_speedMultiplier = std::clamp(output[0], 0.0f, 2.0f);
  result.m_confidence = std::clamp(output[1], 0.0f, 1.0f);
  result.m_isValid = true;
  return result;
}

bool TFLiteTrafficMLModel::IsReady() const { return m_isReady; }

std::string TFLiteTrafficMLModel::GetModelInfo() const
{
  if (!m_isReady)
    return "TFLite model not loaded";
  return "TFLite: " + m_modelPath;
}
}  // namespace traffic
