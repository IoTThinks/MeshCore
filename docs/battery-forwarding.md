# Battery-controlled forwarding

The repeater can stop forwarding packets automatically when the battery voltage gets low, and start again once the battery has recovered. This saves power on solar or battery-powered nodes, so the node stays alive and reachable instead of draining completely.

## Commands

| Command | Description |
|---|---|
| `battery` | Shows the current battery voltage, the forwarding state (`on` / `SUSPENDED`) and the configured thresholds. |
| `battery off` | Disables battery-controlled forwarding. |
| `battery <suspend-mV> <resume-mV>` | Enables the feature. Forwarding is suspended when the battery drops below `<suspend-mV>` and resumed when it reaches `<resume-mV>` or higher. |

## Example

```
battery 3300 3500
```

Forwarding stops below 3.30 V and starts again at 3.50 V.

## Behaviour

- Both values must be within 1000–30000 mV, and `<resume-mV>` must be greater than `<suspend-mV>`. The gap between them prevents the state from toggling back and forth.
- The battery is sampled every 30 seconds. The state only changes after 3 consecutive readings beyond the threshold, which filters out voltage sag during TX and ADC noise.
- When new thresholds are set, the current voltage is checked immediately.
- While suspended, the repeater:
  - does not forward packets
  - does not answer discovery requests
  - reports itself as disabled (same as `set repeat off`)
- Its own adverts and remote CLI access keep working.
- A reading of 0 mV, from a board without battery measurement, never suspends forwarding.
- The setting is independent of `set repeat on/off` and is saved in the node preferences, so it survives a reboot.
