# Kael OS networking

Kael currently has a small, poll-driven IPv4 stack, an RTL8139 driver, and a
polling DMA-ring driver for the Marvell Yukon-II 88E8072 (`11ab:436c`). The
Yukon-II driver has not yet been verified on the ProBook hardware. The stack
can use DHCP and DNS, ping an address, and fetch **HTTP** pages as plain text. It does not support Wi-Fi,
HTTPS/TLS, JavaScript, images, or modern page layout.

## Try it in QEMU

```sh
make
make run-net
```

QEMU's user-mode network provides DHCP and DNS. In the Terminal window:

```text
netinfo
dhcp
ping 10.0.2.2
browse http://neverssl.com
```

The browser opens as a window. Use the arrow keys to scroll, `q` to close it,
and Tab to return to the Terminal.

## Real hardware

The driver supports PCI RTL8139 adapters (including QEMU) and the Marvell
Yukon-II 88E8072 wired Ethernet controller (`11ab:436c`). It does not drive the
computer's Wi-Fi card or USB network adapters. On the ProBook, connect a wired
Ethernet cable to a network that provides DHCP, then run `netinfo`, `dhcp`,
`ping <gateway>`, and `browse http://neverssl.com`. This is an initial bare-metal
driver implementation and has not yet been hardware-tested. Static IPv4
settings can be entered with:

```text
ipconfig <ip> <mask> <gateway> [dns]
```

HTTP is unencrypted and sends page requests in the clear. Sites that redirect
to HTTPS cannot be loaded.
