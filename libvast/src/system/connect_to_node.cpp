//    _   _____   __________
//   | | / / _ | / __/_  __/     Visibility
//   | |/ / __ |_\ \  / /          Across
//   |___/_/ |_/___/ /_/       Space and Time
//
// SPDX-FileCopyrightText: (c) 2018 The VAST Contributors
// SPDX-License-Identifier: BSD-3-Clause

#include "vast/system/connect_to_node.hpp"

#include "vast/fwd.hpp"

#include "vast/command.hpp"
#include "vast/concept/parseable/vast/endpoint.hpp"
#include "vast/concept/printable/to_string.hpp"
#include "vast/concept/printable/vast/port.hpp"
#include "vast/config.hpp"
#include "vast/data.hpp"
#include "vast/defaults.hpp"
#include "vast/endpoint.hpp"
#include "vast/error.hpp"
#include "vast/logger.hpp"
#include "vast/system/version_command.hpp"

#include <caf/actor_system.hpp>
#include <caf/actor_system_config.hpp>
#include <caf/io/middleman.hpp>
#include <caf/scoped_actor.hpp>
#include <caf/settings.hpp>

#if VAST_ENABLE_OPENSSL
#  include <caf/openssl/all.hpp>
#endif

using namespace caf;

namespace vast::system {

caf::expected<node_actor>
connect_to_node(scoped_actor& self, const caf::settings& opts) {
  // Fetch values from config.
  auto id = get_or(opts, "vast.node-id", defaults::system::node_id);
  endpoint node_endpoint;
  auto endpoint_str = get_or(opts, "vast.endpoint", defaults::system::endpoint);
  if (!parsers::endpoint(endpoint_str, node_endpoint))
    return caf::make_error(ec::parse_error, "invalid endpoint", endpoint_str);
  // Default to port 42000/tcp if none is set.
  if (!node_endpoint.port)
    node_endpoint.port = port{defaults::system::endpoint_port, port_type::tcp};
  if (node_endpoint.port->type() == port_type::unknown)
    node_endpoint.port->type(port_type::tcp);
  if (node_endpoint.port->type() != port_type::tcp)
    return caf::make_error(ec::invalid_configuration, "invalid protocol",
                           *node_endpoint.port);
  VAST_DEBUG("client connects to remote node with id {}", id);
  auto& sys_cfg = self->system().config();
  auto use_encryption
    = !sys_cfg.openssl_certificate.empty() || !sys_cfg.openssl_key.empty()
      || !sys_cfg.openssl_passphrase.empty() || !sys_cfg.openssl_capath.empty()
      || !sys_cfg.openssl_cafile.empty();
  auto host = node_endpoint.host;
  if (node_endpoint.host.empty())
    node_endpoint.host = "localhost";
  VAST_INFO("client connects to VAST node at {}", endpoint_str);
  auto result = [&]() -> caf::expected<node_actor> {
    if (use_encryption) {
#if VAST_ENABLE_OPENSSL
      return openssl::remote_actor<node_actor>(
        self->system(), node_endpoint.host, node_endpoint.port->number());
#else
      return caf::make_error(ec::unspecified, "not compiled with OpenSSL "
                                              "support");
#endif
    }
    auto& mm = self->system().middleman();
    return mm.remote_actor<node_actor>(node_endpoint.host,
                                       node_endpoint.port->number());
  }();
  if (!result)
    return result.error();
  VAST_DEBUG("client connected to VAST node at {}:{}", node_endpoint.host,
             to_string(*node_endpoint.port));
  self
    ->request(*result, defaults::system::initial_request_timeout, atom::get_v,
              atom::version_v)
    .receive(
      [&](record& remote_version) {
        auto local_version = retrieve_versions();
        if (local_version["VAST"] != remote_version["VAST"]) {
          VAST_WARN("client version {} does not match remote version {}; "
                    "this may caused unexpected behavior",
                    local_version["VAST"], remote_version["VAST"]);
        }
        if (local_version["plugins"] != remote_version["plugins"]) {
          VAST_WARN("client plugins {} do not match remote plugins {}; "
                    "this may caused unexpected behavior",
                    local_version["plugins"], remote_version["plugins"]);
        }
      },
      [&](caf::error& error) {
        result = std::move(error);
      });
  return result;
}

} // namespace vast::system
