# Shared by the test scripts in this folder (sourced, not run).
#
# SRC       the ScummVM source folder (found from this folder)
# HOST      the native build tree that runs the unit tests (see README.md)
# WORK      the runtime tests' run folders (DOS_TEST_WORK)
# DOSBOX    the DOSBox-X program
#
# The tests' folders are build-... folders in the source folder (ignored by "/build*" in .gitignore):
# build-dos-<name> (test builds), build-dos-tests (runtime tests), build-host-test (unit tests).
# The cross-compiler and the libraries are found as dos-env.sh says.

TESTS_DIR=$(cd "$(dirname "$0")" && pwd)
DOS_DIR=$(cd "$TESTS_DIR/.." && pwd)
. "$DOS_DIR/dos-env.sh"
HOST=${SCUMMVM_HOST_TREE:-$SRC/build-host-test}
WORK=${DOS_TEST_WORK:-$SRC/build-dos-tests}
DOSBOX=${DOSBOX:-$(command -v dosbox-x || echo /Applications/dosbox-x.app/Contents/MacOS/dosbox-x)}

# The folder of a named DOS test build
build_dir() {
  echo "$SRC/build-dos-$1"
}

# Configure a named DOS test build into its folder (build_dir):
#
#   stable            the stable profile, engines as plug-ins, on the port's own backend
#   stable-sdl        the same on SDL3's DOS driver, configure-dos.sh's default (needs deps/build-sdl3.sh)
#   stable-static     engines inside the program (DOS_STATIC=1): the plug-in checks' control
#   testbed           Testbed only, as a plug-in: for the tests that need no game data
#   sdl-testbed       the same on SDL3 (the sdl test's build)
configure_named_build() {
  (
    . "$DOS_DIR/engines.conf"
    unset ENGINES_ONLY DOS_STATIC DOS_BACKEND
    case "$1" in
      stable)           profile=stable; DOS_BACKEND=dos ;;
      stable-sdl)       profile=stable; DOS_BACKEND=sdl ;;
      stable-static)    profile=stable; DOS_STATIC=1; DOS_BACKEND=dos ;;
      testbed)          profile=stable; ENGINES_ONLY=testbed; DOS_BACKEND=dos ;;
      sdl-testbed)      profile=stable; ENGINES_ONLY=testbed; DOS_BACKEND=sdl ;;
      *) echo "unknown DOS test build: $1" >&2; exit 2 ;;
    esac
    export ENGINES_ONLY DOS_STATIC DOS_BACKEND
    sh "$DOS_DIR/configure-dos.sh" "$profile" "$(build_dir "$1")"
  )
}

# The test generator (cxxtestgen) starts "python"; macOS has python3 only. The host tree has a
# bin/ folder with a python link to it.
host_path() {
  mkdir -p "$HOST/bin"
  [ -e "$HOST/bin/python" ] || ln -s "$(command -v python3)" "$HOST/bin/python"
  PATH="$HOST/bin:$PATH"
  export PATH
}

# Build the unit test runner in the host tree (quietly; the log is kept)
build_runner() {
  host_path
  (cd "$HOST" && make -j8 test/runner > build-tests.log 2>&1) || {
    echo "BUILD FAILED"
    tail -20 "$HOST/build-tests.log"
    return 1
  }
}
