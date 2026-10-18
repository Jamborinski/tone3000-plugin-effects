# TONE3000 v0.0.14 (DRAFT — not yet released)

**Draft date:** 2026-10-13
**Platforms:** VST3 · AU · CLAP · LV2 · Standalone

## Reverb — Plate/"140" model: final training pass

The Plate reverb's **"140"** model was retrained against the full Nevo
Studios EMT 140 family (0.5 s – 4.5 s), plus cross-checked against the
EMT 240 Gold and Stocktronics RX 4000 plate captures. Two changes, both on
the Plate/"140" path only:

- **Crisper plate attack.** The onset is now a few strong, well-spaced
  early pings over a quieter incoherent floor — the plate-class signature
  the real captures show. The model's attack now reaches within 0.9 dB of
  the EMT 140 reference (45.7 vs 46.6) instead of the old ~9 dB gap, at
  the reference's own 2.0 s length.
- **Size now means plate class.** The Size dial was previously inert on
  the Plate path. It now sweeps the tail's high-frequency balance across a
  plate's physical class — large-plate class brighter, compact class
  darker — anchored at the 50 % neutral setting (bit-identical to before).

Body, decay law, level, stability, and CPU class all hold their existing
guard rails; the other five reverb modes are byte-identical and the
convolution engine is untouched.

*Full engineering record (family tables, cross-model, per-lever A/B with
declines + numbers, CPU): `docs/tickets/plate-140-final-training-pass-closeout.md`.*

## Housekeeping

- `Size` on the Chamber / Hall / Digital reverb paths is still inert —
  flagged for a follow-up audit (a no-op there too, pending mode-appropriate
  treatment).
