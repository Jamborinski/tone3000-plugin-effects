#!/usr/bin/env python3
# Node-view variants of the 93-icon library (ui-svg-icon-library-draft).
#
# Strip cells render each icon at ~28-34px on 2x, where 0.5-1u dots and fine
# lines fuse. These variants keep the silhouettes but drop detail: knob rows
# collapse to 2-3 big knobs, LEDs/vent-slots/dot-columns/small windows vanish,
# redundant inner panels go away. EVERY library icon gets a node/ copy —
# unchanged ones are copied verbatim (user m02599: a copy is required even
# when identical; m02597: keep sizes consistent across cells, no icon should
# barely fill its node while another nearly fills it).
import pathlib

HERE = pathlib.Path(__file__).parent
OUT = HERE / "node"
OUT.mkdir(exist_ok=True)
HEAD = ('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" '
        'stroke="#ffffff" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">')

# node variants that DIFFER from the library (name -> body).
N = {}

# ---- amp / preamp family (knob rows -> 3 big) ------------------------
N["amp"] = ('<rect x="2.4" y="4.6" width="19.2" height="14.8" rx="2"/>'
  '<circle cx="8" cy="9.4" r="1.4" fill="#ffffff" stroke="none"/>'
  '<circle cx="12" cy="9.4" r="1.4" fill="#ffffff" stroke="none"/>'
  '<circle cx="16" cy="9.4" r="1.4" fill="#ffffff" stroke="none"/>'
  '<rect x="4.9" y="12.8" width="14.2" height="4.2"/>')

N["combo"] = ('<rect x="2.4" y="4" width="19.2" height="16" rx="2"/>'
  '<circle cx="7.2" cy="8" r="1.4" fill="#ffffff" stroke="none"/>'
  '<circle cx="12" cy="8" r="1.4" fill="#ffffff" stroke="none"/>'
  '<circle cx="16.8" cy="8" r="1.4" fill="#ffffff" stroke="none"/>'
  '<rect x="4.9" y="11.2" width="14.2" height="6"/>'
  '<circle cx="12" cy="14.2" r="1.6"/>')

N["outboard"] = ('<rect x="2.4" y="6.4" width="19.2" height="11.2" rx="2"/>'
  '<rect x="4.9" y="8.9" width="14.2" height="6.2"/>'
  '<circle cx="8.6" cy="10.9" r="1.2" fill="#ffffff" stroke="none"/>'
  '<circle cx="12" cy="10.9" r="1.2" fill="#ffffff" stroke="none"/>'
  '<circle cx="15.4" cy="10.9" r="1.2" fill="#ffffff" stroke="none"/>')

N["preamp"] = ('<rect x="3" y="6" width="18" height="12" rx="2"/>'
  '<circle cx="8.5" cy="12" r="1.5" fill="#ffffff" stroke="none"/>'
  '<circle cx="15.5" cy="12" r="1.5" fill="#ffffff" stroke="none"/>')

N["preamp-tube"] = ('<path d="M9 10.5a3 3 0 0 1 6 0V17a1 1 0 0 1-1 1h-4a1 1 0 0 1-1-1Z"/>'
  '<path d="M12 3v3.5"/>')

N["power-tube"] = ('<path d="M8 10a4 4 0 0 1 8 0v5a1 1 0 0 1-1 1H9a1 1 0 0 1-1-1Z"/>'
  '<path d="M7.5 16.7h9" stroke-width="2.8"/>')

# ---- specific amp heads (knob arrays -> 3 big) -----------------------
N["fman-amp"] = ('<rect x="1.6" y="5.4" width="20.8" height="13.2" rx="1.6"/>'
  '<rect x="4.4" y="7.6" width="5" height="3.6"/>'
  '<path fill="#ffffff" stroke="none" fill-rule="evenodd" d="M8 12.6H19A1 1 0 0 1 20 13.6V16.4A1 1 0 0 1 19 17.4H8A1 1 0 0 1 7 16.4V13.6A1 1 0 0 1 8 12.6Z M10 15a0.95 0.95 0 1 0 1.9 0a0.95 0.95 0 1 0-1.9 0Z M13 15a0.95 0.95 0 1 0 1.9 0a0.95 0.95 0 1 0-1.9 0Z M16 15a0.95 0.95 0 1 0 1.9 0a0.95 0.95 0 1 0-1.9 0Z"/>')
N["lead8-amp"] = ('<rect x="1.6" y="5.4" width="20.8" height="13.2" rx="1.6"/>'
  '<path fill="#ffffff" stroke="none" fill-rule="evenodd" d="M4.2 13.2H19.8A1 1 0 0 1 20.8 14.2V16.4A1 1 0 0 1 19.8 17.4H4.2A1 1 0 0 1 3.2 16.4V14.2A1 1 0 0 1 4.2 13.2Z M6.75 15.3a1.15 1.15 0 1 0 2.3 0a1.15 1.15 0 1 0-2.3 0Z M10.85 15.3a1.15 1.15 0 1 0 2.3 0a1.15 1.15 0 1 0-2.3 0Z M14.95 15.3a1.15 1.15 0 1 0 2.3 0a1.15 1.15 0 1 0-2.3 0Z"/>')
N["mtch-amp"] = ('<rect x="1.6" y="5.4" width="20.8" height="13.2" rx="1.6"/>'
  '<circle cx="7.5" cy="8.5" r="0.95" fill="#ffffff" stroke="none"/>'
  '<circle cx="12" cy="8.5" r="0.95" fill="#ffffff" stroke="none"/>'
  '<circle cx="16.5" cy="8.5" r="0.95" fill="#ffffff" stroke="none"/>'
  '<rect x="5.4" y="10.8" width="13.2" height="4.6"/>')
N["plx-amp"] = ('<rect x="1.6" y="5.4" width="20.8" height="13.2" rx="1.6"/>'
  '<path fill="#ffffff" stroke="none" fill-rule="evenodd" d="M4.6 12.4H19.4A1 1 0 0 1 20.4 13.4V16.4A1 1 0 0 1 19.4 17.4H4.6A1 1 0 0 1 3.6 16.4V13.4A1 1 0 0 1 4.6 12.4Z M7.6 14.9a0.95 0.95 0 1 0 1.9 0a0.95 0.95 0 1 0-1.9 0Z M11.05 14.9a0.95 0.95 0 1 0 1.9 0a0.95 0.95 0 1 0-1.9 0Z M14.5 14.9a0.95 0.95 0 1 0 1.9 0a0.95 0.95 0 1 0-1.9 0Z"/>')
N["an-amp"] = ('<rect x="1.6" y="5.4" width="20.8" height="13.2" rx="1.6"/>'
  '<circle cx="5.4" cy="9.2" r="1.05" fill="#ffffff" stroke="none"/>'
  '<circle cx="9.8" cy="9.2" r="1.05" fill="#ffffff" stroke="none"/>'
  '<circle cx="14.2" cy="9.2" r="1.05" fill="#ffffff" stroke="none"/>'
  '<circle cx="18.6" cy="9.2" r="1.05" fill="#ffffff" stroke="none"/>'
  '<rect x="4" y="11.6" width="16" height="4.8"/>')
N["dmb-amp"] = ('<rect x="1.6" y="5.4" width="20.8" height="13.2" rx="1.6"/>'
  '<circle cx="5.4" cy="9" r="1.05" fill="#ffffff" stroke="none"/>'
  '<circle cx="9.8" cy="9" r="1.05" fill="#ffffff" stroke="none"/>'
  '<circle cx="14.2" cy="9" r="1.05" fill="#ffffff" stroke="none"/>'
  '<circle cx="18.6" cy="9" r="1.05" fill="#ffffff" stroke="none"/>'
  '<rect x="4" y="11.4" width="16" height="4.8"/>')

# ---- distortion-family stomps (drop inner frames, keep silhouette) ----
N["overdrive"] = ('<rect x="5" y="2.6" width="14" height="18.8" rx="2.4"/>'
  '<circle cx="9.7" cy="7.2" r="1.5" fill="#ffffff" stroke="none"/>'
  '<circle cx="14.3" cy="7.2" r="1.5" fill="#ffffff" stroke="none"/>'
  '<circle cx="12" cy="11.2" r="1.5" fill="#ffffff" stroke="none"/>'
  '<circle cx="12" cy="18" r="2" fill="#ffffff" stroke="none"/>')
N["distortion"] = ('<rect x="5" y="2.6" width="14" height="18.8" rx="2.4"/>'
  '<circle cx="9.7" cy="7.4" r="1.5" fill="#ffffff" stroke="none"/>'
  '<circle cx="14.3" cy="7.4" r="1.5" fill="#ffffff" stroke="none"/>'
  '<circle cx="12" cy="11.6" r="1.5" fill="#ffffff" stroke="none"/>'
  '<circle cx="12" cy="18.2" r="2.4" fill="#ffffff" stroke="none"/>')
N["fuzz"] = ('<path d="M6.5 2.8h11L19 21.2H5z"/>'
  '<circle cx="10.3" cy="7" r="1.2" fill="#ffffff" stroke="none"/>'
  '<circle cx="13.7" cy="7" r="1.2" fill="#ffffff" stroke="none"/>'
  '<circle cx="12" cy="10.6" r="1.2" fill="#ffffff" stroke="none"/>'
  '<circle cx="12" cy="17.4" r="2" fill="#ffffff" stroke="none"/>')
N["autowah"] = ('<rect x="4" y="3.5" width="16" height="17" rx="2"/>'
  '<circle cx="8" cy="7.5" r="1.6" fill="#ffffff" stroke="none"/>'
  '<circle cx="12" cy="7.5" r="1.6" fill="#ffffff" stroke="none"/>'
  '<circle cx="16" cy="7.5" r="1.6" fill="#ffffff" stroke="none"/>'
  '<circle cx="9.5" cy="14.2" r="1.6" fill="#ffffff" stroke="none"/>'
  '<circle cx="14.5" cy="14.2" r="1.6" fill="#ffffff" stroke="none"/>')

# ---- named overdrives (white faces: drop tiny LEDs/dots) -------------
N["bb_overdrive"] = ('<rect x="3" y="3" width="18" height="18" rx="1.6" fill="#ffffff" stroke="none"/>'
  '<rect x="4.6" y="4.6" width="14.8" height="14.8" fill="none" stroke="#1c1d22" stroke-width="1.2" stroke-linejoin="miter"/>'
  '<rect x="6.2" y="6.2" width="11.6" height="3.6" fill="#1c1d22" stroke="none" stroke-linejoin="miter"/>'
  '<circle cx="8.8" cy="8" r="1.05" fill="#ffffff" stroke="none"/>'
  '<circle cx="12" cy="8" r="1.05" fill="#ffffff" stroke="none"/>'
  '<circle cx="15.2" cy="8" r="1.05" fill="#ffffff" stroke="none"/>'
  '<circle cx="12" cy="16.4" r="1.5" fill="#1c1d22" stroke="none"/>')
N["bd_overdrive"] = ('<rect x="5" y="2.2" width="14" height="19.6" rx="1.8" fill="#ffffff" stroke="none"/>'
  '<rect x="6.4" y="3.6" width="11.2" height="16.8" fill="none" stroke="#1c1d22" stroke-width="1.2" stroke-linejoin="miter"/>'
  '<circle cx="9" cy="6.2" r="1.45" fill="#1c1d22" stroke="none"/>'
  '<circle cx="15" cy="6.2" r="1.45" fill="#1c1d22" stroke="none"/>'
  '<circle cx="12" cy="9.6" r="1.2" fill="#1c1d22" stroke="none"/>'
  '<rect x="7.8" y="12.4" width="8.4" height="6" rx="0.6" fill="#1c1d22" stroke="none"/>')
N["k_overdrive"] = ('<rect x="2.2" y="3.5" width="19.6" height="17" rx="2" fill="#ffffff" stroke="none"/>'
  '<rect x="3.6" y="4.9" width="16.8" height="14.2" fill="none" stroke="#1c1d22" stroke-width="1.2" stroke-linejoin="miter"/>'
  '<circle cx="6.8" cy="8.2" r="1.55" fill="#1c1d22" stroke="none"/>'
  '<circle cx="12" cy="8.2" r="1.55" fill="#1c1d22" stroke="none"/>'
  '<circle cx="17.2" cy="8.2" r="1.55" fill="#1c1d22" stroke="none"/>'
  '<circle cx="12" cy="16.2" r="1.4" fill="#1c1d22" stroke="none"/>')
N["ts_overdrive"] = ('<rect x="4.6" y="2.2" width="14.8" height="19.6" rx="2" fill="#ffffff" stroke="none"/>'
  '<rect x="6" y="3.6" width="12" height="16.8" fill="none" stroke="#1c1d22" stroke-width="1.2" stroke-linejoin="miter"/>'
  '<circle cx="8.9" cy="6.5" r="1.3" fill="#1c1d22" stroke="none"/>'
  '<circle cx="15.1" cy="6.5" r="1.3" fill="#1c1d22" stroke="none"/>'
  '<circle cx="12" cy="10" r="1.2" fill="#1c1d22" stroke="none"/>'
  '<rect x="8.4" y="14" width="7.2" height="4.2" fill="none" stroke="#1c1d22" stroke-width="1.2" stroke-linejoin="miter"/>')
# nash_overdrive: kept verbatim (3 knobs + nameplate + pad already reads)

# ---- fuzz / distortion named -----------------------------------------
N["octave_fuzz"] = ('<rect x="2.2" y="4" width="19.6" height="16" rx="1.6"/>'
  '<rect x="4.8" y="6.4" width="14.4" height="4" rx="0.6"/>'
  '<circle cx="8.5" cy="15" r="1.3" fill="#ffffff" stroke="none"/>'
  '<circle cx="12" cy="15" r="1.3" fill="#ffffff" stroke="none"/>'
  '<circle cx="15.5" cy="15" r="1.3" fill="#ffffff" stroke="none"/>')
N["bm_fuzz"] = ('<rect x="5" y="2.2" width="14" height="19.6" rx="1.8" fill="#ffffff" stroke="none"/>'
  '<rect x="6" y="3.4" width="12" height="17.2" fill="none" stroke="#1c1d22" stroke-width="1.2"/>'
  '<rect x="7.6" y="4.8" width="8.8" height="4.8" stroke-linejoin="miter"/>'
  '<circle cx="9.6" cy="7.2" r="1.4" fill="#1c1d22" stroke="none"/>'
  '<circle cx="12" cy="7.2" r="1.4" fill="#1c1d22" stroke="none"/>'
  '<circle cx="14.4" cy="7.2" r="1.4" fill="#1c1d22" stroke="none"/>'
  '<path d="M7.4 13.4H10.2Q12 16.4 13.8 13.4H16.6V18.6a1 1 0 0 1-1 1H8.4a1 1 0 0 1-1-1Z" fill="#1c1d22" stroke="none"/>'
  '<circle cx="12" cy="16.7" r="1.3" fill="#ffffff" stroke="none"/>')
N["fz_fuzz"] = ('<rect x="5.2" y="2.2" width="13.6" height="19.6" rx="1.6"/>'
  '<rect x="6.8" y="3.8" width="10.4" height="16.4" fill="#ffffff" stroke="none"/>'
  '<circle cx="9.4" cy="7.6" r="1.3" fill="#1c1d22" stroke="none"/>'
  '<circle cx="14.6" cy="7.6" r="1.3" fill="#1c1d22" stroke="none"/>'
  '<circle cx="12" cy="10.4" r="1.3" fill="#1c1d22" stroke="none"/>'
  '<rect x="8" y="13" width="8" height="6" fill="#1c1d22" stroke="none"/>')
N["tb_fuzz"] = ('<rect x="6.6" y="1.8" width="10.8" height="20.4" rx="1.6"/>'
  '<circle cx="9.6" cy="6.4" r="1.3" fill="#ffffff" stroke="none"/>'
  '<circle cx="14.4" cy="6.4" r="1.3" fill="#ffffff" stroke="none"/>'
  '<circle cx="12" cy="17.4" r="1.1" fill="#ffffff" stroke="none"/>')
N["ds_distortion"] = ('<rect x="5.8" y="2.2" width="12.4" height="19.6" rx="1.6"/>'
  '<circle cx="9.4" cy="6" r="1.55" fill="#ffffff" stroke="none"/>'
  '<circle cx="14.6" cy="6" r="1.55" fill="#ffffff" stroke="none"/>'
  '<circle cx="12" cy="8.6" r="1.55" fill="#ffffff" stroke="none"/>'
  '<rect x="7.6" y="13.2" width="8.8" height="6.4" fill="#ffffff" stroke="none"/>')
N["oc_distortion"] = ('<rect x="5.8" y="2.2" width="12.4" height="19.6" rx="1.6"/>'
  '<circle cx="9.4" cy="6" r="1.55" fill="#ffffff" stroke="none"/>'
  '<circle cx="14.6" cy="6" r="1.55" fill="#ffffff" stroke="none"/>'
  '<circle cx="12" cy="8.6" r="1.55" fill="#ffffff" stroke="none"/>'
  '<circle cx="12" cy="16.4" r="1.3" fill="#ffffff" stroke="none"/>')
N["rl_distortion"] = ('<rect x="6.2" y="2.2" width="11.6" height="19.6" rx="1.6"/>'
  '<circle cx="9.2" cy="6.2" r="1.45" fill="#ffffff" stroke="none"/>'
  '<circle cx="14.8" cy="6.2" r="1.45" fill="#ffffff" stroke="none"/>'
  '<circle cx="12" cy="15.6" r="1.3" fill="#ffffff" stroke="none"/>')
N["sd_distortion"] = ('<rect x="5.4" y="2.2" width="13.2" height="19.6" rx="1.6"/>'
  '<circle cx="8.8" cy="7" r="1.6" fill="#ffffff" stroke="none"/>'
  '<circle cx="15.2" cy="7" r="1.6" fill="#ffffff" stroke="none"/>'
  '<circle cx="12" cy="10.2" r="1.6" fill="#ffffff" stroke="none"/>'
  '<rect x="7.9" y="13.4" width="8.2" height="5.4" fill="#ffffff" stroke="none"/>')
N["vermin_distortion"] = ('<rect x="4.8" y="2.2" width="14.4" height="19.6" rx="1.8"/>'
  '<rect x="6.4" y="3.8" width="11.2" height="16.4" fill="#ffffff" stroke="none"/>'
  '<rect x="7.6" y="5" width="8.8" height="4.2" fill="#1c1d22" stroke="none"/>'
  '<circle cx="9.6" cy="7.1" r="1.1" fill="#ffffff" stroke="none"/>'
  '<circle cx="12" cy="7.1" r="1.1" fill="#ffffff" stroke="none"/>'
  '<circle cx="14.4" cy="7.1" r="1.1" fill="#ffffff" stroke="none"/>'
  '<rect x="7.6" y="15.8" width="8.8" height="3.6" fill="#1c1d22" stroke="none"/>'
  '<circle cx="12" cy="17.6" r="0.9" fill="#ffffff" stroke="none"/>')

# ---- chorus sub-family -------------------------------------------------
N["ce-chorus"] = ('<rect x="3" y="5" width="18" height="14" rx="2"/>'
  '<rect x="5.8" y="7.2" width="12.4" height="4.8"/>'
  '<circle cx="9.5" cy="9.6" r="1.2" fill="#ffffff" stroke="none"/>'
  '<circle cx="14.5" cy="9.6" r="1.2" fill="#ffffff" stroke="none"/>'
  '<circle cx="8.4" cy="15.6" r="1.5" fill="#ffffff" stroke="none"/>'
  '<circle cx="15.6" cy="15.6" r="1.5" fill="#ffffff" stroke="none"/>')
N["dig-chorus"] = ('<rect x="5.5" y="2.8" width="13" height="18.4" rx="2"/>'
  '<circle cx="9.3" cy="6.2" r="1.2" fill="#ffffff" stroke="none"/>'
  '<circle cx="14.7" cy="6.2" r="1.2" fill="#ffffff" stroke="none"/>'
  '<path d="M8.2 12.6c.8-.8 1.6-.8 2.4 0s1.6.8 2.4 0 1.6-.8 2.4 0" stroke-width="1.6"/>'
  '<circle cx="12" cy="18.2" r="1.4" fill="#ffffff" stroke="none"/>')
N["tri-chorus"] = ('<rect x="4.5" y="2.8" width="15" height="18.4" rx="2"/>'
  '<circle cx="9" cy="6.6" r="1.3" fill="#ffffff" stroke="none"/>'
  '<circle cx="15" cy="6.6" r="1.3" fill="#ffffff" stroke="none"/>'
  '<path d="M12 10.5 16 16H8Z" fill="#ffffff" stroke="none"/>'
  '<circle cx="12" cy="19" r="1.3" fill="#ffffff" stroke="none"/>')
N["dim-chorus"] = ('<rect x="5.5" y="2.8" width="13" height="18.4" rx="2" fill="#ffffff" stroke="none"/>'
  '<rect x="6.9" y="4.2" width="10.2" height="15.6" fill="none" stroke="#1c1d22" stroke-width="1.2" stroke-linejoin="miter"/>'
  '<rect x="8" y="5.2" width="8" height="5.6" rx="0.8" fill="#ffffff" stroke="none"/>'
  '<rect x="9.7" y="6.3" width="1.7" height="3.4" rx="0.4" fill="#1c1d22" stroke="none"/>'
  '<rect x="12.6" y="6.3" width="1.7" height="3.4" rx="0.4" fill="#1c1d22" stroke="none"/>'
  '<rect x="8.6" y="14.2" width="6.8" height="4.2" rx="0.6" fill="#ffffff" stroke="#1c1d22" stroke-width="1.2"/>')

# ---- tremolo sub-family (8/7-knob rows -> 2-3 big + switch) ----------
N["analog-tremolo"] = ('<rect x="5.5" y="2.8" width="13" height="18.4" rx="2"/>'
  '<circle cx="9.8" cy="7.4" r="1.6" fill="#ffffff" stroke="none"/>'
  '<circle cx="14.2" cy="7.4" r="1.6" fill="#ffffff" stroke="none"/>'
  '<rect x="8.3" y="14.6" width="7.4" height="4" rx="1" fill="#ffffff" stroke="none"/>')
N["bias-tremolo"] = ('<rect x="2.5" y="5" width="19" height="14" rx="2"/>'
  '<circle cx="7.2" cy="9.2" r="1.4" fill="#ffffff" stroke="none"/>'
  '<circle cx="12" cy="9.2" r="1.4" fill="#ffffff" stroke="none"/>'
  '<circle cx="16.8" cy="9.2" r="1.4" fill="#ffffff" stroke="none"/>'
  '<circle cx="12" cy="15.6" r="1.4" fill="#ffffff" stroke="none"/>')
N["digital-tremolo"] = ('<rect x="5.5" y="2.8" width="13" height="18.4" rx="2"/>'
  '<circle cx="9.7" cy="7.9" r="1.45" fill="#ffffff" stroke="none"/>'
  '<circle cx="14.3" cy="7.9" r="1.45" fill="#ffffff" stroke="none"/>'
  '<path d="M8.8 12.6c.7-.7 1.4-.7 2.1 0s1.4.7 2.1 0 1.4-.7 2.1 0" stroke-width="1.4"/>'
  '<circle cx="12" cy="18" r="1.5" fill="#ffffff" stroke="none"/>')
N["harmonic-tremolo"] = ('<rect x="5.5" y="2.8" width="13" height="18.4" rx="2"/>'
  '<circle cx="9.7" cy="7.9" r="1.5" fill="#ffffff" stroke="none"/>'
  '<circle cx="14.3" cy="7.9" r="1.5" fill="#ffffff" stroke="none"/>'
  '<circle cx="12" cy="12.6" r="1.5" fill="#ffffff" stroke="none"/>'
  '<circle cx="12" cy="18" r="1.5" fill="#ffffff" stroke="none"/>')
N["opto-tremolo"] = ('<rect x="5.5" y="2.8" width="13" height="18.4" rx="2"/>'
  '<circle cx="9.7" cy="7.9" r="1.5" fill="#ffffff" stroke="none"/>'
  '<circle cx="14.3" cy="7.9" r="1.5" fill="#ffffff" stroke="none"/>'
  '<circle cx="9.7" cy="12.6" r="1.5" fill="#ffffff" stroke="none"/>'
  '<circle cx="14.3" cy="12.6" r="1.5" fill="#ffffff" stroke="none"/>'
  '<circle cx="12" cy="18" r="1.5" fill="#ffffff" stroke="none"/>')

# ---- phasers -----------------------------------------------------------
N["rotary-phaser"] = ('<path d="M3.6 7.4 10.4 4.2l10 1.4v13.6l-10.6 3.2-6.8-3.2z"/>'
  '<path d="M10.4 4.2v18.2"/>'
  '<path d="M5.2 11.5h3.4"/>'
  '<path d="M5.2 16.5h3.4"/>'
  '<path d="M12.4 10h5.6"/>'
  '<path d="M12.4 15h5.6"/>')
N["uni-phaser"] = ('<rect x="2" y="3.8" width="20" height="16.4" rx="2"/>'
  '<circle cx="6" cy="10.5" r="1.8" fill="#ffffff" stroke="none"/>'
  '<circle cx="12" cy="10.5" r="1.8" fill="#ffffff" stroke="none"/>'
  '<circle cx="18" cy="10.5" r="1.8" fill="#ffffff" stroke="none"/>'
  '<rect x="10.2" y="14.4" width="3.6" height="2.8" fill="#ffffff" stroke="none"/>')

# ---- compressors --------------------------------------------------------
N["vca-compressor"] = ('<rect x="1.6" y="5.4" width="20.8" height="13.2" rx="1.6"/>'
  '<circle cx="5.4" cy="12" r="1.5" fill="#ffffff" stroke="none"/>'
  '<circle cx="10.2" cy="12" r="1.5" fill="#ffffff" stroke="none"/>'
  '<circle cx="15" cy="12" r="1.5" fill="#ffffff" stroke="none"/>'
  '<rect x="17.2" y="9" width="3.2" height="6" fill="#ffffff" stroke="none"/>')
N["tube-compressor"] = ('<rect x="1.6" y="5.4" width="20.8" height="13.2" rx="1.6"/>'
  '<circle cx="4.9" cy="12" r="2" fill="#ffffff" stroke="none"/>'
  '<path d="M10.4 9.4a1.6 1.6 0 0 1 3.2 0v3.8h-3.2Z"/>'
  '<circle cx="19.1" cy="12" r="2" fill="#ffffff" stroke="none"/>')
N["vari-mu-compressor"] = ('<rect x="1.6" y="5.4" width="20.8" height="13.2" rx="1.6"/>'
  '<circle cx="4.8" cy="12" r="1.9" fill="#ffffff" stroke="none"/>'
  '<circle cx="10.2" cy="12" r="1.6"/>'
  '<circle cx="13.8" cy="12" r="1.6"/>'
  '<circle cx="19.2" cy="12" r="1.9" fill="#ffffff" stroke="none"/>')

# ---- delays --------------------------------------------------------------
N["mag-echo"] = ('<rect x="4" y="4" width="16" height="16" rx="2"/>'
  '<circle cx="12" cy="12" r="3.2"/>'
  '<circle cx="7" cy="7" r="1.2" fill="#ffffff" stroke="none"/>'
  '<circle cx="17" cy="7" r="1.2" fill="#ffffff" stroke="none"/>')
N["bbd"] = ('<rect x="3.5" y="3.5" width="17" height="17" rx="2"/>'
  '<circle cx="10.8" cy="16.8" r="1.9" fill="#ffffff" stroke="none"/>'
  '<circle cx="12.9" cy="8.4" r="1.2" fill="#ffffff" stroke="none"/>'
  '<circle cx="16.9" cy="8.4" r="1.2" fill="#ffffff" stroke="none"/>'
  '<circle cx="16.9" cy="12" r="1.2" fill="#ffffff" stroke="none"/>'
  '<circle cx="16.9" cy="15.6" r="1.2" fill="#ffffff" stroke="none"/>')
N["analog-delay"] = ('<rect x="5.5" y="2.8" width="13" height="18.4" rx="2"/>'
  '<circle cx="9.6" cy="6.2" r="1.3" fill="#ffffff" stroke="none"/>'
  '<circle cx="14.4" cy="6.2" r="1.3" fill="#ffffff" stroke="none"/>'
  '<rect x="10" y="9.2" width="4" height="3"/>'
  '<circle cx="12" cy="17" r="1.8" fill="#ffffff" stroke="none"/>')
N["tape-delay"] = ('<rect x="3" y="3.5" width="18" height="17" rx="2"/>'
  '<circle cx="8.3" cy="9.5" r="2.2"/>'
  '<circle cx="15.7" cy="9.5" r="2.2"/>'
  '<circle cx="8.3" cy="9.5" r="0.8" fill="#ffffff" stroke="none"/>'
  '<circle cx="15.7" cy="9.5" r="0.8" fill="#ffffff" stroke="none"/>'
  '<circle cx="12" cy="17" r="1.7" fill="#ffffff" stroke="none"/>')


N["digital-delay"] = ('<rect x="5.5" y="2.8" width="13" height="18.4" rx="2"/>'
  '<circle cx="9.4" cy="6.4" r="1.5" fill="#ffffff" stroke="none"/>'
  '<circle cx="14.6" cy="6.4" r="1.5" fill="#ffffff" stroke="none"/>'
  '<rect x="8.4" y="10.6" width="7.2" height="5.6" rx="1" fill="#ffffff" stroke="none"/>')
N["ep-delay"] = ('<rect x="2.5" y="5" width="19" height="14" rx="2"/>'
  '<path d="M4.5 13 6.5 5.5h11L19.5 13z"/>'
  '<circle cx="12" cy="9" r="2"/>'
  '<path d="M4.5 13h15"/>'
  '<circle cx="8" cy="16.2" r="1" fill="#ffffff" stroke="none"/>'
  '<circle cx="12" cy="16.2" r="1" fill="#ffffff" stroke="none"/>'
  '<circle cx="16" cy="16.2" r="1" fill="#ffffff" stroke="none"/>')
N["mod-delay"] = ('<rect x="5.5" y="2.8" width="13" height="18.4" rx="2"/>'
  '<circle cx="8.8" cy="6.2" r="1.9" fill="#ffffff" stroke="none"/>'
  '<circle cx="15.2" cy="6.2" r="1.9" fill="#ffffff" stroke="none"/>'
  '<circle cx="12" cy="16.8" r="2" fill="#ffffff" stroke="none"/>')
N["se-delay"] = ('<rect x="1.6" y="5.4" width="20.8" height="13.2" rx="1.6"/>'
  '<circle cx="5" cy="10" r="1.4" fill="#ffffff" stroke="none"/>'
  '<circle cx="19" cy="10" r="1.4" fill="#ffffff" stroke="none"/>'
  '<circle cx="12" cy="10" r="3.2"/>'
  '<circle cx="12" cy="10" r="1.2" stroke-width="1.6"/>')
N["shimmer-delay"] = ('<path d="M12 3A9.3 9.3 0 1 0 12 21A13 13 0 0 1 12 3z" fill="#ffffff" stroke="none"/>'
  '<circle cx="18.6" cy="6.4" r="1.1" fill="#ffffff" stroke="none"/>'
  '<circle cx="20.4" cy="12.4" r="1.1" fill="#ffffff" stroke="none"/>'
  '<circle cx="17.6" cy="18.2" r="1.1" fill="#ffffff" stroke="none"/>')

# ---- reverbs -------------------------------------------------------------
N["plate-reverb"] = ('<rect x="2.8" y="4" width="18.4" height="16" rx="2"/>'
  '<rect x="5.6" y="6.4" width="12.8" height="11.2"/>'
  '<rect x="7.9" y="8.2" width="2" height="7.6" fill="#ffffff" stroke="none"/>'
  '<rect x="11" y="8.2" width="2" height="7.6" fill="#ffffff" stroke="none"/>'
  '<rect x="14.1" y="8.2" width="2" height="7.6" fill="#ffffff" stroke="none"/>')
N["room-reverb"] = ('<path d="M12 2.5 20 7v10l-8 4.5-8-4.5V7z"/>'
  '<circle cx="12" cy="12" r="1.9"/>')

# ---- tone-zone -------------------------------------------------------------
N["experimental"] = ('<path d="M9.8 3h4.4"/>'
  '<path d="M10 3v4.8L4.4 17.4A2 2 0 0 0 6.2 20.4h11.6a2 2 0 0 0 1.8-3L14 7.8V3"/>'
  '<path d="M6.6 14h10.8" stroke-width="1.6"/>'
  '<circle cx="12" cy="17" r="1" fill="#ffffff" stroke="none"/>')

# ---- everything else: verbatim copy ------------------------------------------
LIB = HERE / "*.svg"
counts = {"changed": 0, "copied": 0}
for lib in sorted(HERE.glob("*.svg")):
    name = lib.name[:-len(".svg")]
    if name in N:
        (OUT / (name + ".svg")).write_text(HEAD + N[name] + "</svg>\n")
        counts["changed"] += 1
    else:
        (OUT / (name + ".svg")).write_text(lib.read_text())
        counts["copied"] += 1
print(f"node: {counts['changed']} changed, {counts['copied']} verbatim "
      f"({counts['changed'] + counts['copied']} total)")
