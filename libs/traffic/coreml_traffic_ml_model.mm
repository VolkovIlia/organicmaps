#include "traffic/coreml_traffic_ml_model.hpp"

#include "traffic/traffic_ml_model.hpp"

#include "base/logging.hpp"

#import <CoreML/CoreML.h>
#import <Foundation/Foundation.h>

#include <algorithm>

namespace traffic
{
class CoreMLTrafficMLModel::Impl
{
public:
  MLModel * m_model = nil;
  std::string m_modelName;
  bool m_isReady = false;

  explicit Impl(std::string const & modelName) : m_modelName(modelName)
  {
    @autoreleasepool
    {
      NSString * name = [NSString stringWithUTF8String:modelName.c_str()];
      NSURL * modelURL = [[NSBundle mainBundle] URLForResource:name withExtension:@"mlmodelc"];

      if (!modelURL)
      {
        LOG(LWARNING, ("CoreML model not found:", modelName));
        return;
      }

      NSError * error = nil;
      m_model = [MLModel modelWithContentsOfURL:modelURL error:&error];

      if (error || !m_model)
      {
        LOG(LWARNING, ("Failed to load CoreML model:", modelName));
        return;
      }

      m_isReady = true;
      LOG(LINFO, ("CoreML traffic model loaded:", modelName));
    }
  }

  ~Impl() { m_model = nil; }

  TrafficMLPrediction Predict(TrafficMLFeatures const & features) const
  {
    TrafficMLPrediction result;
    if (!m_isReady || !m_model)
      return result;

    @autoreleasepool
    {
      auto const inputVec = TrafficMLFeatureBuilder::ToVector(features);
      if (inputVec.size() != TrafficMLFeatureBuilder::kInputSize)
        return result;

      // Create MLMultiArray for input.
      NSError * error = nil;
      MLMultiArray * inputArray =
          [[MLMultiArray alloc] initWithShape:@[ @(TrafficMLFeatureBuilder::kInputSize) ]
                                     dataType:MLMultiArrayDataTypeFloat32
                                        error:&error];

      if (error || !inputArray)
        return result;

      for (size_t i = 0; i < inputVec.size(); ++i)
        inputArray[i] = @(inputVec[i]);

      // Create feature provider.
      MLDictionaryFeatureProvider * input =
          [[MLDictionaryFeatureProvider alloc] initWithDictionary:@{@"input" : inputArray}
                                                            error:&error];

      if (error || !input)
        return result;

      // Run prediction.
      id<MLFeatureProvider> output = [m_model predictionFromFeatures:input error:&error];
      if (error || !output)
        return result;

      // Extract output.
      MLMultiArray * outputArray = [output featureValueForName:@"output"].multiArrayValue;
      if (!outputArray || outputArray.count < 2)
        return result;

      result.m_speedMultiplier = std::clamp([outputArray[0] floatValue], 0.0f, 2.0f);
      result.m_confidence = std::clamp([outputArray[1] floatValue], 0.0f, 1.0f);
      result.m_isValid = true;
    }
    return result;
  }
};

CoreMLTrafficMLModel::CoreMLTrafficMLModel(std::string const & modelName)
  : m_impl(std::make_unique<Impl>(modelName))
{
}

CoreMLTrafficMLModel::~CoreMLTrafficMLModel() = default;

TrafficMLPrediction CoreMLTrafficMLModel::Predict(TrafficMLFeatures const & features) const
{
  return m_impl->Predict(features);
}

bool CoreMLTrafficMLModel::IsReady() const { return m_impl->m_isReady; }

std::string CoreMLTrafficMLModel::GetModelInfo() const
{
  if (!m_impl->m_isReady)
    return "CoreML model not loaded";
  return "CoreML: " + m_impl->m_modelName;
}
}  // namespace traffic
