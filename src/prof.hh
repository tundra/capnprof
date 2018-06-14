#pragma once

#include "trace.hh"
#include "heatmap.hh"

#include <capnp/schema-parser.h>
#include <kj/filesystem.h>
#include <kj/memory.h>
#include <string>
#include <vector>

namespace capnprof {

class InputMap {
public:
  InputMap(HeatMap &heat_map, kj::ArrayPtr<const capnp::word> data);
  ~InputMap();
  double weigh(const void *start, uint32_t size_bytes);

private:
  HeatMap &heat_map_;
  kj::ArrayPtr<uint8_t> counts_;
  kj::ArrayPtr<const capnp::word> data_;
};

class Profiler {
public:
  Profiler();
  Profiler &add_include_path(std::string path);
  Profiler &parse_schema(std::string path);
  Profiler &set_heat_map(HeatMap &value);
  void dump(Trace::Order order = Trace::Order::SELF_BYTES,
      bool reverse = false, uint32_t limit = 0, uint32_t cutoff_bytes = 0);

  void profile(std::string struct_name, kj::ArrayPtr<const capnp::word> data);
  void profile_archive(std::string struct_name, kj::ArrayPtr<const uint8_t> data);
  capnp::ParsedSchema &parsed_schema() { return parsed_schema_; }
  Profiler &set_trace_depth(uint32_t value);

  void traces(Trace::Order order, bool reverse, std::vector<Trace*> *traces_out);
  Trace &root();

private:
  void profile_with_context(std::string struct_name,
      kj::ArrayPtr<const capnp::word> data, TraceContext &context);

  void profile_struct(TracePath &path, capnp::DynamicStruct::Reader reader);
  void profile_value(TracePath &path, capnp::DynamicValue::Reader reader);
  void profile_list(TracePath &path, capnp::DynamicList::Reader reader);
  void profile_text(TracePath &path, capnp::Text::Reader reader);
  void profile_data(TracePath &path, capnp::Data::Reader reader);

  static void format_quantity(double bytes, char *buf, uint32_t bufsize, const char **suffixes);
  static void format_bytes(uint32_t bytes, char *buf, uint32_t bufsize);
  static void format_weight(double value, char *buf, uint32_t bufsize);

  TracePool pool_;
  kj::Own<kj::Filesystem> fs_;
  capnp::SchemaParser schema_parser_;
  capnp::ParsedSchema parsed_schema_;
  std::vector<std::string> include_paths_;
  uint32_t trace_depth_;
  HeatMap *heat_map_;
};

} // namespace capnprof
