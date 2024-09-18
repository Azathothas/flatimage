///
// @author      : Ruan E. Formigoni (ruanformigoni@gmail.com)
// @file        : dwarfs
///

#pragma once

#include <filesystem>
#include "log.hpp"
#include "fuse.hpp"
#include "subprocess.hpp"
#include "../macro.hpp"

namespace ns_dwarfs
{

namespace
{

namespace fs = std::filesystem;

};

// class Dwarfs {{{
class Dwarfs
{
  private:
    std::unique_ptr<ns_subprocess::Subprocess> m_subprocess;
    fs::path m_path_dir_mountpoint;

  public:
    Dwarfs(Dwarfs const&) = delete;
    Dwarfs(Dwarfs&&) = delete;
    Dwarfs& operator=(Dwarfs const&) = delete;
    Dwarfs& operator=(Dwarfs&&) = delete;

    Dwarfs(fs::path const& path_file_image, fs::path const& path_dir_mount)
      : m_path_dir_mountpoint(path_dir_mount)
    {
      // Check if image exists and is a regular file
      ethrow_if(not fs::is_regular_file(path_file_image)
        , "'{}' does not exist or is not a regular file"_fmt(path_file_image)
      );

      // Check if mountpoint exists and is directory
      ethrow_if(not fs::is_directory(path_dir_mount)
        , "'{}' does not exist or is not a directory"_fmt(path_dir_mount)
      );

      // Find command in PATH
      auto opt_path_file_dwarfs = ns_subprocess::search_path("dwarfs");
      ethrow_if(not opt_path_file_dwarfs.has_value(), "Could not find dwarfs");

      // Create command
      m_subprocess = std::make_unique<ns_subprocess::Subprocess>(*opt_path_file_dwarfs);

      // Spawn command
      auto ret = m_subprocess->with_piped_outputs()
        .with_args(path_file_image, path_dir_mount)
        .spawn()
        .wait();
      ereturn_if(not ret, "Mount '{}' exited unexpectedly"_fmt(m_path_dir_mountpoint));
      ereturn_if(ret and *ret != 0, "Mount '{}' exited with non-zero exit code '{}'"_fmt(m_path_dir_mountpoint, *ret));
    } // Dwarfs
    
    ~Dwarfs()
    {
      ns_fuse::unmount(m_path_dir_mountpoint);
    } // Dwarfs

    fs::path const& get_dir_mountpoint()
    {
      return m_path_dir_mountpoint;
    }
}; // class Dwarfs }}}

} // namespace ns_dwarfs

/* vim: set expandtab fdm=marker ts=2 sw=2 tw=100 et :*/
