// SPDX-License-Identifier: Apache-2.0
// Frozen AOSP V1 AIDL snapshot matching platform android.frameworks.sensorservice-V1-ndk.so.
package android.frameworks.sensorservice;
@VintfStability
interface IDirectReportChannel {
  int configure(int sensorHandle, android.hardware.sensors.ISensors.RateLevel rate);
}
