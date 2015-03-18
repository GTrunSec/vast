#include "vast/expression.h"

namespace vast {

expression const& negation::expression() const
{
  assert(! empty());
  return *begin();
}

expression& negation::expression()
{
  assert(! empty());
  return *begin();
}


expression::node& expose(expression& e)
{
  return e.node_;
}

expression::node const& expose(expression const& e)
{
  return e.node_;
}

bool operator==(expression const& lhs, expression const& rhs)
{
  return lhs.node_ == rhs.node_;
}

bool operator<(expression const& lhs, expression const& rhs)
{
  return lhs.node_ < rhs.node_;
}

} // namespace vast
