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

#include "vast/detail/sigma.hpp"

#include "vast/concept/parseable/core.hpp"
#include "vast/concept/parseable/string.hpp"
#include "vast/detail/base64.hpp"
#include "vast/detail/string.hpp"
#include "vast/error.hpp"
#include "vast/expression_visitors.hpp"

#include <map>
#include <regex>
#include <string>
#include <tuple>
#include <vector>

namespace vast::detail::sigma {

// TODO: A lot of code in here is directly copied from
// src/concept/parseable/expression.cpp. We should factor the implementation in
// the future.

namespace {

using expression_map = std::map<std::string, expression>;

/// A symbol-table-like parser for Sigma search identifers. In addition to the
/// exact match as in a symbol table, this parser also performs the additional
/// syntax "1/all of X" where X can be "them", a search identifier, or a
/// wildcard pattern. This parsers is effective a predicate operand in the
/// "condition" field of the "detection" attribute.
struct search_id_symbol_table : parser<search_id_symbol_table> {
  using attribute = expression;

  enum class quantifier { all, any };

  explicit search_id_symbol_table(const expression_map& exprs) {
    id.symbols.reserve(exprs.size());
    for (auto& [key, value] : exprs)
      id.symbols.emplace(key, value);
  }

  template <class Connective>
  static expression join(std::vector<expression> xs) {
    Connective result;
    result.reserve(xs.size());
    std::move(xs.begin(), xs.end(), std::back_insert_iterator(result));
    return expression{std::move(result)};
  }

  // Forces a conjunction or disjunction on a given expression.
  template <quantifier Quantifier>
  static expression force(expression x) {
    if constexpr (Quantifier == quantifier::all) {
      if (auto xs = caf::get_if<disjunction>(&x)) {
        conjunction result;
        result.reserve(xs->size());
        std::move(xs->begin(), xs->end(), std::back_insert_iterator(result));
        return expression{std::move(result)};
      }
    } else {
      if (auto xs = caf::get_if<conjunction>(&x)) {
        disjunction result;
        result.reserve(xs->size());
        std::move(xs->begin(), xs->end(), std::back_insert_iterator(result));
        return expression{std::move(result)};
      }
    }
    return x;
  }

  template <class Iterator, class Attribute>
  bool parse(Iterator& f, const Iterator& l, Attribute& result) const {
    using namespace parser_literals;
    auto select_all = [this] {
      std::vector<expression> result;
      result.reserve(id.symbols.size());
      for (auto& x : id.symbols)
        result.push_back(x.second);
      return result;
    };
    // Performs *-wildcard search on all search identifiers.
    auto search = [this](std::string str) {
      str = std::regex_replace(str, std::regex("\\*"), ".*");
      auto rx = std::regex{str};
      disjunction result;
      for (auto& [sym, expr] : id.symbols)
        if (std::regex_search(sym.begin(), sym.end(), rx))
          result.push_back(expr);
      return expression{std::move(result)};
    };
    // clang-format off
    auto ws = ignore(*parsers::space);
    auto them
      = "them"_p ->* select_all
      ;
    auto pattern
      = (+parsers::any - parsers::space) ->* search
      ;
    auto expr
      = "all of"_p >> ws >> them ->* join<conjunction>
      | "1 of"_p >> ws >> them ->* join<disjunction>
      | "all of"_p >> ws >> id ->* force<quantifier::all>
      | "any of"_p >> ws >> id ->* force<quantifier::any>
      | id
      | pattern
      ;
    // clang-format on
    return expr(f, l, result);
  }

  symbol_table<expression> id;
};

/// Parses the "detection" attribute from a Sigma rule. See the Sigma wiki for
/// details: https://github.com/Neo23x0/sigma/wiki/Specification#detection
struct detection_parser : parser<detection_parser> {
  using attribute = expression;

  explicit detection_parser(const expression_map& exprs) : search_id{exprs} {
  }

  static expression to_expr(
    std::tuple<expression, std::vector<std::tuple<bool_operator, expression>>>
      expr) {
    auto& [x, xs] = expr;
    if (xs.empty())
      return x;
    // We split the expression chain at each OR node in order to take care of
    // operator precedance: AND binds stronger than OR.
    disjunction dis;
    auto con = conjunction{x};
    for (auto& [op, expr] : xs)
      if (op == bool_operator::logical_and) {
        con.emplace_back(std::move(expr));
      } else if (op == bool_operator::logical_or) {
        VAST_ASSERT(!con.empty());
        if (con.size() == 1)
          dis.emplace_back(std::move(con[0]));
        else
          dis.emplace_back(std::move(con));
        con = conjunction{std::move(expr)};
      } else {
        VAST_ASSERT(!"negations must not exist here");
      }
    if (con.size() == 1)
      dis.emplace_back(std::move(con[0]));
    else
      dis.emplace_back(std::move(con));
    return dis.size() == 1 ? std::move(dis[0]) : expression{dis};
  };

  template <class Iterator>
  bool parse(Iterator& f, const Iterator& l, expression& result) const {
    using namespace parser_literals;
    auto ws = ignore(*parsers::space);
    auto negate = [](expression x) { return negation{std::move(x)}; };
    rule<Iterator, expression> expr;
    rule<Iterator, expression> group;
    // clang-format off
    group
      = '(' >> ws >> ref(expr) >> ws >> ')'
      | "not"_p >> ws >> search_id ->* negate
      | "not"_p >> ws >> '(' >> ws >> (ref(expr) ->* negate) >> ws >> ')'
      | search_id
      ;
    auto and_or
      = "or"_p  ->* [] { return bool_operator::logical_or; }
      | "and"_p  ->* [] { return bool_operator::logical_and; }
      ;
    expr
      = (group >> *(ws >> and_or >> ws >> ref(group)) >> ws) ->* to_expr
      ;
    // clang-format on
    auto p = expr >> parsers::eoi;
    return p(f, l, result);
  }

  search_id_symbol_table search_id;
};

caf::expected<expression> parse_search_id(const data& x) {
  if (auto xs = caf::get_if<record>(&x)) {
    conjunction result;
    for (auto& [key, rhs] : *xs) {
      auto keys = split(key, "|");
      auto extractor = field_extractor{std::string{keys[0]}};
      auto op = relational_operator::equal;
      auto all = false;
      auto re = false;
      // Value factory that takes into account whether the `re` modifier is
      // present.
      auto make = [&](const data& value) {
        // The following invariants apply according to the Sigma spec:
        // - All values are treated as case-insensitive strings
        // - You can use wildcard characters '*' and '?' in strings
        // - Wildcards can be escaped with \, e.g. \*. If some wildcard after a
        //   backslash should be searched, the backslash has to be escaped: \\*.
        // - Regular expressions are case-sensitive by default
        // - You don't have to escape characters except the string quotation
        //   marks '
        if (re)
          if (auto str = caf::get_if<std::string>(&value))
            return data{pattern{*str}};
        // TODO: figure out a solution for case-insensitve search
        // TODO: apply the wildcard behavior above.
        return value;
      };
      // Parse modifiers.
      for (auto i = keys.begin() + 1; i != keys.end(); ++i) {
        if (*i == "all") {
          all = true;
        } else if (*i == "contains") {
          op = relational_operator::ni;
        } else if (*i == "endswith" || *i == "startswith") {
          // Once we have regex support we should transform a lot of these
          // modifier in pattern qualifiers, e.g., `endswith` for a value X
          // should become /X$/.
          op = relational_operator::ni;
        } else if (*i == "base64") {
          // TODO
        } else if (*i == "base64offset") {
          // TODO
        } else if (*i == "utf16le" || *i == "wide") {
          // TODO
        } else if (*i == "utf16be") {
          // TODO
        } else if (*i == "utf16") {
          // TODO
        } else if (*i == "re") {
          re = true;
          op = relational_operator::match;
        }
      }
      // Parse RHS.
      if (caf::holds_alternative<record>(rhs))
        return caf::make_error(ec::type_clash, "nested maps not allowed");
      if (auto values = caf::get_if<list>(&rhs)) {
        std::vector<expression> connective;
        for (auto& value : *values) {
          if (caf::holds_alternative<list>(value))
            return caf::make_error(ec::type_clash, "nested lists disallowed");
          if (caf::holds_alternative<record>(value))
            return caf::make_error(ec::type_clash, "nested records disallowed");
          connective.emplace_back(predicate{extractor, op, make(value)});
        }
        auto expr = all ? expression{conjunction(std::move(connective))}
                        : expression{disjunction(std::move(connective))};
        result.emplace_back(std::move(expr));
      } else {
        result.emplace_back(predicate{std::move(extractor), op, make(rhs)});
      }
    }
    return result;
  } else if (auto xs = caf::get_if<list>(&x)) {
    disjunction result;
    for (auto& search_id : *xs)
      if (auto expr = parse_search_id(search_id))
        result.push_back(std::move(*expr));
      else
        return expr.error();
    return result;
  } else {
    return caf::make_error(ec::type_clash, "search id not a list or record");
  }
}

} // namespace

caf::expected<expression> parse(const data& rule) {
  auto xs = caf::get_if<record>(&rule);
  if (!xs)
    return caf::make_error(ec::type_clash, "rule must be a record");
  // Extract detection attribute.
  const record* detection;
  if (auto i = xs->find("detection"); i == xs->end())
    return caf::make_error(ec::invalid_query, "no detection attribute");
  else
    detection = caf::get_if<record>(&i->second);
  if (!detection)
    return caf::make_error(ec::type_clash, "detection not a record");
  // Resolve all named sub-expression except for "condition".
  expression_map exprs;
  for (auto& [key, value] : *detection) {
    if (key == "condition")
      continue;
    if (auto expr = parse_search_id(value))
      exprs[key] = std::move(*expr);
    else
      return expr.error();
  }
  // Extract condition.
  const std::string* condition;
  if (auto i = detection->find("condition"); i == detection->end())
    return caf::make_error(ec::invalid_query, "no condition key");
  else
    condition = caf::get_if<std::string>(&i->second);
  if (!condition)
    return caf::make_error(ec::type_clash, "condition not a string");
  // Parse condition.
  expression result;
  detection_parser p{exprs};
  if (!p(*condition, result))
    return caf::make_error(ec::parse_error, "invalid condition syntax");
  return result;
}

} // namespace vast::detail::sigma
