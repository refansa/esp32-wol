# esp32-wol

Wake a LAN machine from anywhere on your tailnet.

A small ESP32-S3 firmware that joins Wi-Fi, brings up a Tailscale tunnel with
[MicroLink v2](https://github.com/refansa/microlink), and exposes an HTTP
endpoint that sends a Wake-on-LAN magic packet. Hit it from your phone on the
tailnet and the desktop at home boots.

## Quick start

Built with ESP-IDF 6.1. Set the Wi-Fi credentials, the target MAC address, and
a Tailscale auth key under **Project config** in `idf.py menuconfig`, then:

```sh
idf.py set-target esp32s3
idf.py build flash monitor
```

The log prints the tailnet IP once the tunnel is up. Then:

```sh
curl http://<device-ip>:8000/wol      # wake the target
curl http://<device-ip>:8000/health   # liveness check
```

`<device-ip>` is either the LAN address or the Tailscale IP. The server listens
on both. Everything else (ports, peer limits, cellular, net switching) lives in
menuconfig.

## Notes

- The target needs Wake-on-LAN enabled in its BIOS and NIC. If
  `255.255.255.255` gets dropped, use the subnet broadcast instead, e.g.
  `192.168.1.255`.
- MicroLink expects PSRAM for its default 512 KB HTTP/2 and JSON buffers, and
  needs ChaCha20/ChaCha20-Poly1305 enabled in mbedTLS. On a board without
  PSRAM, lower those buffers under **MicroLink V2**.
- The endpoints are unauthenticated: anyone with LAN or tailnet access can wake
  the machine.
- Bad Wi-Fi credentials make the app give up after a few attempts instead of
  retrying forever. Once it has connected at least once, drops are retried
  indefinitely.

## Layout

`main/` is the application, `components/microlink` is the vendored Tailscale
client, and `components/wireguard_lwip` is its WireGuard stack.

## License

MIT. Vendored components keep their upstream licenses. See `LICENSE` and the
component directories. Not affiliated with Tailscale Inc.
