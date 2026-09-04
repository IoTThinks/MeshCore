# CLI Commands

This document provides an overview of CLI commands that can be sent to MeshCore Repeaters, Room Servers and Sensors.

## Navigation

- [Operational](#operational)
- [Neighbors](#neighbors-repeater-only)
- [Statistics](#statistics)
- [Logging](#logging)
- [Information](#info)
- [Configuration](#configuration)
  - [Radio](#radio)
  - [System](#system)
  - [Routing](#routing)
  - [ACL](#acl)
  - [Region Management](#region-management-v110)
    - [Region Examples](#region-examples)
  - [GPS](#gps-when-gps-support-is-compiled-in)
  - [Sensors](#sensors-when-sensor-support-is-compiled-in)
  - [Bridge](#bridge-when-bridge-support-is-compiled-in)
  - [Ethernet](#ethernet-when-ethernet-support-is-compiled-in)

---

## Operational

### Reboot the node
**Usage:** 
- `reboot`

**Note:** No reply is sent.

---

### Power-off the node
**Usage:**
- `poweroff`, or
- `shutdown`

**Note:** No reply is sent.

---

### Reset the clock and reboot
**Usage:**
- `clkreboot`

**Note:** No reply is sent.

---

### Sync the clock with the remote device
**Usage:** 
- `clock sync`

---

### Display current time in UTC
**Usage:**
- `clock`

---

### Set the time to a specific timestamp
**Usage:** 
- `time <epoch_seconds>`

**Parameters:**
- `epoch_seconds`: Unix epoch time

---

### Send a flood advert
**Usage:** 
- `advert`

---

### Send a zero-hop advert
**Usage:**
- `advert.zerohop`

---

### Start an Over-The-Air (OTA) firmware update
**Usage:**
- `start ota`

---

### Erase/Factory Reset
**Usage:**
- `erase`

**Serial Only:** Yes

**Warning:** _**This is destructive!**_

---

## Neighbors (Repeater Only)

### List nearby neighbors
**Usage:** 
- `neighbors`

**Note:** The output of this command is limited to the 8 most recent adverts.

**Note:** Each line is encoded as `{pubkey-prefix}:{timestamp}:{snr*4}`

---

### Remove a neighbor
**Usage:** 
- `neighbor.remove <pubkey_prefix>`

**Parameters:** 
- `pubkey_prefix`: The public key of the node to remove from the neighbors list. This can be a short prefix or the full key. All neighbors matching the provided prefix will be removed.

**Note:** You can remove all neighbors by sending a space character as the prefix. The space indicates an empty prefix, which matches all existing neighbors.

---

### Discover zero hop neighbors

**Usage:** 
- `discover.neighbors`

---

## Statistics

### Clear Stats
**Usage:** `clear stats`

---

### System Stats - Battery, Uptime, Queue Length and Debug Flags
**Usage:** 
- `stats-core`

**Serial Only:** Yes

---

### Radio Stats - Noise floor, Last RSSI/SNR, Airtime, Receive errors
**Usage:** `stats-radio`

**Serial Only:** Yes

---

### Packet stats - Packet counters: Received, Sent
**Usage:** `stats-packets`

**Serial Only:** Yes

---

## Logging

### Begin capture of rx log to node storage
**Usage:** `log start`

---

### End capture of rx log to node storage
**Usage:** `log stop`

---

### Erase captured log
**Usage:** `log erase`

---

### Print the captured log to the serial terminal
**Usage:** `log`

**Serial Only:** Yes

---

## Info

### Get the Version
**Usage:** `ver`

---

### Show the hardware name
**Usage:** `board`

---

## Configuration

### Radio

#### View or change this node's radio parameters
**Usage:**
- `get radio`
- `set radio <freq>,<bw>,<sf>,<cr>`

**Parameters:**
- `freq`: Frequency in MHz
- `bw`: Bandwidth in kHz
- `sf`: Spreading factor (5-12)
- `cr`: Coding rate (5-8)

**Set by build flag:** `LORA_FREQ`, `LORA_BW`, `LORA_SF`, `LORA_CR`

**Default:** `869.525,250,11,5`

**Note:** Requires reboot to apply

---

#### View or change this node's transmit power
**Usage:**
- `get tx`
- `set tx <dbm>`

**Parameters:**
- `dbm`: Power level in dBm (1-22)

**Set by build flag:** `LORA_TX_POWER`

**Default:** Varies by board

**Notes:** This setting only controls the power level of the LoRa chip. Some nodes have an additional power amplifier stage which increases the total output. Refer to the node's manual for the correct setting to use. **Setting a value too high may violate the laws in your country.**

---

#### Change the radio parameters for a set duration
**Usage:** 
- `tempradio <freq>,<bw>,<sf>,<cr>,<timeout_mins>`

**Parameters:**
- `freq`: Frequency in MHz (300-2500)
- `bw`: Bandwidth in kHz (7.8-500)
- `sf`: Spreading factor (5-12)
- `cr`: Coding rate (5-8)
- `timeout_mins`: Duration in minutes (must be > 0)

**Note:** This is not saved to preferences and will clear on reboot

---

#### View or change this node's frequency
**Usage:**
- `get freq`
- `set freq <frequency>`

**Parameters:**
- `frequency`: Frequency in MHz

**Default:** `869.525`

**Note:** Requires reboot to apply
**Serial Only:** `set freq <frequency>`

---

#### View or change this node's rx boosted gain mode (SX12xx and LR1110, v1.14.1+)
**Usage:**
- `get radio.rxgain`
- `set radio.rxgain <state>`

**Parameters:**
  - `state`: `on`|`off`

**Default:** `on`

**Temporary Note:** If you upgraded from an older version to 1.14.1 without erasing flash, this setting is `off` because of [#2118](https://github.com/meshcore-dev/MeshCore/issues/2118)

---

#### View or change RX duty-cycle power saving
**Usage:**
- `get radio.rxps`
- `set radio.rxps off`
- `set radio.rxps on`
- `set radio.rxps conservative`
- `set radio.rxps balanced`
- `set radio.rxps max`
- `set radio.rxps max preamble <16|32>`
- `set radio.rxps overdrive`
- `set radio.rxps overdrive preamble <16|32>`
- `set radio.rxps riskyWorkingMax`
- `set radio.rxps riskyWorkingMax preamble <16|32>`
- `set radio.rxps <1-10>`
- `set radio.rxps level <1-10>`
- `set radio.rxps level <1-10> preamble <16|32>`
- `set radio.rxps <rx_us> <sleep_us>`

**Parameters:**
- `rx_us`, `sleep_us`: Receive and sleep durations in microseconds (`1000`-`30000000`).
- `level`: `1`-`8`, expressed as **symbols of the sender's preamble the receiver catches** - how much of it lands inside an open RX window. Level `1` catches the most (15 symbols on the 16-symbol profile, 24 on the 32-symbol one) and costs the most power; level `8` catches 8 on both profiles and costs the least. Eight is the floor because that is what an LR11x0 needs to latch, so a level means the same geometry on either radio family. `9` (`overdrive`) and `10` (`riskyWorkingMax`) go below that floor and outside the datasheet timer condition; see below.
- `preamble`: LoRa preamble length in symbols; `16` or `32`.
- `state`: `on` or `off`.

**Notes:**
- `on` and `conservative` select level `3` (catches 13 symbols), `balanced` selects level `6` (catches 10), and `max` selects level `8` (catches 8); all three use a 16-symbol preamble.
- Levels `1`-`8` satisfy the SX1261/2 duty-cycle timer condition; `max` is the top of that range. `overdrive` (level `9`) and `riskyWorkingMax` (level `10`) trade that guarantee for lower measured duty cycles; see the fuller entries below.
- Like the other named profiles, `max`, `overdrive`, and `riskyWorkingMax` assume a 16-symbol sender preamble. At SF5-SF8 that is markedly less economical than the equivalent numeric level, which follows the SF onto the 32-symbol profile. Use the explicit `preamble 32` form to select the 32-symbol profile.
- Level-based settings automatically recalculate their timings when the spreading factor or bandwidth changes. Custom `<rx_us> <sleep_us>` timings remain fixed.
- The selected mode is applied immediately, persisted, and restored after reboot.

---

#### View or change the LoRa FEM receive-path gain state on supported boards
**Usage:**
- `get radio.fem.rxgain`
- `set radio.fem.rxgain <state>`

**Parameters:**
- `state`: `on`|`off`

**Notes:**
- This controls the external LoRa FEM receive-path LNA where the board supports it.
- This is separate from `radio.rxgain`, which controls the radio chip receive gain mode.

---

#### View or change the LoRa FEM transmit-path gain state on supported boards
**Usage:**
- `get radio.fem.txgain`
- `set radio.fem.txgain <state>`

**Parameters:**
- `state`: `on`|`off`

**Notes:**
- This controls a software-selectable external LoRa FEM transmit gain where the board supports it.
- On Station G3, remove the PA PL1 jumper to allow software control. `on` selects PA PL1 high/short and `off` selects PA PL1 low/open. The PA PL2 hardware jumper determines whether this switches between power levels 1/3 or 2/4.
- Select an operating level and SX1262 transmit power that comply with local RF limits and the Station G3 power-supply requirements.
- The setting is saved immediately, but on Station G3 the level is applied to the hardware at the start of the next transmit, so that the PA supply rail is never re-targeted while the PA is being driven. `get` reports the configured state, which may lead the hardware until the node next transmits.

---

#### View or change RX power saving
**Usage:**
- `get radio.rxps`
- `set radio.rxps off`
- `set radio.rxps on`
- `set radio.rxps conservative`
- `set radio.rxps balanced`
- `set radio.rxps max`
- `set radio.rxps max preamble <symbols>`
- `set radio.rxps overdrive`
- `set radio.rxps overdrive preamble <symbols>`
- `set radio.rxps riskyWorkingMax`
- `set radio.rxps riskyWorkingMax preamble <symbols>`
- `set radio.rxps <level>`
- `set radio.rxps level <level>`
- `set radio.rxps level <level> preamble <symbols>`
- `set radio.rxps <rx_us> <sleep_us>`

**Parameters:**
- `level`: `1-10`. Levels `1`-`8` are the guarded scale and are expressed in **symbols of the sender's preamble caught**: 15, 14, 13, 12, 11, 10, 9, 8 on the 16-symbol profile and 24, 20, 16, 14, 12, 10, 9, 8 on the 32-symbol one. Level `8` is named `max`. `9` (`overdrive`) and `10` (`riskyWorkingMax`) drop below the 8-symbol floor and outside the datasheet timer condition; only `10` is expected to lose packets.
- `symbols`: `16` or `32` preamble symbols.
- `rx_us`: receive-window duration in microseconds, `1000-30000000`.
- `sleep_us`: radio sleep duration in microseconds, `1000-30000000`.

**Repeater default:** `off`

**Profiles:**
- `on` and `conservative`: level 3, catches 13 symbols, with a 16-symbol preamble.
- `balanced`: level 6, catches 10 symbols, with a 16-symbol preamble.
- `max`: level 8, catches the 8-symbol floor, the cheapest guarded setting that still works on both radio families, with a 16-symbol preamble.
- `overdrive`: level 9, with a 16-symbol preamble. Outside the datasheet timer condition; see below.
- `riskyWorkingMax`: level 10, with a 16-symbol preamble. The measured edge of what still works, and the only setting here that is expected to drop packets; see below.
- Every named profile assumes a 16-symbol sender, which is the worst case a mixed network can present. At SF5-SF8 this is less economical than a numeric level, because a 32-symbol profile cannot catch a 16-symbol preamble. `max preamble 32`, `overdrive preamble 32`, and `riskyWorkingMax preamble 32` select the 32-symbol profile explicitly.
- A numeric level, or `level <level>`, automatically uses 32 preamble symbols for SF5-SF8 and 16 for SF9-SF12.
- `level <level> preamble <symbols>` explicitly fixes the preamble used in the calculation.
- Explicit `rx_us sleep_us` values select manual timing (`level=0`).

**How the timings are derived:**

```text
sleep  = (preamble - caught symbols) symbols
listen = whatever the duty-cycle timer condition needs at that sleep
```

The capture cost - 6 symbols on SX126x, 8 on LR11x0, both measured on the bench -
no longer enters this arithmetic; it is only a validation, since a level that
catches fewer symbols than the radio needs to latch is rejected. That is why the
same level produces the same periods on either family. The listen window is not
a free parameter:
it follows from the timer condition below. Three constraints are applied on top,
all of them measured rather than assumed:

- **Sleep floor.** The driver subtracts the sleep-to-RX transition (`tcxoDelay + 1000 us`) from the requested sleep before writing the register. Below that the arm call fails outright; just above it the SX1262 arms with no error and then detects no preambles at all. Levels whose own sleep falls under the floor are raised to it, and collapse onto the same timing as a result. If even the floor would break preamble capture - a short symbol leaves no room between the two - the level is rejected with `ERROR: RXPS does not fit this SF/BW with preamble <n>` rather than silently falling back to continuous RX.
- **Timer condition.** Semtech requires `Tpreamble + Theader <= 2 * rxPeriod + sleepPeriod`, because the radio restarts its receive timer with that value when it detects a preamble. Guarded levels `1`-`8` satisfy it; `overdrive` and `riskyWorkingMax` deliberately do not. When it is broken, an SX1262 usually still receives normally, but isolated `rxPeriod` register values one tick wide lose most of the packets they have already latched, and nothing in the value predicts which. LR11x0 has always enforced an equivalent rule in its driver.
- **Register granularity.** Both radio families program the duty cycle in 15.625 us ticks and truncate on the way in, so the reported periods are snapped to whole ticks and are the ones the hardware actually runs.

**`overdrive` (level 9):**

`overdrive` is the geometry that predates the timer condition being enforced: an
8-symbol receive window with the sleep at the capture limit. It breaks the
condition deliberately, in exchange for the lowest duty cycle available. The
name is meant in the overclocking sense - measured to work, outside the vendor's
stated envelope - rather than as a warning that it drops packets. It does not.

| Assumed sender preamble | `overdrive` | Level 10 | Saving |
|---|---|---|---|
| 32 symbols | 23.5% | 27.5% (SF8) - 33.4% (SF6) | 4.0 - 9.9 pp |
| 16 symbols | 44.4% | 49.7% (SF8) - 56.6% (SF6) | 5.3 - 12.2 pp |

Both figures are dimensionless in symbols, so they hold at any spreading factor.
The sleep floor and tick rounding still apply, and the receive window is moved
one tick if it lands on a register value measured to misbehave. That list of
values is not exhaustive, which is the whole of the residual risk: levels
`1`-`8` are safe on any register value, `overdrive` is safe on every value that
has been measured. It is a text-CLI setting only and is never selected by
companion firmware.

**`riskyWorkingMax` (level 10):**

The end of the road: the profile extrapolated past `overdrive` until packet
delivery started to fall, then stepped back to the last setting that still held. The
sleep goes past the capture budget on purpose, so a preamble sometimes arrives
while the radio is asleep - which is exactly why delivery falls. Measured at SF8
with an LR1110 receiver witnessing every transmission on the same link:

| Assumed sender preamble | Delivery | Sleep share | vs `overdrive` |
|---|---|---|---|
| 32 symbols | 196/200 (98.0%) | 79.3% | +2.8 pp sleep, -2.0 pp delivery |
| 16 symbols | 197/200 (98.5%) | 56.4% | +0.9 pp sleep, -1.0 pp delivery |

`overdrive` on the same link and in the same run read 200/200 and 199/200. So
this level buys a little sleep and pays for it in packets, which is the whole
trade and the reason the name is what it is. The command is case-sensitive and
has no short form. On a mesh that relies on retries the cost may be acceptable;
on a link that matters it is not.

Level-based settings are recalculated after SF or bandwidth changes. Manual timings are not recalculated. Settings are persisted in `/prefs.json`. Companion firmware does not expose this text command and applies its fixed `balanced` profile (level 6, catches 10 symbols, preamble 16) at startup and after radio-parameter changes.

`set radio.rxps` echoes the timings it applied, so the periods the radio really
runs are visible immediately:

```text
OK - on,level=9(overdrive),preamble=16,rx=32782,sleep=40954
OK - on,level=10,preamble=32,rx=40375,sleep=106500
OK - on,level=5,preamble=16,rx=49485,sleep=22750
```

`get radio.rxps` reports:

```text
desired=<on|off>,effective=<armed|continuous>,supported=<yes|no>,
level=<0-10>[(overdrive)|(riskyWorkingMax)][,catch=<symbols>],preamble=<0|16|32>,rx=<us>,sleep=<us>,
err=<RadioLib error>,fail=<count>[,erx=<us>,eslp=<us>]
```

- `desired` is the saved user setting.
- `level=9(overdrive)` and `level=10(riskyWorkingMax)` mark profiles that run outside the datasheet timer condition. Any other level is guarded.
- `catch` is how many symbols of a sender's preamble the node actually catches, and appears only for guarded levels. It is not simply the number the level asks for: the sleep floor can shorten the sleep further, and at SF6 with a 16-symbol profile that collapses levels 1-6 onto the same point. Reporting the requested value there would be a lie, so the effective one is reported instead.
- `effective=armed` means receive duty-cycle is active.
- `effective=continuous` means RXPS is disabled, unsupported, or the last arm attempt fell back to continuous RX.
- `fail` counts failed arm operations; each one falls back to continuous RX. `clear stats` resets both this total and the consecutive-failure backoff, granting three fresh arm attempts.
- `erx` and `eslp` appear only when the driver had to clamp the requested periods, and report the effective periods after driver clamping. On LR1110 the RX window is stretched when `2*rx + sleep` would not cover the extended period Semtech requires, so the real duty cycle can be less economical than `rx`/`sleep` suggest.
- RXPS is currently supported by the SX1262 and LR1110 wrappers. Other radios remain in continuous RX and reject attempts to enable RXPS.
- There is intentionally no RXPS watchdog, watchdog command, or periodic recovery. Recovery is limited to the immediate continuous-RX fallback after an arm error. After 3 consecutive arm failures the node stops retrying on every RX restart and stays in continuous RX until the RXPS configuration is set again or `clear stats` grants a fresh set of attempts.
- On boards with a host-controlled RXEN pin, the RF switch is held in receive mode for the whole duty cycle (otherwise the node would be deaf). An external LNA on that pin therefore stays biased during the sleep windows, so the real power saving is smaller than the `rx`/`sleep` ratio implies.

---


### System

#### View or change this node's name
**Usage:**
- `get name`
- `set name <name>`

**Parameters:**
- `name`: Node name

**Set by build flag:** `ADVERT_NAME`

**Default:** Varies by board

**Note:** Advertised names can use up to 23 bytes when location is included and 31 bytes otherwise. Emoji and Unicode characters may take more than one byte. Names that exceed the available advert space are truncated at a valid UTF-8 code point boundary.

---

#### View or change this node's latitude
**Usage:**
- `get lat`
- `set lat <degrees>`

**Set by build flag:** `ADVERT_LAT`

**Default:** `0`

**Parameters:**
- `degrees`: Latitude in degrees

---

#### View or change this node's longitude
**Usage:**
- `get lon`
- `set lon <degrees>`

**Set by build flag:** `ADVERT_LON`

**Default:** `0`

**Parameters:**
- `degrees`: Longitude in degrees

---

#### View or change this node's identity (Private Key)
**Usage:**
- `get prv.key`
- `set prv.key <private_key>`

**Parameters:**
- `private_key`: Private key in hex format (64 hex characters)

**Serial Only:**
- `get prv.key`: Yes
- `set prv.key`: No

**Note:** Requires reboot to take effect after setting

---

#### Change this node's admin password
**Usage:**
- `password <new_password>`

**Parameters:**
- `new_password`: New admin password

**Set by build flag:** `ADMIN_PASSWORD`

**Default:** `password`

**Note:** Command reply echoes the updated password for confirmation.

**Note:** Any node using this password will be added to the admin ACL list.

---

#### View or change this node's guest password
**Usage:**
- `get guest.password`
- `set guest.password <password>`

**Parameters:**
- `password`: Guest password

**Set by build flag:** `ROOM_PASSWORD` (Room Server only)

**Default:** `<blank>`

---

#### View or change this node's owner info
**Usage:**
- `get owner.info`
- `set owner.info <text>`

**Parameters:**
- `text`: Owner information text

**Default:** `<blank>`

**Note:** `|` characters are translated to newlines

**Note:** Requires firmware 1.12+

---

#### Fine-tune the battery reading
**Usage:**
- `get adc.multiplier`
- `set adc.multiplier <value>`

**Parameters:**
- `value`: ADC multiplier (0.0-10.0)

**Default:** `0.0` (value defined by board)

**Note:** Returns "Error: unsupported by this board" if hardware doesn't support it

---

#### View this node's public key
**Usage:** `get public.key`

---

#### View this node's firmware version
**Usage:** `ver`

---

#### View this node's configured role
**Usage:** `get role`

---

#### View or change this node's power saving flag (Repeater Only)
**Usage:**
- `powersaving`
- `powersaving on`
- `powersaving off`

**Parameters:** 
- `on`: enable power saving
- `off`: disable power saving

**Default:** `off`

**Note:** When enabled, device enters sleep mode between radio transmissions

---

#### View or set reboot interval (Repeater and room server)
**Usage:**
- `get reboot.interval`
- `set reboot.interval <hours>`

**Parameters:** 
- `hours`: 0-255. 0 is disabled

**Default:** `0` (disabled)

---

### Routing

#### View or change this node's repeat flag
**Usage:**
- `get repeat`
- `set repeat <state>`

**Parameters:**
  - `state`: `on`|`off`

**Default:** `on`

---

#### View or change this node's advert path hash size
**Usage:**
- `get path.hash.mode`
- `set path.hash.mode <value>`

**Parameters:**
- `value`: Path hash size (0-2)
  - `0`: 1 Byte hash size (256 unique ids)[64 max flood]
  - `1`: 2 Byte hash size (65,536 unique ids)[32 max flood]
  - `2`: 3 Byte hash size (16,777,216 unique ids)[21 max flood]
  - `3`: DO NOT USE (Reserved) 

**Default:** `0`

**Note:** the 'path.hash.mode' sets the low-level ID/hash encoding size used when the repeater adverts. This setting has no impact on what packet ID/hash size this repeater forwards, all sizes should be forwarded on firmware >= 1.14. This feature was added in firmware 1.14

**Temporary Note:** adverts with ID/hash sizes of 2 or 3 bytes may have limited flood propagation in your network while this feature is new as v1.13.0 firmware and older will drop packets with multibyte path ID/hashes as only 1-byte hashes are supported. Consider your install base of firmware >=1.14 has reached a criticality for effective network flooding before implementing higher ID/hash sizes. 

---

#### View or change this node's loop detection
**Usage:**
- `get loop.detect`
- `set loop.detect <state>`

**Parameters:**
- `state`: 
  - `off`: no loop detection is performed
  - `minimal`: packets are dropped if repeater's ID/hash appears 4 or more times (1-byte), 2 or more (2-byte), 1 or more (3-byte)
  - `moderate`: packets are dropped if repeater's ID/hash appears 2 or more times (1-byte), 1 or more (2-byte), 1 or more (3-byte)
  - `strict`: packets are dropped if repeater's ID/hash appears 1 or more times (1-byte), 1 or more (2-byte), 1 or more (3-byte)
  
**Default:** `off`

**Note:** When it is enabled, repeaters will now reject flood packets which look like they are in a loop. This has been happening recently in some meshes when there is just a single 'bad' repeater firmware out there (probably some forked or custom firmware). If the payload is messed with, then forwarded, the same packet ends up causing a packet storm, repeated up to the max 64 hops. This feature was added in firmware 1.14

**Example:** If preference is `loop.detect minimal`, and a 1-byte path size packet is received, the repeater will see if its own ID/hash is already in the path. If it's already encoded 4 times, it will reject the packet.  If the packet uses 2-byte path size, and repeater's own ID/hash is already encoded 2 times, it rejects. If the packet uses 3-byte path size, and the repeater's own ID/hash is already encoded 1 time, it rejects. 

---

#### View or change the retransmit delay factor for flood traffic
**Usage:**
- `get txdelay`
- `set txdelay <value>`

**Parameters:**
- `value`: Transmit delay factor (0-2)

**Default:** `0.5`

**Note:** When multiple nearby repeaters all hear the same flood packet, each waits a random amount of time before retransmitting to avoid simultaneous collisions. This factor scales the size of that random window. Higher values reduce collision risk at the cost of added latency. `0` disables the window entirely.

---

#### View or change the retransmit delay factor for direct traffic
**Usage:**
- `get direct.txdelay`
- `set direct.txdelay <value>`

**Parameters:**
- `value`: Direct transmit delay factor (0-2)

**Default:** `0.2`

**Note:** Same collision-avoidance random window as `txdelay`, but applied to direct (non-flood, routed) traffic. The default is lower because direct packets are addressed to a specific next hop, so far fewer nodes compete to retransmit them.

---

#### [Experimental] View or change the processing delay for received traffic
**Usage:**
- `get rxdelay`
- `set rxdelay <value>`

**Parameters:**
- `value`: Receive delay base (0-20)

**Default:** `0.0`

**Note:** When enabled, repeaters that received a flood packet with a weak signal are held in a delay queue before processing, while those that received it with a strong signal process it immediately. This gives strong-signal paths forwarding priority. By the time weak-signal nodes process their copy, the packet may have already propagated and will be suppressed as a duplicate, reducing redundant retransmissions.

---

#### View or change the duty cycle limit
**Usage:**
- `get dutycycle`
- `set dutycycle <value>`

**Parameters:**
- `value`: Duty cycle percentage (1-100)

**Default:** `50%` (equivalent to airtime factor 1.0)

**Examples:**
- `set dutycycle 100` — no duty cycle limit
- `set dutycycle 50` — 50% duty cycle (default)
- `set dutycycle 10` — 10% duty cycle
- `set dutycycle 1` — 1% duty cycle (strictest EU requirement)

> **Note:** Added in firmware v1.15.0

---

#### View or change the airtime factor (duty cycle limit)
> **Deprecated** as of firmware v1.15.0. Use [`get/set dutycycle`](#view-or-change-the-duty-cycle-limit) instead.

**Usage:**
- `get af`
- `set af <value>`

**Parameters:**
- `value`: Airtime factor (0-9). After each transmission, the repeater enforces a silent period of approximately the on-air transmission time multiplied by the value. This results in a long-term duty cycle of roughly 1 divided by (1 plus the value). For example:
  - `af = 1` → ~50% duty
  - `af = 2` → ~33% duty
  - `af = 3` → ~25% duty
  - `af = 9` → ~10% duty
  You are responsible for choosing a value that is appropriate for your jurisdiction and channel plan (for example EU 868 Mhz 10% duty cycle regulation).

**Default:** `1.0`

---

#### View or change the local interference threshold
**Usage:**
- `get int.thresh`
- `set int.thresh <value>`

**Parameters:**
- `value`: Interference threshold value

**Default:** `0.0`

---

#### Enable or disable hardware Channel Activity Detection (CAD)
**Usage:**
- `get cad`
- `set cad <on|off>`

**Description:** When enabled, the radio performs a hardware Channel Activity Detection scan before transmitting and defers if the channel is busy. Runs independently of `int.thresh` — either, both, or none may be active.

**Parameters:**
- `on|off`: Enable or disable hardware CAD

**Default:** `off`

---

#### View or change the AGC Reset Interval
**Usage:**
- `get agc.reset.interval`
- `set agc.reset.interval <value>`

**Parameters:**
- `value`: Interval in seconds rounded down to a multiple of 4 (17 becomes 16). 0 to disable.

**Default:** `0.0`

---

#### Enable or disable Multi-Acks support
**Usage:**
- `get multi.acks`
- `set multi.acks <state>`

**Parameters:**
- `state`: `0` (disable) or `1` (enable)

**Default:** `0`

---

#### View or change the flood advert interval
**Usage:**
- `get flood.advert.interval`
- `set flood.advert.interval <hours>`

**Parameters:**
- `hours`: Interval in hours (3-168)

**Default:** `12` (Repeater) - `0` (Sensor)

---

#### View or change the zero-hop advert interval
**Usage:**
- `get advert.interval`
- `set advert.interval <minutes>`

**Parameters:**
- `minutes`: Interval in minutes rounded down to the nearest multiple of 2 (61 becomes 60) (60-240)

**Default:** `0`

---

#### Limit the number of hops for a flood message
**Usage:**
- `get flood.max`
- `set flood.max <value>`

**Parameters:**
- `value`: Maximum flood hop count (0-64)

**Default:** `64`

---

#### Limit the number of hops for an unscoped flood message
**Usage:**
- `get flood.max.unscoped`
- `set flood.max.unscoped <value>`

**Parameters:**
- `value`: Maximum flood hop count (0-64) for a packet without a scope (no region set)

**Default:** `64` - (`0xFF` indicates it hasn't been set, will track flood.max until it is.)

**Note:** An alternative to `region denyf *`, setting `flood.max.unscoped` to a lower value such as `3` would allow for local unscoped messages to propagate, while preventing noisy neighbors from flooding a local region.

---

#### Limit the number of hops for an advert flood message
**Usage:**
- `get flood.max.advert`
- `set flood.max.advert <value>`

**Parameters:**
- `value`: Maximum flood hop count (0-64) for an advert packet

**Default:** `8`

---

### ACL

#### Add, update or remove permissions for a companion
**Usage:** 
- `setperm <pubkey> <permissions>`

**Parameters:**
- `pubkey`: Companion public key
- `permissions`: 
  - `0`: Guest
  - `1`: Read-only
  - `2`: Read-write
  - `3`: Admin

**Note:** Removes the entry when `permissions` is omitted

---

#### View the current ACL
**Usage:** 
- `get acl`

**Serial Only:** Yes

---

#### View or change this room server's 'read-only' flag
**Usage:**
- `get allow.read.only`
- `set allow.read.only <state>`

**Parameters:**
- `state`: `on` (enable) or `off` (disable)

**Default:** `off`

---

### Region Management (v1.10.+)

#### Bulk-load region lists
**Usage:** 
- `region load`
- `region load <name> [flood_flag]`

**Parameters:**
- `name`: A name of a region. `*` represents the wildcard region

**Note:** `flood_flag`: Optional `F` to allow flooding

**Note:** Indentation creates parent-child relationships (max 8 levels)

**Note:** `region load` with an empty name will not work remotely (it's interactive)

---

#### Save any changes to regions made since reboot
**Usage:** 
- `region save`

---

#### Allow a region
**Usage:** 
- `region allowf <name>`

**Parameters:** 
- `name`: Region name (or `*` for wildcard)

**Note:** Setting on wildcard `*` allows packets without region transport codes

---

#### Block a region
**Usage:** 
- `region denyf <name>`

**Parameters:** 
- `name`: Region name (or `*` for wildcard)

**Note:** Setting on wildcard `*` drops packets without region transport codes

---

#### Show information for a region
**Usage:** 
- `region get <name>`

**Parameters:**
- `name`: Region name (or `*` for wildcard)

---

#### View or change the home region for this node
**Usage:** 
- `region home`
- `region home <name>`

**Parameters:**
- `name`: Region name

---

#### View or change the default scope region for this node
**Usage:** 
- `region default`
- `region default {name|<null>}`

**Parameters:**
- `name`: Region name,  or <null> to reset/clear

---

#### View or set the direct path override for the current remote client
**Usage:**
- `get outpath`
- `set outpath <hop1_hex,hop2_hex,...>`
- `set outpath direct`
- `set outpath clear`
- `set outpath flood`

**Parameters:**
- `hopN_hex`: Hop hash, `2`, `4`, or `6` hex characters. All hops must use the same width.

**Notes:**
- These commands require remote client context (they target the caller's ACL entry).
- The path hash size is inferred from the hop hash width.
- `outpath` overrides the primary direct route used for replies to the caller.
- `direct` sets a zero-hop direct route for a caller reachable without repeaters.
- `clear` forgets the current direct path and allows normal path discovery to repopulate it.
- `flood` forces replies to use flood packets until the client logs in again.

---

#### Create a new region
**Usage:** 
- `region put <name> [parent_name]`

**Parameters:**
- `name`: Region name
- `parent_name`: Parent region name (optional, defaults to wildcard)

---

#### Define region hierarchy (single line)
**Usage:**
- `region def <token> [<token> ...]`

**Parameters (tokens):** Space-separated. A logical **cursor** starts at the wildcard `*`.

- **`name`** — Create `name` as a child of the current cursor (equivalent to `region put name` with the cursor as parent). Cursor moves to `name`.
- **`name|jump`** *(or `name,jump`)* — Create `name` as a child of the current cursor, then move the cursor to `jump` (must already exist on the node, or have been created earlier in this command). `jump` is **not** the parent of `name`; use this form to pop back up and start another branch.

**Behavior:** Each created region defaults to flood-allowed (same as `region put`). The reply is the resulting region tree (same format as bare `region`); review it before running `region save` to persist. On error, the reply is `Err - ...` and any regions placed before the failure remain on the node, just like a partial chain of `region put`.

**Existing regions:** `region def` does not clear the existing tree — if a name already exists, its parent is updated to the current cursor; otherwise a new region is created. To start from scratch, `region remove` the unwanted regions first.

**Limits:** Repeater serial accepts one line up to **160 characters**. For larger trees, split across multiple `region def` commands; the cursor resets to `*` between commands, so lead the next command with `child|ancestor` to reposition. Each token splits at most once on `|` — `region def a|b|c|d` is not a flat-list shorthand; see the flat-list example below.

**Example — linear chain** (each token becomes a child of the previous):
```
region def a b c d e
region save
```

**Example — branched tree** (equivalent to `region put a`, `region put b a`, `region put c b`, `region put d c`, `region put e b`, `region put f e`):
```
region def a b c d|b e f
region save
```

**Example — error and partial state:**
```
region def a b c|nope d
```
The reply is `Err - unknown jump: nope`. `a`, `b`, and `c` were placed before the failure; `d` was not. Run `region` to inspect, then re-run with a corrected jump or repair with `region remove` / `region put`.

**Example — flat list** (each region a child of `*`). Use `|*` after each token to pop the cursor back to the root before the next token:
```
region def a|* b|* c|* d|* e|* f
region save
```

---

#### Remove a region
**Usage:** 
- `region remove <name>`

**Parameters:**
- `name`: Region name

**Note:** Must remove all child regions before the region can be removed 

---

#### View all regions
**Usage:** 
- `region list <filter>`

**Serial Only:** Yes

**Parameters:**
- `filter`: `allowed`|`denied`

**Note:** Requires firmware 1.12+

---

#### Dump all defined regions and flood permissions
**Usage:** 
- `region`

**Serial Only:** For firmware older than 1.12.0

---

### Region Examples

**Example 1: Using F Flag with Named Public Region**
```
region load
#Europe F
<blank line to end region load>
region save
```

**Explanation:**
- Creates a region named `#Europe` with flooding enabled
- Packets from this region will be flooded to other nodes

---

**Example 2: Using Wildcard with F Flag**
```
region load 
* F
<blank line to end region load>
region save
```

**Explanation:**
- Creates a wildcard region `*` with flooding enabled
- Enables flooding for all regions automatically
- Applies only to packets without transport codes

---

**Example 3: Using Wildcard Without F Flag**
```
region load 
*
<blank line to end region load>
region save
```
**Explanation:**
- Creates a wildcard region `*` without flooding
- This region exists but doesn't affect packet distribution
- Used as a default/empty region

---

**Example 4: Nested Public Region with F Flag**
```
region load 
#Europe F
  #UK
    #London
    #Manchester
  #France
    #Paris
    #Lyon
<blank line to end region load>
region save
```

**Explanation:**
- Creates `#Europe` region with flooding enabled
- Adds nested child regions (`#UK`, `#France`)
- All nested regions inherit the flooding flag from parent

---

**Example 5: Wildcard with Nested Public Regions**
```
region load 
* F
  #NorthAmerica
    #USA
      #NewYork
      #California
    #Canada
      #Ontario
      #Quebec
<blank line to end region load>
region save
```

**Explanation:**
- Creates wildcard region `*` with flooding enabled
- Adds nested `#NorthAmerica` hierarchy
- Enables flooding for all child regions automatically
- Useful for global networks with specific regional rules

---
### GPS (When GPS support is compiled in)

#### View or change GPS state
**Usage:**
- `gps`
- `gps <state>`

**Parameters:**
- `state`: `on`|`off`

**Default:** `off`

**Note:** Output format:
- `off` when the GPS hardware is disabled
- `on, {active|deactivated}, {fix|no fix}, {sat count} sats` when the GPS hardware is enabled

---

#### Sync this node's clock with GPS time
**Usage:** 
- `gps sync`

---

#### Set this node's location based on the GPS coordinates
**Usage:** 
- `gps setloc`

---

#### View or change the GPS advert policy
**Usage:**
- `gps advert`
- `gps advert <policy>`

**Parameters:** 
- `policy`: `none`|`share`|`prefs` 
  - `none`: don't include location in adverts
  - `share`: share gps location (from SensorManager)
  - `prefs`: location stored in node's lat and lon settings

**Default:** `prefs`

---

### Sensors (When sensor support is compiled in)

#### View the list of sensors on this node
**Usage:** `sensor list [start]`

**Parameters:**
- `start`: Optional starting index (defaults to 0)

**Note:** Output format: `<var_name>=<value>\n`

---

#### View or change the value of a sensor
**Usage:** 
- `sensor get <key>`
- `sensor set <key> <value>`

**Parameters:**
- `key`: Sensor setting name
- `value`: The value to set the sensor to

---

### Bridge (When bridge support is compiled in)

#### View the compiled bridge type
**Usage:** `get bridge.type`

---

#### View or change the bridge enabled flag
**Usage:**
- `get bridge.enabled`
- `set bridge.enabled <state>`

**Parameters:**
- `state`: `on`|`off`

**Default:** `off`

---

#### Add a delay to packets routed through this bridge
**Usage:**
- `get bridge.delay`
- `set bridge.delay <ms>`

**Parameters:**
- `ms`: Delay in milliseconds (0-10000)

**Default:** `500`

---

#### View or change the source of packets bridged to the external interface
**Usage:**
- `get bridge.source`
- `set bridge.source <source>`

**Parameters:**
- `source`: 
  - `logRx`: bridges received packets
  - `logTx`: bridges transmitted packets

**Default:** `logTx`

---

#### View or change the speed of the bridge (RS-232 only)
**Usage:**
- `get bridge.baud`
- `set bridge.baud <rate>`

**Parameters:**
- `rate`: Baud rate (`9600`, `19200`, `38400`, `57600`, or `115200`)

**Default:** `115200`

---

#### View or change the channel used for bridging (ESPNow only)
**Usage:**
- `get bridge.channel`
- `set bridge.channel <channel>`

**Parameters:**
- `channel`: Channel number (1-14)

---

#### Set the ESP-Now secret
**Usage:** 
- `get bridge.secret`
- `set bridge.secret <secret>`

**Parameters:**
- `secret`: ESP-NOW bridge secret, up to 15 characters

**Default:** Varies by board

---

#### View the bootloader version (nRF52 only)
**Usage:** `get bootloader.ver`

---

#### View power management support
**Usage:** `get pwrmgt.support`

---

#### View the current power source
**Usage:** `get pwrmgt.source`

**Note:** Returns an error on boards without power management support.

---

#### View the boot reset and shutdown reasons
**Usage:** `get pwrmgt.bootreason`

**Note:** Returns an error on boards without power management support.

---

#### View the boot voltage
**Usage:** `get pwrmgt.bootmv`

**Note:** Returns an error on boards without power management support.

---

### Ethernet (when Ethernet support is compiled in)

Ethernet support is available on RAK4631 boards with a RAK13800 (W5100S) Ethernet module. Use the `_ethernet` firmware variants (e.g. `RAK_4631_repeater_ethernet`) to enable this feature.

---

#### View Ethernet connection status
**Usage:**
- `eth.status`

**Output:**
- `ETH: <ip>:<port>` when connected (e.g. `ETH: 192.168.1.50:23`)
- `ETH: not connected` when Ethernet is not active

**Notes:**
- Available on repeater and room server firmware only. Companion radio ethernet firmware does not expose a CLI.
- The Ethernet interface obtains an IP address via DHCP automatically on boot.
- A TCP server listens on port 23 (default) for CLI connections.
- Connect with any TCP client (e.g. `nc`, PuTTY) to access the same CLI available over serial.

---
