#!/bin/sh
# Copyright (c) 2026 Nordic Semiconductor ASA
# SPDX-License-Identifier: Apache-2.0
#
# Build one test suite. Usage: ./build.sh <suite-name> [make arguments]
#
# The suite is built as a single mode executable, so running it needs neither
# the main controller nor expect:
#
#     cd suites/<name>/build && ./<name> ../<name>.cfg
#
# Everything is built in suites/<name>/build, where the suite sources and the
# module sources it asks for are linked side by side. The flat layout is not
# just tidiness: the makefile Titan generates builds a dependency rule with
# sed and the stem as the pattern, which breaks as soon as a source is named
# through a path with a slash in it.
#
# TTCN3_DIR must point at a Titan installation. Both layouts are handled: a
# distribution package puts the headers in $TTCN3_DIR/include/titan, a source
# install puts them in $TTCN3_DIR/include.

set -eu

suite=${1:-}
if [ -z "$suite" ]; then
	echo "usage: $0 <suite-name> [make arguments]" >&2
	exit 1
fi
shift

here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
suite_dir="$here/suites/$suite"
build_dir="$suite_dir/build"

if [ ! -d "$suite_dir" ]; then
	echo "no such suite: $suite" >&2
	exit 1
fi

# Single mode by default: the executable is the whole test suite, and running
# it needs neither a main controller nor expect. A suite whose test cases
# create parallel test components cannot use it, and says so in build.conf.
MODE=single
# Libraries a suite has to link against beyond what Titan itself needs. A test
# port that talks to the network below the IP layer usually brings one.
LIBS=
if [ -f "$suite_dir/build.conf" ]; then
	. "$suite_dir/build.conf"
fi

case "$MODE" in
single)   mode_flag=-s ;;
parallel) mode_flag= ;;
*)        echo "unknown MODE '$MODE' in $suite_dir/build.conf" >&2; exit 1 ;;
esac

if [ -z "${TTCN3_DIR:-}" ]; then
	echo "TTCN3_DIR is unset; point it at a Titan installation" >&2
	exit 1
fi

if [ ! -d "$here/modules" ]; then
	echo "third party modules are missing; run ./fetch-modules.sh first" >&2
	exit 1
fi

if [ -d "$TTCN3_DIR/include/titan" ]; then
	titan_inc="$TTCN3_DIR/include/titan"
	titan_lib="$TTCN3_DIR/lib/titan"
else
	titan_inc="$TTCN3_DIR/include"
	titan_lib="$TTCN3_DIR/lib"
fi

rm -rf "$build_dir"
mkdir -p "$build_dir"

link_source()
{
	if [ ! -f "$1" ]; then
		echo "missing source: $1" >&2
		exit 1
	fi
	ln -sf "$1" "$build_dir/$(basename "$1")"
}

# A suite that only runs test cases from a third party module has no sources
# of its own, so an empty match here is not an error.
for f in "$suite_dir"/*.ttcn "$suite_dir"/*.cc "$suite_dir"/*.hh \
	 "$here"/common/*.ttcn "$here"/common/*.cc "$here"/common/*.hh; do
	if [ -f "$f" ]; then
		link_source "$f"
	fi
done

link_module_sources()
{
	while read -r path; do
		case "$path" in
		''|\#*) continue ;;
		esac
		link_source "$here/modules/$path"
	done < "$1"
}

link_module_sources "$here/common/sources.txt"
link_module_sources "$suite_dir/sources.txt"

cd "$build_dir"

names=$(ls ./*.ttcn ./*.cc ./*.hh 2>/dev/null | sed 's#^\./##')

# shellcheck disable=SC2086
"$TTCN3_DIR/bin/ttcn3_makefilegen" -g $mode_flag -f -e "$suite" $names

make CPPFLAGS="-D\$(PLATFORM) -I. -I$titan_inc" \
     LDFLAGS="-L$titan_lib" \
     LINUX_LIBS="-lxml2 $LIBS" \
     CXXFLAGS="-O2 -Wall -Wno-unused-variable -Wno-deprecated-declarations" \
     "$@"

echo "Built $build_dir/$suite"
