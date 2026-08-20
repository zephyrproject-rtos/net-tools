# TTCN-3 protocol conformance suites for Zephyr

Test suites written in [TTCN-3](https://www.ttcn-3.org/) and built with
[Eclipse Titan](https://projects.eclipse.org/projects/tools.titan), run against
a Zephyr instance over a real network interface.

These are black box tests. A suite speaks the protocol to Zephyr the way any
other host on the link would, and checks what comes back against the standard.
Nothing is compiled into Zephyr for them, and no Zephyr side test hooks are
needed: the system under test is an ordinary sample application.

| Suite | System under test | What it covers |
|---|---|---|
| `mdns` | `samples/net/mdns_responder` | Name resolution over IPv4 and IPv6, record shape, silence for names the responder does not own |

## Getting a Titan

Either install the packaged one:

```
sudo apt install --no-install-recommends eclipse-titan
export TTCN3_DIR=/usr
```

or build a current one from source, following `docker/Dockerfile.ttcn3`, which
is what continuous integration uses. The packaged version trails the protocol
modules, so prefer the source build if a suite fails to compile.

## Running a suite

Fetch the third party modules the suites build against. This clones them into
`modules/` at the commits pinned in `modules.txt`, and is safe to re-run:

```
./fetch-modules.sh
```

Bring up the network interface that faces Zephyr, and start the sample:

```
sudo ../net-setup.sh --config ../zeth.conf start
west build -b native_sim -d build/mdns "$ZEPHYR_BASE/samples/net/mdns_responder"
./build/mdns/zephyr/zephyr.exe &
```

Then build and run the suite:

```
./build.sh mdns
cd suites/mdns/build && ./mdns ../mdns.cfg
```

The last line of the output is the verdict:

```
Verdict statistics: 0 none (0.00 %), 7 pass (100.00 %), 0 inconc (0.00 %), 0 fail (0.00 %), 0 error (0.00 %).
Test execution summary: 7 test cases were executed. Overall verdict: pass
```

To run one test case instead of the whole suite, name it on the command line:

```
./mdns ../mdns.cfg MDNS_Suite.tc_a_query
```

Addresses, the interface name and the timeouts all come from
`[MODULE_PARAMETERS]` in the suite's `.cfg`, so a run can be moved onto a
different link without touching the suite.

## Layout

```
common/          TTCN-3 shared by every suite
modules/         third party modules, cloned by fetch-modules.sh, not in git
modules.txt      which third party modules, and at which commit
fetch-modules.sh clones them
build.sh         builds one suite
suites/<name>/   the suite, its sources.txt and its .cfg
```

`build.sh` links the suite sources, the shared layer and the module sources
named in the suite's `sources.txt` into `suites/<name>/build`, and builds there.
The flat layout matters: the makefile Titan generates builds a dependency rule
with `sed` using the target stem as the pattern, which breaks as soon as a
source is named through a path containing a slash.

## Third party modules

The suites build against protocol modules and test ports from the Eclipse Titan
project, which are separate repositories under
`https://gitlab.eclipse.org/eclipse/titan/`. They are not vendored here; they
are cloned on demand and pinned by commit so that a suite which passes today
still builds tomorrow.

They are distributed under the Eclipse Public License 2.0, whereas everything
written here is Apache 2.0. Both are approved by the Open Source Initiative,
which is what Zephyr asks of tooling that never becomes part of a Zephyr
image; see `doc/contribute/external.rst` in the Zephyr tree.

To move a pin, change the commit in `modules.txt` and re-run
`./fetch-modules.sh`.

## Adding a suite

1. Create `suites/<name>/` with the TTCN-3 source, a `sources.txt` naming the
   module sources it needs, and a `<name>.cfg`.
2. Take addresses and timeouts from `common/Zephyr_SUT.ttcn` rather than
   writing them into the suite.
3. If the suite needs a module that is not in `modules.txt` yet, add it there
   with a pinned commit.
4. Add a row to the table at the top of this file.

## Known divergences from the standard

Where a suite asserts behaviour that does not match the RFC, the assertion says
so at the point it is made. Today that is RFC 6762 6.7 in the `mdns` suite: the
responder does not distinguish a legacy unicast query, so its answers to one
carry identifier zero, no question section, the cache flush bit set and the
full TTL. Answers to a real mDNS resolver, which queries from port 5353, are
not affected.
