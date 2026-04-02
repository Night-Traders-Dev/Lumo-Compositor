#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage: ./build.sh [options]

Configure, build, and optionally test the compositor from the repository root.

Options:
  -b, --build-dir DIR         Meson build directory relative to compositor/
                              (default: build)
  -x, --xwayland MODE         xWayland feature mode: enabled, disabled, or auto
  -t, --touch-quirks MODE     OrangePi touch-quirk install mode: enabled,
                              disabled, or auto
      --udev-rules-dir DIR    Install path for bundled udev rules
      --buildtype TYPE        Meson build type (debug, debugoptimized, release,
                              plain)
      --reconfigure           Force meson setup --reconfigure
      --wipe                  Remove the build directory before configuring
      --setup-only            Run meson setup only
      --test                  Run meson test after compiling
      --meson-setup-arg ARG   Forward an extra raw argument to meson setup
      --meson-compile-arg ARG Forward an extra raw argument to meson compile
      --meson-test-arg ARG    Forward an extra raw argument to meson test
  -h, --help                  Show this help

Examples:
  ./build.sh
  ./build.sh --xwayland disabled
  ./build.sh --touch-quirks disabled
  ./build.sh --build-dir build-noxwayland --xwayland disabled --test
  ./build.sh --buildtype debugoptimized --test
EOF
}

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
project_dir="$script_dir/compositor"

build_dir="build"
xwayland_mode="enabled"
touch_quirks_mode="enabled"
udev_rules_dir=""
buildtype=""
force_reconfigure=false
wipe=false
setup_only=false
run_tests=false

meson_setup_args=()
meson_compile_args=()
meson_test_args=()

while (($#)); do
  case "$1" in
    -h|--help)
      usage
      exit 0
      ;;
    -b|--build-dir)
      shift
      build_dir="${1:?build.sh: missing build dir}"
      ;;
    --build-dir=*)
      build_dir="${1#*=}"
      ;;
    -x|--xwayland)
      shift
      xwayland_mode="${1:?build.sh: missing xwayland mode}"
      ;;
    --xwayland=*)
      xwayland_mode="${1#*=}"
      ;;
    -t|--touch-quirks)
      shift
      touch_quirks_mode="${1:?build.sh: missing touch quirk mode}"
      ;;
    --touch-quirks=*)
      touch_quirks_mode="${1#*=}"
      ;;
    --udev-rules-dir)
      shift
      udev_rules_dir="${1:?build.sh: missing udev rules dir}"
      ;;
    --udev-rules-dir=*)
      udev_rules_dir="${1#*=}"
      ;;
    --buildtype)
      shift
      buildtype="${1:?build.sh: missing buildtype}"
      ;;
    --buildtype=*)
      buildtype="${1#*=}"
      ;;
    --reconfigure)
      force_reconfigure=true
      ;;
    --wipe)
      wipe=true
      ;;
    --setup-only)
      setup_only=true
      ;;
    --test)
      run_tests=true
      ;;
    --meson-setup-arg)
      shift
      meson_setup_args+=("${1:?build.sh: missing meson setup arg}")
      ;;
    --meson-compile-arg)
      shift
      meson_compile_args+=("${1:?build.sh: missing meson compile arg}")
      ;;
    --meson-test-arg)
      shift
      meson_test_args+=("${1:?build.sh: missing meson test arg}")
      ;;
    --)
      shift
      meson_setup_args+=("$@")
      break
      ;;
    *)
      printf 'build.sh: unknown option: %s\n' "$1" >&2
      usage >&2
      exit 1
      ;;
  esac
  shift
done

if [[ ! -d "$project_dir" ]]; then
  printf 'build.sh: missing compositor directory: %s\n' "$project_dir" >&2
  exit 1
fi

case "$xwayland_mode" in
  enabled|disabled|auto) ;;
  *)
    printf 'build.sh: invalid xwayland mode: %s (expected enabled, disabled, or auto)\n' \
      "$xwayland_mode" >&2
    exit 1
    ;;
esac

case "$touch_quirks_mode" in
  enabled|disabled|auto) ;;
  *)
    printf 'build.sh: invalid touch quirk mode: %s (expected enabled, disabled, or auto)\n' \
      "$touch_quirks_mode" >&2
    exit 1
    ;;
esac

# Verify version.h and meson.build are in sync
version_h="$project_dir/include/lumo/version.h"
if [[ -f "$version_h" ]]; then
  header_version=$(grep 'LUMO_VERSION_STRING' "$version_h" | head -1 | sed 's/.*"\(.*\)".*/\1/')
  meson_version=$(grep "version:" "$project_dir/meson.build" | head -1 | sed "s/.*'\(.*\)'.*/\1/")
  if [[ -n "$header_version" && -n "$meson_version" && "$header_version" != "$meson_version" ]]; then
    printf 'build.sh: VERSION MISMATCH: version.h=%s meson.build=%s\n' \
      "$header_version" "$meson_version" >&2
    printf 'build.sh: edit include/lumo/version.h and meson.build to match\n' >&2
    exit 1
  fi
fi

if [[ -z "$buildtype" ]]; then
  :
else
  meson_setup_args+=(--buildtype "$buildtype")
fi

meson_setup_args+=(-Dxwayland="$xwayland_mode")
meson_setup_args+=(-Dorangepi_touch_quirks="$touch_quirks_mode")
if [[ -n "$udev_rules_dir" ]]; then
  meson_setup_args+=(-Dudev_rules_dir="$udev_rules_dir")
fi

if [[ "$build_dir" = /* ]]; then
  build_path="$build_dir"
else
  build_path="$project_dir/$build_dir"
fi

cd "$project_dir"

if $wipe; then
  rm -rf "$build_path"
fi

reconfigure=false
if [[ -e "$build_path/build.ninja" || -d "$build_path/meson-private" ]]; then
  reconfigure=true
fi
if $force_reconfigure; then
  reconfigure=true
fi
if $wipe; then
  reconfigure=false
fi

setup_cmd=(meson setup "$build_path")
if $reconfigure; then
  setup_cmd+=(--reconfigure)
fi
setup_cmd+=("${meson_setup_args[@]}")

printf '==> Configuring Lumo compositor in %s\n' "$build_path"
"${setup_cmd[@]}"

if $setup_only; then
  exit 0
fi

printf '==> Compiling Lumo compositor\n'
compile_cmd=(meson compile -C "$build_path")
compile_cmd+=("${meson_compile_args[@]}")
"${compile_cmd[@]}"

if $run_tests; then
  printf '==> Running Lumo tests\n'
  test_cmd=(meson test -C "$build_path" --print-errorlogs)
  test_cmd+=("${meson_test_args[@]}")
  "${test_cmd[@]}"
fi
