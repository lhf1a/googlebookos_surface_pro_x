// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <array>
#include <cstdint>
#include <vector>
namespace spx {
struct Acceleration {std::array<float,3> xyz;int accuracy;int64_t timestamp;};
class SscAccel {
 public:
  ~SscAccel(){close();}
  bool open();
  bool rate(float hz);
  int read(Acceleration& sample,int timeoutMs);
  void close();
 private:
  int fd=-1;
  uint32_t node=0,port=0;
  uint16_t transaction=0;
  std::vector<uint8_t> uid;
  int64_t timeOffset=0;
  bool timeSet=false;
  std::vector<Acceleration> pending;
  bool send(uint32_t id,const std::vector<uint8_t>& data);
  bool receive(std::vector<uint8_t>& packet,int timeoutMs);
};
}
