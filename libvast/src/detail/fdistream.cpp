//    _   _____   __________
//   | | / / _ | / __/_  __/     Visibility
//   | |/ / __ |_\ \  / /          Across
//   |___/_/ |_/___/ /_/       Space and Time
//
// SPDX-FileCopyrightText: (c) 2016 The VAST Contributors
// SPDX-License-Identifier: BSD-3-Clause

#include "vast/detail/fdistream.hpp"

namespace vast {
namespace detail {

fdistream::fdistream(int fd, size_t buffer_size)
  : std::istream{nullptr},
    buf_{fd, buffer_size} {
  rdbuf(&buf_);
}

} // namespace detail
} // namespace vast
