// Copyright (c) 2018 Tundra. All right reserved.
// Use of this code is governed by the terms defined in LICENSE.

#include "prof.hh"

#include <zipprof.h>

#include "gtest/gtest.h"

#include <fstream>
#include <capnp/serialize.h>

using namespace capnprof;
using namespace capnp;
using namespace kj;

TEST(prof, trace_path_equals) {
  TracePool pool;
  for (uint32_t i = 1; i <= 3; i++) {
    TraceContext context(i, pool, NULL);
    TracePath r(context);
    TracePath c(r, "c");
    TracePath bc(c, "b");
    TracePath abc(bc, "a");
    TracePath d(r, "d");
    TracePath bd(d, "b");
    TracePath abd(bd, "a");
    EXPECT_FALSE(c.hash() == d.hash());
    Trace tabd(abd, 0);
    Trace tabc(abc, 0);
    if (i < 3) {
      EXPECT_EQ(abd.hash(), abc.hash());
      EXPECT_EQ(tabd, tabc);
      EXPECT_EQ(abd, tabc);
    } else {
      EXPECT_FALSE(abd.hash() == abc.hash());
      EXPECT_FALSE(tabd == tabc);
      EXPECT_FALSE(abd == tabc);
    }
    if (i == 1) {
      EXPECT_FALSE(c == d);
      EXPECT_TRUE(c == c);
      EXPECT_TRUE(abd == abc);
    } else if (i == 2) {
      EXPECT_FALSE(c == d);
      EXPECT_TRUE(c == c);
      EXPECT_TRUE(abd == abc);
    } else if (i == 3) {
      EXPECT_FALSE(c == d);
      EXPECT_TRUE(c == c);
      EXPECT_FALSE(abd == abc);
    }
  }
}

TEST(prof, pool) {
  TracePool pool;
  TraceContext context(2, pool, NULL);
  TracePath r(context);
  TracePath z(r, "z");
  TracePath yz(z, "y");
  TracePath xyz(yz, "x");
  EXPECT_EQ(0, z.trace().serial());
  EXPECT_EQ(1, yz.trace().serial());
  EXPECT_EQ(2, xyz.trace().serial());
  EXPECT_EQ(0, z.trace().serial());
  EXPECT_EQ(3, pool.size());

  TracePath w(r, "w");
  TracePath yw(w, "y");
  TracePath xyw(yw, "x");
  EXPECT_EQ(3, w.trace().serial());
  EXPECT_EQ(4, yw.trace().serial());
  EXPECT_EQ(2, xyz.trace().serial());
  EXPECT_EQ(5, pool.size());
}

void build_message(Profiler &profiler, std::string struct_name,
    OutputStream &out, std::function<void (DynamicStruct::Builder&)> thunk) {
  MallocMessageBuilder message_builder;
  StructSchema schema = profiler.parsed_schema().getNested(struct_name).asStruct();
  DynamicStruct::Builder struct_builder = message_builder.initRoot<DynamicStruct>(schema);
  thunk(struct_builder);
  capnp::writeMessage(out, message_builder);
}

static void profile_struct(Profiler &profiler, std::string struct_name,
    std::function<void (DynamicStruct::Builder&)> thunk) {
  VectorOutputStream out;
  build_message(profiler, struct_name, out, thunk);
  ArrayPtr<byte> bytes = out.getArray();
  ArrayPtr<const word> words(reinterpret_cast<word*>(bytes.begin()),
      bytes.size() / sizeof(word));
  profiler.profile(struct_name, words);
  KJ_ASSERT(profiler.root().stats().accum_bytes() < bytes.size());
}

static void profile_struct_zipped(Profiler &profiler, std::string struct_name,
    const zipprof::Compressor &compr, std::function<void (DynamicStruct::Builder&)> thunk) {
  VectorOutputStream out;
  build_message(profiler, struct_name, out, thunk);

  ArrayPtr<byte> data = out.getArray();
  zipprof::DeflateProfile profile = zipprof::Profiler::profile_string(
      std::string(data.asChars().begin(), data.size()), compr);
  DeflateHeatMap heat_map(profile);
  profiler.set_heat_map(heat_map);

  ArrayPtr<const word> words(reinterpret_cast<word*>(data.begin()),
      data.size() / sizeof(word));
  profiler.profile(struct_name, words);
}

TEST(prof, anything_works) {
  Profiler profiler;
  profiler.parse_schema("tests/res/test.capnp");

  profile_struct(profiler, "Root", [](DynamicStruct::Builder &root) {
    root.init("a", 100);
    root.init("b", 200);
    root.init("c", 400);
  });

  std::vector<Trace*> traces;
  profiler.traces(Trace::Order::SELF_BYTES, false, &traces);
  EXPECT_EQ(4, traces.size());
  EXPECT_EQ("Root.c", traces[0]->path()[0].repr());
  EXPECT_EQ(1600, traces[0]->stats().self_bytes());
  EXPECT_EQ("Root.b", traces[1]->path()[0].repr());
  EXPECT_EQ(800, traces[1]->stats().self_bytes());
  EXPECT_EQ("Root.a", traces[2]->path()[0].repr());
  EXPECT_EQ(400, traces[2]->stats().self_bytes());
}

TEST(prof, singleton_primitive_list) {
  Profiler profiler;
  profiler.parse_schema("tests/res/test.capnp");

  profile_struct(profiler, "AllPrimitiveLists", [](DynamicStruct::Builder &root) {
    root.init("bools", 4);
    root.init("int8s", 4);
    root.init("int16s", 4);
    root.init("int32s", 4);
    root.init("int64s", 4);
    root.init("uint8s", 4);
    root.init("uint16s", 4);
    root.init("uint32s", 4);
    root.init("uint64s", 4);
    root.init("float32s", 4);
    root.init("float64s", 4);
    root.init("enums", 4);
  });

  std::vector<Trace*> traces;
  profiler.traces(Trace::Order::SELF_DATA_BYTES, true, &traces);
  EXPECT_EQ(13, traces.size());
  EXPECT_EQ(0, traces[0]->stats().self_data_bytes());
  EXPECT_EQ(8 * 12, traces[0]->stats().self_pointer_bytes());
  EXPECT_EQ(8, traces[1]->stats().self_data_bytes());
  EXPECT_EQ(8, traces[2]->stats().self_data_bytes());
  EXPECT_EQ(8, traces[3]->stats().self_data_bytes());
  EXPECT_EQ(8, traces[4]->stats().self_data_bytes());
  EXPECT_EQ(8, traces[5]->stats().self_data_bytes());
  EXPECT_EQ(8, traces[6]->stats().self_data_bytes());
  EXPECT_EQ(16, traces[7]->stats().self_data_bytes());
  EXPECT_EQ(16, traces[8]->stats().self_data_bytes());
  EXPECT_EQ(16, traces[9]->stats().self_data_bytes());
  EXPECT_EQ(32, traces[10]->stats().self_data_bytes());
  EXPECT_EQ(32, traces[11]->stats().self_data_bytes());
  EXPECT_EQ(32, traces[12]->stats().self_data_bytes());
}

TEST(prof, long_primitive_list) {
  Profiler profiler;
  profiler.parse_schema("tests/res/test.capnp");

  profile_struct(profiler, "AllPrimitiveLists", [](DynamicStruct::Builder &root) {
    root.init("bools", 64);
    root.init("int8s", 64);
    root.init("int16s", 64);
    root.init("int32s", 64);
    root.init("int64s", 64);
    root.init("uint8s", 64);
    root.init("uint16s", 64);
    root.init("uint32s", 64);
    root.init("uint64s", 64);
    root.init("float32s", 64);
    root.init("float64s", 64);
    root.init("enums", 64);
  });

  std::vector<Trace*> traces;
  profiler.traces(Trace::Order::SELF_DATA_BYTES, true, &traces);
  EXPECT_EQ(13, traces.size());
  EXPECT_EQ(0, traces[0]->stats().self_data_bytes());
  EXPECT_EQ(8 * 12, traces[0]->stats().self_pointer_bytes());
  EXPECT_EQ(8, traces[1]->stats().self_data_bytes());
  EXPECT_EQ(64, traces[2]->stats().self_data_bytes());
  EXPECT_EQ(64, traces[3]->stats().self_data_bytes());
  EXPECT_EQ(128, traces[4]->stats().self_data_bytes());
  EXPECT_EQ(128, traces[5]->stats().self_data_bytes());
  EXPECT_EQ(128, traces[6]->stats().self_data_bytes());
  EXPECT_EQ(256, traces[7]->stats().self_data_bytes());
  EXPECT_EQ(256, traces[8]->stats().self_data_bytes());
  EXPECT_EQ(256, traces[9]->stats().self_data_bytes());
  EXPECT_EQ(512, traces[10]->stats().self_data_bytes());
  EXPECT_EQ(512, traces[11]->stats().self_data_bytes());
  EXPECT_EQ(512, traces[12]->stats().self_data_bytes());
}


TEST(prof, point_list) {
  Profiler profiler;
  profiler.parse_schema("tests/res/test.capnp");

  profile_struct(profiler, "PointList", [](DynamicStruct::Builder &root) {
    root.init("points", 3).as<DynamicList>();
  });

  std::vector<Trace*> traces;
  profiler.traces(Trace::Order::SELF_BYTES, false, &traces);
  EXPECT_EQ(3, traces.size());
  EXPECT_EQ(48, traces[0]->stats().self_bytes());
}

TEST(prof, linked_list) {
  Profiler profiler;
  profiler.parse_schema("tests/res/test.capnp");
  profiler.set_trace_depth(2);

  profile_struct(profiler, "Link", [](DynamicStruct::Builder &root) {
    DynamicStruct::Builder current = root;
    for (uint32_t i = 0; i < 16; i++)
      current = current.init("next").as<DynamicStruct>();
  });

  std::vector<Trace*> traces;
  profiler.traces(Trace::Order::SELF_BYTES, false, &traces);
  EXPECT_EQ(3, traces.size());
  EXPECT_EQ(240, traces[0]->stats().self_bytes());
}

TEST(prof, zipped) {
  Profiler profiler;
  profiler.parse_schema("tests/res/test.capnp");

  profile_struct_zipped(profiler, "IntLists",
      zipprof::Compressor::zlib_best_compression(), [](DynamicStruct::Builder &root) {
    const uint32_t kCount = 128;
    // All 0s
    root.init("a", kCount);
    // Increasing values
    DynamicList::Builder bs = root.init("b", kCount).as<DynamicList>();
    for (uint32_t i = 0; i < kCount; i++)
      bs.set(i, i);
    // Arithmetic
    DynamicList::Builder cs = root.init("c", kCount).as<DynamicList>();
    for (uint32_t i = 0; i < kCount; i++)
      cs.set(i, (i + 7) * 3);
    // Random
    DynamicList::Builder ds = root.init("d", kCount).as<DynamicList>();
    for (uint32_t i = 0; i < kCount; i++)
      ds.set(i, std::rand());
  });

  profiler.dump(Trace::Order::ACCUM_WEIGHT);

}
