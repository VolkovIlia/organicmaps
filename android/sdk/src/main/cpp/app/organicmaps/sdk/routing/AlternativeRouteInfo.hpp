#pragma once

#include "app/organicmaps/sdk/core/jni_helper.hpp"
#include "app/organicmaps/sdk/util/Distance.hpp"

#include "routing/alternative_route.hpp"

#include "platform/distance.hpp"

#include <vector>

/// @brief Create a Java AlternativeRouteInfo object from C++ AlternativeRoute.
/// @param env JNI environment.
/// @param alt C++ AlternativeRoute struct.
/// @param primaryDurationSeconds Duration of primary route for time difference calculation.
/// @return Java AlternativeRouteInfo object.
inline jobject CreateAlternativeRouteInfo(JNIEnv * env, routing::AlternativeRoute const & alt,
                                          double primaryDurationSeconds)
{
  static jclass const klass = jni::GetGlobalClassRef(env, "app/organicmaps/sdk/routing/AlternativeRouteInfo");
  // Constructor signature: (int routeIndex, Distance distance, int durationSeconds,
  //                         int overlapPercent, int timeDifferenceSeconds)
  static jmethodID const ctor = jni::GetConstructorID(env, klass,
      "(ILapp/organicmaps/sdk/util/Distance;III)V");

  // Create Distance object from meters
  platform::Distance const dist = platform::Distance::CreateFormatted(alt.distanceMeters);
  jobject const distanceObj = ToJavaDistance(env, dist);

  int const overlapPercent = static_cast<int>(alt.overlapWithPrimary * 100.0);
  int const timeDiff = static_cast<int>(alt.durationSeconds - primaryDurationSeconds);

  jobject const result = env->NewObject(klass, ctor,
      static_cast<jint>(alt.routeIndex),
      distanceObj,
      static_cast<jint>(alt.durationSeconds),
      static_cast<jint>(overlapPercent),
      static_cast<jint>(timeDiff));

  env->DeleteLocalRef(distanceObj);

  ASSERT(result, (jni::DescribeException()));
  return result;
}

/// @brief Create a Java array of AlternativeRouteInfo objects.
/// @param env JNI environment.
/// @param alternatives Vector of C++ AlternativeRoute structs.
/// @param primaryDurationSeconds Duration of primary route for time difference calculation.
/// @return Java AlternativeRouteInfo[] array, or nullptr if empty.
inline jobjectArray CreateAlternativeRouteInfoArray(JNIEnv * env,
                                                     std::vector<routing::AlternativeRoute> const & alternatives,
                                                     double primaryDurationSeconds)
{
  if (alternatives.empty())
    return nullptr;

  static jclass const klass = jni::GetGlobalClassRef(env, "app/organicmaps/sdk/routing/AlternativeRouteInfo");

  jobjectArray const result = env->NewObjectArray(static_cast<jsize>(alternatives.size()), klass, nullptr);

  for (size_t i = 0; i < alternatives.size(); ++i)
  {
    jobject const altInfo = CreateAlternativeRouteInfo(env, alternatives[i], primaryDurationSeconds);
    env->SetObjectArrayElement(result, static_cast<jsize>(i), altInfo);
    env->DeleteLocalRef(altInfo);
  }

  return result;
}
