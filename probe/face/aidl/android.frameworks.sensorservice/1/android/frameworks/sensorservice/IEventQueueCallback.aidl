// SPDX-License-Identifier: Apache-2.0
// Frozen AOSP V1 AIDL snapshot matching platform android.frameworks.sensorservice-V1-ndk.so.
package android.frameworks.sensorservice;
@VintfStability
interface IEventQueueCallback {
  oneway void onEvent(in android.hardware.sensors.Event e);
}
