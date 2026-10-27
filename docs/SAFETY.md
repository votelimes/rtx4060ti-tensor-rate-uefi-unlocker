# Safety and recovery

This experimental application writes GPU registers and runs a firmware payload.
It can hang the GPU or firmware, blank the display, or require a physical power
cycle. Use only on hardware you own and can recover. Simulation and one successful
hardware boot are not guarantees against hardware or data loss.

## Containment implemented

- Exact device, subsystem, chip, measured VRAM, PCI layout and ROM checks.
- Cold selector masks and effective readout checks; three stability samples and
  comparison across preflight, countdown and firmware-driver release.
- Asset length/hash checks before use; Windows loader preloaded before GPU release.
- Five-second automatic-start delay with Esc cancellation; no automatic retry.
- Persistent one-attempt lock, written/flushed/read back before driver release.
- Explicit firmware-driver ownership checks; a bounded nonrecursive child walk
  during reconnection (16 nodes, maximum depth four).
- Checked 32-bit DMA mappings and bus-master containment. Resources are retained
  if it cannot be established that the GPU has stopped using them.
- A single SEC2 payload launch per attempt, status/readback gates, one FLR and
  post-reset/post-GOP checks. A clean handoff is required before Windows starts.
- Independent two-slot emergency journal and regular flushed report on the USB.
- No VBIOS flashing, BootOrder/BootNext modification or automatic reboot.

## Limits

UEFI calls such as ConnectController are synchronous. The application cannot
interrupt a hung firmware driver. The documented 60 seconds is an observation
interval, not an enforced timeout. Both logs depend on the same USB/firmware;
write/flush/readback checks cannot guarantee survival of every power or USB fault.
`stall_us` is the sum of requested waits, not measured wall-clock duration.

The active NVIDIA UEFI driver was identified before the successful hardware run.
The application does not enforce a universal NVIDIA-driver signature allowlist.
Keep the firmware video policy on UEFI; do not infer that every GOP provider is
the native NVIDIA driver.

## If something goes wrong

- Before GPU release, a rejected preflight reports its stage and returns to
  firmware after a pause. Do not bypass a failed gate.
- On a `STOP` requiring power-off, turn the PC off with the physical power button.
- If the display goes blank and Windows does not appear within 60 seconds, power
  off. Remove the USB after shutdown, then boot the normal Windows entry.
- Reconnect the USB after normal boot and save both logs before another attempt.
- Keep `ATTEMPT.LCK` until the preceding attempt has been reviewed and the machine
  has been powered off. Archive it deliberately; do not add automatic lock deletion.

Removing the USB while the UEFI program runs is not a recovery procedure and may
destroy the last available evidence. This program has no remote recovery channel.
