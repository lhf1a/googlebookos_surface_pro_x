// SPDX-License-Identifier: Apache-2.0
// Frozen AOSP V1 AIDL snapshot matching platform android.frameworks.sensorservice-V1-ndk.so.
package android.frameworks.sensorservice;
@VintfStability
interface ISensorManager {
  android.frameworks.sensorservice.IDirectReportChannel createAshmemDirectChannel(in android.hardware.common.Ashmem mem, long maxReportLatencyNs);
  android.frameworks.sensorservice.IEventQueue createEventQueue(android.frameworks.sensorservice.IEventQueueCallback callback);
  android.frameworks.sensorservice.IDirectReportChannel createGrallocDirectChannel(in ParcelFileDescriptor buffer, long maxReportLatencyNs);
  android.hardware.sensors.SensorInfo getDefaultSensor(android.hardware.sensors.SensorType type);
  android.hardware.sensors.SensorInfo[] getSensorList();
}
