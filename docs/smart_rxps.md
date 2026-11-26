# SmartRXPS (experimental)

SmartRXPS is a test build on top of EasySkyMesh PowerSaving v18 (MeshCore 1.18).
It tries to keep most of the battery saving of RX PowerSaving without its two
costs: missing packets and going deaf to old firmware.

## What it changes

1. **Automatic 16/32 profile.** The 32-symbol profile saves the most power but
   cannot hear radios on MeshCore < 1.16 or PowerSaving < 16, because their
   preamble is only 16 symbols long. SmartRXPS times every received preamble
   (PREAMBLE_DETECTED to RX_DONE, minus the known packet airtime) to tell 16-symbol
   senders from 32-symbol ones.
   - It starts on the 16-symbol profile, which hears everyone (`safe16`).
   - After 30 minutes, if it has timed at least 10 new-firmware senders and no
     old-firmware sender for 6 hours, it moves to the 32-symbol profile (`eff32`).
   - Every 50 minutes on `eff32` it spends 10 minutes back on the 16-symbol
     profile (`audit16`) to check again.
   - The first old-firmware sender it hears sends it straight back to `safe16`.
   This only applies to guarded levels 1-8 at SF 5-8. Manual periods, levels 9-10
   and SF 9-12 behave exactly like stock EasySkyMesh.

2. **Listen after transmit (hold).** For N seconds after every transmit the radio
   stays on continuous RX, so replies, ACKs and path returns to your own messages
   are not slept through. Default 15 s on companions, 5 s on repeaters.

3. **Re-arm watchdog.** Stock firmware gives up on RX power saving for good after
   three failed arm attempts. SmartRXPS retries after 5, 10, 20, 40, then every
   60 minutes, and reports the state.

## Commands

| Command | What it does |
| --- | --- |
| `get radio.rxps.smart` | Status line (see below) |
| `set radio.rxps smart on` / `off` | Turn the automatic profile on or off |
| `set radio.rxps hold <0-120>` | Seconds of continuous RX after each transmit (0 = off) |
| `get radio.rxps` | Stock status: level, periods, armed or continuous |

All stock `set radio.rxps ...` commands still work. With smart on, the level you
choose is used on both profiles; the preamble part of your setting is ignored.

### Reading `get radio.rxps.smart`

`> smart=eff32,in=12m,old=0,new=57,amb=3,bad=1,last=27.4,oldAgo=never,hold=15s(0),holds=9,rearm=no,tries=0`

- `smart`: `safe16`, `eff32`, `audit16`, or `off`; `in` = minutes in that state
- `old` / `new`: preambles timed from old (16) and new (32) firmware senders
- `amb` / `bad`: readings it could not classify / discarded as disturbed
- `last`: last reading in preamble symbols left after detection (~28 = new, ~13 = old)
- `oldAgo`: minutes since the last old-firmware sender
- `hold`: hold setting (seconds left now); `holds`: how many holds so far
- `rearm`: `wait` while the watchdog is waiting to retry; `tries` so far

## Turning it off

`set radio.rxps smart off` and `set radio.rxps hold 0` give stock EasySkyMesh
behaviour without reflashing.
