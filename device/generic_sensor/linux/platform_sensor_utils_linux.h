// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVICE_GENERIC_SENSOR_LINUX_PLATFORM_SENSOR_UTILS_LINUX_H_
#define DEVICE_GENERIC_SENSOR_LINUX_PLATFORM_SENSOR_UTILS_LINUX_H_

#include "base/files/file_path.h"
#include "base/memory/ptr_util.h"
#include "device/generic_sensor/generic_sensor_export.h"

namespace device {

struct SensorDataLinux;
struct SensorReading;

// Generic reader class that reads sensors data from
// sensors files located in the base iio folder.
class DEVICE_GENERIC_SENSOR_EXPORT SensorReader {
 public:
  ~SensorReader();

  // Creates a new instance of SensorReader if sensor read files
  // has been found and |sensor_paths_| are set.
  static std::unique_ptr<SensorReader> Create(const SensorDataLinux& data);

  // Reads sensor values into |*reading| from sensor files
  // specified in |sensor_paths_|.
  bool ReadSensorReading(SensorReading* reading);

 private:
  explicit SensorReader(std::vector<base::FilePath> sensor_paths);

  // Contains paths to sensor files that are set when
  // Create() is called.
  const std::vector<base::FilePath> sensor_paths_;

  DISALLOW_COPY_AND_ASSIGN(SensorReader);
};

}  // namespace device

#endif  // DEVICE_GENERIC_SENSOR_LINUX_PLATFORM_SENSOR_UTILS_LINUX_H_
