@0xb746f9af8c151183;

struct Root {
  a @0 :List(UInt32);
  b @1 :List(UInt32);
  c @2 :List(UInt32);
}

enum Abc {
  a @0;
  b @1;
  c @2;
}

struct AllPrimitiveLists {
  bools @0 :List(Bool);
  int8s @1 :List(Int8);
  int16s @2 :List(Int16);
  int32s @3 :List(Int32);
  int64s @4 :List(Int64);
  uint8s @5 :List(UInt8);
  uint16s @6 :List(UInt16);
  uint32s @7 :List(UInt32);
  uint64s @8 :List(UInt64);
  float32s @9 :List(Float32);
  float64s @10 :List(Float64);
  enums @11 :List(Abc);
}

struct Point {
  x @0 :Float64;
  y @1 :Float64;
}

struct PointList {
  points @0 :List(Point);
}

struct Link {
  value @0 :UInt32;
  next @1 :Link;
}

struct IntLists {
  a @0 :List(UInt32);
  b @1 :List(UInt32);
  c @2 :List(UInt32);
  d @3 :List(UInt32);
}
