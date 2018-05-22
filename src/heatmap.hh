#pragma once

#include <kj/common.h>

namespace capnprof {

class HeatMap {
public:
  virtual ~HeatMap() { }
  virtual double weight(uint32_t first_byte, uint32_t limit_byte) = 0;


};

} // namespace capnprof
