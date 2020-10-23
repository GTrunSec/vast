/******************************************************************************
 *                    _   _____   __________                                  *
 *                   | | / / _ | / __/_  __/     Visibility                   *
 *                   | |/ / __ |_\ \  / /          Across                     *
 *                   |___/_/ |_/___/ /_/       Space and Time                 *
 *                                                                            *
 * This file is part of VAST. It is subject to the license terms in the       *
 * LICENSE file found in the top-level directory of this distribution and at  *
 * http://vast.io/license. No part of VAST, including this file, may be       *
 * copied, modified, propagated, or distributed except according to the terms *
 * contained in the LICENSE file.                                             *
 ******************************************************************************/

#define SUITE value_index

#include "vast/index/arithmetic_index.hpp"

#include "vast/test/test.hpp"

#include "vast/concept/parseable/to.hpp"
#include "vast/concept/parseable/vast/address.hpp"
#include "vast/concept/parseable/vast/data.hpp"
#include "vast/concept/parseable/vast/subnet.hpp"
#include "vast/concept/parseable/vast/time.hpp"
#include "vast/concept/printable/to_string.hpp"
#include "vast/concept/printable/vast/bitmap.hpp"
#include "vast/load.hpp"
#include "vast/save.hpp"
#include "vast/table_slice.hpp"
#include "vast/value_index_factory.hpp"

#include <caf/test/dsl.hpp>

using namespace vast;
using namespace std::string_literals;

namespace {

struct fixture {
  fixture() {
    factory<value_index>::initialize();
  }
};

} // namespace

FIXTURE_SCOPE(value_index_tests, fixture)

TEST(duration) {
  using namespace std::chrono;
  caf::settings opts;
  opts["base"] = "uniform64(10)";
  // Default binning gives granularity of seconds.
  auto idx = arithmetic_index<vast::duration>{duration_type{}, opts};
  MESSAGE("append");
  REQUIRE(idx.append(make_data_view(milliseconds(1000))));
  REQUIRE(idx.append(make_data_view(milliseconds(2000))));
  REQUIRE(idx.append(make_data_view(milliseconds(3000))));
  REQUIRE(idx.append(make_data_view(milliseconds(911))));
  REQUIRE(idx.append(make_data_view(milliseconds(1011))));
  REQUIRE(idx.append(make_data_view(milliseconds(1411))));
  REQUIRE(idx.append(make_data_view(milliseconds(2222))));
  REQUIRE(idx.append(make_data_view(milliseconds(2322))));
  MESSAGE("lookup");
  auto lookup = [&](relational_operator op, auto dv) {
    return to_string(unbox(idx.lookup(op, dv)));
  };
  auto hun = make_data_view(milliseconds(1034));
  auto twelve = make_data_view(milliseconds(1200));
  auto twokay = make_data_view(milliseconds(2000));
  CHECK_EQUAL(lookup(equal, hun), "10001100");
  CHECK_EQUAL(lookup(less_equal, twokay), "11011111");
  CHECK_EQUAL(lookup(greater, twelve), "11101111");
  CHECK_EQUAL(lookup(greater_equal, twelve), "11101111");
  CHECK_EQUAL(lookup(less, twelve), "10011100");
  CHECK_EQUAL(lookup(less_equal, twelve), "10011100");
}

TEST(time) {
  caf::settings opts;
  opts["base"] = "uniform64(10)";
  arithmetic_index<vast::time> idx{time_type{}, opts};
  auto ts = to<vast::time>("2014-01-16+05:30:15");
  MESSAGE("append");
  REQUIRE(idx.append(make_data_view(unbox(ts))));
  ts = to<vast::time>("2014-01-16+05:30:12");
  REQUIRE(idx.append(make_data_view(unbox(ts))));
  ts = to<vast::time>("2014-01-16+05:30:15");
  REQUIRE(idx.append(make_data_view(unbox(ts))));
  ts = to<vast::time>("2014-01-16+05:30:18");
  REQUIRE(idx.append(make_data_view(unbox(ts))));
  ts = to<vast::time>("2014-01-16+05:30:15");
  REQUIRE(idx.append(make_data_view(unbox(ts))));
  ts = to<vast::time>("2014-01-16+05:30:19");
  REQUIRE(idx.append(make_data_view(unbox(ts))));
  MESSAGE("lookup");
  ts = to<vast::time>("2014-01-16+05:30:15");
  auto fifteen = idx.lookup(equal, make_data_view(unbox(ts)));
  CHECK(to_string(unbox(fifteen)) == "101010");
  ts = to<vast::time>("2014-01-16+05:30:20");
  auto twenty = idx.lookup(less, make_data_view(unbox(ts)));
  CHECK(to_string(unbox(twenty)) == "111111");
  ts = to<vast::time>("2014-01-16+05:30:18");
  auto eighteen = idx.lookup(greater_equal, make_data_view(unbox(ts)));
  CHECK(to_string(unbox(eighteen)) == "000101");
  MESSAGE("serialization");
  std::vector<char> buf;
  CHECK_EQUAL(save(nullptr, buf, idx), caf::none);
  arithmetic_index<vast::time> idx2{time_type{}, opts};
  CHECK_EQUAL(load(nullptr, buf, idx2), caf::none);
  eighteen = idx2.lookup(greater_equal, make_data_view(unbox(ts)));
  CHECK(to_string(*eighteen) == "000101");
}

TEST(none values - arithmetic) {
  auto idx = factory<value_index>::make(count_type{}, caf::settings{});
  REQUIRE_NOT_EQUAL(idx, nullptr);
  REQUIRE(idx->append(make_data_view(caf::none)));
  REQUIRE(idx->append(make_data_view(42)));
  REQUIRE(idx->append(make_data_view(43)));
  REQUIRE(idx->append(make_data_view(caf::none)));
  REQUIRE(idx->append(make_data_view(caf::none)));
  auto bm = idx->lookup(less, make_data_view(50));
  CHECK_EQUAL(to_string(unbox(bm)), "01100");
  bm = idx->lookup(greater, make_data_view(42));
  CHECK_EQUAL(to_string(unbox(bm)), "00100");
}

FIXTURE_SCOPE_END()
