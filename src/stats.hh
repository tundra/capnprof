#pragma once

#include <cstdint>

namespace capnprof {

static inline uint32_t word_align(uint32_t value) {
  return (value + 0x7) & ~0x7;
}

static inline const uint8_t *word_align(const uint8_t *ptr) {
  return reinterpret_cast<const uint8_t*>(word_align(reinterpret_cast<uint64_t>(ptr)));
}

class Stats {
public:
  Stats();

  uint32_t self_data_bytes() const { return self_data_bytes_; }
  uint32_t self_pointer_bytes() const { return self_pointer_bytes_; }
  uint32_t self_bytes() const { return self_data_bytes() + self_pointer_bytes(); }
  uint32_t child_data_bytes() const { return child_data_bytes_; }
  uint32_t child_pointer_bytes() const { return child_pointer_bytes_; }
  uint32_t child_bytes() const { return child_data_bytes() + child_pointer_bytes(); }
  uint32_t accum_bytes() const { return self_bytes() + child_bytes(); }

  double self_data_weight() const { return self_data_weight_; }
  double self_pointer_weight() const { return self_pointer_weight_; }
  double self_weight() const { return self_data_weight() + self_pointer_weight(); }
  double child_data_weight() const { return child_data_weight_; }
  double child_pointer_weight() const { return child_pointer_weight_; }
  double child_weight() const { return child_data_weight() + child_pointer_weight(); }
  double accum_weight() const { return self_weight() + child_weight(); }

  double self_factor() const { return safediv(self_weight(), self_bytes()); }
  double accum_factor() const { return safediv(accum_weight(), accum_bytes()); }

  static double safediv(double a, double b) { return (b == 0) ? 0 : (a / b); }

private:
  friend class TracePath;
  uint32_t self_data_bytes_;
  uint32_t self_pointer_bytes_;
  uint32_t child_data_bytes_;
  uint32_t child_pointer_bytes_;

  double self_data_weight_;
  double self_pointer_weight_;
  double child_data_weight_;
  double child_pointer_weight_;
};

} // namespace capnprof
