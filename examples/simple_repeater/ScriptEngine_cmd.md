# MeshCore Repeater Automated Messages

This allows a repeater node to automatically send messages to a channel based on a condition (trigger) or on a fixed time interval. Rules are stored in SPIFFS and survive reboots. All commands are sent via the standard CLI interface.

---

## Command Reference

### `report add`

Adds a new rule. Rules are evaluated every 60 seconds.

```
report add <trigger> /<interval> @<channel> [at:HH:MM] [@scope:<region>] [message]
```

| Field | Description                                                                                                 |
|---|-------------------------------------------------------------------------------------------------------------|
| `<trigger>` | What causes the report to fire — see [Triggers](#triggers)                                                    |
| `/<interval>` | Minimum seconds between sends — minimum is `1800` (30 min)                                                  |
| `@<channel>` | Destination channel — see [Channels](#channels)                                                             |
| `at:HH:MM` | Optional time of day to fire (UTC) — see [Time of Day](#time-of-day)                                        |
| `@scope:<region>` | Optional region scope name (e.g. `diag`)                                                                    |
| `[message]` | Optional message template — see [Template Tokens](#template-tokens). Defaults to `{value}{unit}` if omitted |

The `at:` and `@scope:` tokens are optional and can appear in any order after the channel.

---

### `report list`

Lists all configured reports with their index, state, and parameters.

```
report list
```

Example output:
```
1:on report:bat/3600s at:08:00 @hash:alertchan "battery {value}{unit}"
2:on bat<3400/1800s @private:b8da... "battery low {value}{unit}"
```

---

### `report del <n>`

Deletes the report at index `n` (1-based). Subsequent reports are renumbered.

```
report del 1
```

---

### `report clear`

Deletes all reports.

```
report clear
```

---

### `report test <n>`

Forces report `n` to fire immediately, regardless of trigger condition, interval, or `at:` time. Useful for verifying channel setup and message formatting.

```
report test 1
```

---

### `report enable <n>` / `report disable <n>`

Enables or disables report `n` without deleting it.

```
report enable 2
report disable 2
```

---

## Triggers

### Condition triggers

Fire only when the measured value crosses the threshold.

| Trigger    | Description                     | Unit |
|------------|---------------------------------|---|
| `bat<XXXX` | Battery voltage below threshold | mV |
| `bat>XXXX` | Battery voltage above threshold | mV |
| `temp<XX`  | Temperature below threshold     | °C (integer) |
| `temp>XX`  | Temperature above threshold     | °C (integer) |
| `noise>XX` | Noise floor above threshold     | dBm (negative integer) |
| `noise<XX` | Noise floor below threshold     | dBm (negative integer) |

Examples:
```
bat<3400      fires if battery is below 3400 mV
bat>4250      fires if battery is above 4250 mV
temp<-10      fires if temperature is below -10°C
temp>60       fires if temperature is above 60°C
noise>-90     fires if noise floor is above -90 dBm
noise<-80     fires if noise floor is below -80 dBm
```

### Periodic triggers

Fire unconditionally on every interval, regardless of value.

| Trigger | Description |
|---|---|
| `report:bat` | Report current battery voltage |
| `report:temp` | Report current temperature |
| `report:noise` | Report current noise floor |

Examples:
```
report:bat      send battery voltage every interval
report:temp     send temperature every interval
report:noise    send noise floor every interval
```

---

## Time of Day

The optional `at:HH:MM` token restricts a report to fire only at a specific time of day. Without it, the report fires whenever the trigger condition is met and the interval has elapsed.

```
at:08:00      fire at 08:00 UTC
at:23:30      fire at 23:30 UTC
```

> **Note:** All times are UTC. Account for your local timezone offset when setting the time.

When using `at:`, set the interval to `86400` (24 hours) to fire once per day:

```
report add report:bat /86400 at:08:00 @hash:alertchan "Battery {value}{unit}"
```

The report engine evaluates every 60 seconds, so the fire window is within ±1 minute of the specified time.

`at:` can be combined with condition triggers to create a daily check at a specific time:

```
report add bat<3400 /86400 at:06:00 @hash:alertchan "battery low {value}{unit}"
```

---

## Channels

Two channel types are supported.

### Hash channel (`@hash:name`)

Sends to a public hashtag channel. The channel name must be entered **without** the `#` prefix — the `#` is added internally when deriving the channel key.

```
@hash:alertchan      sends to #alertchan
@hash:open_diag      sends to #open_diag
```

### Private channel (`@private:hexkey`)

Sends to a key-protected private channel. The key is a 32-character hex string (16 raw bytes), as shown in the companion app channel settings.

```
@private:b8fe52900c881c97afe2ca8327681911
```

Since CLI commands are encrypted peer-to-peer, the key can be safely sent over LoRa via the CLI.

---

## Scope (optional)

If a region scope is specified, the packet is sent with a transport code matching that region. If omitted, `default_scope` is used as-is (which may be null, resulting in a plain unscoped flood).

```
@scope:diag
@scope:world
```

The scope name must match a region name configured in the node's region map.

---

## Template Tokens

The message field supports the following tokens, which are replaced at send time:

| Token | Replaced with | Example |
|---|---|---|
| `{value}` | The measured value as an integer | `3742` or `-103` |
| `{unit}` | The unit for the measured value | `mV`, `C`, or `dBm` |

Temperature is always reported in °C.

Tokens can be combined freely. If no message is provided, the default template `{value}{unit}` is used.

Example messages:
```
"{value}{unit}"               →  "3742mV"
"battery low {value}{unit}"   →  "battery low 3312mV"
"Noise floor: {value}{unit}"  →  "Noise floor: -103dBm"
"Temp alert: {value}{unit}"   →  "Temp alert: 62C"
```

---

## Limits

| Parameter | Value |
|---|---|
| Maximum reports | 8 |
| Minimum interval | 1800 seconds (30 minutes) |
| Message template length | 64 characters |
| Channel name / key length | 36 characters |
| Scope name length | 32 characters |

---

## Examples

```
# Report battery every hour on #alertchan
report add report:bat /3600 @hash:alertchan "battery {value}{unit}"

# Report battery every day at 08:00 UTC on #alertchan
report add report:bat /86400 at:08:00 @hash:alertchan "battery {value}{unit}"

# Report noise floor every day at 06:00 UTC with region scope
report add report:noise /86400 at:06:00 @hash:alertchan @scope:diag "noise floor {value}{unit}"

# Alert if battery drops below 3.4V (check every 30 min)
report add bat<3400 /1800 @hash:alertchan "battery low {value}{unit}"

# Daily battery check at 07:00 UTC — only alert if low
report add bat<3400 /86400 at:07:00 @hash:alertchan "battery low {value}{unit}"

# Alert if temperature exceeds 60°C
report add temp>60 /1800 @hash:alertchan "temp high {value}{unit}"

# Report noise floor every 2 hours on a private channel
report add report:noise /7200 @private:b8fe52900c881c97afe2ca8327681911 "noise floor {value}{unit}"

# List all reports
report list

# Force report 1 to fire immediately for testing
report test 1

# Disable report 2 temporarily
report disable 2

# Delete report 1
report del 1
```