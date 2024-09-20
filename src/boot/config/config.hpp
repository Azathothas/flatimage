///
// @author      : Ruan E. Formigoni (ruanformigoni@gmail.com)
// @file        : config
///

#pragma once

#include <unistd.h>
#include <filesystem>

#include "../../cpp/lib/env.hpp"

namespace ns_config
{

namespace
{

namespace fs = std::filesystem;

} // namespace

// struct FlatimageConfig {{{
struct FlatimageConfig
{
  std::string str_dist;
  bool is_root;
  bool is_readonly;
  bool is_debug;

  uint64_t offset_filesystem;
  fs::path path_dir_global;
  fs::path path_dir_mount;
  fs::path path_dir_app;
  fs::path path_dir_app_bin;
  fs::path path_dir_instance;
  fs::path path_file_binary;
  fs::path path_dir_binary;
  fs::path path_file_bashrc;
  fs::path path_file_bash;
  fs::path path_dir_mount_layers;
  fs::path path_dir_runtime;
  fs::path path_dir_runtime_host;
  fs::path path_dir_host_home;
  fs::path path_dir_host_config;
  fs::path path_dir_data_overlayfs;
  fs::path path_dir_mount_overlayfs;

  fs::path path_dir_static;
  fs::path path_file_config_boot;
  fs::path path_file_config_environment;
  fs::path path_file_config_permissions;
  fs::path path_file_config_desktop;
  fs::path path_file_config_bindings;
  fs::path path_file_config_casefold;
  fs::path path_dir_layers;

  uint32_t layer_compression_level;
  uint32_t ext2_slack_minimum;

  std::string env_path;
}; // }}}

// config() {{{
inline FlatimageConfig config()
{
  FlatimageConfig config;

  ns_env::set("PID", std::to_string(getpid()), ns_env::Replace::Y);
  ns_env::set("FIM_DIST", "TRUNK", ns_env::Replace::Y);

  // Distribution
  config.str_dist = "TRUNK";

  // Flags
  config.is_root = ns_env::exists("FIM_ROOT", "1");
  config.is_readonly = ns_env::exists("FIM_RO", "1");
  config.is_debug = ns_env::exists("FIM_DEBUG", "1");

  // Paths in /tmp
  config.offset_filesystem        = std::stoll(ns_env::get_or_throw("FIM_OFFSET"));
  config.path_dir_global          = ns_env::get_or_throw("FIM_DIR_GLOBAL");
  config.path_file_binary         = ns_env::get_or_throw("FIM_FILE_BINARY");
  config.path_dir_binary          = config.path_file_binary.parent_path();
  config.path_dir_app             = ns_env::get_or_throw("FIM_DIR_APP");
  config.path_dir_app_bin         = ns_env::get_or_throw("FIM_DIR_APP_BIN");
  config.path_dir_instance        = ns_env::get_or_throw("FIM_DIR_INSTANCE");
  config.path_dir_mount           = ns_env::get_or_throw("FIM_DIR_MOUNT");
  config.path_file_bashrc         = config.path_dir_app / ".bashrc";
  config.path_file_bash           = config.path_dir_app_bin / "bash";
  config.path_dir_mount_layers    = config.path_dir_mount / "layers";
  config.path_dir_mount_overlayfs = config.path_dir_mount / "overlayfs";

  // Paths inside the ext2 filesystem
  config.path_dir_static              = config.path_dir_mount_overlayfs / "fim/static";
  config.path_file_config_boot        = config.path_dir_mount_overlayfs / "fim/config/boot.json";
  config.path_file_config_environment = config.path_dir_mount_overlayfs / "fim/config/environment.json";
  config.path_file_config_permissions = config.path_dir_mount_overlayfs / "fim/config/permissions.json";
  config.path_file_config_desktop     = config.path_dir_mount_overlayfs / "fim/config/desktop.json";
  config.path_file_config_bindings    = config.path_dir_mount_overlayfs / "fim/config/bindings.json";
  config.path_file_config_casefold    = config.path_dir_mount_overlayfs / "fim/config/casefold.json";
  config.path_dir_layers              = config.path_dir_mount_overlayfs / "fim/layers";

  // Paths only available inside the container (runtime)
  config.path_dir_runtime = "/tmp/fim/run";
  config.path_dir_runtime_host = config.path_dir_runtime / "host";
  ns_env::set("FIM_DIR_RUNTIME", config.path_dir_runtime, ns_env::Replace::Y);
  ns_env::set("FIM_DIR_RUNTIME_HOST", config.path_dir_runtime_host, ns_env::Replace::Y);

  // Home directory
  config.path_dir_host_home = fs::path{ns_env::get_or_throw("HOME")}.relative_path();

  // Create host config directory
  config.path_dir_host_config = config.path_file_binary.parent_path() / ".{}.config"_fmt(config.path_file_binary.filename());
  ethrow_if(not fs::exists(config.path_dir_host_config) and not fs::create_directories(config.path_dir_host_config)
    , "Could not create configuration directory in '{}'"_fmt(config.path_dir_host_config)
  );
  ns_env::set("FIM_DIR_CONFIG", config.path_dir_host_config, ns_env::Replace::Y);

  // Overlayfs write data to remain on the host
  config.path_dir_data_overlayfs = config.path_dir_host_config / "overlays";

  // Environment
  config.env_path = config.path_dir_app_bin.string() + ":" + ns_env::get_or_throw("PATH");
  config.env_path += ":/sbin:/usr/sbin:/usr/local/sbin:/bin:/usr/bin:/usr/local/bin";
  ns_env::set("PATH", config.env_path, ns_env::Replace::Y);

  // Filesystem configuration
  config.layer_compression_level  = std::stoi(ns_env::get_or_else("FIM_COMPRESSION_LEVEL", "15"));
  config.ext2_slack_minimum       = std::stoi(ns_env::get_or_else("FIM_SLACK_MINIMUM", "20"));

  // PID
  ns_env::set("FIM_PID", getpid(), ns_env::Replace::Y);

  // LD_LIBRARY_PATH
  if ( ns_env::exists("LD_LIBRARY_PATH") )
  {
    ns_env::prepend("LD_LIBRARY_PATH", "/usr/lib/x86_64-linux-gnu:/usr/lib/i386-linux-gnu:");
  }
  else
  {
    ns_env::set("LD_LIBRARY_PATH", "/usr/lib/x86_64-linux-gnu:/usr/lib/i386-linux-gnu", ns_env::Replace::Y);
  } // else

  return config;
} // config() }}}

} // namespace ns_config


/* vim: set expandtab fdm=marker ts=2 sw=2 tw=100 et :*/
