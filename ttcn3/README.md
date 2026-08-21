# TTCN-3 protocol conformance suites for Zephyr

Test suites written in [TTCN-3](https://www.ttcn-3.org/) and built with
[Eclipse Titan](https://projects.eclipse.org/projects/tools.titan), run against
a Zephyr instance over a real network interface.

**The documentation for these suites lives in the Zephyr tree**, at
[Protocol conformance testing with TTCN-3](https://docs.zephyrproject.org/latest/connectivity/networking/conformance/index.html).
It covers what each suite tests, how to install Titan, how to set up the network
interfaces, how to run everything through Twister, and how to add a suite. Start
there.

What follows is the part that is specific to this repository: how the build
works and how the third party modules are managed.

## Layout

```
common/          TTCN-3 shared by every suite, and the ethernet test port
modules/         third party modules, cloned by fetch-modules.sh, not in git
modules.txt      which third party modules, and at which commit
fetch-modules.sh clones them
build.sh         builds one suite
suites/<name>/   the suite, its sources.txt and its .cfg
```

## Building a suite

`./fetch-modules.sh` once, then:

```
./build.sh <suite> [make arguments]
```

Anything after the suite name is passed to `make`, so `./build.sh mdns -j8` and
`./build.sh mdns clean` both work. `TTCN3_DIR` has to point at a Titan
installation; both the packaged layout (`include/titan`, `lib/titan`) and a
source build (`include`, `lib`) are detected.

The result is `suites/<suite>/build/<suite>`. To run it, put Titan's library
directory on `LD_LIBRARY_PATH` and give it the configuration file:

```
cd suites/mdns/build && ./mdns ../mdns.cfg
```

A single test case is run by naming it, which is the quick loop while writing
one:

```
./mdns ../mdns.cfg MDNS_Suite.tc_a_query
```

`build.sh` wipes the build directory and rebuilds it flat, symlinking the suite
sources, the shared layer and the module sources named in `common/sources.txt`
and the suite's own `sources.txt` side by side. The flat layout matters: the
makefile Titan generates builds a dependency rule with `sed` using the target
stem as the pattern, which breaks as soon as a source is named through a path
containing a slash.

### build.conf

A suite may carry a `build.conf`, which is both sourced as a shell fragment by
`build.sh` and read by the Zephyr side harness:

| Setting | Effect |
|---|---|
| `MODE=parallel` | Test cases create parallel test components, so the suite is built for and run through Titan's main controller. Default is `MODE=single`. |
| `PRIVILEGED=yes` | The suite binds a privileged port or opens a packet socket, so it has to be run as root. |
| `L2=yes` | The suite works below the IP layer and wants the address-less `zethL2` interface. |
| `LIBS=...` | Extra libraries to link against, beyond what Titan itself needs. |

### Building in the container

`docker/Dockerfile.ttcn3` builds an image carrying a current Titan. Give it your
own user id when building a suite through a bind mount, or it leaves artifacts
behind that you cannot delete:

```
docker run --rm --user "$(id -u):$(id -g)" -v "$PWD:/ttcn3" -w /ttcn3 \
       net-tools-ttcn3 ./build.sh mdns
```

## Third party modules

The suites build against protocol modules and test ports from the Eclipse Titan
project, which are separate repositories under
`https://gitlab.eclipse.org/eclipse/titan/`. They are not vendored here; they
are cloned on demand and pinned by commit so that a suite which passes today
still builds tomorrow. `modules.txt` holds one `<repository> <commit>` line per
module. To move a pin, change the commit there and re-run `./fetch-modules.sh`.

One test port is written here rather than taken from them. The Titan project
publishes `LANL2asp` for reading and writing ethernet frames, but it captures
with libpcap and opens the handle with a zero read timeout, which on Linux
asks the kernel to wait indefinitely for a capture block to fill. On a link as
quiet as a test link the frames never reach the test, and there is no parameter
to change it. `common/Ethernet_PT.cc` reads a packet socket instead.

The modules are distributed under the Eclipse Public License 2.0, whereas
everything written here is Apache 2.0. Both are approved by the Open Source
Initiative, which is what Zephyr asks of tooling that never becomes part of a
Zephyr image; see `doc/contribute/external.rst` in the Zephyr tree.

## Configuration files

Addresses, the interface name and the timeouts all come from
`[MODULE_PARAMETERS]` in the suite's `.cfg`, so a run can be moved onto a
different link without touching the suite. A MAC address there is twelve
hexadecimal digits with no separators, and `tsp_tester_mac` has to match the
`source_address` given to the ethernet test port in the same file.

Each suite logs to its build directory under the name set by `LogFile`, as
`<suite>-<component>.<seq>.log`.
