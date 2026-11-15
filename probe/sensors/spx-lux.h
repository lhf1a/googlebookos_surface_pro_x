// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <array>
#include <cstdint>
#include <string>
namespace spx {
class LuxCalibration {
 public:
  bool load(const std::string& path);
  bool convert(int atime, int gain, const std::array<int,4>& xyzi, float& lux);
 private:
  std::array<uint8_t,1480> data{};
  std::array<int,4> previous{35,45,6,0};
  bool ready=false;
  int integer(size_t offset) const;
  float real(size_t offset) const;
  void real(size_t offset,float value);
  double linear(int channel,int raw) const;
  float actualGain(int gain) const;
};
}
