#pragma once

#include "zipprof.h"

#include <kj/common.h>

namespace capnprof {

class HeatMap {
public:
  virtual ~HeatMap() { }
  virtual double weight(uint32_t first_byte, uint32_t limit_byte) = 0;
};

class DeflateHeatMap : public HeatMap {
public:
  DeflateHeatMap(zipprof::DeflateProfile &profile)
      : profile_(profile) { }
  virtual double weight(uint32_t first_byte, uint32_t limit_byte);
private:
  zipprof::DeflateProfile &profile_;
};

} // namespace capnprof
