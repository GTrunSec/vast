#ifndef VAST_DETAIL_BITMAP_INDEX_PORT_H
#define VAST_DETAIL_BITMAP_INDEX_PORT_H

#include <ze/value.h>
#include "vast/bitmap.h"
#include "vast/bitmap_index.h"
#include "vast/exception.h"
#include "vast/to_string.h"

namespace vast {
namespace detail {

/// A bitmap index for transport-layer ports.
template <typename Bitstream>
class port_bitmap_index : public bitmap_index<Bitstream>
{
  typedef bitmap_index<Bitstream> super;
  typedef std::underlying_type<ze::port::port_type>::type proto_type;

public:
  virtual bool patch(size_t n) override
  {
    auto success = num_.patch(n);
    return proto_.patch(n) && success;
  }

  virtual option<Bitstream>
  lookup(relational_operator op, ze::value const& value) const override
  {
    if (op == in || op == not_in)
      throw error::operation("unsupported relational operator", op);
    if (num_.empty())
      return {};
    auto& port = value.get<ze::port>();
    auto nbs = num_.lookup(op, port.number());
    if (! nbs)
      return {};
    if (port.type() != ze::port::unknown)
      if (auto tbs = num_[port.type()])
          *nbs &= *tbs;
    return nbs;
  };

  virtual std::string to_string() const override
  {
    return vast::to_string(num_);
  }

private:
  virtual bool push_back_impl(ze::value const& value) override
  {
    auto& port = value.get<ze::port>();
    num_.push_back(port.number());
    proto_.push_back(static_cast<proto_type>(port.type()));
    return true;
  }

  bitmap<uint16_t, Bitstream, range_encoder> num_;
  bitmap<proto_type, Bitstream> proto_;
};

} // namespace detail
} // namespace vast

#endif

