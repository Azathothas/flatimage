#!/usr/bin/env -S bash -euET -o pipefail -O inherit_errexit

######################################################################
# @author      : Ruan E. Formigoni (ruanformigoni@gmail.com)
# @file        : _build
# @created     : Monday Jan 23, 2023 21:18:26 -03
#
# @description : Tools to build the filesystem
######################################################################

#shellcheck disable=2155

set -e

# Max size (M) = actual occupied size + offset
export ARTS_IMG_SIZE_OFFSET="${ARTS_IMG_SIZE_OFFSET:=50}"

ARTS_SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

# shellcheck source=/dev/null
source "${ARTS_SCRIPT_DIR}/_common.sh"

# Creates a filesystem image
# $1 = folder to create image from
# $2 = output file name
function _create_image()
{
  # Check inputs
  [ -d "$1" ] || _die "Invalid directory $1"
  [ -n "$2" ] || _die "Empty output file name"

  # Set vars & max size
  local dir="$1"
  local out="$(basename "$2")"
  local slack="50" # 50M
  local size="$(du -sh -BM "$dir" 2>/dev/null | awk '{printf "%d\n", $1}')"
  size="$((size+slack))M"

  _msg "dir: $dir"
  _msg "outfile: $out"
  _msg "max size: $size"

  # Create filesystem
  truncate -s "$size" "$out"
  mke2fs -d "$dir" -b1024 -t ext2 "$out"
}

# Concatenates binary files and filesystem to create arts image
# $1 = Path to system image
# $2 = Output file name
function _create_elf()
{
  local img="$1"
  local out="$2"

  cp bin/elf "$out"
  cat bin/proot >> "$out"
  cat bin/fuse2fs >> "$out"
  cat bin/dwarfs >> "$out"
  cat bin/mkdwarfs >> "$out"
  cat "$img" >> "$out"

}

# Extracts the filesystem of a docker image
# $1 = Dockerfile
# $2 = Docker Tag, e.g, ubuntu:subsystem
# $3 = Output tarball name
function _create_subsystem_docker()
{
  local dockerfile="$1"
  local tag="$2"
  local out="$3"

  [ -f "$1" ] || _die "Invalid dockerfile $1"
  [ -n "$2" ] || _die "Empty tag parameter"
  [ -n "$3" ] || _die "Empty output filename"

  _msg "dockerfile: $(readlink -f "$dockerfile")"
  _msg "tag: $tag"
  _msg "outfile: $out"

  # Build image
  docker build -f "$dockerfile" -t "$tag" "$(dirname "$dockerfile")"
  # Start docker
  docker run --rm -t "$tag" sleep 10 &
  sleep 5 # Wait for mount
  # Extract subsystem
  local container="$(docker container list | grep "$tag" | awk '{print $1}')"
  # Create subsystem snapshot
  docker export "$container" > "${out%.tar}.tar"
  # Finish background job
  kill %1
}

# Creates a subsystem with debootstrap
# Requires root permissions
# $1 = Distribution, e.g., bionic, focal, jammy
function _create_subsystem_debootstrap()
{
  mkdir -p dist
  mkdir -p bin

  local dist="$1"

  [[ "$dist" =~ bionic|focal|jammy ]] || _die "Invalid distribution $1"

  # Fetch proot
  wget -qO bin/proot https://proot.gitlab.io/proot/bin/proot

  # Build
  debootstrap "$dist" "/tmp/$dist" http://archive.ubuntu.com/ubuntu/

  # Update sources
  # shellcheck disable=2002
  cat "$ARTS_SCRIPT_DIR/../../sources/${dist}.list" | tee "/tmp/$dist/etc/apt/sources.list"

  # Update packages
  chroot "/tmp/$dist" /bin/bash -c 'apt -y update && apt -y upgrade'
  chroot "/tmp/$dist" /bin/bash -c 'apt -y clean && apt -y autoclean && apt -y autoremove'
  chroot "/tmp/$dist" /bin/bash -c 'apt install -y rsync alsa-utils alsa-base alsa-oss pulseaudio libc6 libc6-i386 libpaper1 fontconfig-config ppp man-db libnss-mdns ippusbxd gdm3'

  # Create share symlink
  ln -s /usr/share "/tmp/$dist/share"

  # Install dwarfs
  local tempdir="$(mktemp -d)"
  wget -q --show-progress --progress=dot:mega \
    https://github.com/mhx/dwarfs/releases/download/v0.6.2/dwarfs-0.6.2-Linux.tar.xz -O - |
    tar xJ -C"$tempdir" --strip-components=1
  rsync -K -a "$tempdir/" "/tmp/$dist"
  cp "$tempdir/sbin/dwarfs" bin # Save to embed in final binary
  cp "$tempdir/bin/mkdwarfs" bin # Save to embed in final binary
  rm -rf "$tempdir"

  # Embed runner
  mkdir -p "/tmp/$dist/arts/"
  cp "$ARTS_SCRIPT_DIR/_boot.sh" "/tmp/$dist/arts/boot"

  # Set dist
  sed -i 's/ARTS_DIST="TRUNK"/ARTS_DIST="FOCAL"/' "/tmp/$dist/arts/boot"

  # Set permissions
  chown -R "$(id -u)":users "/tmp/$dist"
  chmod 777 -R "/tmp/$dist"

  # Create image
  _create_image  "/tmp/$dist" "$dist.img"

  # Create elf
  _create_elf "$dist.img" "$dist.arts"

  tar -cf "$dist.tar" "$dist.arts"
  xz -3zv "$dist.tar"

  mv "$dist.tar.xz" dist/
}

# Creates an arch subsystem
# Requires root permissions
function _create_subsystem_arch()
{
  mkdir -p dist
  mkdir -p bin

  # Fetch proot
  wget -qO bin/proot https://proot.gitlab.io/proot/bin/proot

  # Fetch bootstrap
  git clone https://github.com/tokland/arch-bootstrap.git

  # Build
  ./arch-bootstrap/arch-bootstrap.sh arch

  # Update mirrorlist
  cp "$ARTS_SCRIPT_DIR/../../sources/arch.list" arch/etc/pacman.d/mirrorlist

  # Enable multilib
  gawk -i inplace '/#\[multilib\]/,/#Include = (.*)/ { sub("#", ""); } 1' ./arch/etc/pacman.conf
  chroot arch /bin/bash -c "pacman -Sy"

  # Audio & video
  pkgs_va+=("alsa-lib lib32-alsa-lib alsa-plugins lib32-alsa-plugins libpulse")
  pkgs_va+=("lib32-libpulse jack2 lib32-jack2 alsa-tools alsa-utils")
  # pkgs_va+=("mesa lib32-mesa vulkan-radeon lib32-vulkan-radeon vulkan-intel nvidia-utils")
  # pkgs_va+=("lib32-vulkan-intel lib32-nvidia-utils vulkan-icd-loader lib32-vulkan-icd-loader")
  # pkgs_va+=("vulkan-mesa-layers lib32-vulkan-mesa-layers libva-mesa-driver")
  # pkgs_va+=("lib32-libva-mesa-driver libva-intel-driver lib32-libva-intel-driver")
  # pkgs_va+=("intel-media-driver mesa-utils vulkan-tools nvidia-prime libva-utils")
  # pkgs_va+=("lib32-mesa-utils")
  chroot arch /bin/bash -c "pacman -S ${pkgs_va[*]} --noconfirm"
  chroot arch /bin/bash -c "pacman -S git fakeroot binutils --noconfirm"
  chroot arch /bin/bash -c "pacman -Scc --noconfirm"
  rm -rf ./arch/var/cache/pacman/pkg/*

  # Install yay
  wget -q --show-progress --progress=dot:mega \
    -O yay.tar.gz https://github.com/Jguer/yay/releases/download/v11.3.2/yay_11.3.2_x86_64.tar.gz
  tar -xf yay.tar.gz --strip-components=1 --wildcards "*yay"
  rm yay.tar.gz
  mv yay arch/usr/bin

  # Create share symlink
  ln -sf /usr/share ./arch/share

  # Install dwarfs
  local tempdir="$(mktemp -d)"
  wget -q --show-progress --progress=dot:mega \
    https://github.com/mhx/dwarfs/releases/download/v0.6.2/dwarfs-0.6.2-Linux.tar.xz -O - |
    tar xJ -C"$tempdir" --strip-components=1
  rsync -K -a "$tempdir/" arch
  cp "$tempdir/sbin/dwarfs" bin # Save to embed in final binary
  cp "$tempdir/bin/mkdwarfs" bin # Save to embed in final binary
  rm -rf "$tempdir"

  # Embed runner
  mkdir -p "./arch/arts/"
  cp "$ARTS_SCRIPT_DIR/_boot.sh" "./arch/arts/boot"

  # Set dist
  sed -i 's/ARTS_DIST="TRUNK"/ARTS_DIST="ARCH"/' ./arch/arts/boot

  # Set permissions
  chown -R "$(id -u)":users "./arch"
  chmod 755 -R "./arch"

  # Create image
  _create_image  "./arch" "arch.img"

  # Create elf
  _create_elf "arch.img" "arch.arts"

  tar -cf arch.tar arch.arts
  xz -3zv arch.tar

  mv "arch.tar.xz" dist/
}

function main()
{
  case "$1" in
    "debootstrap")   _create_subsystem_debootstrap "${@:2}" ;;
    "archbootstrap") _create_subsystem_arch ;;
    *) _die "Invalid option $2" ;;
  esac
}

main "$@"
