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

## Step 2 -- Start the host DHCPv6-PD server

On Debian/Ubuntu, `kea-dhcp6` ships with an *enforced* AppArmor profile that is
written for its systemd unit. Running it by hand aborts with `Permission denied`
when creating its PID/lockfile (e.g. `/run/kea/logger_lockfile`), **even as
root** and even if you redirect the paths, because AppArmor confines the binary.
Put the profile in complain mode first (needs the `apparmor-utils` package):

```console
sudo aa-complain /usr/sbin/kea-dhcp6
```

Then, in another terminal, run Kea in the foreground so you can watch the logs:

```console
sudo kea-dhcp6 -c kea-dhcp6-pd.conf
```

**Order matters:** Kea opens its DHCPv6 socket and joins the server multicast
group `ff02::1:2` on `zeth0` only once, at start-up. Always start Kea **after**
`zeth-dhcpv6-pd.conf` has brought `zeth0` up, and restart Kea if you recreate
the interfaces. You can confirm Kea is actually listening with:

```console
sudo ss -ulnp6 | grep 547            # expect a socket on [ff02::1:2]%zeth0:547
ip -6 maddr show dev zeth0 | grep ff02::1:2
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

## Step 3 -- Build and run the Zephyr requesting router

In your Zephyr tree:

```console
west build -b native_sim samples/net/dhcpv6_pd
west build -t run
```

The `native_sim` binary attaches to `zeth0` and `zeth1` automatically. On
start-up the sample logs the chosen upstream and downstream interfaces and
begins DHCPv6 on the upstream link.

## Verifying

**On the Kea server** you should see a Solicit/Advertise/Request/Reply
exchange, with a lease from the IA_NA pool and a delegated prefix from the
IA_PD pool logged.

**On the Zephyr shell** (in the `west build -t run` console):

```console
uart:~$ net dhcpv6
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
on `zeth0`, handing out IA_NA addresses and delegating IA_PD prefixes. You then
drive it with a DHCPv6 *client* on the host.

Because `native_sim` and its `zeth0` TAP are the two ends of the same link, the
host client and the Zephyr server talk directly over `zeth0` -- no bridge is
needed (that is only required when connecting two `native_sim` instances).

### Step 1 -- Create the interface

A single link is enough for server mode:

```console
sudo ./net-setup.sh -i zeth0 start
```

> **Note:** `zeth.conf` puts the static address `2001:db8::2/64` on
> `zeth0`, which overlaps the sample's default server pool (`2001:db8::`). For a
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

Use any host DHCPv6 client bound to `zeth0`:

```console
sudo dhclient -6 -d -v zeth0        # request an IA_NA address only
sudo dhclient -6 -P -d -v zeth0     # also request a delegated prefix (IA_PD)
```

or, with `odhcp6c` (handy for prefix delegation):

```console
sudo odhcp6c -v -P 64 zeth0
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
a single `zeth0` is simpler for iterating on the server role.

## Troubleshooting

* **Zephyr sends Solicit but Kea never replies** -- Kea is running but did not
  open its socket on `zeth0`, usually because it was started before the
  interface existed. Verify with `sudo ss -ulnp6 | grep 547` (expect a listener
  on `[ff02::1:2]%zeth0:547`) and `ip -6 maddr show dev zeth0 | grep ff02::1:2`.
  If missing, restart Kea after bringing the interfaces up. The
  `service-sockets-*` options in `kea-dhcp6-pd.conf` make this fail loudly
  rather than silently.
* **No Solicit reaches Kea** -- check the server is bound to `zeth0`
  (`interfaces-config` in `kea-dhcp6-pd.conf`) and that `zeth0` is up.
* **`net dhcpv6 server status` shows "no leases assigned"** -- the Zephyr
  server has no active bindings. It only tracks clients that completed a DHCPv6
  exchange with it, so a peer using SLAAC (Router Advertisements) will not show
  up. Run `sudo dhclient -6 -P -d -v zeth0` (or `dhcpcd -6`) on the host to
  create a lease, and confirm `zeth0` is up before starting the server so it can
  join the `ff02::1:2` multicast group.
* **Delegation succeeds but no downstream address** -- the requesting-router
  build needs `CONFIG_NET_IPV6_RA` (to advertise) and, for WAN-to-LAN
  forwarding, `CONFIG_NET_IPV6_FORWARDING`. Ensure the host `zeth1` has
  `accept_ra=2` (set by `zeth-dhcpv6-pd.conf`).
* **Delegated prefix too small for multiple downstreams** -- the server
  delegates a `/56`, leaving room for many `/64` downstreams. If you shrink it
  to `/64`, only a single downstream can be served.
