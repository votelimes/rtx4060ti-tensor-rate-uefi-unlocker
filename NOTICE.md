# Attribution and licenses

This project is a board-specific experimental adaptation, not an original discovery
of the Ada tensor-rate mechanism and not an NVIDIA-supported product.

- UEFI base: [TOTORONG/AD103_win_unlocker](https://github.com/TOTORONG/AD103_win_unlocker),
  revision `51df9c17ec89eb8486df52cc9079dde4bd1f9974`.
- Linux mechanism/reference: [JustAResearcher/gpu-unlock-ada](https://github.com/JustAResearcher/gpu-unlock-ada),
  revision used during development `d28c485418c3d9d034cd4bac19200bc33dd3453b`.
- Retain the per-file copyright and license notices. The adapted application and
  derived changes use GPL-2.0-only (see LICENSE). EDK2 headers and NVIDIA-derived
  open-source portions retain their respective licenses in LICENSES and deps.

Adaptation dated October 27, 2026: AD106/board profile, captured-ROM validation,
resource containment and durable logging, explicit driver ownership, bounded
nonrecursive GOP recovery, automatic countdown and additional negative tests.
Development and documentation were assisted by OpenAI Codex.

No NVIDIA firmware blobs, VBIOS dump, personal machine identifiers or credentials
are distributed here. ASSETS-MANIFEST.json lists hashes, not redistribution rights.
Obtain required assets separately under their applicable terms; this repository
does not grant rights to redistribute third-party firmware or extracted payloads.
Historical upstream comments mentioning PORT-NOTES.txt or THIRD_PARTY_NOTICES.md
refer to upstream development material; this file documents this distribution.
