#include "traffic/traffic_ml_model_factory.hpp"

#include "traffic/stub_traffic_ml_model.hpp"

#include "std/target_os.hpp"

#ifdef OMIM_OS_ANDROID
#include "traffic/tflite_traffic_ml_model.hpp"
#endif

#ifdef OMIM_OS_IPHONE
#include "traffic/coreml_traffic_ml_model.hpp"
#endif

namespace traffic
{
std::unique_ptr<ITrafficMLModel> CreateTrafficMLModel(std::string const & modelPath)
{
#ifdef OMIM_OS_ANDROID
  return std::make_unique<TFLiteTrafficMLModel>(modelPath);
#elif defined(OMIM_OS_IPHONE)
  return std::make_unique<CoreMLTrafficMLModel>(modelPath);
#else
  // Desktop/testing: use stub model.
  (void)modelPath;
  return std::make_unique<StubTrafficMLModel>();
#endif
}

bool IsPlatformMLAvailable()
{
#if defined(OMIM_OS_ANDROID) || defined(OMIM_OS_IPHONE)
  return true;
#else
  return false;
#endif
}
}  // namespace traffic
