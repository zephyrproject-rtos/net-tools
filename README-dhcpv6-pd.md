# Manual DHCPv6 prefix delegation (RFC 8415) testing

This describes how to set up the host so you can manually test the Zephyr
[`dhcpv6_pd`](https://github.com/zephyrproject-rtos/zephyr/tree/main/samples/net/dhcpv6_pd)
sample end to end: a host DHCPv6 server delegates an IPv6 prefix to a Zephyr
*requesting router*, which then carves `/64` sub-prefixes out of it and
advertises them on its downstream link(s).

## Topology

```
                delegates prefix (IA_PD)
                assigns address  (IA_NA)
  +--------------------+  zeth0    +--------------------------+  zeth1   +-----------------+
  | Host DHCPv6 server | <-------> | Zephyr requesting router | <------> | Host downstream |
  |   (kea-dhcp6)      |  WAN      |   (native_sim,           |  LAN     |   (SLAAC from   |
  |   2001:db8::2      |  link     |    dhcpv6_pd sample)     |  link    |   delegated /64)|
  +--------------------+           +--------------------------+          +-----------------+
```

* `zeth0` -- upstream/WAN link. The host runs the DHCPv6-PD server here.
  It hands the Zephyr router an IA_NA address from `2001:db8::/64` and an
  IA_PD `/56` delegated prefix from `2001:db8:beef::/48`.
* `zeth1` -- downstream/LAN link. The Zephyr router advertises a `/64` carved
  from the delegated prefix (`2001:db8:beef:0::/64`) via Router Advertisements.
  The host end auto-configures an address from it (SLAAC), which confirms
  delegation worked.

## Why not dnsmasq?

dnsmasq cannot act as a delegating router: its "prefix delegation" support only
lets it serve addresses from a prefix the *host* already received, it does not
answer IA_PD requests. Use ISC **Kea** (recommended) or legacy ISC dhcpd
(`prefix6`) instead.

## Files used

| File | Role |
| --- | --- |
| `zeth-dhcpv6-pd.conf` | Creates/configures the host TAP interfaces `zeth0` (WAN) and `zeth1` (LAN). |
| `zeth-dhcpv6-pd.conf.stop` | Removes the `zeth1` interface on teardown. |
| `kea-dhcp6-pd.conf` | Kea `kea-dhcp6` server config: IA_NA pool + IA_PD `/56` pool on `zeth0`. |

## Prerequisites

* A Zephyr workspace with the `dhcpv6_pd` sample and a working `native_sim`
  toolchain.
* ISC Kea installed on the host:
  * Debian/Ubuntu: `sudo apt install kea-dhcp6-server`
  * Fedora: `sudo dnf install kea`
  * or build from <https://www.isc.org/kea/>.
* `sudo`/root, since creating TAP interfaces and binding the DHCPv6 server
  need privileges.

## Step 1 -- Create the host interfaces

From this `net-tools` directory:

```console
sudo ./net-setup.sh --config zeth-dhcpv6-pd.conf --iface zeth0
```

This creates `zeth0` (address `2001:db8::2/64`) and `zeth1` (no global
address, `accept_ra` enabled). The script stays in the foreground and removes
the interfaces on Ctrl-C. To manage them separately instead:

```console
sudo ./net-setup.sh --config zeth-dhcpv6-pd.conf --iface zeth0 start
# ... run your tests ...
sudo ./net-setup.sh --config zeth-dhcpv6-pd.conf --iface zeth0 stop
```

Both interfaces stay in `NO-CARRIER ... state DOWN` until the `native_sim`
binary attaches to them in step 2. That is expected, and it is why the DHCPv6
server is started last.

## Step 2 -- Build and run the Zephyr requesting router

In your Zephyr tree:

```console
west build -b native_sim samples/net/dhcpv6_pd
west build -t run
```

The `native_sim` binary attaches to `zeth0` and `zeth1` automatically. On
start-up the sample logs the chosen upstream and downstream interfaces and
begins DHCPv6 on the upstream link.

**Start this before Kea.** A TAP interface has no carrier until a process
attaches to it, so until the `native_sim` binary is running `zeth0` stays
`NO-CARRIER,UP ... state DOWN` and Kea refuses to bind to it (see the
troubleshooting section below). Carrier cannot be forced on with
`ip link set zeth0 carrier on` either. Confirm the link is up with:

```console
ip link show zeth0                   # expect LOWER_UP ... state UP
```

## Step 3 -- Start the host DHCPv6-PD server

On Debian/Ubuntu, `kea-dhcp6` ships with an *enforced* AppArmor profile that is
written for its systemd unit. Running it by hand aborts with `Permission denied`
when creating its PID/lockfile (e.g. `/run/kea/logger_lockfile`), **even as
root** and even if you redirect the paths, because AppArmor confines the binary.
Put the profile in complain mode first (needs the `apparmor-utils` package):

```console
sudo aa-complain /usr/sbin/kea-dhcp6
```

Then, in a third terminal, run Kea in the foreground so you can watch the logs:

```console
sudo kea-dhcp6 -c kea-dhcp6-pd.conf
```

**Order matters:** Kea opens its DHCPv6 socket and joins the server multicast
group `ff02::1:2` on `zeth0` only once, at start-up, and it only does so if the
interface is *running* (has carrier). Always start Kea **after** both
`zeth-dhcpv6-pd.conf` has created `zeth0` and the `native_sim` binary has
attached to it, and restart Kea if you recreate the interfaces or restart the
sample. You can confirm Kea is actually listening with:

```console
sudo ss -ulnp6 | grep 547            # expect a socket on [ff02::1:2]%zeth0:547
ip -6 maddr show dev zeth0 | grep ff02::1:2
```

Because the sample is already running at this point, its first Solicits went out
before Kea was listening and the retransmissions back off exponentially. Rather
than waiting, restart the client from the Zephyr shell once Kea is up:

```console
uart:~$ net dhcpv6 client stop 1
uart:~$ net dhcpv6 client start 1
```

Restore enforcement when you are done testing:

```console
sudo aa-enforce /usr/sbin/kea-dhcp6
```

If the kea server cannot be started, it might be that the kea is already
running. You can stop it like this

```console
sudo systemctl stop kea-dhcp6-server.service
```

and optionally disable it like this

```console
sudo systemctl disable kea-dhcp6-server.service
```

Leases are stored in `/var/lib/kea/kea-leases6.csv`; delete it between runs for
a clean slate.

## Verifying

**On the Kea server** you should see a Solicit/Advertise/Request/Reply
exchange, with a lease from the IA_NA pool and a delegated prefix from the
IA_PD pool logged.

**On the Zephyr shell** (in the `west build -t run` console):

```console
uart:~$ net ipv6
uart:~$ net iface
```

Expect an IA_NA address (e.g. `2001:db8::100`) on the upstream interface and
the delegated prefix (`2001:db8:beef::/56`) with a `/64` installed and
advertised on the downstream interface.

**On the host downstream link** (`zeth1`), confirm SLAAC picked up the
delegated `/64`:

```console
ip -6 addr show dev zeth1
```

You should see a global address inside `2001:db8:beef:0::/64`.

## Cleanup

```console
sudo ./net-setup.sh --config zeth-dhcpv6-pd.conf --iface zeth0 stop
sudo rm -f /var/lib/kea/kea-leases6.csv
```

## Reverse direction: Zephyr as the DHCPv6 server

The `dhcpv6_pd` sample can also be built as the *delegating router* (DHCPv6
server) with `overlay-server.conf`. In this mode Zephyr answers DHCPv6 requests
on `zeth`, handing out IA_NA addresses and delegating IA_PD prefixes. You then
drive it with a DHCPv6 *client* on the host.

Because `native_sim` and its `zeth` TAP are the two ends of the same link, the
host client and the Zephyr server talk directly over `zeth` -- no bridge is
needed (that is only required when connecting two `native_sim` instances).

> **Note the different interface name.** `overlay-server.conf` sets
> `CONFIG_ETH_NATIVE_TAP_INTERFACE_COUNT=1`, and with a single interface the
> native TAP driver uses `CONFIG_ETH_NATIVE_TAP_DRV_NAME` verbatim as the host
> device name, i.e. `zeth`. The numbered names `zeth0`/`zeth1` only appear in
> the two-interface requesting-router build. Do not use
> `zeth-dhcpv6-pd.conf` here.

### Step 1 -- Create the interface

A single link is enough for server mode, so the default `zeth.conf` setup does:

```console
sudo ./net-setup.sh start
```

> **Note:** `zeth.conf` puts the static address `2001:db8::2/64` on
> `zeth`, which overlaps the sample's default server pool (`2001:db8::`). For a
> clean test the server pool base in the sample's `src/main.c` is set to `2001:db8:abcd::`.

### Step 2 -- Build and run Zephyr as the server

```console
west build -p -b native_sim samples/net/dhcpv6_pd -- \
    -DEXTRA_CONF_FILE=overlay-server.conf
west build -t run
```

On start-up the sample logs `Starting DHCPv6-PD server` and the server joins the
`ff02::1:2` (All_DHCP_Relay_Agents_and_Servers) multicast group so it can
receive client messages.

### Step 3 -- Run a DHCPv6 client on the host

Use any host DHCPv6 client bound to `zeth`:

```console
sudo dhclient -6 -d -v zeth        # request an IA_NA address only
sudo dhclient -6 -P -d -v zeth     # also request a delegated prefix (IA_PD)
```

or, with `odhcp6c` (handy for prefix delegation):

```console
sudo odhcp6c -v -P 64 zeth
```

You should see a Solicit/Advertise/Request/Reply exchange. The host receives an
address from the server's IA_NA pool and, when requesting `-P`, a delegated
`/64` from the pool (`2001:db8:abcd::/64` with the sample defaults).

`systemd-networkd` can request a prefix instead, with a `.network` file
containing:

```ini
[Network]
DHCP=ipv6

[DHCPv6]
PrefixDelegationHint=::/64
```

### Step 4 -- Verify on the Zephyr server

On the Zephyr shell, list the leases the server has handed out:

```console
uart:~$ net dhcpv6 server status
     Iface  IA_NA address                   IA_PD prefix                        Expiry (sec)
 1.   eth0 -                               2001:db8:abcd::/64                         86371
```

The `IA_NA address` column shows `-` when the client requested only a prefix
(`dhclient -P`); request an address as well (plain `dhclient -6`, or `dhcpcd`)
to populate it. `net iface` shows the same interface state.

Note that a lease appears **only** when a client completes a DHCPv6 exchange
with this server. A downstream host that auto-configures from a Router
Advertisement (SLAAC) does *not* use DHCPv6 and therefore creates no lease --
run one of the DHCPv6 clients above if `net dhcpv6 server status` reports
"no leases assigned".

### Alternative: a second Zephyr instance as the client

Instead of a host client you can run a second `native_sim` instance built as the
requesting router (the default `prj.conf`). Because two `native_sim` instances
cannot share the same TAP interface, connect them via a host bridge (see
`zeth-bridge.conf` and the `net-setup.sh` bridge usage). A host DHCPv6 client on
a single `zeth` is simpler for iterating on the server role.

## Troubleshooting

* **Kea exits with `failed to open socket: the interface zeth0 is not running`**
  -- `zeth0` exists and is administratively up, but has no carrier
  (`ip link show zeth0` reports `NO-CARRIER ... state DOWN`). A TAP interface
  only gets carrier while a process is attached to it, so this simply means the
  `native_sim` binary is not running yet. Start the sample first (step 2), then
  Kea. Carrier cannot be forced on for a detached TAP device, so
  `ip link set zeth0 carrier on` fails with `Operation not permitted`. Kea
  retries `service-sockets-max-retries` times (5, every 5 s in
  `kea-dhcp6-pd.conf`) before giving up, which is also enough to start the
  sample while Kea is still retrying.
* **Zephyr sends Solicit but Kea never replies** -- Kea is running but did not
  open its socket on `zeth0`, usually because it was started before the
  interface existed or before it had carrier. Verify with
  `sudo ss -ulnp6 | grep 547` (expect a listener on `[ff02::1:2]%zeth0:547`) and
  `ip -6 maddr show dev zeth0 | grep ff02::1:2`. If missing, restart Kea after
  bringing the interfaces up. The `service-sockets-*` options in
  `kea-dhcp6-pd.conf` make this fail loudly rather than silently. If Kea *is*
  listening, the client may still be backing off from the retransmissions it
  sent before Kea came up; restart it with `net dhcpv6 client stop 1` followed
  by `net dhcpv6 client start 1` on the Zephyr shell.
* **No Solicit reaches Kea** -- check the server is bound to `zeth0`
  (`interfaces-config` in `kea-dhcp6-pd.conf`) and that `zeth0` is up.
* **`net dhcpv6 server status` shows "no leases assigned"** -- the Zephyr
  server has no active bindings. It only tracks clients that completed a DHCPv6
  exchange with it, so a peer using SLAAC (Router Advertisements) will not show
  up. Run `sudo dhclient -6 -P -d -v zeth` (or `dhcpcd -6`) on the host to
  create a lease, and confirm `zeth` is up before starting the server so it can
  join the `ff02::1:2` multicast group.
* **Only one Zephyr interface, and `zeth0` never gets carrier** -- the sample
  was built with `overlay-server.conf`, which drops
  `CONFIG_ETH_NATIVE_TAP_INTERFACE_COUNT` to 1. That build attaches to `zeth`,
  not `zeth0`/`zeth1`, and acts as the DHCPv6 server rather than the requesting
  router. Check with `grep CONFIG_NET_DHCPV6 build/zephyr/.config` and rebuild
  without the overlay (use `-p` -- the overlay is sticky across incremental
  builds) for the requesting-router role.
* **Delegation succeeds but no downstream address** -- the requesting-router
  build needs `CONFIG_NET_IPV6_ND_RA_TX` (to advertise) and, for WAN-to-LAN
  forwarding, `CONFIG_NET_IPV6_FORWARDING`. Ensure the host `zeth1` has
  `accept_ra=2` (set by `zeth-dhcpv6-pd.conf`).
* **Delegated prefix too small for multiple downstreams** -- the server
  delegates a `/56`, leaving room for many `/64` downstreams. If you shrink it
  to `/64`, only a single downstream can be served.
