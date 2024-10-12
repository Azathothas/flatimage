///
// @author      : Ruan E. Formigoni (ruanformigoni@gmail.com)
// @file        : filesystems
///

#pragma once

#include <memory>
#include <fcntl.h>

#include "../cpp/lib/overlayfs.hpp"
#include "../cpp/lib/squashfs.hpp"
#include "../cpp/lib/dwarfs.hpp"
#include "../cpp/lib/ciopfs.hpp"

#include "config/config.hpp"

namespace ns_filesystems
{

// class Filesystems {{{
class Filesystems
{
  private:
    fs::path m_path_dir_mount;
    std::vector<fs::path> m_vec_path_dir_mountpoints;
    std::vector<std::unique_ptr<ns_dwarfs::Dwarfs>> m_layers;
    std::unique_ptr<ns_ciopfs::Ciopfs> m_ciopfs;
    std::unique_ptr<ns_overlayfs::Overlayfs> m_overlayfs;
    std::optional<pid_t> m_opt_pid_janitor;
    uint64_t mount_dwarfs(fs::path const& path_dir_mount, fs::path const& path_file_binary, uint64_t offset);
    void mount_ciopfs(fs::path const& path_dir_lower, fs::path const& path_dir_upper);
    void mount_overlayfs(fs::path const& path_dir_layers
      , fs::path const& path_dir_data
      , fs::path const& path_dir_mount
    );
    // In case the parent process fails to clean the mountpoints, this child does it
    void spawn_janitor();

  public:
    Filesystems(ns_config::FlatimageConfig const& config);
    ~Filesystems();
    Filesystems(Filesystems const&) = delete;
    Filesystems(Filesystems&&) = delete;
    Filesystems& operator=(Filesystems const&) = delete;
    Filesystems& operator=(Filesystems&&) = delete;
}; // class Filesystems }}}

// fn: Filesystems::Filesystems {{{
inline Filesystems::Filesystems(ns_config::FlatimageConfig const& config)
  : m_path_dir_mount(config.path_dir_mount)
{
  // Mount compressed layers
  uint64_t index_fs = mount_dwarfs(config.path_dir_mount_layers, config.path_file_binary, config.offset_filesystem);
  // Check if should mount ciopfs
  if ( ns_env::exists("FIM_CASEFOLD", "1") )
  {
    mount_ciopfs(config.path_dir_mount_layers / std::to_string(index_fs-1)
      , config.path_dir_mount_layers / std::to_string(index_fs)
    );
    ns_log::debug()("ciopfs is enabled");
  } // if
  // Mount overlayfs
  mount_overlayfs(config.path_dir_mount_layers, config.path_dir_data_overlayfs, config.path_dir_mount_overlayfs);
  // Spawn janitor
  spawn_janitor();
} // fn Filesystems::Filesystems }}}

// fn: Filesystems::Filesystems {{{
inline Filesystems::~Filesystems()
{
  if ( m_opt_pid_janitor and *m_opt_pid_janitor > 0)
  {
    // Stop janitor loop & wait for cleanup
    kill(*m_opt_pid_janitor, SIGTERM);
    // Wait for janitor to finish execution
    int status;
    waitpid(*m_opt_pid_janitor, &status, 0);
    ereturn_if(not WIFEXITED(status), "Janitor exited abnormally");
    int code = WEXITSTATUS(status);
    ereturn_if(code != 0, "Janitor exited with code '{}'"_fmt(code));
  } // if
  else
  {
    ns_log::error()("Janitor is not running");
  } // else
} // fn Filesystems::Filesystems }}}

// fn: spawn_janitor {{{
inline void Filesystems::spawn_janitor()
{
  // Find janitor binary
  fs::path path_file_janitor = fs::path{ns_env::get_or_throw("FIM_DIR_APP_BIN")} / "janitor";

  // Fork and execve into the janitor process
  pid_t pid_parent = getpid();
  m_opt_pid_janitor = fork();
  ethrow_if(m_opt_pid_janitor < 0, "Failed to fork janitor");

  // Is parent
  dreturn_if(m_opt_pid_janitor > 0, "Spawned janitor with PID '{}'"_fmt(*m_opt_pid_janitor));

  // Redirect stdout/stderr to a log file
  fs::path path_stdout = std::string{ns_env::get_or_throw("FIM_DIR_MOUNT")} + ".janitor.stdout.log";
  fs::path path_stderr = std::string{ns_env::get_or_throw("FIM_DIR_MOUNT")} + ".janitor.stderr.log";
  int fd_stdout = open(path_stdout.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
  int fd_stderr = open(path_stderr.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
  eabort_if(fd_stdout < 0, "Failed to open stdout janitor file");
  eabort_if(fd_stderr < 0, "Failed to open stderr janitor file");
  dup2(fd_stdout, STDOUT_FILENO);
  dup2(fd_stderr, STDERR_FILENO);
  close(fd_stdout);
  close(fd_stderr);
  close(STDIN_FILENO);

  // Keep parent pid in a variable
  ns_env::set("PID_PARENT", pid_parent, ns_env::Replace::Y);

  // Create args to janitor
  std::vector<std::string> vec_argv_custom;
  vec_argv_custom.push_back(path_file_janitor);
  std::copy(m_vec_path_dir_mountpoints.rbegin(), m_vec_path_dir_mountpoints.rend(), std::back_inserter(vec_argv_custom));
  auto argv_custom = std::make_unique<const char*[]>(vec_argv_custom.size() + 1);
  argv_custom[vec_argv_custom.size()] = nullptr;
  std::ranges::transform(vec_argv_custom, argv_custom.get(), [](auto&& e) { return e.c_str(); });

  // Execve to janitor
  execve(path_file_janitor.c_str(), (char**) argv_custom.get(), environ);

  // Exit process in case of an error
  std::abort();
} // fn: spawn_janitor }}}

// fn: mount_dwarfs {{{
inline uint64_t Filesystems::mount_dwarfs(fs::path const& path_dir_mount, fs::path const& path_file_binary, uint64_t offset)
{
  // Open the main binary
  std::ifstream file_binary(path_file_binary, std::ios::binary);

  // Filesystem index
  uint64_t index_fs{};

  // Advance offset
  file_binary.seekg(offset);

  while (true)
  {
    // Read filesystem size
    int64_t size_fs;
    dbreak_if(not file_binary.read(reinterpret_cast<char*>(&size_fs), sizeof(size_fs)), "Stopped reading at index {}"_fmt(index_fs));
    ns_log::debug()("Filesystem size is '{}'", size_fs);
    offset += 8;

    // Check if filesystem is of type 'DWARFS'
    ebreak_if(not ns_dwarfs::is_dwarfs(path_file_binary, offset), "Invalid dwarfs filesystem appended on the image");

    // Create mountpoint
    fs::path path_dir_mount_index = path_dir_mount / std::to_string(index_fs);
    std::error_code ec;
    fs::create_directories(path_dir_mount_index, ec);
    ebreak_if(ec, "Could not create directories: {}"_fmt(ec.message()));

    // Mount filesystem
    ns_log::debug()("Offset to filesystem is '{}'", offset);
    this->m_layers.emplace_back(std::make_unique<ns_dwarfs::Dwarfs>(path_file_binary
      , path_dir_mount_index
      , offset
      , size_fs
      , getpid()
    ));

    // Include in mountpoints vector
    m_vec_path_dir_mountpoints.push_back(path_dir_mount_index);

    // Go to next filesystem if exists
    index_fs += 1;
    offset += size_fs;
    file_binary.seekg(offset);
  } // while

  return index_fs;
} // fn: mount_dwarfs }}}

// fn: mount_overlayfs {{{
inline void Filesystems::mount_overlayfs(fs::path const& path_dir_layers
  , fs::path const& path_dir_data
  , fs::path const& path_dir_mount)
{
  m_overlayfs = std::make_unique<ns_overlayfs::Overlayfs>(path_dir_layers
    , path_dir_data
    , path_dir_mount
    , getpid()
  );
  m_vec_path_dir_mountpoints.push_back(path_dir_mount);
} // fn: mount_overlayfs }}}

// fn: mount_ciopfs {{{
inline void Filesystems::mount_ciopfs(fs::path const& path_dir_lower, fs::path const& path_dir_upper)
{
  this->m_ciopfs = std::make_unique<ns_ciopfs::Ciopfs>(path_dir_lower, path_dir_upper);
  m_vec_path_dir_mountpoints.push_back(path_dir_upper);
} // fn: mount_ciopfs }}}

} // namespace ns_filesystems

/* vim: set expandtab fdm=marker ts=2 sw=2 tw=100 et :*/