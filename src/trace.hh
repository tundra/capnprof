#pragma once

#include "stats.hh"

#include <capnp/message.h>
#include <capnp/dynamic.h>

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace capnprof {

class Trace;
class TracePool;
class InputMap;

class TraceContext {
public:
  TraceContext(uint32_t max_depth, TracePool &pool, InputMap *input_map);
  uint32_t max_depth() { return max_depth_; }
  TracePool &pool() { return pool_; }
  InputMap &input_map() { return *input_map_; }

private:
  uint32_t max_depth_;
  TracePool &pool_;
  InputMap *input_map_;
};

class TraceLinkBehavior {

};

class TraceLink {
public:
  enum class Type {
    ROOT,
    STRUCT_FIELD,
    ARRAY,
    STRING
  };

  TraceLink() : TraceLink(Type::ROOT) { }
  TraceLink(Type type) : type_(type) { }
  TraceLink(capnp::StructSchema::Field field);
  TraceLink(const char *value);
  uint32_t hash();
  bool operator==(const TraceLink &that) const;
  bool operator!=(const TraceLink &that) const;
  std::string repr() const;

  capnp::StructSchema::Field *as_struct_field() { return reinterpret_cast<capnp::StructSchema::Field*>(as_struct_field_); }
  const capnp::StructSchema::Field *as_struct_field() const { return reinterpret_cast<const capnp::StructSchema::Field*>(as_struct_field_); }

private:
  Type type_;
  union {
    uint8_t as_struct_field_[sizeof(capnp::StructSchema::Field)];
    const char *as_string_;
  };
};

// A stack-allocated chain of names that identify a path through the structure.
class TracePath {
public:
  // Create a root trace path where traces are capped at the given max depth.
  TracePath(TraceContext &context);

  // Create a trace path that extends a previous one with a new link.
  TracePath(TracePath &prev, TraceLink link);

  bool operator==(const TracePath &that) const;
  bool operator==(const Trace &that) const;
  uint32_t hash() const { return full_hash_; }

  const TracePath *prev() const { return prev_; }
  const TraceLink &link() const { return link_; }
  uint32_t depth() const { return depth_; }
  TraceContext &context() const { return context_; }
  Trace &trace();

  void add_data(kj::ArrayPtr<const kj::byte> data);
  void add_pointers(kj::ArrayPtr<const kj::byte> pointers);

  template <typename F>
  inline void for_each_parent(F func);

  template <typename F>
  inline void for_each_link(F func);

private:
  TraceContext &context_;
  TracePath *prev_;
  TraceLink link_;
  uint32_t depth_;
  uint32_t name_hash_;
  uint32_t full_hash_;
  Trace *trace_cache_;
};

class Trace {
public:
  enum class Order {
    SERIAL,
    SELF_DATA_BYTES,
    SELF_POINTER_BYTES,
    SELF_BYTES,
    ACCUM_BYTES,
    SELF_DATA_WEIGHT,
    SELF_POINTER_WEIGHT,
    SELF_WEIGHT,
    ACCUM_WEIGHT,
    SELF_FACTOR,
    ACCUM_FACTOR,
  };

  Trace(const TracePath &path, uint32_t serial);
  ~Trace();

  bool operator==(const Trace &that) const;
  uint32_t hash() const { return hash_; }

  kj::ArrayPtr<TraceLink> path() const { return path_; }
  uint32_t serial() const { return serial_; }
  uint32_t depth() const { return depth_; }
  Stats &stats() { return stats_; }
  const Stats &stats() const { return stats_; }
  void print(std::ostream &out);

  static bool by_serial(const Trace *a, const Trace *b);

  template <typename R>
  static std::function<bool(const Trace *a, const Trace *b)> by_stat(R (Stats::*stat)() const);

private:
  friend class TracePath;

  kj::ArrayPtr<TraceLink> path_;
  uint32_t serial_;
  uint32_t depth_;
  uint32_t hash_;
  bool is_seen_;
  Stats stats_;
};

std::ostream &operator<<(std::ostream &out, const Trace &trace);

class TraceKey {
public:
  class Hash {
  public:
    uint32_t operator()(const TraceKey &key) const;
  };

  TraceKey(const TracePath &path) : is_path(true), as_path(&path) { }
  TraceKey(const Trace &trace) : is_path(false), as_trace(&trace) { }
  bool operator==(const TraceKey &that) const;
private:
  bool is_path;
  union {
    const TracePath *as_path;
    const Trace *as_trace;
  };
};

class TracePool {
public:
  TracePool();
  ~TracePool();
  Trace &get_or_create(const TracePath &path);
  uint32_t size() { return traces_.size(); }

  void flush(Trace::Order order, bool reverse, std::vector<Trace*> *traces_out);

private:
  friend class Profiler;

  template <typename F>
  void flush(F func, std::vector<Trace*> *traces_out);

  template <typename F>
  void flush(F func, bool reverse, std::vector<Trace*> *traces_out);

  uint32_t next_serial_;
  std::unordered_map<TraceKey, Trace*, TraceKey::Hash> traces_;
};

} // namespace capnprof
