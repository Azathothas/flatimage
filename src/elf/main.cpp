//
// @author      : Ruan E. Formigoni (ruanformigoni@gmail.com)
// @file        : main.cpp
// @created     : Monday Jan 23, 2023 21:18:26 -03
//
// @description : Main elf for arts
//

#include <sstream>
#include <algorithm>
#include <numeric>
#include <fstream>
#include <iterator>
#include <filesystem>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <optional>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <elf.h>
#define FMT_HEADER_ONLY
#include <fmt/ranges.h>
#include <memory>

#if defined(__LP64__)
#define ElfW(type) Elf64_ ## type
#else
#define ElfW(type) Elf32_ ## type
#endif

// Unix environment variables
extern char** environ;

// Aliases {{{
namespace fs = std::filesystem;
using u64 = unsigned long;
// }}}

// Literals {{{
auto operator"" _fmt(const char* c_str, std::size_t)
{ return [=](auto&&... args){ return fmt::format(fmt::runtime(c_str), args...); }; }

auto operator"" _err(const char* c_str, std::size_t)
{ return [=](auto&&... args){ fmt::print(fmt::runtime(c_str), args...); exit(1); }; }
// }}}

// fn: create_temp_dir {{{
std::string create_temp_dir(std::string const& prefix)
{
  std::string temp_dir_template = prefix + "XXXXXX";
  auto temp_dir_template_cstr = std::unique_ptr<char[]>(new char[temp_dir_template.size() + 1]);
  std::strcpy(temp_dir_template_cstr.get(), temp_dir_template.c_str());
  char* temp_dir_cstr = mkdtemp(temp_dir_template_cstr.get());
  if (temp_dir_cstr == NULL) { "Failed to create temporary dir {}"_err(temp_dir_template_cstr.get()); }
  return std::string{temp_dir_cstr};
} // function: create_temp_dir }}}

// fn: write_from_offset {{{
void write_from_offset(std::string const& f_in_str, std::string const& f_out_str, std::pair<u64,u64> offset)
{
  std::ifstream f_in{f_in_str, f_in.binary | f_in.in};
  std::ofstream f_ou{f_out_str, f_ou.binary | f_ou.out};

  if ( ! f_in.good() or ! f_ou.good() ) { "Failed to open startup files\n"_err(); }

  f_in.seekg(offset.second);
  auto end = f_in.tellg();
  f_in.seekg(offset.first);

  for(char ch; f_in.tellg() != end;)
  {
    f_in.get(ch);
    f_ou.write(&ch, sizeof(char));
  }

  f_ou.close(); f_in.close();
} // function: write_from_offset

// }}}

// fn: read_elf_header {{{
//  Read the current binary's data. The first part of the file is the
//  executable, and the rest is the rw filesystem, therefore, we need to
//  discover where the filesystem starts (offset) to mount it.

u64 read_elf_header(const char* elfFile, u64 offset = 0) {
  // Either Elf64_Ehdr or Elf32_Ehdr depending on architecture.
  ElfW(Ehdr) header;

  FILE* file = fopen(elfFile, "rb");
  fseek(file, offset, SEEK_SET);
  if(file) {
    // read the header
    fread(&header, sizeof(header), 1, file);
    // check so its really an elf file
    if (std::memcmp(header.e_ident, ELFMAG, SELFMAG) == 0) {
      fclose(file);
      offset = header.e_shoff + (header.e_ehsize * header.e_shnum);
      return offset;
    }
    // finally close the file
    fclose(file);
  }
  "Could not read elf header from {}"_err(elfFile); exit(1);
} // }}}

// fn: main {{{
int main(int argc, char** argv)
{

  // Parse arguments {{{

  // Get arguments from 1..n-1
  std::string str_args;
  std::for_each(argv+1, argv+argc, [&](char* p) { str_args.append(fmt::format("\"{}\" ", p)); });
  // }}}

  // Launch program {{{
  if ( auto str_offset_fs = getenv("ARTS_MAIN_LAUNCH") )
  {
    // This part of the code executes the runner, it mounts the image as a
    // readable/writeable filesystem, and then executes the application
    // contained inside of it.

    //
    // Get base dir
    //
    char* cstr_dir_base = getenv("ARTS_BASE");
    if ( cstr_dir_base == NULL ) { "ARTS_BASE dir variable is empty"_err(); }

    //
    // Get bin dir
    //
    char* cstr_dir_bin = getenv("ARTS_BIN");
    if ( cstr_dir_bin == NULL ) { "ARTS_BIN dir variable is empty"_err(); }

    //
    // Get file path
    //
    char* cstr_dir_file = getenv("ARTS_FILE");
    if ( cstr_dir_bin == NULL ) { "ARTS_FILE file variable is empty"_err(); }

    //
    // Get temp dir
    //
    char* cstr_dir_temp = getenv("ARTS_TEMP");
    if ( cstr_dir_temp == NULL ) { "Could not open tempdir to mount image\n"_err(); }
    std::string str_dir_temp{cstr_dir_temp};
    std::string str_dir_mount{"{}/{}"_fmt(cstr_dir_temp,"mount")};
    fs::create_directory(str_dir_mount);

    //
    // Get offset
    //
    int offset {std::stoi(str_offset_fs)};

    //
    // Extract boot script
    //
    if(system("{}/ext2rd -o{} {} ./arts/boot:{}/boot"_fmt(cstr_dir_bin, offset, cstr_dir_file, cstr_dir_bin).c_str()) != 0)
    {
      "Could not extract arts boot script from {} to {}"_err(cstr_dir_file, cstr_dir_temp);
    }

    //
    // Set environment
    //
    setenv("ARTS_BASE", cstr_dir_base, 1);
    setenv("ARTS_MOUNT", str_dir_mount.c_str(), 1);
    setenv("ARTS_OFFSET", str_offset_fs, 1);
    setenv("ARTS_TEMP", str_dir_temp.c_str(), 1);

    //
    // Execute application
    //
    std::string cmd = "{}/boot {}"_fmt(cstr_dir_bin, str_args);
    system(cmd.c_str());
  } // }}}
  
  else // Write Runner {{{
  {
    // This part of the code is executed to write the runner,
    // rightafter the code is replaced by the runner.
    // This is done because the current executable cannot mount itself.

    //
    // Get path to called executable
    //
    if (!std::filesystem::exists("/proc/self/exe")) {
      "Error retrieving executable path for self"_err();
    }
    auto path_absolute = fs::read_symlink("/proc/self/exe");

    //
    // Create base dir
    //
    std::string str_dir_base;
    if ( char* str_cache_home = getenv("XDG_CACHE_HOME")  )
    {
      str_dir_base = std::string{str_cache_home} + "/arts";
    } // if
    else if ( char* str_home = getenv("HOME") )
    {
      str_dir_base = std::string{str_home} + "/.cache/arts";
    } // else if
    else
    {
      str_dir_base = "/tmp/arts";
    } // else

    if ( ! fs::exists(str_dir_base) && ! fs::create_directories(str_dir_base) )
    {
      "Failed to create directory {}"_err(str_dir_base);
    }

    //
    // Create bin dir
    //
    std::string str_dir_bin = str_dir_base + "/bin/";
    if ( ! fs::exists(str_dir_bin) && ! fs::create_directories(str_dir_bin) )
    {
      "Failed to create directory {}"_err(str_dir_bin);
    }

    //
    // Create temp dir
    //
    std::string str_dir_mounts = str_dir_base + "/mounts/";
    if ( ! fs::exists(str_dir_mounts) && ! fs::create_directories(str_dir_mounts) )
    {
      "Failed to create directory {}"_err(str_dir_mounts);
    }
    std::string str_dir_temp = create_temp_dir(str_dir_base + "/mounts/");

    //
    // Starting offsets
    //
    u64 offset_beg = 0;
    u64 offset_end = read_elf_header(path_absolute.c_str());

    //
    // Write by binary offset
    //
    auto f_write_bin = [&](std::string str_dir, std::string str_bin, u64 offset_end)
    {
      // Update offsets
      offset_beg = offset_end;
      offset_end = read_elf_header(path_absolute.c_str(), offset_beg) + offset_beg;
      // Create output path to write binary into
      std::string str_file = "{}/{}"_fmt(str_dir, str_bin);
      // Write binary only if it doesnt already exist
      if ( ! fs::exists(str_file) )
      {
        write_from_offset(path_absolute.string(), str_file, {offset_beg, offset_end});
      }
      // Set permissions
      fs::permissions(str_file.c_str(), fs::perms::owner_all | fs::perms::group_all);
      // Return new values for offsets
      return std::make_pair(offset_beg, offset_end);
    };

    //
    // Write binaries
    //
    std::tie(offset_beg, offset_end) = f_write_bin(str_dir_bin, "main", 0);
    std::tie(offset_beg, offset_end) = f_write_bin(str_dir_bin, "ext2rd", offset_end);
    std::tie(offset_beg, offset_end) = f_write_bin(str_dir_bin, "fuse2fs", offset_end);
    std::tie(offset_beg, offset_end) = f_write_bin(str_dir_bin, "e2fsck", offset_end);

    //
    // Option to show offset and exit
    //
    if( getenv("ARTS_MAIN_OFFSET") ){ fmt::print("{}\n", offset_end); exit(0); }

    //
    // Set env variables to execve
    //
    setenv("ARTS_MAIN_LAUNCH", std::to_string(offset_end).c_str(), 1);
    setenv("ARTS_BASE", str_dir_base.c_str(), 1);
    setenv("ARTS_BIN", str_dir_bin.c_str(), 1);
    setenv("ARTS_FILE", path_absolute.c_str(), 1);
    setenv("ARTS_TEMP", str_dir_temp.c_str(), 1);

    //
    // Launch Runner
    //
    execve("{}/main"_fmt(str_dir_bin).c_str(), argv, environ);
  }
  // }}}

  return EXIT_SUCCESS;
} // }}}

// cmd: !mkdir -p build && cd build && conan install .. --build=missing && cd .. || cd ..
// cmd: !cmake -H. -Bbuild -DCMAKE_EXPORT_COMPILE_COMMANDS=1
// cmd: !cmake --build build

/* vim: set expandtab fdm=marker ts=2 sw=2 tw=80 et :*/
