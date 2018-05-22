#include "prof.hh"

#include <capnp/message.h>
#include <capnp/serialize.h>

#include <cmath>
#include <cstdio>
#include <iostream>
#include <sstream>
#include <unordered_map>

using namespace capnprof;
using namespace capnp;
using namespace kj;

InputMap::InputMap(HeatMap &heat_map, kj::ArrayPtr<const capnp::word> data)
    : heat_map_(heat_map)
    , counts_(new uint8_t[data.size()], data.size())
    , data_(data) {
  memset(counts_.begin(), 0, counts_.size());
}

InputMap::~InputMap() {
  delete counts_.begin();
}

double InputMap::weigh(const void *start, uint32_t size) {
  KJ_ASSERT(size == word_align(size));
  if (!(data_.begin() <= start && start < data_.end()))
    return 0;
  uint32_t first_byte = reinterpret_cast<const uint8_t*>(start) - reinterpret_cast<const uint8_t*>(data_.begin());
  double weight = heat_map_.weight(first_byte, first_byte + size);
  uint32_t first_word = first_byte / sizeof(word);
  uint32_t limit_word = first_word + (size / sizeof(word));
  for (uint32_t i = first_word; i < limit_word; i++) {
    KJ_ASSERT(counts_[i] == 0);
    counts_[i] += 1;
  }
  return weight;
}

void Profiler::profile_value(TracePath &path, DynamicValue::Reader reader) {
  switch (reader.getType()) {
    case capnp::DynamicValue::VOID:
    case capnp::DynamicValue::BOOL:
    case capnp::DynamicValue::INT:
    case capnp::DynamicValue::UINT:
    case capnp::DynamicValue::FLOAT:
    case capnp::DynamicValue::ENUM:
      break;
    case capnp::DynamicValue::TEXT:
      profile_text(path, reader.as<Text>());
      break;
    case capnp::DynamicValue::DATA:
      profile_data(path, reader.as<Data>());
      break;
    case capnp::DynamicValue::LIST:
      profile_list(path, reader.as<DynamicList>());
      break;
    case capnp::DynamicValue::STRUCT:
      profile_struct(path, reader.as<DynamicStruct>());
      break;
    default:
      std::cout << reader.getType() << std::endl;
      break;
  }
}

void Profiler::profile_list(TracePath &path, DynamicList::Reader reader) {
  if (reader.size() == 0)
    return;
  AnyList::Reader any_reader(reader);
  capnp::Type elm_type = reader.getSchema().getElementType();
  switch (elm_type.which()) {
    case schema::Type::Which::BOOL:
    case schema::Type::Which::INT8:
    case schema::Type::Which::INT16:
    case schema::Type::Which::INT32:
    case schema::Type::Which::INT64:
    case schema::Type::Which::UINT8:
    case schema::Type::Which::UINT16:
    case schema::Type::Which::UINT32:
    case schema::Type::Which::UINT64:
    case schema::Type::Which::FLOAT32:
    case schema::Type::Which::FLOAT64:
    case schema::Type::Which::ENUM: {
      ArrayPtr<const byte> bytes = any_reader.getRawBytes();
      path.add_data(bytes);
      break;
    }
    case schema::Type::Which::STRUCT: {
      TracePath inner(path, TraceLink::Type::ARRAY);
      for (auto value: reader)
        profile_value(inner, value);
      break;
    }
    default: {
      KJ_UNREACHABLE;
    }
  }
}

void Profiler::profile_text(TracePath &path, Text::Reader reader) {
  path.add_data(reader.asBytes());
}

void Profiler::profile_data(TracePath &path, Data::Reader reader) {
  path.add_data(reader.asBytes());
}

void Profiler::profile_struct(TracePath &path, DynamicStruct::Reader reader) {
  AnyStruct::Reader any_reader(reader);
  ArrayPtr<const byte> data_section = any_reader.getDataSection();
  path.add_data(data_section);
  uint32_t pointer_count = any_reader.getPointerSection().size();
  ArrayPtr<const byte> pointer_section(word_align(data_section.end()),
      pointer_count * sizeof(word));
  path.add_pointers(pointer_section);
  for (auto field: reader.getSchema().getFields()) {
    if (!reader.has(field))
      continue;
    DynamicValue::Reader value = reader.get(field);
    std::stringstream buf;
    buf << reader.getSchema().getShortDisplayName().cStr() << "." << field.getProto().getName().cStr();
    TracePath inner(path, field);
    profile_value(inner, value);
  }
}

void Profiler::format_quantity(double quant, char *buf, uint32_t bufsize,
    const char **suffixes) {
  memset(buf, 0, bufsize);
  if (quant == 0) {
    sprintf(buf, "0");
  } else {
    double value;
    uint32_t suffix;
    bool round;
    if (quant < 1) {
      value = quant * 1000;
      suffix = 0;
      round = true;
    } else if (quant < 1024) {
      value = quant;
      suffix = 1;
      round = true;
    } else if (quant < 1024 * 1024) {
      value = quant / 1024.0;
      suffix = 2;
      round = false;
    } else if (quant < 1024 * 1024 * 1024) {
      value = quant / (1024 * 1024.0);
      suffix = 3;
      round = false;
    } else {
      value = quant / (1024 * 1024 * 1024.0);
      suffix = 4;
      round = false;
    }
    if (round) {
      sprintf(buf, "%i%s", static_cast<uint32_t>(::round(value)), suffixes[suffix]);
    } else {
      sprintf(buf, "%.1f%s", value, suffixes[suffix]);
    }
  }
}

void Profiler::format_bytes(uint32_t bytes, char *buf, uint32_t bufsize) {
  static const char *kSuffixes[5] = {"", "B", "K", "M", "T"};
  format_quantity(bytes, buf, bufsize, kSuffixes);
}

void Profiler::format_weight(double weight, char *buf, uint32_t bufsize) {
  static const char *kSuffixes[5] = {"mzB", "zB", "zK", "zM", "zT"};
  format_quantity(weight, buf, bufsize, kSuffixes);
}

class IdentityHeatMap : public HeatMap {
public:
  virtual double weight(uint32_t first_byte, uint32_t limit_byte) override;
};

double IdentityHeatMap::weight(uint32_t first_byte, uint32_t limit_byte) {
  return limit_byte - first_byte;
}

static IdentityHeatMap kIdentityHeatMap;

Profiler::Profiler()
    : fs_(kj::newDiskFilesystem())
    , trace_depth_(4)
    , heat_map_(&kIdentityHeatMap) { }

Profiler &Profiler::add_include_path(std::string path) {
  include_paths_.push_back(path);
  return *this;
}

Profiler &Profiler::set_heat_map(HeatMap &value) {
  heat_map_ = &value;
  return *this;
}

Profiler &Profiler::parse_schema(std::string path) {
  std::vector<StringPtr> include_paths;
  for (const std::string &path : include_paths_)
    include_paths.push_back(path.c_str());
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
  parsed_schema_ = schema_parser_.parseDiskFile(path, path,
      ArrayPtr<const StringPtr>(include_paths.data(), include_paths.size()));
#pragma GCC diagnostic pop
  return *this;
}

Profiler &Profiler::set_trace_depth(uint32_t value) {
  trace_depth_ = value;
  return *this;
}

void Profiler::traces(Trace::Order order, bool reverse, std::vector<Trace*> *traces_out) {
  pool_.flush(order, reverse, traces_out);
}

Trace &Profiler::root() {
  TraceContext context(trace_depth_, pool_, NULL);
  return TracePath(context).trace();
}

void Profiler::profile(std::string struct_name, ArrayPtr<const word> data) {
  ParsedSchema schema = parsed_schema_.getNested(struct_name);
  capnp::FlatArrayMessageReader message(data);
  capnp::DynamicStruct::Reader reader = message.getRoot<capnp::DynamicStruct>(schema.asStruct());
  InputMap input_map(*heat_map_, data);
  TraceContext context(trace_depth_, pool_, &input_map);
  TracePath root(context);
  profile_struct(root, reader);
}

void Profiler::dump(Trace::Order order) {
  std::vector<Trace*> traces;
  pool_.flush(order, false, &traces);
  uint32_t rank = 1;
  fprintf(stdout, "rank #trc     self    accum    zself   zaccum path\n");
  for (Trace* trace : traces) {
    Stats &stats = trace->stats();
    char self_bytes[32];
    format_bytes(stats.self_bytes(), self_bytes, 32);
    char accum_bytes[32];
    format_bytes(stats.accum_bytes(), accum_bytes, 32);
    std::stringstream buf;
    char self_weight[32];
    format_weight(stats.self_weight(), self_weight, 32);
    char accum_weight[32];
    format_weight(stats.accum_weight(), accum_weight, 32);
    buf << *trace;
    std::string path = buf.str();
    const char *dots = (path.size() > 32) ? "..." : "";
    fprintf(stdout, "%4i %4i %8s %8s %8s %8s %.32s%s\n", rank, trace->serial(), self_bytes,
        accum_bytes, self_weight, accum_weight, path.c_str(), dots);
    rank += 1;
  }

  traces.clear();
  pool_.flush(Trace::Order::SERIAL, false, &traces);
  for (Trace *trace : traces) {
    trace->print(std::cout);
    std::cout << std::endl;
  }
}
