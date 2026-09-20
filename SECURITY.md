# Security

## What this device is

A hobby ESP32 board on a home LAN. Keep that threat model in mind:

- **The setup page is unauthenticated.** Plain HTTP on port 80, no password.
  Anyone who can reach the board on your network can change its stops, switch
  its theme, or reboot it. This is a deliberate trade for a device with no
  keyboard: it holds no credentials and stores nothing sensitive. Don't put it
  on an untrusted or guest network, and don't forward a port to it.
- **WiFi credentials and the AT API key are compiled into the firmware**, from
  `src/secrets.h`. Anyone with physical access and a USB cable can read the
  flash. Don't hand a flashed board to someone you wouldn't give the key to,
  and use a separate key you can revoke.
- **Outbound TLS is pinned** to DigiCert Global Root G2, the root of
  `api.at.govt.nz`'s chain, rather than trusting a bundle — see `src/at_ca.h`.
- `src/secrets.h` is gitignored and has never been committed. Keep it that way.

## Reporting a vulnerability

Something worse than the above — remote code execution, a way to read the key
off the device over the network, a parser that can be crashed by a hostile HTTP
response — is worth reporting privately.

Use GitHub's **Report a vulnerability** button under this repository's Security
tab, which opens a private advisory. Please don't open a public issue for it.

Expect a reply in a week or two; this is a side project, not a product. There's
no bounty, and no formal support commitment.

## Supported versions

The tip of the default branch. There are no maintained release branches.
