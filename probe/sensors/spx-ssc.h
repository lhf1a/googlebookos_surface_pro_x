// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>
namespace spx {
struct SscSample {
  std::array<float, 3> xyz;
  int accuracy;
  int64_t timestamp;
};
using Acceleration = SscSample;
enum class SscProbeResult { Found, Missing, Unavailable };
class SscSensor {
 public:
  explicit SscSensor(std::string dataType = "accel");
  ~SscSensor() { close(); }
  const std::string& dataType() const { return type; }
  bool open(int timeoutMs = 1500);
  SscProbeResult probe(int timeoutMs = 250);
  bool rate(float hz);
  int read(SscSample& sample, int timeoutMs);
  void close();
 private:
  std::string type;
  int fd = -1;
  uint32_t node = 0, port = 0;
  uint16_t transaction = 0;
  std::vector<uint8_t> uid;
  int64_t timeOffset = 0;
  bool timeSet = false;
  std::vector<SscSample> pending;
  SscProbeResult openInternal(int timeoutMs);
  bool send(uint32_t id, const std::vector<uint8_t>& data);
  bool receive(std::vector<uint8_t>& packet, int timeoutMs);
};
class SscAccel : public SscSensor {
 public:
  SscAccel() : SscSensor("accel") {}
};
}
