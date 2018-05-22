#include "trace.hh"

#include "prof.hh"

#include <iostream>
#include <algorithm>
#include <sstream>

using namespace capnprof;
using namespace kj;
using namespace capnp;

TraceContext::TraceContext(uint32_t max_depth, TracePool &pool, InputMap *input_map)
    : max_depth_(max_depth)
    , pool_(pool)
    , input_map_(input_map) { }

TracePath::TracePath(TraceContext &context)
    : context_(context)
    , prev_(NULL)
    , depth_(0)
    , name_hash_(0)
    , full_hash_(0)
    , trace_cache_(NULL) { }

TraceLink::TraceLink(StructSchema::Field field)
    : type_(Type::STRUCT_FIELD) {
  new (as_struct_field()) StructSchema::Field(field);
}

TraceLink::TraceLink(const char *str)
    : type_(Type::STRING)
    , as_string_(str) { }

std::string TraceLink::repr() const {
  switch (type_) {
  case Type::ROOT:
    return "(root)";
  case Type::ARRAY:
    return "[]";
  case Type::STRUCT_FIELD: {
    const capnp::StructSchema::Field *field = as_struct_field();
    std::stringstream buf;
    buf << field->getContainingStruct().getShortDisplayName().cStr()
        << "."
        << field->getProto().getName().cStr();
    return buf.str();
  }
  case Type::STRING:
    return as_string_;
  default:
    return "?";
  }
}

uint32_t TraceLink::hash() {
  return std::hash<std::string>()(repr());
}

bool TraceLink::operator==(const TraceLink &that) const {
  if (type_ != that.type_)
    return false;
  switch (type_) {
  case Type::STRING:
    return strcmp(as_string_, that.as_string_) == 0;
  case Type::STRUCT_FIELD:
    return *as_struct_field() == *that.as_struct_field();
  default:
    return true;
  }
}

bool TraceLink::operator!=(const TraceLink &that) const {
  return !(*this == that);
}

TracePath::TracePath(TracePath &prev, TraceLink link)
    : context_(prev.context())
    , prev_(&prev)
    , link_(link)
    , depth_(std::min(prev.depth() + 1, context().max_depth()))
    , name_hash_(link.hash())
    , full_hash_(0)
    , trace_cache_(NULL) {
  const TracePath *current = this;
  for (uint32_t i = 0; i < depth(); i++) {
    full_hash_ = (full_hash_ ^ current->name_hash_);
    current = current->prev();
  }
}

bool TracePath::operator==(const TracePath &that) const {
  if (depth() != that.depth())
    return false;
  const TracePath *left = this;
  const TracePath *right = &that;
  for (uint32_t i = 0; i < depth(); i++) {
    if (left->link() != right->link())
      return false;
    left = left->prev();
    right = right->prev();
  }
  return true;
}

bool TracePath::operator==(const Trace &that) const {
  if (depth() != that.depth())
    return false;
  const TracePath *left = this;
  const ArrayPtr<TraceLink> right = that.path();
  for (uint32_t i = 0; i < depth(); i++) {
    if (left->link() != right[i])
      return false;
    left = left->prev();
  }
  return true;
}

Trace &TracePath::trace() {
  if (trace_cache_ == NULL)
    trace_cache_ = &context().pool().get_or_create(*this);
  return *trace_cache_;
}

template <typename F>
void TracePath::for_each_link(F func) {
  Trace &trace = this->trace();
  bool was_seen = trace.is_seen_;
  if (!was_seen) {
    trace.is_seen_ = true;
    func(trace);
  }
  for_each_parent(func);
  trace.is_seen_ = was_seen;
}

template <typename F>
void TracePath::for_each_parent(F func) {
  if (prev_ != NULL)
    prev_->for_each_link(func);
}

void TracePath::add_data(ArrayPtr<const byte> raw_data) {
  if (raw_data.size() == 0)
    return;
  uint32_t raw_size = raw_data.size();
  uint32_t padded_size = word_align(raw_size);
  double weight = context().input_map().weigh(raw_data.begin(), padded_size);
  Trace &trace = this->trace();
  trace.is_seen_ = true;
  trace.stats().self_data_bytes_ += padded_size;
  trace.stats().self_data_weight_ += weight;
  for_each_parent([=](Trace &trace) {
    trace.stats().child_data_bytes_ += padded_size;
    trace.stats().child_data_weight_ += weight;
  });
  trace.is_seen_ = false;
}

void TracePath::add_pointers(ArrayPtr<const byte> pointers) {
  uint32_t size = pointers.size();
  double weight = context().input_map().weigh(pointers.begin(), size);
  Trace &trace = this->trace();
  trace.is_seen_ = true;
  trace.stats().self_pointer_bytes_ += size;
  trace.stats().self_pointer_weight_ += weight;
  for_each_parent([=](Trace &trace) {
    trace.stats().child_pointer_bytes_ += size;
    trace.stats().child_pointer_weight_ += weight;
  });
  trace.is_seen_ = false;
}

Trace::Trace(const TracePath &path, uint32_t serial)
    : path_(new TraceLink[path.depth()], path.depth())
    , serial_(serial)
    , depth_(path.depth())
    , hash_(path.hash())
    , is_seen_(false) {
  const TracePath *current = &path;
  for (uint32_t i = 0; i < depth(); i++) {
    path_[i] = current->link();
    current = current->prev();
  }
}

std::ostream &capnprof::operator<<(std::ostream &out, const Trace &trace) {
  if (trace.depth() == 0) {
    out << TraceLink().repr();
  } else {
    for (uint32_t i = 0; i < trace.depth(); i++) {
      const TraceLink &part = trace.path()[trace.depth() - i - 1];
      if (i > 0)
        out << " ";
      out << part.repr();
    }
  }
  return out;
}

void Trace::print(std::ostream &out) {
  out << "TRACE " << serial_ << ":" << std::endl;
  out << "    " << *this << std::endl;
}

Trace::~Trace() {
  delete[] path_.begin();
}

bool Trace::operator==(const Trace &that) const {
  if (depth() != that.depth())
    return false;
  for (uint32_t i = 0; i < depth(); i++) {
    if (path_[i] != that.path_[i])
      return false;
  }
  return true;
}

bool Trace::by_serial(const Trace *a, const Trace *b) {
  return a->serial() > b->serial();
}

template <typename R>
std::function<bool(const Trace *a, const Trace *b)> Trace::by_stat(R (Stats::*stat)() const) {
  return [=](const Trace *a, const Trace *b)->bool {
    return (a->stats().*stat)() > (b->stats().*stat)();
  };
}

bool TraceKey::operator==(const TraceKey &that) const {
  return is_path
      ? (that.is_path ? *as_path == *that.as_path : *as_path == *that.as_trace)
      : (that.is_path ? *that.as_path == *as_trace : *as_trace == *that.as_trace);
}

uint32_t TraceKey::Hash::operator()(const TraceKey &key) const {
  return key.is_path ? key.as_path->hash() : key.as_trace->hash();
}

TracePool::TracePool()
    : next_serial_(0) { }

TracePool::~TracePool() {
  for (auto entry : traces_)
    delete entry.second;
  traces_.clear();
}

Trace &TracePool::get_or_create(const TracePath &path) {
  auto iter = traces_.find(TraceKey(path));
  if (iter != traces_.end())
    return *(iter->second);
  Trace *trace = new Trace(path, next_serial_++);
  traces_[TraceKey(*trace)] = trace;
  return *trace;
}

template <typename F>
void TracePool::flush(F func, std::vector<Trace*> *traces_out) {
  for (auto entry : traces_)
    traces_out->push_back(entry.second);
  std::sort(traces_out->begin(), traces_out->end(), func);
}

template <typename F>
void TracePool::flush(F func, bool reverse, std::vector<Trace*> *traces_out) {
  if (reverse) {
    auto rev_func = [=](const Trace *a, const Trace *b) { return func(b, a); };
    return flush(rev_func, traces_out);
  } else {
    return flush(func, traces_out);
  }
}

void TracePool::flush(Trace::Order order, bool reverse, std::vector<Trace*> *traces_out) {
  switch (order) {
  case Trace::Order::SERIAL:
    return flush(Trace::by_serial, !reverse, traces_out);
  case Trace::Order::SELF_DATA_BYTES:
    return flush(Trace::by_stat(&Stats::self_data_bytes), reverse, traces_out);
  case Trace::Order::SELF_POINTER_BYTES:
    return flush(Trace::by_stat(&Stats::self_pointer_bytes), reverse, traces_out);
  case Trace::Order::SELF_BYTES:
    return flush(Trace::by_stat(&Stats::self_bytes), reverse, traces_out);
  case Trace::Order::ACCUM_BYTES:
    return flush(Trace::by_stat(&Stats::accum_bytes), reverse, traces_out);
  case Trace::Order::SELF_DATA_WEIGHT:
    return flush(Trace::by_stat(&Stats::self_data_weight), reverse, traces_out);
  case Trace::Order::SELF_POINTER_WEIGHT:
    return flush(Trace::by_stat(&Stats::self_pointer_weight), reverse, traces_out);
  case Trace::Order::SELF_WEIGHT:
    return flush(Trace::by_stat(&Stats::self_weight), reverse, traces_out);
  case Trace::Order::ACCUM_WEIGHT:
    return flush(Trace::by_stat(&Stats::accum_weight), reverse, traces_out);
  default:
    break;
  }
}
