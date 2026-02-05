#include "traffic/traffic_ml_model_factory.hpp"

#include "traffic/stub_traffic_ml_model.hpp"

#include "std/target_os.hpp"

// TODO(mesh-traffic): Enable TFLite when TensorFlow Lite is configured
// #ifdef OMIM_OS_ANDROID
// #include "traffic/tflite_traffic_ml_model.hpp"
// #endif

#ifdef OMIM_OS_IPHONE
#include "traffic/coreml_traffic_ml_model.hpp"
#endif

namespace traffic
{
std::unique_ptr<ITrafficMLModel> CreateTrafficMLModel(std::string const & modelPath)
{
// TODO(mesh-traffic): Enable TFLite when TensorFlow Lite is configured
// #ifdef OMIM_OS_ANDROID
//   return std::make_unique<TFLiteTrafficMLModel>(modelPath);
#if defined(OMIM_OS_IPHONE)
  return std::make_unique<CoreMLTrafficMLModel>(modelPath);
#else
  // Desktop/Android/testing: use stub model until TFLite is configured.
  (void)modelPath;
  return std::make_unique<StubTrafficMLModel>();
#endif
}

bool IsPlatformMLAvailable()
{
// TODO(mesh-traffic): Return true for Android when TFLite is configured
#if defined(OMIM_OS_IPHONE)
  return true;
#else
  return false;
#endif
}
}  // namespace traffic
