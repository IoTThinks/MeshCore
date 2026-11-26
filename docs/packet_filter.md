<!-- PowerSaving variant -->
# MeshCore Repeater — Packet Filter Engine

The filter engine allows fine-grained control over which packets a repeater forwards. Rules are evaluated in order — the first matching rule wins. If no rule matches, the default policy (`mode`) applies.

The engine supports two independent profiles. Only the active profile evaluates packets. Profiles and the active selection survive reboot.

Rules are stored in `/filter_rules.json`. If the format changes between firmware versions, the file is automatically discarded and rules start fresh. A legacy `/filter_rules.bin` file is migrated automatically on first boot and then deleted.

---

## Build flag

The filter is compiled in with `-D PACKET_FILTER=1`, which is set in `[arduino_base]` in the main `platformio.ini`.

To leave it out completely (no code, no RAM, no `filter` CLI command), add `-D PACKET_FILTER=0` to the `build_flags` of a board or env. The later `-D` wins. A build where `PACKET_FILTER` is not defined at all also leaves the filter out.

---

## Filter Builder

`packet_filter_builder.html` is a single-file web page that builds the rules from menus and outputs the `filter` commands ready to paste. Open it in a browser; no install or server is needed. It enforces the limits listed below, can save/open setups as JSON, calculates channel hashes from a `#name`, and (in Chrome or Edge) can read the current rules from a repeater over USB.

---

## Profiles

```
filter set profile 1    → switch active profile to 1 (default at first boot)
filter set profile 2    → switch active profile to 2
filter profile          → show active profile number
```

A typical use is profile 1 as a normal blacklist and profile 2 as a lockdown whitelist.

---

## Commands

All commands are prefixed with `filter`. Commands operate on the active profile by default. Prefix with `1` or `2` to target a specific profile.

| Command | Description |
|---|---|
| `filter add <action> <field> [<op> <value>] [and ...]` | Add rule to active profile |
| `filter 1 add ...` / `filter 2 add ...` | Add rule to specific profile |
| `filter del <id>` | Delete rule by ID |
| `filter disable <id>` | Temporarily disable a rule |
| `filter enable <id>` | Re-enable a disabled rule |
| `filter list [<page>]` | List rules — page 0 by default |
| `filter 1 list` / `filter 2 list` | List rules in specific profile |
| `filter clear` | Delete all rules in active profile |
| `filter mode <allow\|drop>` | Set default policy for active profile |
| `filter 1 mode drop` / `filter 2 mode allow` | Set mode for specific profile |
| `filter set profile <1\|2>` | Switch active profile |
| `filter profile` | Show active profile number |

> Rules are added **disabled** by default. Use `filter enable <id>` to activate them.

---

## Actions

| Token | Description |
|---|---|
| `drop` | Drop the packet — do not forward |
| `allow` | Allow the packet — forward it |

---

## Fields

| Token | Matches | Operators | Notes |
|---|---|---|---|
| `route` | Route type | `eq` `neq` | |
| `payload` | Payload type | `eq` `neq` `gt` `lt` | |
| `hops` | Number of hops | `eq` `neq` `gt` `lt` | |
| `pathsize` | Path hash size in bytes (1–3) | `eq` `neq` `gt` `lt` | |
| `path` | Last hop repeater hash | `eq` `neq` | OR-list; primary field only |
| `channel` | Channel hash byte | `eq` `neq` | OR-list; GRP_TXT/GRP_DATA only; primary field only |
| `snr` | SNR in whole dB | `eq` `neq` `gt` `lt` | Prefer over `rssi` for flood packets |
| `rssi` | RSSI in dBm | `eq` `neq` `gt` `lt` | May be inaccurate for delayed flood packets |
| `repeater` | Management payload types | *(no op/value)* | Primary field only; see below |
| `scope` | Named region (transport scope) | *(no op)* | Case-sensitive name |

---

## Operators

| Token | Meaning |
|---|---|
| `eq` | Equal to |
| `neq` | Not equal to |
| `gt` | Greater than |
| `lt` | Less than |

---

## AND conditions

A rule can combine multiple conditions with `and`. All conditions must match for the rule to fire.

```
filter add <action> <field> [<op> <value>] [and <field> <op> <value>] [and source <hash> ...] [and scope <name>]
```

**Available AND conditions:**

| AND keyword | Description |
|---|---|
| `and <field> <op> <value>` | Scalar field — maximum one per rule |
| `and source <hash> [...]` | OR-list of 1-byte sender hashes from `payload[1]` |
| `and scope <name>` | Named region — packet must belong to this scope |

**Restrictions:**
- `path`, `channel`, `repeater` and `scope` cannot be used as scalar AND fields
- The scalar AND field must differ from the primary field
- `source` and `scope` can be combined freely with any primary field and with each other

**`source` check coverage:**
Checks `payload[1]` as src_hash for: `req`, `resp`, `txt`, `anonreq`, `path`. Auto-passes for every other payload type, including `advert`, `ack`, `ctrl`, `grptxt` and `grpdata` (no src_hash in payload).

---

## `repeater` field

Matches all management payload types needed to maintain control over the repeater:
`advert`, `txt`, `req`, `resp`, `ack`, `anonreq`, `path`, `ctrl`

No operator or value — just `repeater`. Primarily useful in `drop` mode.

```
filter add allow repeater                             ← allow all management traffic
filter add allow repeater and source 0x6D             ← only from sender 0x6D
filter add allow repeater and source 0xAA 0xBB 0xCC  ← OR-list of senders
filter add allow repeater and scope myregion          ← only within a scope
filter add allow repeater and source 0x6D and scope myregion
```

---

## `scope` field

Matches packets whose transport scope matches a named region in the repeater's region map. Region names are **case-sensitive**.

```
filter add allow scope myregion          ← primary field
filter add allow payload eq grptxt and scope myregion  ← scope as AND condition
```

Only matches scoped packets (`tflood`/`tdirect`). Direct packets (`direct`/`flood`) have no transport scope and auto-pass the scope AND condition.

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
| `txt` | `PAYLOAD_TYPE_TXT_MSG` | Direct text / remote CLI command |
| `ack` | `PAYLOAD_TYPE_ACK` | Acknowledgement |
| `advert` | `PAYLOAD_TYPE_ADVERT` | Node advertisement |
| `grptxt` | `PAYLOAD_TYPE_GRP_TXT` | Group text message |
| `grpdata` | `PAYLOAD_TYPE_GRP_DATA` | Group data packet |
| `anonreq` | `PAYLOAD_TYPE_ANON_REQ` | Anonymous request / login |
| `path` | `PAYLOAD_TYPE_PATH` | Path packet |
| `trace` | `PAYLOAD_TYPE_TRACE` | Trace packet |
| `multi` | `PAYLOAD_TYPE_MULTIPART` | Multipart packet |
| `ctrl` | `PAYLOAD_TYPE_CONTROL` | Control/discovery packet |
| `raw` | `PAYLOAD_TYPE_RAW_CUSTOM` | Raw custom packet |

Numeric values are also accepted, e.g. `5` or `0x05`.

### `path` values

OR-list of hex strings. Hash size is auto-detected from character count:

| Hex chars | Bytes | Example |
|---|---|---|
| 2 | 1 | `AB` |
| 4 | 2 | `ABBA` |
| 6 | 3 | `ABBA11` |

All hashes in a rule must be the same size. The size must match the network's hash size.

```
filter add drop path eq AB              ← single 1-byte hash
filter add drop path eq AB CD EF        ← any of three 1-byte hashes
filter add drop path eq ABBA CDCD       ← any of two 2-byte hashes
```

### `channel` values

OR-list of hex bytes, with or without `0x` prefix. Only applies to `grptxt` and `grpdata`.

```
filter add drop channel eq 0xAB
filter add drop channel eq 0x81 0x11 0x22
```

### `source` values

OR-list of 1-byte hex hashes (sender hash from `payload[1]`). Up to 4 entries.

```
filter add allow repeater and source 0x6D
filter add allow repeater and source 0xAA 0xBB 0xCC 0xDD
```

### `snr` values

Signed integer in whole dB, e.g. `-10` or `5`.

> Prefer `snr` over `rssi` for flood packet rules — RSSI may be inaccurate for packets that have been queued in the inbound buffer.

### `rssi` values

Signed integer in dBm, e.g. `-110`. Reliable for direct packets; may be stale for flood packets.

---

## Default mode

Each profile has its own default policy applied when no rule matches:

| Token | Behaviour |
|---|---|
| `allow` | Forward the packet *(default at first boot)* |
| `drop` | Drop the packet |

---

## `filter list` output

Page size adapts dynamically. The header shows the profile number and `*` if active.

```
p1* mode:allow rules:3/8
0 drop payload eq grptxt
1 drop route eq tflood and hops gt 5
2* drop snr lt -10
```

```
p2 mode:drop rules:6/8 pg1/2
0 allow repeater and source 0x6D
1 allow scope myregion
-> filter 2 list 1
```

- `p1*` — profile 1, currently active
- `*` after rule ID — rule is disabled
- Pages are 0-indexed in commands, 1-indexed in output

---

## Examples

### Block group messages from 1-byte-path nodes (keeps DMs)
```
filter add drop pathsize eq 1 and payload eq grptxt
filter add drop pathsize eq 1 and payload eq grpdata
```

### Block direct messages from a specific sender
```
filter add drop payload eq txt and source 0xBB
```

> `source` cannot single out a sender of group messages: `grptxt`/`grpdata` carry no readable sender hash, so the condition passes for all of them.

### Allow a private channel through a lockdown
```
filter add allow channel eq 0xBB
```

### Lockdown profile — allow only management traffic from known sender
```
filter 2 mode drop
filter 2 add allow repeater and source 0x6D
filter 2 add allow scope myregion
filter 2 enable 0
filter 2 enable 1
filter set profile 2
```

Return to normal:
```
filter set profile 1
```

### Write to a specific profile without switching
```
filter 2 add drop payload eq grptxt
filter 2 mode drop
filter 2 list
filter 2 clear
```

### Combined AND conditions
```
filter add drop channel eq 0x11 0x22 and hops gt 8
filter add drop snr lt -10 and rssi lt -100
filter add allow repeater and source 0xAB 0xCD and scope myregion
```

---

## JSON file format

```json
{ap:1,pa:{mode:0,ra:{...},...},pb:{mode:1,ra:{...},...}}
```

| Key | Description |
|---|---|
| `ap` | Active profile (0 = profile 1, 1 = profile 2) |
| `pa` / `pb` | Profile 1 / Profile 2 |
| `mode` | Default policy (0=allow, 1=drop) |
| `ra`..`rh` | Rules 0–7 |

Per rule keys:

| Key | Field |
|---|---|
| `act` | Action (0=drop, 1=allow) |
| `fld` | Field (0=route 1=payload 2=hops 3=pathsize 4=path 5=channel 6=snr 7=rssi 8=repeater 9=scope) |
| `op` | Operator (0=eq 1=neq 2=gt 3=lt) |
| `val` | Scalar value |
| `af` / `aop` / `av` | Scalar AND field / operator / value |
| `ch` / `ch_n` | Channel hashes (4-byte blob) / count |
| `path` / `pl` / `pn` | Path hashes (12-byte blob) / hash length / count |
| `src` / `src_n` | Source hashes (4-byte blob) / count |
| `scn` | Scope name (string, max 15 chars, case-sensitive) |
| `en` / `use` | Enabled / in use (true/false) |

---

## Limits

| Parameter | Value |
|---|---|
| Profiles | 2 |
| Maximum rules per profile | 8 |
| Maximum scalar AND conditions per rule | 1 |
| Maximum path hashes per rule | 4 |
| Maximum channel hashes per rule | 4 |
| Maximum source hashes per AND condition | 4 |
| Path hash size | 1, 2 or 3 bytes — all hashes in a rule must be the same size |
| Scope name length | Max 15 characters, case-sensitive |
| `path` / `channel` / `repeater` / `scope` as scalar AND field | Not supported |
