// Copyright (c) 2018 Tundra. All right reserved.
// Use of this code is governed by the terms defined in LICENSE.md.

#include "prof.hh"

#include <argp.h>
#include <fstream>
#include <iostream>
#include <sstream>

using namespace capnprof;


class Arguments {
public:
  Arguments();
  void parse(kj::ArrayPtr<char*> cmdline);

private:
  friend class CapnProf;
  static error_t dispatch_parse_option(int key, char *arg, argp_state *state);
  error_t parse_option(int key, char *arg, argp_state *state);

  static const argp_option kOptions[9];
  static const argp kParser;

  std::vector<std::string> import_paths;
  std::vector<std::string> args;
  std::string type;
  std::string schema;
  std::string order;
  uint32_t depth;
  uint32_t count;
  double cutoff;
  bool reverse;
};

Arguments::Arguments()
    : order("accum")
    , depth(5)
    , count(0xFFFFFFFF)
    , cutoff(0)
    , reverse(false) { }

const argp_option Arguments::kOptions[] = {
    {"import-path", 'I', "PATH", 0, ""},
    {"type", 't', "TYPE", 0, ""},
    {"schema", 's', "SCHEMA", 0, ""},
    {"depth", 'd', "DEPTH", 0, ""},
    {"count", 'c', "COUNT", 0, ""},
    {"cutoff", 'x', "CUTOFF", 0, ""},
    {"order", 'o', "ORDER", 0, ""},
    {"reverse", 'r', 0, 0, ""},
    {NULL}
};

error_t Arguments::dispatch_parse_option(int key, char *arg, struct argp_state *state) {
  return static_cast<Arguments*>(state->input)->parse_option(key, arg, state);
}

error_t Arguments::parse_option(int key, char *arg, struct argp_state *state) {
  switch (key) {
  case 'I':
    import_paths.push_back(arg);
    break;
  case 't':
    type = arg;
    break;
  case 's':
    schema = arg;
    break;
  case 'd':
    depth = atoi(arg);
    break;
  case 'c':
    count = atoi(arg);
    break;
  case 'x':
    cutoff = atof(arg);
    break;
  case 'o':
    order = arg;
    break;
  case 'r':
    reverse = true;
    break;
  case ARGP_KEY_ARG:
    args.push_back(arg);
    break;
  case ARGP_KEY_END:
    break;
  default:
    return ARGP_ERR_UNKNOWN;
  }
  return 0;
}

void Arguments::parse(kj::ArrayPtr<char*> cmdline) {
  argp_parse(&kParser, cmdline.size(), cmdline.begin(), 0, 0, this);
}

const argp Arguments::kParser = { kOptions, dispatch_parse_option, "", NULL };

class CapnProf {
public:
  int main(kj::ArrayPtr<char*> cmdline);

private:
  void profile_files();
  Trace::Order parse_order(std::string str);

  Arguments &args() { return args_; }
  Arguments args_;
};

static std::string read_file(std::string path) {
  std::stringstream bytes;
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    std::cerr << "Couldn't open file " << path << std::endl;
    return std::string();
  }
  bytes << file.rdbuf();
  file.close();
  return bytes.str();
}

void CapnProf::profile_files() {
  Profiler profiler;
  for (std::string import_path : args().import_paths)
    profiler.add_include_path(import_path);
  profiler.parse_schema(args().schema);
  profiler.set_trace_depth(args().depth);
  for (std::string arg : args().args) {
    std::string content_str = read_file(arg);
    kj::ArrayPtr<const uint8_t> contents(
        reinterpret_cast<const uint8_t*>(content_str.c_str()),
        content_str.size());
    profiler.profile_archive(args().type, contents);
  }
  uint32_t cutoff_bytes;
  if (args().cutoff == 0) {
    cutoff_bytes = 0;
  } else {
    uint32_t total_bytes = profiler.root().stats().accum_bytes();
    cutoff_bytes = static_cast<uint32_t>(total_bytes * args().cutoff);
  }
  profiler.dump(parse_order(args().order), args().reverse, args().count, cutoff_bytes);
}

Trace::Order CapnProf::parse_order(std::string str) {
  if (str == "serial") {
    return Trace::Order::SERIAL;
  } else if (str == "self") {
    return Trace::Order::SELF_BYTES;
  } else if (str == "accum") {
    return Trace::Order::ACCUM_BYTES;
  } else if (str == "zself") {
    return Trace::Order::SELF_WEIGHT;
  } else if (str == "zaccum") {
    return Trace::Order::ACCUM_WEIGHT;
  } else if (str == "zself%") {
    return Trace::Order::SELF_FACTOR;
  } else if (str == "zaccum%") {
    return Trace::Order::ACCUM_FACTOR;
  } else {
    return Trace::Order::SERIAL;
  }
}

int CapnProf::main(kj::ArrayPtr<char*> cmdline) {
  args().parse(cmdline);
  profile_files();
  return 0;
}

int main(int argc, char *argv[]) {
  CapnProf capnprof;
  return capnprof.main(kj::ArrayPtr<char*>(argv, argc));
}
