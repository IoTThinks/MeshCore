# MeshCore Filter Engine — Test Suite

Standalone test suite for the MeshCore repeater packet filter engine. Compiles and runs on Linux without any Arduino or MeshCore dependencies.

---

## Contents

| File | Description |
|---|---|
| `FilterRule.h` | Rule struct, enums and constants |
| `FilterParser.h/.cpp` | Command parser |
| `ChannelFilter.h/.cpp` | Rule storage, evaluation, persistence and CLI dispatch |
| `mock_mesh.h` | Minimal stubs replacing Arduino/MeshCore/FILESYSTEM |
| `auto_test.cpp` | Automated test suite (pass/fail) |
| `shell.cpp` | Interactive command shell |
| `Makefile` | Build targets |

---

## Build

```bash
make            # builds both auto_test and shell
make run_tests  # builds and runs auto_test
make clean      # removes binaries
```

Requires `g++` with C++11 support.

---

## Automated tests

Runs 92 tests covering parser, evaluation, persistence, AND conditions and page length.

```bash
./auto_test
```

Example output:

```
═══════════════════════════════════════
 MeshCore Filter Engine — Test Suite
═══════════════════════════════════════

── Parser — valid commands
  PASS  add drop payload eq grptxt -> OK
  PASS  add allow route eq flood -> OK
  ...

═══════════════════════════════════════
 Results: 92 passed, 0 failed
═══════════════════════════════════════
```

Exit code is `0` on success, `1` if any test fails.

---

## Interactive shell

Lets you type `filter` commands and test packet evaluation interactively. Rules are not persisted between sessions.

```bash
./shell
```

### Filter commands

```
filter add drop payload eq grptxt
filter add allow route eq flood and hops lt 4
filter disable 0
filter enable 0
filter list
filter list 1
filter del 0
filter clear
filter mode drop
filter mode allow
```

### Packet evaluation

Use `eval` to test a constructed packet against the current rules:

```
eval route:<0-3> type:<0-15> snr:<dB> rssi:<dBm> hops:<n> [channel:<hex>] [hop:<hex>]
```

| Parameter | Description |
|---|---|
| `route` | Route type: `0`=tflood `1`=flood `2`=direct `3`=tdirect |
| `type` | Payload type: `0`=req `2`=txt `4`=advert `5`=grptxt `6`=grpdata ... |
| `snr` | SNR in whole dB, e.g. `-10` |
| `rssi` | RSSI in dBm, e.g. `-90` |
| `hops` | Number of hops |
| `channel` | Channel hash byte in hex, e.g. `AB` (only relevant for grptxt/grpdata) |
| `hop` | Last hop repeater hash in hex, e.g. `CD` |

Example session:

```
> filter add drop channel eq 0x11 and hops gt 8
  -> OK - rule 0 added
> filter enable 0
  -> OK - rule 0 enabled
> filter list
  -> mode:allow rules:1/8
     0 drop channel eq 0x11 and hops gt 8
> eval route:1 type:5 snr:-5 rssi:-90 hops:9 channel:11
  Packet: route=1 type=5 snr=-5dB rssi=-90dBm hops=9 channel=0x11
  Result: *** DROPPED ***
> eval route:1 type:5 snr:-5 rssi:-90 hops:3 channel:11
  Packet: route=1 type=5 snr=-5dB rssi=-90dBm hops=3 channel=0x11
  Result: PASSED (forwarded)
> quit
```

---

## Notes

- Rules added via `filter add` are **disabled by default**. Use `filter enable <id>` to activate them.
- The `mock_mesh.h` stub provides an in-memory filesystem — rules saved during a shell session are lost on exit.
- The patched `ChannelFilter.h/.cpp` in this directory have Arduino/MeshCore includes removed. Do not copy these back into the main project — use the originals there.