# MeshCore Repeater — Packet Filter Engine

The filter engine allows fine-grained control over which packets a repeater forwards. Rules are evaluated in order — the first matching rule wins. If no rule matches, the default policy (`mode`) applies.

Rules survive reboot and are stored in `/filter_rules.bin`.

> **Note:** The current rule file format does not include a version field. If you upgrade from a version without AND condition support, delete `/filter_rules.bin` and re-enter your rules.

---

## Commands

All commands are prefixed with `filter`.

| Command | Description |
|---|---|
| `filter add <action> <field> <op> <value> [and <field> <op> <value>]` | Add a new rule |
| `filter del <id>` | Delete rule by ID |
| `filter disable <id>` | Temporarily disable a rule |
| `filter enable <id>` | Re-enable a disabled rule |
| `filter list` | List rules — page 0 |
| `filter list <page>` | List rules — specific page (0-indexed) |
| `filter clear` | Delete all rules |
| `filter mode <allow\|drop>` | Set default policy when no rule matches |

---

## Actions

| Token | Description |
|---|---|
| `drop` | Drop the packet — do not forward |
| `allow` | Allow the packet — forward it |

---

## Fields

| Token | Matches | Operators |
|---|---|---|
| `route` | Route type of the packet | `eq` `neq` |
| `payload` | Payload type of the packet | `eq` `neq` `gt` `lt` |
| `hops` | Number of hops the packet has travelled | `eq` `neq` `gt` `lt` |
| `pathsize` | Hash size per hop entry in path (1–3 bytes) | `eq` `neq` `gt` `lt` |
| `path` | Last hop repeater hash (OR-match against a list) | `eq` `neq` |
| `channel` | Channel hash byte (GRP_TXT and GRP_DATA only) | `eq` `neq` |
| `snr` | SNR of the received packet in whole dB | `eq` `neq` `gt` `lt` |
| `rssi` | RSSI of the received packet in dBm | `eq` `neq` `gt` `lt` |

---

## Operators

| Token | Meaning |
|---|---|
| `eq` | Equal to |
| `neq` | Not equal to |
| `gt` | Greater than |
| `lt` | Less than |

> `gt` and `lt` are not meaningful for `path` and are silently ignored.

---

## AND condition

A rule can have an optional AND condition. Both the primary and the AND condition must match for the rule to fire.

```
filter add <action> <field> <op> <value> and <field> <op> <value>
```

**Restrictions:**
- Maximum one AND condition per rule
- `path` is not supported as the AND field (it can still be the primary field)
- The AND field must be different from the primary field

---

## Values

### `route` values

| Token | Constant |
|---|---|
| `tflood` | `ROUTE_TYPE_TRANSPORT_FLOOD` |
| `flood` | `ROUTE_TYPE_FLOOD` |
| `direct` | `ROUTE_TYPE_DIRECT` |
| `tdirect` | `ROUTE_TYPE_TRANSPORT_DIRECT` |

### `payload` values

| Token | Constant | Description |
|---|---|---|
| `req` | `PAYLOAD_TYPE_REQ` | Request |
| `resp` | `PAYLOAD_TYPE_RESPONSE` | Response |
| `txt` | `PAYLOAD_TYPE_TXT_MSG` | Direct text message |
| `ack` | `PAYLOAD_TYPE_ACK` | Acknowledgement |
| `advert` | `PAYLOAD_TYPE_ADVERT` | Node advertisement |
| `grptxt` | `PAYLOAD_TYPE_GRP_TXT` | Group text message |
| `grpdata` | `PAYLOAD_TYPE_GRP_DATA` | Group data packet |
| `anonreq` | `PAYLOAD_TYPE_ANON_REQ` | Anonymous request |
| `path` | `PAYLOAD_TYPE_PATH` | Path packet |
| `trace` | `PAYLOAD_TYPE_TRACE` | Trace packet |
| `multi` | `PAYLOAD_TYPE_MULTIPART` | Multipart packet |
| `ctrl` | `PAYLOAD_TYPE_CONTROL` | Control/discovery packet |
| `raw` | `PAYLOAD_TYPE_RAW_CUSTOM` | Raw custom packet |

Numeric values (decimal or hex) are also accepted for `payload`, e.g. `5` or `0x05`.

### `path` values

One or more hex strings separated by spaces. Each hash must be the same length — 2, 4 or 6 hex characters (1, 2 or 3 bytes). The size must match the hash size your network is configured to use. The rule matches if the last hop in the packet path equals **any** of the listed hashes (OR logic).

> `path` cannot be used as an AND field. It can only be the primary field.

### `channel` values

A single hex byte with or without `0x` prefix, e.g. `0xAB` or `AB`. Only applies to `grptxt` and `grpdata` packets.

### `snr` values

Signed integer in whole dB, e.g. `-10` or `5`. Stored internally in quarter-dB units to match the radio driver.

### `rssi` values

Signed integer in dBm, e.g. `-110`.

---

## Default mode

When no rule matches, the default policy applies:

| Token | Behaviour |
|---|---|
| `allow` | Forward the packet *(default at first boot)* |
| `drop` | Drop the packet |

Use `filter mode drop` together with explicit `allow` rules to build a whitelist. Use `filter mode allow` (the default) with `drop` rules to build a blacklist.

---

## `filter list` output format

The number of rules per page is determined dynamically — longer rules (with AND) may result in fewer rules per page to stay within the packet size limit. If more pages exist, the last line shows the next command to run.

```
mode:allow rules:6/8 p1/2
0 drop payload eq grptxt
1 drop route eq tflood and hops gt 5
2* drop snr lt -10
-> filter list 1
```

```
mode:allow rules:6/8 p2/2
3 allow path eq AB 12
4 drop channel eq 0xAB and rssi lt -100
5 drop rssi lt -110
```

- The number at the start is the rule ID used with `del`, `disable`, and `enable`.
- A `*` after the ID means the rule is currently **disabled**.
- Page numbers are 0-indexed in the command, but displayed as 1-indexed in the header.

---

## Examples

### Drop all group text messages
```
filter add drop payload eq grptxt
```

### Drop all transport flood packets
```
filter add drop route eq tflood
```

### Drop packets that have travelled more than 5 hops
```
filter add drop hops gt 5
```

### Drop packets with poor SNR
```
filter add drop snr lt -10
```

### Drop packets with weak signal
```
filter add drop rssi lt -110
```

### Drop packets arriving via a specific repeater
```
filter add drop path eq AB
```

### Drop packets arriving via any of several repeaters
```
filter add drop path eq AB 12 CD
```

### Drop packets on a specific channel
```
filter add drop channel eq 0xAB
```

### Drop group messages on a specific channel only when more than 8 hops away
```
filter add drop channel eq 0x11 and hops gt 8
```

### Drop packets with poor SNR AND weak signal (both must be true)
```
filter add drop snr lt -10 and rssi lt -100
```

### Drop packets via a specific repeater only if they have travelled far
```
filter add drop path eq AB and hops gt 4
```

### Whitelist mode — only forward flood packets, drop everything else
```
filter mode drop
filter add allow route eq flood
```

### Whitelist mode — only forward direct text messages with good signal
```
filter mode drop
filter add allow payload eq txt and rssi gt -90
```

### Disable a rule temporarily without deleting it
```
filter disable 2
```

### Re-enable it
```
filter enable 2
```

### View all rules
```
filter list
```

### View second page
```
filter list 1
```

### Delete rule 1
```
filter del 1
```

### Delete all rules and reset to default policy
```
filter clear
filter mode allow
```

---

## Limits

| Parameter | Value |
|---|---|
| Maximum rules | 8 |
| Maximum AND conditions per rule | 1 |
| Maximum path hashes per rule | 4 |
| Maximum path hash size | 3 bytes (2, 4 or 6 hex chars — must match the network's configured hash size) |
| `path` as AND field | Not supported |