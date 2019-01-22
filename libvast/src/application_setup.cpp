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

#include "vast/application_setup.hpp"

#include <caf/io/middleman.hpp>
#include <caf/timestamp.hpp>

#include "vast/config.hpp"

#ifdef VAST_USE_OPENSSL
#include <caf/openssl/manager.hpp>
#endif

#include "vast/defaults.hpp"
#include "vast/detail/system.hpp"
#include "vast/error.hpp"
#include "vast/filesystem.hpp"
#include "vast/system/application.hpp"

namespace vast {

path make_log_dirname() {
  auto dir_name = caf::deep_to_string(caf::make_timestamp());
  dir_name += '#';
  dir_name += std::to_string(detail::process_id());
  return path{"log"} / dir_name;
}

config::config(std::string application_name)
  : application_name{std::move(application_name)} {
  // Tweak default logging options.
  set("logger.component-filter", "vast");
  set("logger.console", caf::atom("COLORED"));
  set("logger.file-verbosity", caf::atom("DEBUG"));
  // Allow VAST clusters to form a mesh.
  set("middleman.enable-automatic-connections", true);
  // Load CAF modules.
  load<caf::io::middleman>();
#ifdef VAST_USE_OPENSSL
  load<caf::openssl::manager>();
#endif
}

caf::error config::parse(int argc, char** argv) {
  if (auto err = configuration::parse(argc, argv))
    return err;
  if (!caf::get_if<std::string>(this, "logger.file-name")) {
    path dir = get_or(*this, "vast.dir", defaults::command::directory);
    if (auto log_file = setup_log_file(dir.complete()); !log_file) {
      std::cerr << "failed to setup log file: " << to_string(log_file.error())
                << std::endl;
      return log_file.error();
    } else {
      set("logger.file-name", log_file->str());
    }
  }
  return caf::none;
}

caf::expected<path> config::setup_log_file(const path& base_dir) {
  auto log_dir = base_dir / make_log_dirname();
  // Create the log directory first, which we need to create the symlink
  // afterwards.
  if (!exists(log_dir))
    if (auto res = mkdir(log_dir); !res)
      return res.error();
  // Create user-friendly symlink to current log directory.
  auto link_dir = log_dir.chop(-1) / "current";
  if (exists(link_dir))
    if (!rm(link_dir))
      return make_error(ec::filesystem_error, "cannot remove log symlink");
  create_symlink(log_dir.trim(-1), link_dir);
  return log_dir / application_name + ".log";
}

// Parses the options from the root command and adds them to the global
// configuration.
void config::merge_root_options(system::application& app) {
  // Delegate to the root command for argument parsing.
  caf::settings options;
  app.root.options.parse(options, command_line.begin(), command_line.end());
  // Move everything into the system-wide options, but use "vast" as category
  // instead of the default "global" category.
  auto& src = options["global"].as_dictionary();
  src["vast"].as_dictionary().insert(src.begin(), src.end());
}

} // namespace vast
