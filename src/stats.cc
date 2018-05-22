#include "stats.hh"

using namespace capnprof;

Stats::Stats()
    : self_data_bytes_(0)
    , self_pointer_bytes_(0)
    , child_data_bytes_(0)
    , child_pointer_bytes_(0)
    , self_data_weight_(0)
    , self_pointer_weight_(0)
    , child_data_weight_(0)
    , child_pointer_weight_(0) { }
