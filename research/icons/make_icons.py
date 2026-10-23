#!/usr/bin/env python3
# One-time authoring sheet for the SVG icon-library ticket (ui-svg-icon-library-draft).
# Writes the proposed icons into research/icons/*.svg — 24-unit space, white-authored
# (fill/stroke #ffffff) so Icons::draw's replaceColour(white -> tint) path handles them,
# round caps/joins, Lucide house stroke 2. Reference corpus: "SVG Style Images/"
# generic_*.png (style only — shapes are original).
import pathlib

OUT = pathlib.Path(__file__).parent
HEAD = '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">'

def w(name, body):
    (OUT / name).write_text(HEAD + body + "</svg>\n")

# ---- effect families -----------------------------------------------
w("amp.svg",  # amp head (NAM): double-outlined chassis, knob row, grille field
  '<rect x="2.4" y="4.6" width="19.2" height="14.8" rx="2"/>'
  '<rect x="4.9" y="7.1" width="14.2" height="3.4"/>'
  '<circle cx="7.7" cy="8.8" r="0.9" fill="#ffffff" stroke="none"/>'
  '<circle cx="12" cy="8.8" r="0.9" fill="#ffffff" stroke="none"/>'
  '<circle cx="16.3" cy="8.8" r="0.9" fill="#ffffff" stroke="none"/>'
  '<rect x="4.9" y="12" width="14.2" height="4.4"/>')

w("cabinet.svg",  # cab: double-outlined square + speaker cone
  '<rect x="3" y="3" width="18" height="18" rx="2"/>'
  '<rect x="5.6" y="5.6" width="12.8" height="12.8"/>'
  '<circle cx="12" cy="12" r="4.4" stroke-width="2.4"/>'
  '<circle cx="12" cy="12" r="1.5"/>')

w("combo.svg",  # head + cab in one box: control strip over grille
  '<rect x="2.4" y="4" width="19.2" height="16" rx="2"/>'
  '<rect x="4.9" y="6.3" width="14.2" height="3.4"/>'
  '<circle cx="7.7" cy="8" r="0.9" fill="#ffffff" stroke="none"/>'
  '<circle cx="10.9" cy="8" r="0.9" fill="#ffffff" stroke="none"/>'
  '<circle cx="14.1" cy="8" r="0.9" fill="#ffffff" stroke="none"/>'
  '<circle cx="16.9" cy="8" r="0.9" fill="#ffffff" stroke="none"/>'
  '<rect x="4.9" y="11.4" width="14.2" height="6.6"/>'
  '<circle cx="12" cy="14.7" r="1.6"/>')

w("outboard.svg",  # stompbox chassis: knob row + display window
  '<rect x="2.4" y="6.4" width="19.2" height="11.2" rx="2"/>'
  '<rect x="4.9" y="8.9" width="14.2" height="6.2"/>'
  '<circle cx="7.8" cy="10.9" r="1" fill="#ffffff" stroke="none"/>'
  '<circle cx="10.4" cy="10.9" r="1" fill="#ffffff" stroke="none"/>'
  '<circle cx="13" cy="10.9" r="1" fill="#ffffff" stroke="none"/>'
  '<circle cx="15.6" cy="10.9" r="1" fill="#ffffff" stroke="none"/>'
  '<rect x="7.7" y="13.3" width="8.6" height="1.3" fill="#ffffff" stroke="none"/>')

w("compressor.svg",  # VU meter: arc, tick, needle, pivot
  '<path d="M5.2 16.5a6.8 6.8 0 0 1 13.6 0"/>'
  '<path d="M12 9.1v1.9"/>'
  '<path d="M12 16.5 15.2 11.5"/>'
  '<circle cx="12" cy="16.5" r="1.3" fill="#ffffff" stroke="none"/>')

w("reverb.svg",  # isometric solid cube (reads as "room")
  '<path d="M12 2.6 20.2 7.55v9.9L12 22.4l-8.2-4.95v-9.9z"/>'
  '<path d="M12 7.2 16.85 10.05v4.9L12 17.8l-4.85-2.85v-4.9z" stroke-width="1.6"/>')

w("delay.svg",  # echo burst `(((( : concentric left-bulging arcs, shrinking
  # rightward (the staggered-delay reference: waves travel left, no source dot)
  '<path d="M7.5 5a8.75 8.75 0 0 0 0 14"/>'
  '<path d="M11 7a6.25 6.25 0 0 0 0 10"/>'
  '<path d="M14.5 8.8a4 4 0 0 0 0 6.4"/>'
  '<path d="M18 10.4a2 2 0 0 0 0 3.2"/>')
w("flanger.svg",  # jet-vortex spiral (the flanger "sweep")
  '<path d="M12 12a1.5 1.5 0 0 1 3 0 3 3 0 0 1-6 0 4.5 4.5 0 0 1 9 0 6 6 0 0 1-12 0 7.5 7.5 0 0 1 15 0"/>')
w("overdrive.svg",  # stomp: double frame, 3-knob triangle, footswitch
  '<rect x="5" y="2.6" width="14" height="18.8" rx="2.4"/>'
  '<rect x="7.5" y="5.1" width="9" height="13.8"/>'
  '<circle cx="10.3" cy="8" r="1.3" fill="#ffffff" stroke="none"/>'
  '<circle cx="13.7" cy="8" r="1.3" fill="#ffffff" stroke="none"/>'
  '<circle cx="12" cy="11.3" r="1.3" fill="#ffffff" stroke="none"/>'
  '<circle cx="12" cy="18.4" r="1.9" fill="#ffffff" stroke="none"/>')

w("distortion.svg",  # stomp: double frame, 2+1 knobs, big footswitch
  '<rect x="5" y="2.6" width="14" height="18.8" rx="2.4"/>'
  '<rect x="7.5" y="5.1" width="9" height="9.4"/>'
  '<circle cx="10" cy="7.9" r="1.2" fill="#ffffff" stroke="none"/>'
  '<circle cx="14" cy="7.9" r="1.2" fill="#ffffff" stroke="none"/>'
  '<circle cx="12" cy="11.3" r="1.2" fill="#ffffff" stroke="none"/>'
  '<circle cx="12" cy="18.2" r="2.4" fill="#ffffff" stroke="none"/>')

w("fuzz.svg",  # trapezoid pedal, inner panel, 3-knob triangle
  '<path d="M6.5 2.8h11L19 21.2H5z"/>'
  '<path d="M8.4 5.2h7.2l.9 7.2H7.5z"/>'
  '<circle cx="10.1" cy="7.3" r="1" fill="#ffffff" stroke="none"/>'
  '<circle cx="13.9" cy="7.3" r="1" fill="#ffffff" stroke="none"/>'
  '<circle cx="12" cy="10.2" r="1" fill="#ffffff" stroke="none"/>'
  '<circle cx="12" cy="17.4" r="2" fill="#ffffff" stroke="none"/>')

# ---- reverb sub-families -------------------------------------------
w("ir-speaker.svg",  # speaker cone + radiating arcs (top/bottom/left/right)
  '<circle cx="12" cy="12" r="5"/>'
  '<circle cx="12" cy="12" r="1.9"/>'
  '<path d="M9 3.6a9 9 0 0 1 6 0" stroke-width="1.8"/>'
  '<path d="M9 20.4a9 9 0 0 0 6 0" stroke-width="1.8"/>'
  '<path d="M3.6 9a9 9 0 0 0 0 6" stroke-width="1.8"/>'
  '<path d="M20.4 9a9 9 0 0 1 0 6" stroke-width="1.8"/>')

w("spring-reverb.svg",  # staggered spring coils
  '<path d="M3.5 8c1.3-2.4 2.7-2.4 4 0s2.7 2.4 4 0 2.7-2.4 4 0 2.7 2.4 4 0 2.5-2.4 3.5 0"/>'
  '<path d="M3.5 12.5c1.3-2.4 2.7-2.4 4 0s2.7 2.4 4 0 2.7-2.4 4 0 2.7 2.4 4 0 2.5-2.4 3.5 0"/>'
  '<path d="M3.5 17c1.3-2.4 2.7-2.4 4 0s2.7 2.4 4 0 2.7-2.4 4 0 2.7 2.4 4 0 2.5-2.4 3.5 0"/>')

w("chamber-reverb.svg",  # room in perspective: front box + recessed ceiling
  '<path d="M4.5 7.5h15V21h-15z"/>'
  '<path d="M4.5 7.5 8 3.5h8l3.5 4"/>'
  '<path d="M12 21v-6.5" stroke-width="1.6"/>')

w("hall-reverb.svg",  # arch facade with pillars
  '<path d="M4.5 21.2v-9.4a7.5 7.5 0 0 1 15 0v9.4"/>'
  '<path d="M12 21.2v-9.6"/>'
  '<path d="M2.8 21.2h18.4"/>')

# ---- more pedals ----------------------------------------------------
w("tuner.svg",  # plain tuning fork: two prongs + stem + foot
  '<path d="M7.5 2.8v7a4.5 4.5 0 0 0 9 0v-7"/>'
  '<path d="M12 14.3v5.7"/>'
  '<path d="M8.8 20h6.4"/>')
w("pedal.svg",  # generic stomp: chassis + knob + footswitch (form factor only)
  '<path d="M7 3.5h10a1.5 1.5 0 0 1 1.5 1.5v14a1.5 1.5 0 0 1-1.5 1.5H7A1.5 1.5 0 0 1 5.5 19V5A1.5 1.5 0 0 1 7 3.5Z"/>'
  '<circle cx="12" cy="9" r="2.6" fill="#ffffff" stroke="none"/>'
  '<path d="M8.5 17.5h7"/>')
w("wave.svg",  # waveform generator: clean two-cycle triangle wave
  '<path d="M2.5 13 7 5.5 12 18.5 17 5.5 21.5 13"/>')
w("volume.svg",  # volume: speaker + two radiating arcs
  '<path d="M11 5 6.5 9H3v6h3.5L11 19V5Z"/>'
  '<path d="M14.5 9.5a4 4 0 0 1 0 5"/>'
  '<path d="M17.5 7a8 8 0 0 1 0 10"/>')
w("looper.svg",  # loop ring with head dot
  '<path d="M16.6 7.4A7 7 0 1 0 19 12"/>'
  '<circle cx="17.8" cy="7.2" r="2" fill="#ffffff" stroke="none"/>')

# ---- graph / signal glyphs ------------------------------------------
w("graph-in.svg",  # signal in: line into port
  '<path d="M2.6 12h9.4"/>'
  '<circle cx="17.4" cy="12" r="3"/>')

w("graph-out.svg",  # signal out: port into line
  '<circle cx="6.6" cy="12" r="3"/>'
  '<path d="M9.6 12h11.8"/>')

w("graph-split.svg",  # fork: one in, two out
  '<path d="M2.6 12h4.4c2.2 0 3.4 0 4.9 1.5s2.5 3 4.3 3h5.2"/>'
  '<path d="M7 12c2.2 0 3.4 0 4.9-1.5S14.4 7.5 16.2 7.5h5.2"/>'
  '<circle cx="19.4" cy="7.5" r="1.3" fill="#ffffff" stroke="none"/>'
  '<circle cx="19.4" cy="16.5" r="1.3" fill="#ffffff" stroke="none"/>')

w("graph-merge.svg",  # sum: two in, one out
  '<path d="M2.6 7.5h5.2c1.8 0 2.8 0 4.3 1.5s2.7 3 4.9 3h4.4"/>'
  '<path d="M2.6 16.5h5.2c2.2 0 3.4 0 4.9-1.5s2.5-3 4.3-3h4.4"/>'
  '<circle cx="19.6" cy="12" r="1.3" fill="#ffffff" stroke="none"/>'
  '<circle cx="4.6" cy="7.5" r="1.3" fill="#ffffff" stroke="none"/>'
  '<circle cx="4.6" cy="16.5" r="1.3" fill="#ffffff" stroke="none"/>')

w("graph-bypass.svg",  # straight through, no node
  '<path d="M2.6 12h18.8"/>'
  '<path d="M18 8.4 21.4 12 18 15.6"/>')

w("phaser.svg",  # phase cancellation: jagged squiggle flattening out
  '<path d="M2.5 12 5 7l2.5 8 2.5-6 2.5 4 2.5-5 2.5 4h4.5"/>'
)
w("gate.svg",  # noise gate: wicket/gate post with swinging bar on ground line
  '<path d="M3.5 20.5h17"/>'
  '<path d="M7.5 20.5V4.5"/>'
  '<path d="M7.5 6.5 18 11"/>')
w("wah.svg",  # tilting stomp: low base + top plate hinged up on the right
  '<path d="M5 18.5h14"/>'
  '<path d="M5.5 18.5V15a1 1 0 0 1 1-1H18a1 1 0 0 1 1 1v3.5"/>'
  '<path d="M16 14 8.5 5.5a1.3 1.3 0 0 0-1.9.2l-1.2 1.7a1.3 1.3 0 0 0 .3 1.8"/>'
  '<path d="M8.5 5.5 16 14"/>'
  '<circle cx="9.5" cy="11.5" r="1.5" fill="#ffffff" stroke="none"/>')
w("autowah.svg",  # auto-wah: 3-knob row (top) + 3 vertical switch row (below)
  '<rect x="4" y="3.5" width="16" height="17" rx="2"/>'
  '<circle cx="8" cy="7.5" r="1.6" fill="#ffffff" stroke="none"/>'
  '<circle cx="12" cy="7.5" r="1.6" fill="#ffffff" stroke="none"/>'
  '<circle cx="16" cy="7.5" r="1.6" fill="#ffffff" stroke="none"/>'
  '<rect x="6.2" y="11.5" width="3" height="4.5" rx="0.8"/>'
  '<rect x="10.5" y="11.5" width="3" height="4.5" rx="0.8"/>'
  '<rect x="14.8" y="11.5" width="3" height="4.5" rx="0.8"/>')
w("preamp.svg",  # rack preamp: face + centered 4-knob row
  '<rect x="3" y="6" width="18" height="12" rx="2"/>'
  '<circle cx="6.7" cy="12" r="1.3" fill="#ffffff" stroke="none"/>'
  '<circle cx="10.2" cy="12" r="1.3" fill="#ffffff" stroke="none"/>'
  '<circle cx="13.7" cy="12" r="1.3" fill="#ffffff" stroke="none"/>'
  '<circle cx="17.2" cy="12" r="1.3" fill="#ffffff" stroke="none"/>')
w("poweramp.svg",  # tube amp: chassis + 3 power tubes + volume knob
  '<rect x="3" y="14" width="18" height="6.5" rx="1.5"/>'
  '<path d="M6.5 14V7.5a1.5 1.5 0 0 1 3 0V14"/>'
  '<path d="M14.5 14V7.5a1.5 1.5 0 0 1 3 0V14"/>'
  '<path d="M10.5 14v-3a1.5 1.5 0 0 1 3 0v3"/>'
  '<circle cx="18.5" cy="17.2" r="1.2" fill="#ffffff" stroke="none"/>')
w("preamp-tube.svg",  # preamp tube (12AX7): slim capsule + stem + 3 pins
  '<path d="M9 10.5a3 3 0 0 1 6 0V17a1 1 0 0 1-1 1h-4a1 1 0 0 1-1-1Z"/>'
  '<path d="M12 3v3.5"/>'
  '<path d="M9.5 21v-3M12 21v-3M14.5 21v-3"/>')
w("power-tube.svg",  # power tube (6L6): fat capsule + inset base + 3 short pins
  '<path d="M8 10a4 4 0 0 1 8 0v5a1 1 0 0 1-1 1H9a1 1 0 0 1-1-1Z"/>'
  '<path d="M9 17v1.8M12 17v1.8M15 17v1.8"/>'
  '<path d="M7.5 16.7h9" stroke-width="2.8"/>')
w("pan.svg",  # pan: L/R arrows + fader with ticks
  '<path d="M3 12h18"/>'
  '<path d="m6 8-3 4 3 4"/>'
  '<path d="m18 8 3 4-3 4"/>'
  '<path d="M9.5 9v6M14.5 9v6"/>')
w("rotary.svg",  # rotary speaker: sweep arc + speaker cone
  '<path d="M14 8.5a5 5 0 1 0 3 6.5"/>'
  '<path d="M17 5a8.5 8.5 0 0 1 3.6 5.2"/>'
  '<circle cx="14" cy="12" r="1.6" fill="#ffffff" stroke="none"/>'
  '<path d="m8.5 15.5-4 3 6-1z" fill="#ffffff" stroke="none"/>')
w("pitch.svg",  # pitch shifter: wave + up arrow
  '<path d="M3 9c1.5-3 3-3 4.5 0s3 3 4.5 0 3-3 4.5 0 3 3 4.5 0"/>'
  '<path d="M12 13.5v7"/>'
  '<path d="m8.5 17 3.5-3.5L15.5 17"/>')
w("mag-echo.svg",  # magnetic echo (Memory Man / Echorec-style console): reel + 2 corner knobs
  '<rect x="4" y="4" width="16" height="16" rx="2"/>'
  '<circle cx="12" cy="12" r="3.2"/>'
  '<circle cx="12" cy="12" r="0.7" fill="#ffffff" stroke="none"/>'
  '<circle cx="7" cy="7" r="1.2" fill="#ffffff" stroke="none"/>'
  '<circle cx="17" cy="7" r="1.2" fill="#ffffff" stroke="none"/>')
w("bbd.svg",  # BBD/analog delay (Memory Man): pedal + diagonal + knob + 3 dots
  '<rect x="3.5" y="3.5" width="17" height="17" rx="2"/>'
  '<path d="M7.5 6.5 14 12.9"/>'
  '<circle cx="10.8" cy="16.8" r="1.9" fill="#ffffff" stroke="none"/>'
  '<circle cx="16.9" cy="8.4" r="1.2" fill="#ffffff" stroke="none"/>'
  '<circle cx="16.9" cy="12" r="1.2" fill="#ffffff" stroke="none"/>'
  '<circle cx="16.9" cy="15.6" r="1.2" fill="#ffffff" stroke="none"/>')

# ---- chorus sub-family (refs: SVG Style Images/Chorus/*) ---------
w("analog-chorus.svg",  # analog chorus: plain stomp, TWO big knobs + footswitch
  '<rect x="5.5" y="2.8" width="13" height="18.4" rx="2"/>'
  '<circle cx="9.4" cy="8" r="1.9" fill="#ffffff" stroke="none"/>'
  '<circle cx="14.6" cy="8" r="1.9" fill="#ffffff" stroke="none"/>'
  '<circle cx="12" cy="17.6" r="1.6" fill="#ffffff" stroke="none"/>')
w("ce-chorus.svg",  # CE/outboard chorus: wide face, 5-dot row, 2 corner knobs
  '<rect x="3" y="5" width="18" height="14" rx="2"/>'
  '<rect x="5.8" y="7.2" width="12.4" height="4.8"/>'
  '<circle cx="7.9" cy="9.6" r="1" fill="#ffffff" stroke="none"/>'
  '<circle cx="9.95" cy="9.6" r="1" fill="#ffffff" stroke="none"/>'
  '<circle cx="12" cy="9.6" r="1" fill="#ffffff" stroke="none"/>'
  '<circle cx="14.05" cy="9.6" r="1" fill="#ffffff" stroke="none"/>'
  '<circle cx="16.1" cy="9.6" r="1" fill="#ffffff" stroke="none"/>'
  '<circle cx="8.4" cy="15.6" r="1.4" fill="#ffffff" stroke="none"/>'
  '<circle cx="15.6" cy="15.6" r="1.4" fill="#ffffff" stroke="none"/>')
w("dig-chorus.svg",  # digital chorus: stomp with 3 wavy lines + footswitch
  '<rect x="5.5" y="2.8" width="13" height="18.4" rx="2"/>'
  '<circle cx="9.3" cy="6.2" r="1.1" fill="#ffffff" stroke="none"/>'
  '<circle cx="14.7" cy="6.2" r="1.1" fill="#ffffff" stroke="none"/>'
  '<path d="M8.2 10c.8-.8 1.6-.8 2.4 0s1.6.8 2.4 0 1.6-.8 2.4 0" stroke-width="1.6"/>'
  '<path d="M8.2 12.6c.8-.8 1.6-.8 2.4 0s1.6.8 2.4 0 1.6-.8 2.4 0" stroke-width="1.6"/>'
  '<path d="M8.2 15.2c.8-.8 1.6-.8 2.4 0s1.6.8 2.4 0 1.6-.8 2.4 0" stroke-width="1.6"/>'
  '<circle cx="12" cy="18.9" r="1.3" fill="#ffffff" stroke="none"/>')
w("tri-chorus.svg",  # tri chorus: LED dot column + filled triangle + two knobs
  '<rect x="4.5" y="2.8" width="15" height="18.4" rx="2"/>'
  '<circle cx="8.6" cy="6.3" r="1" fill="#ffffff" stroke="none"/>'
  '<circle cx="12" cy="6.3" r="1" fill="#ffffff" stroke="none"/>'
  '<circle cx="15.4" cy="6.3" r="1" fill="#ffffff" stroke="none"/>'
  '<circle cx="7.2" cy="10.4" r="0.55" fill="#ffffff" stroke="none"/>'
  '<circle cx="7.2" cy="12.4" r="0.55" fill="#ffffff" stroke="none"/>'
  '<circle cx="7.2" cy="14.4" r="0.55" fill="#ffffff" stroke="none"/>'
  '<circle cx="7.2" cy="16.4" r="0.55" fill="#ffffff" stroke="none"/>'
  '<path d="M13.4 10.2 16 14.6H10.8z" fill="#ffffff" stroke="none"/>'
  '<circle cx="10" cy="18.6" r="1.1" fill="#ffffff" stroke="none"/>'
  '<circle cx="16" cy="18.6" r="1.1" fill="#ffffff" stroke="none"/>')
w("dim-chorus.svg",  # dim chorus: stomp with bar display (3 thick bars in window) + footswitch
  '<rect x="5.5" y="2.8" width="13" height="18.4" rx="2"/>'
  '<rect x="8" y="5.2" width="8" height="5.6" rx="0.8"/>'
  '<rect x="9.5" y="6.3" width="1.3" height="3.4" rx="0.65" fill="#ffffff" stroke="none"/>'
  '<rect x="11.35" y="6.3" width="1.3" height="3.4" rx="0.65" fill="#ffffff" stroke="none"/>'
  '<rect x="13.2" y="6.3" width="1.3" height="3.4" rx="0.65" fill="#ffffff" stroke="none"/>'
  '<circle cx="9.3" cy="13.6" r="1.1" fill="#ffffff" stroke="none"/>'
  '<circle cx="14.7" cy="13.6" r="1.1" fill="#ffffff" stroke="none"/>'
  '<circle cx="12" cy="18.4" r="1.5" fill="#ffffff" stroke="none"/>')
w("analog-tremolo.svg",  # analog tremolo: stomp + 3-knob cluster + filled footswitch
  '<rect x="5.5" y="2.8" width="13" height="18.4" rx="2"/>'
  '<circle cx="9.8" cy="7" r="1.3" fill="#ffffff" stroke="none"/>'
  '<circle cx="14.2" cy="7" r="1.3" fill="#ffffff" stroke="none"/>'
  '<circle cx="12" cy="10" r="1.3" fill="#ffffff" stroke="none"/>'
  '<rect x="8.3" y="14.6" width="7.4" height="4" rx="1" fill="#ffffff" stroke="none"/>')
w("bias-tremolo.svg",  # bias tremolo: wide outboard, 4-knob row + vent slots + 3 footswitches
  '<rect x="2.5" y="5" width="19" height="14" rx="2"/>'
  '<circle cx="5.6" cy="9.2" r="1.2" fill="#ffffff" stroke="none"/>'
  '<circle cx="8.2" cy="9.2" r="1.2" fill="#ffffff" stroke="none"/>'
  '<circle cx="10.8" cy="9.2" r="1.2" fill="#ffffff" stroke="none"/>'
  '<circle cx="13.4" cy="9.2" r="1.2" fill="#ffffff" stroke="none"/>'
  '<path d="M17.2 7.6h3" stroke-width="1.6"/>'
  '<path d="M17.2 10.4h3" stroke-width="1.6"/>'
  '<circle cx="7.6" cy="15.6" r="1.2" fill="#ffffff" stroke="none"/>'
  '<circle cx="12.8" cy="15.6" r="1.2" fill="#ffffff" stroke="none"/>'
  '<circle cx="18" cy="15.6" r="1.2" fill="#ffffff" stroke="none"/>')
w("digital-tremolo.svg",  # digital tremolo: stomp + black-filled face + 2 top knobs + 3-knob row + 2 bottom knobs
  '<rect x="5.5" y="2.8" width="13" height="18.4" rx="2"/>'
  '<circle cx="9.7" cy="7.9" r="1.15" fill="#ffffff" stroke="none"/>'
  '<circle cx="14.3" cy="7.9" r="1.15" fill="#ffffff" stroke="none"/>'
  '<circle cx="9.8" cy="11.5" r="1.2" fill="#ffffff" stroke="none"/>'
  '<circle cx="12" cy="11.5" r="1.2" fill="#ffffff" stroke="none"/>'
  '<circle cx="14.2" cy="11.5" r="1.2" fill="#ffffff" stroke="none"/>'
  '<path d="M9.1 14c.65-.65 1.3-.65 1.95 0s1.3.65 1.95 0 1.3-.65 1.95 0" stroke-width="1.4"/>'
  '<circle cx="9.7" cy="17.2" r="1.2" fill="#ffffff" stroke="none"/>'
  '<circle cx="14.3" cy="17.2" r="1.2" fill="#ffffff" stroke="none"/>')
w("harmonic-tremolo.svg",  # harmonic tremolo: stomp + black-filled face + 2 large top + dot + 2 mid + 1 large bottom
  '<rect x="5.5" y="2.8" width="13" height="18.4" rx="2"/>'
  '<circle cx="9.7" cy="7.9" r="1.3" fill="#ffffff" stroke="none"/>'
  '<circle cx="14.3" cy="7.9" r="1.3" fill="#ffffff" stroke="none"/>'
  '<circle cx="12" cy="7.9" r="0.65" fill="#ffffff" stroke="none"/>'
  '<circle cx="9.7" cy="12.3" r="1.25" fill="#ffffff" stroke="none"/>'
  '<circle cx="14.3" cy="12.3" r="1.25" fill="#ffffff" stroke="none"/>'
  '<circle cx="12" cy="16.7" r="1.3" fill="#ffffff" stroke="none"/>')
w("opto-tremolo.svg",  # opto tremolo: stomp + black-filled face + 4 large 2x2 knobs + dot + 2 bottom knobs
  '<rect x="5.5" y="2.8" width="13" height="18.4" rx="2"/>'
  '<circle cx="9.7" cy="7.9" r="1.3" fill="#ffffff" stroke="none"/>'
  '<circle cx="14.3" cy="7.9" r="1.3" fill="#ffffff" stroke="none"/>'
  '<circle cx="9.7" cy="12.3" r="1.3" fill="#ffffff" stroke="none"/>'
  '<circle cx="14.3" cy="12.3" r="1.3" fill="#ffffff" stroke="none"/>'
  '<circle cx="12" cy="12.3" r="0.55" fill="#ffffff" stroke="none"/>'
  '<circle cx="9.7" cy="16.8" r="1.1" fill="#ffffff" stroke="none"/>'
  '<circle cx="14.3" cy="16.8" r="1.1" fill="#ffffff" stroke="none"/>')

w("90-phaser.svg",  # Ibanez-style stomp: big top knob, display window, center dot, footswitch knob
  '<rect x="5.5" y="2.8" width="13" height="18.4" rx="2"/>'
  '<circle cx="12" cy="7.6" r="2.6" fill="#ffffff" stroke="none"/>'
  '<rect x="8.4" y="11.4" width="7.2" height="3" rx="0.9"/>'
  '<circle cx="12" cy="17.6" r="1.2" fill="#ffffff" stroke="none"/>')
w("rotary-phaser.svg",  # tilted cabinet/enclosure with horizontal vent slots (front + side)
  '<path d="M3.6 7.4 10.4 4.2l10 1.4v13.6l-10.6 3.2-6.8-3.2z"/>'
  '<path d="M10.4 4.2v18.2"/>'
  '<path d="M5.2 10h3.4"/>'
  '<path d="M5.2 13h3.4"/>'
  '<path d="M5.2 16h3.4"/>'
  '<path d="M12.4 9h5.6"/>'
  '<path d="M12.4 12h5.6"/>'
  '<path d="M12.4 15h5.6"/>'
  '<path d="M12.4 18h5.6"/>'
)
w("uni-phaser.svg",  # dense multi-control face: two rows of mixed-size knobs + center blocks
  '<rect x="2" y="3.8" width="20" height="16.4" rx="2"/>'
  '<circle cx="5.6" cy="8.4" r="1.7" fill="#ffffff" stroke="none"/>'
  '<rect x="9.2" y="8.4" width="2.6" height="2.6" fill="#ffffff" stroke="none"/>'
  '<circle cx="14.3" cy="8.4" r="1.3" fill="#ffffff" stroke="none"/>'
  '<circle cx="18.3" cy="8.4" r="1" fill="#ffffff" stroke="none"/>'
  '<circle cx="5.6" cy="15.6" r="1.7" fill="#ffffff" stroke="none"/>'
  '<rect x="13.1" y="14.2" width="2" height="2.8" fill="#ffffff" stroke="none"/>'
  '<circle cx="18.3" cy="15.6" r="1" fill="#ffffff" stroke="none"/>')

w("fet-compressor.svg",  # wide rack: 2 big knobs + filled block right
  '<rect x="1.6" y="5.4" width="20.8" height="13.2" rx="1.6"/>'
  '<circle cx="5.6" cy="12" r="2" fill="#ffffff" stroke="none"/>'
  '<circle cx="10.4" cy="12" r="2" fill="#ffffff" stroke="none"/>'
  '<rect x="14.6" y="9" width="4.4" height="6" fill="#ffffff" stroke="none"/>')
w("opto-compressor.svg",  # wide rack: 2 knobs flanking a center open window
  '<rect x="1.6" y="5.4" width="20.8" height="13.2" rx="1.6"/>'
  '<circle cx="5.4" cy="12" r="1.6" fill="#ffffff" stroke="none"/>'
  '<rect x="9" y="9.2" width="6" height="5.6"/>'
  '<circle cx="18.6" cy="12" r="1.6" fill="#ffffff" stroke="none"/>')
w("vca-compressor.svg",  # wide rack: 4-knob row + filled block right
  '<rect x="1.6" y="5.4" width="20.8" height="13.2" rx="1.6"/>'
  '<circle cx="4.4" cy="12" r="1.3" fill="#ffffff" stroke="none"/>'
  '<circle cx="7.4" cy="12" r="1.3" fill="#ffffff" stroke="none"/>'
  '<circle cx="10.4" cy="12" r="1.3" fill="#ffffff" stroke="none"/>'
  '<circle cx="13.4" cy="12" r="1.3" fill="#ffffff" stroke="none"/>'
  '<rect x="15.8" y="9" width="4.4" height="6" fill="#ffffff" stroke="none"/>')
w("pedal-compressor.svg",  # stomp: 2x2 big knobs + big footswitch
  '<rect x="5.5" y="2.8" width="13" height="18.4" rx="2"/>'
  '<circle cx="8.6" cy="6.4" r="1.9" fill="#ffffff" stroke="none"/>'
  '<circle cx="15.4" cy="6.4" r="1.9" fill="#ffffff" stroke="none"/>'
  '<circle cx="8.6" cy="10.8" r="1.9" fill="#ffffff" stroke="none"/>'
  '<circle cx="15.4" cy="10.8" r="1.9" fill="#ffffff" stroke="none"/>'
  '<circle cx="12" cy="16.8" r="2.3" fill="#ffffff" stroke="none"/>')
w("tube-compressor.svg",  # wide rack: big knobs both ends + tiny tube in center
  '<rect x="1.6" y="5.4" width="20.8" height="13.2" rx="1.6"/>'
  '<circle cx="4.9" cy="12" r="2" fill="#ffffff" stroke="none"/>'
  '<path d="M10.4 9.4a1.6 1.6 0 0 1 3.2 0v3.8h-3.2Z"/>'
  '<path d="M9.9 14.3h4.2" stroke-width="1.8"/>'
  '<path d="M11.1 15.4v1.3M12.9 15.4v1.3" stroke-width="1.4"/>'
  '<circle cx="19.1" cy="12" r="2" fill="#ffffff" stroke="none"/>')
w("vari-mu-compressor.svg",  # wide rack: 2 gauges + 2 big end knobs + 3 small bottom knobs
  '<rect x="1.6" y="5.4" width="20.8" height="13.2" rx="1.6"/>'
  '<circle cx="4.8" cy="12" r="1.9" fill="#ffffff" stroke="none"/>'
  '<circle cx="10.2" cy="9.9" r="1.5"/>'
  '<circle cx="10.2" cy="10.7" r="0.5" fill="#ffffff" stroke="none"/>'
  '<circle cx="13.8" cy="9.9" r="1.5"/>'
  '<circle cx="13.8" cy="10.7" r="0.5" fill="#ffffff" stroke="none"/>'
  '<circle cx="12" cy="15" r="1.1" fill="#ffffff" stroke="none"/>'
  '<circle cx="10" cy="16.4" r="0.9" fill="#ffffff" stroke="none"/>'
  '<circle cx="14" cy="16.4" r="0.9" fill="#ffffff" stroke="none"/>'
  '<circle cx="19.2" cy="12" r="1.9" fill="#ffffff" stroke="none"/>')

w("analog-delay.svg",  # 5-knob top row + center window + footswitch
  '<rect x="5.5" y="2.8" width="13" height="18.4" rx="2"/>'
  '<circle cx="9.6" cy="6" r="1.1" fill="#ffffff" stroke="none"/>'
  '<circle cx="12" cy="6" r="1.1" fill="#ffffff" stroke="none"/>'
  '<circle cx="14.4" cy="6" r="1.1" fill="#ffffff" stroke="none"/>'
  '<rect x="10" y="9.2" width="4" height="3"/>'
  '<circle cx="12" cy="17" r="1.8" fill="#ffffff" stroke="none"/>')
w("tape-delay.svg",  # two-reel tape machine: reels + hubs + tape span
  '<rect x="3" y="3.5" width="18" height="17" rx="2"/>'
  '<circle cx="8.3" cy="9.5" r="2.2"/>'
  '<circle cx="15.7" cy="9.5" r="2.2"/>'
  '<circle cx="8.3" cy="9.5" r="0.55" fill="#ffffff" stroke="none"/>'
  '<circle cx="15.7" cy="9.5" r="0.55" fill="#ffffff" stroke="none"/>'
  '<path d="M8.3 11.7 Q12 13.1 15.7 11.7" stroke-width="1.6"/>'
  '<circle cx="12" cy="17" r="1.5" fill="#ffffff" stroke="none"/>'
)

w("digital-delay.svg",  # tall stomp: divider line, 4 small knobs on top, big filled block below
  '<rect x="5.5" y="2.8" width="13" height="18.4" rx="2"/>'
  '<circle cx="9" cy="6.2" r="0.8" fill="#ffffff" stroke="none"/>'
  '<circle cx="11" cy="6.2" r="0.8" fill="#ffffff" stroke="none"/>'
  '<circle cx="13" cy="6.2" r="0.8" fill="#ffffff" stroke="none"/>'
  '<circle cx="15" cy="6.2" r="0.8" fill="#ffffff" stroke="none"/>'
  '<path d="M9 8.9h6" stroke-width="1.6"/>'
  '<rect x="8" y="11.2" width="8" height="7.6" rx="1.2" fill="#ffffff" stroke="none"/>')
w("ep-delay.svg",  # ECHOLEC-style: trapezoid top + circle + knob row
  '<rect x="2.5" y="5" width="19" height="14" rx="2"/>'
  '<path d="M4.5 13 6.5 5.5h11L19.5 13z"/>'
  '<circle cx="12" cy="9" r="2"/>'
  '<path d="M4.5 13h15"/>'
  '<circle cx="5.4" cy="16" r="0.8" fill="#ffffff" stroke="none"/>'
  '<circle cx="7.4" cy="16" r="0.8" fill="#ffffff" stroke="none"/>'
  '<circle cx="9.4" cy="16" r="0.8" fill="#ffffff" stroke="none"/>'
  '<circle cx="11.4" cy="16" r="0.8" fill="#ffffff" stroke="none"/>'
  '<circle cx="13.4" cy="16" r="0.8" fill="#ffffff" stroke="none"/>'
  '<circle cx="15.4" cy="16" r="0.8" fill="#ffffff" stroke="none"/>'
  '<circle cx="17.4" cy="16" r="0.8" fill="#ffffff" stroke="none"/>'
  '<circle cx="19.4" cy="16" r="0.8" fill="#ffffff" stroke="none"/>')
w("mod-delay.svg",  # stomp: 2 big top knobs + dot + 4 small + big footswitch
  '<rect x="5.5" y="2.8" width="13" height="18.4" rx="2"/>'
  '<circle cx="8.8" cy="6.2" r="1.8" fill="#ffffff" stroke="none"/>'
  '<circle cx="15.2" cy="6.2" r="1.8" fill="#ffffff" stroke="none"/>'
  '<circle cx="12" cy="5.4" r="0.6" fill="#ffffff" stroke="none"/>'
  '<circle cx="8" cy="10" r="0.8" fill="#ffffff" stroke="none"/>'
  '<circle cx="10.4" cy="10" r="0.8" fill="#ffffff" stroke="none"/>'
  '<circle cx="13.6" cy="10" r="0.8" fill="#ffffff" stroke="none"/>'
  '<circle cx="16" cy="10" r="0.8" fill="#ffffff" stroke="none"/>'
  '<circle cx="12" cy="16.8" r="2" fill="#ffffff" stroke="none"/>')
w("se-delay.svg",  # wide rack: row of knobs + center flywheel ring + dot row
  '<rect x="1.6" y="5.4" width="20.8" height="13.2" rx="1.6"/>'
  '<circle cx="3.9" cy="10" r="1.1" fill="#ffffff" stroke="none"/>'
  '<circle cx="6.2" cy="10" r="1.1" fill="#ffffff" stroke="none"/>'
  '<circle cx="17.8" cy="10" r="1.1" fill="#ffffff" stroke="none"/>'
  '<circle cx="20.1" cy="10" r="1.1" fill="#ffffff" stroke="none"/>'
  '<circle cx="12" cy="10" r="3.2"/>'
  '<circle cx="12" cy="10" r="1.2" stroke-width="1.6"/>'
  '<circle cx="5" cy="15.5" r="0.6" fill="#ffffff" stroke="none"/>'
  '<circle cx="7" cy="15.5" r="0.6" fill="#ffffff" stroke="none"/>'
  '<circle cx="9" cy="15.5" r="0.6" fill="#ffffff" stroke="none"/>'
  '<circle cx="15" cy="15.5" r="0.6" fill="#ffffff" stroke="none"/>'
  '<circle cx="17" cy="15.5" r="0.6" fill="#ffffff" stroke="none"/>'
  '<circle cx="19" cy="15.5" r="0.6" fill="#ffffff" stroke="none"/>')

w("plate-reverb.svg",  # chassis with inner panel + 4 vertical bars
  '<rect x="2.8" y="4" width="18.4" height="16" rx="2"/>'
  '<rect x="5.6" y="6.4" width="12.8" height="11.2"/>'
  '<rect x="6.8" y="7.9" width="1.7" height="8.2" fill="#ffffff" stroke="none"/>'
  '<rect x="9.7" y="7.9" width="1.7" height="8.2" fill="#ffffff" stroke="none"/>'
  '<rect x="12.6" y="7.9" width="1.7" height="8.2" fill="#ffffff" stroke="none"/>'
  '<rect x="15.5" y="7.9" width="1.7" height="8.2" fill="#ffffff" stroke="none"/>')
w("room-reverb.svg",  # isometric hex room with center hub + 4 corner spokes
  '<path d="M12 2.5 20 7v10l-8 4.5-8-4.5V7z"/>'
  '<circle cx="12" cy="12" r="1.9"/>'
  '<path d="M10.5 11.1 4.3 7.5M13.5 11.1 19.7 7.5M10.5 12.9 4.3 16.5M13.5 12.9 19.7 16.5" stroke-width="1.6"/>')
w("digital-reverb.svg",  # whirlpool: continuous tightening 3-loop spiral
  '<path d="M12 12a0.6 0.6 0 0 1 1.2 0 1.8 1.8 0 0 1-3.6 0 3 3 0 0 1 6 0 4.5 4.5 0 0 1-9 0 6 6 0 0 1 12 0 7.5 7.5 0 0 1-15 0"/>')

w("shimmer-delay.svg",  # half moon + scattered star field (was: experimental)
  '<path d="M12 3A9.3 9.3 0 1 0 12 21A13 13 0 0 1 12 3z" fill="#ffffff" stroke="none"/>'
  '<circle cx="18" cy="5.4" r="0.9" fill="#ffffff" stroke="none"/>'
  '<circle cx="20.4" cy="10.4" r="0.8" fill="#ffffff" stroke="none"/>'
  '<circle cx="18.6" cy="15.8" r="0.9" fill="#ffffff" stroke="none"/>'
  '<circle cx="16.4" cy="19.4" r="0.7" fill="#ffffff" stroke="none"/>'
  '<circle cx="20.8" cy="18.6" r="0.6" fill="#ffffff" stroke="none"/>'
  '<circle cx="15.4" cy="8.2" r="0.6" fill="#ffffff" stroke="none"/>'
  '<circle cx="15.4" cy="14.6" r="0.6" fill="#ffffff" stroke="none"/>')
w("experimental.svg",  # Erlenmeyer flask: clean outline + liquid line + bubbles
  '<path d="M9.8 3h4.4"/>'
  '<path d="M10 3v4.8L4.4 17.4A2 2 0 0 0 6.2 20.4h11.6a2 2 0 0 0 1.8-3L14 7.8V3"/>'
  '<path d="M6.6 14h10.8" stroke-width="1.6"/>'
  '<circle cx="12" cy="17" r="0.9" fill="#ffffff" stroke="none"/>'
  '<circle cx="9.8" cy="17.8" r="0.6" fill="#ffffff" stroke="none"/>'
  '<circle cx="14.2" cy="17.8" r="0.6" fill="#ffffff" stroke="none"/>')
w("spaces.svg",  # slot canyon: two solid cliff walls + sun between them
  '<path d="M2.8 21V4.6L7.2 6.2V21z" fill="#ffffff" stroke="none"/>'
  '<path d="M21.2 21V4.6L16.8 6.2V21z" fill="#ffffff" stroke="none"/>'
  '<path d="M9.8 21v-6.5h4.4V21z" fill="#ffffff" stroke="none"/>'
  '<circle cx="12" cy="9.2" r="1.4" fill="#ffffff" stroke="none"/>')

w("bb_overdrive.svg",  # white face: dark knob bar w/ cut-outs + led + footswitch
  '<rect x="3" y="3" width="18" height="18" rx="1.6" fill="#ffffff" stroke="none"/>'
  '<rect x="4.6" y="4.6" width="14.8" height="14.8" fill="none" stroke="#1c1d22" stroke-width="1.2" stroke-linejoin="miter"/>'
  '<rect x="6.2" y="6.2" width="11.6" height="3.6" fill="#1c1d22" stroke="none" stroke-linejoin="miter"/>'
  '<circle cx="8.8" cy="8" r="1" fill="#ffffff" stroke="none"/>'
  '<circle cx="12" cy="8" r="1" fill="#ffffff" stroke="none"/>'
  '<circle cx="15.2" cy="8" r="1" fill="#ffffff" stroke="none"/>'
  '<circle cx="12" cy="12.8" r="0.6" fill="#1c1d22" stroke="none"/>'
  '<circle cx="12" cy="16.2" r="1.35" fill="#1c1d22" stroke="none"/>'
)
w("bd_overdrive.svg",  # BD-2: 2 big knobs + tone knob + led, big dark footswitch pad lower
  '<rect x="5" y="2.2" width="14" height="19.6" rx="1.8" fill="#ffffff" stroke="none"/>'
  '<rect x="6.4" y="3.6" width="11.2" height="16.8" fill="none" stroke="#1c1d22" stroke-width="1.2" stroke-linejoin="miter"/>'
  '<circle cx="9" cy="6.2" r="1.35" fill="#1c1d22" stroke="none"/>'
  '<circle cx="15" cy="6.2" r="1.35" fill="#1c1d22" stroke="none"/>'
  '<circle cx="12" cy="4.8" r="0.5" fill="#1c1d22" stroke="none"/>'
  '<circle cx="12" cy="9.4" r="1.1" fill="#1c1d22" stroke="none"/>'
  '<rect x="7.8" y="12.4" width="8.4" height="6" rx="0.6" fill="#1c1d22" stroke="none"/>'
)
w("k_overdrive.svg",  # double-border face: 3 big knobs + left dot + center footswitch
  '<rect x="2.2" y="3.5" width="19.6" height="17" rx="2" fill="#ffffff" stroke="none"/>'
  '<rect x="3.6" y="4.9" width="16.8" height="14.2" fill="none" stroke="#1c1d22" stroke-width="1.2" stroke-linejoin="miter"/>'
  '<circle cx="6.8" cy="8.2" r="1.55" fill="#1c1d22" stroke="none"/>'
  '<circle cx="12" cy="8.2" r="1.55" fill="#1c1d22" stroke="none"/>'
  '<circle cx="17.2" cy="8.2" r="1.55" fill="#1c1d22" stroke="none"/>'
  '<circle cx="5.6" cy="12.8" r="0.65" fill="#1c1d22" stroke="none"/>'
  '<circle cx="12" cy="16.2" r="1.4" fill="#1c1d22" stroke="none"/>'
)
w("nash_overdrive.svg",  # ODR-1: 3 big knobs top, nameplate band, big dark pad lower
  '<rect x="5" y="2.2" width="14" height="19.6" rx="1.8" fill="#ffffff" stroke="none"/>'
  '<rect x="6.4" y="3.6" width="11.2" height="16.8" fill="none" stroke="#1c1d22" stroke-width="1.2" stroke-linejoin="miter"/>'
  '<circle cx="9.2" cy="6.8" r="1.45" fill="#1c1d22" stroke="none"/>'
  '<circle cx="12" cy="6.8" r="1.45" fill="#1c1d22" stroke="none"/>'
  '<circle cx="14.8" cy="6.8" r="1.45" fill="#1c1d22" stroke="none"/>'
  '<rect x="7.8" y="10.6" width="8.4" height="1.8" fill="#1c1d22" stroke="none"/>'
  '<rect x="7.8" y="14" width="8.4" height="5" rx="0.6" fill="#1c1d22" stroke="none"/>'
)
w("ts_overdrive.svg",  # TS808: 3-knob triangle + led, hollow rectangular metal switch
  '<rect x="4.6" y="2.2" width="14.8" height="19.6" rx="2" fill="#ffffff" stroke="none"/>'
  '<rect x="6" y="3.6" width="12" height="16.8" fill="none" stroke="#1c1d22" stroke-width="1.2" stroke-linejoin="miter"/>'
  '<circle cx="8.9" cy="6.5" r="1.3" fill="#1c1d22" stroke="none"/>'
  '<circle cx="15.1" cy="6.5" r="1.3" fill="#1c1d22" stroke="none"/>'
  '<circle cx="12" cy="4.9" r="0.5" fill="#1c1d22" stroke="none"/>'
  '<circle cx="12" cy="10" r="1.2" fill="#1c1d22" stroke="none"/>'
  '<rect x="8.4" y="14" width="7.2" height="4.2" fill="none" stroke="#1c1d22" stroke-width="1.2" stroke-linejoin="miter"/>'
)
w("octave_fuzz.svg",  # desktop box: window top + centred 4-knob row
  '<rect x="2.2" y="4" width="19.6" height="16" rx="1.6"/>'
  '<rect x="4.8" y="6.4" width="14.4" height="4" rx="0.6"/>'
  '<circle cx="7.35" cy="15" r="1.2" fill="#ffffff" stroke="none"/>'
  '<circle cx="10.45" cy="15" r="1.2" fill="#ffffff" stroke="none"/>'
  '<circle cx="13.55" cy="15" r="1.2" fill="#ffffff" stroke="none"/>'
  '<circle cx="16.65" cy="15" r="1.2" fill="#ffffff" stroke="none"/>')

w("bm_fuzz.svg",  # white face: 3 dark knob cut-outs + led + concave-arch dark bottom w/ hex window
  '<rect x="5" y="2.2" width="14" height="19.6" rx="1.8" fill="#ffffff" stroke="none"/>'
  '<rect x="6" y="3.4" width="12" height="17.2" fill="none" stroke="#1c1d22" stroke-width="1.2"/>'
  '<rect x="7.6" y="4.8" width="8.8" height="4.8" stroke-linejoin="miter"/>'
  '<circle cx="9.6" cy="7.2" r="1.3" fill="#1c1d22" stroke="none"/>'
  '<circle cx="12" cy="7.2" r="1.3" fill="#1c1d22" stroke="none"/>'
  '<circle cx="14.4" cy="7.2" r="1.3" fill="#1c1d22" stroke="none"/>'
  '<circle cx="8.2" cy="11.4" r="0.7" fill="#1c1d22" stroke="none"/>'
  '<path d="M7.4 13.4H10.2Q12 16.4 13.8 13.4H16.6V18.6a1 1 0 0 1-1 1H8.4a1 1 0 0 1-1-1Z" fill="#1c1d22" stroke="none"/>'
  '<circle cx="12" cy="16.7" r="1.3" fill="#ffffff" stroke="none"/>')

w("ff_fuzz.svg",  # circular face: dark top arc, 2 knobs, big center switch
  '<circle cx="12" cy="12" r="9.4"/>'
  '<path d="M5.4 6.2A9.4 9.4 0 0 1 18.6 6.2" fill="#ffffff" stroke="none"/>'
  '<circle cx="7.6" cy="9.4" r="1.5" fill="#ffffff" stroke="none"/>'
  '<circle cx="16.4" cy="9.4" r="1.5" fill="#ffffff" stroke="none"/>'
  '<path d="M9.2 18.6 12 11.8l2.8 6.8H9.2Z" fill="#ffffff" stroke="none"/>'
  '<circle cx="12" cy="15.6" r="0.8" fill="#1c1d22" stroke="none"/>')

w("fz_fuzz.svg",  # double-border face, white interior: dark 3-knob triangle + led + dark bottom
  '<rect x="5.2" y="2.2" width="13.6" height="19.6" rx="1.6"/>'
  '<rect x="6.8" y="3.8" width="10.4" height="16.4" fill="#ffffff" stroke="none"/>'
  '<circle cx="12" cy="5.1" r="0.45" fill="#1c1d22" stroke="none"/>'
  '<circle cx="9.4" cy="7.6" r="1.3" fill="#1c1d22" stroke="none"/>'
  '<circle cx="14.6" cy="7.6" r="1.3" fill="#1c1d22" stroke="none"/>'
  '<circle cx="12" cy="10.4" r="1.3" fill="#1c1d22" stroke="none"/>'
  '<rect x="8" y="13" width="8" height="6" fill="#1c1d22" stroke="none"/>')

w("tb_fuzz.svg",  # tall narrow tube: 2 knobs + tiny dot + 1 bottom dot
  '<rect x="6.6" y="1.8" width="10.8" height="20.4" rx="1.6"/>'
  '<circle cx="9.6" cy="6.4" r="1.2" fill="#ffffff" stroke="none"/>'
  '<circle cx="14.4" cy="6.4" r="1.2" fill="#ffffff" stroke="none"/>'
  '<circle cx="12" cy="10.6" r="0.8" fill="#ffffff" stroke="none"/>'
  '<circle cx="12" cy="17.4" r="1" fill="#ffffff" stroke="none"/>')

w("ds_distortion.svg",  # face: 3 knobs + tiny dot + filled bottom
  '<rect x="5.8" y="2.2" width="12.4" height="19.6" rx="1.6"/>'
  '<circle cx="9.4" cy="6" r="1.5" fill="#ffffff" stroke="none"/>'
  '<circle cx="14.6" cy="6" r="1.5" fill="#ffffff" stroke="none"/>'
  '<circle cx="12" cy="8.6" r="1.5" fill="#ffffff" stroke="none"/>'
  '<circle cx="12" cy="4.2" r="0.5" fill="#ffffff" stroke="none"/>'
  '<rect x="7.6" y="13.2" width="8.8" height="6.4" fill="#ffffff" stroke="none"/>')

w("oc_distortion.svg",  # face: 3 knobs + tiny dot + single bottom switch
  '<rect x="5.8" y="2.2" width="12.4" height="19.6" rx="1.6"/>'
  '<circle cx="9.4" cy="6" r="1.5" fill="#ffffff" stroke="none"/>'
  '<circle cx="14.6" cy="6" r="1.5" fill="#ffffff" stroke="none"/>'
  '<circle cx="12" cy="8.6" r="1.5" fill="#ffffff" stroke="none"/>'
  '<circle cx="12" cy="4.2" r="0.5" fill="#ffffff" stroke="none"/>'
  '<circle cx="12" cy="16.4" r="1.2" fill="#ffffff" stroke="none"/>')

w("rl_distortion.svg",  # face: 2 knobs + tiny dot + single bottom switch
  '<rect x="6.2" y="2.2" width="11.6" height="19.6" rx="1.6"/>'
  '<circle cx="9.2" cy="6.2" r="1.4" fill="#ffffff" stroke="none"/>'
  '<circle cx="14.8" cy="6.2" r="1.4" fill="#ffffff" stroke="none"/>'
  '<circle cx="12" cy="6.2" r="0.5" fill="#ffffff" stroke="none"/>'
  '<circle cx="12" cy="15.6" r="1.2" fill="#ffffff" stroke="none"/>')

w("sd_distortion.svg",  # face: 3-knob triangle + filled bottom w/ clearance
  '<rect x="5.4" y="2.2" width="13.2" height="19.6" rx="1.6"/>'
  '<rect x="7" y="3.8" width="10" height="16.4"/>'
  '<circle cx="9.6" cy="6.9" r="1.15" fill="#ffffff" stroke="none"/>'
  '<circle cx="14.4" cy="6.9" r="1.15" fill="#ffffff" stroke="none"/>'
  '<circle cx="12" cy="9.8" r="1.15" fill="#ffffff" stroke="none"/>'
  '<rect x="8.4" y="13.4" width="7.2" height="4.6" fill="#ffffff" stroke="none"/>')

w("vermin_distortion.svg",  # double-border face, white interior: dark 3-knob bar + window + dark bottom band w/ switch
  '<rect x="4.8" y="2.2" width="14.4" height="19.6" rx="1.8"/>'
  '<rect x="6.4" y="3.8" width="11.2" height="16.4" fill="#ffffff" stroke="none"/>'
  '<rect x="7.6" y="5" width="8.8" height="4.2" fill="#1c1d22" stroke="none"/>'
  '<circle cx="9.6" cy="7.1" r="1.05" fill="#ffffff" stroke="none"/>'
  '<circle cx="12" cy="7.1" r="1.05" fill="#ffffff" stroke="none"/>'
  '<circle cx="14.4" cy="7.1" r="1.05" fill="#ffffff" stroke="none"/>'
  '<rect x="8.2" y="11" width="7.6" height="3.2"/>'
  '<rect x="7.6" y="15.8" width="8.8" height="3.6" fill="#1c1d22" stroke="none"/>'
  '<circle cx="12" cy="17.6" r="0.9" fill="#ffffff" stroke="none"/>')

w("fman-amp.svg",  # head: label plate + white knob strip w/ cut-out knobs (black on white)
  '<rect x="1.6" y="5.4" width="20.8" height="13.2" rx="1.6"/>'
  '<rect x="4.4" y="7.6" width="5" height="3.6"/>'
  '<path fill="#ffffff" stroke="none" fill-rule="evenodd" d="M8 12.6H19A1 1 0 0 1 20 13.6V16.4A1 1 0 0 1 19 17.4H8A1 1 0 0 1 7 16.4V13.6A1 1 0 0 1 8 12.6Z M9.6 15a0.6 0.6 0 1 0 1.2 0a0.6 0.6 0 1 0-1.2 0Z M11.2 15a0.6 0.6 0 1 0 1.2 0a0.6 0.6 0 1 0-1.2 0Z M12.8 15a0.6 0.6 0 1 0 1.2 0a0.6 0.6 0 1 0-1.2 0Z M14.4 15a0.6 0.6 0 1 0 1.2 0a0.6 0.6 0 1 0-1.2 0Z M16.0 15a0.6 0.6 0 1 0 1.2 0a0.6 0.6 0 1 0-1.2 0Z M17.6 15a0.6 0.6 0 1 0 1.2 0a0.6 0.6 0 1 0-1.2 0Z"/>')
w("lead8-amp.svg",  # head: white knob strip w/ cut-out knobs - 2 medium + 4 small + 2 holes
  '<rect x="1.6" y="5.4" width="20.8" height="13.2" rx="1.6"/>'
  '<path fill="#ffffff" stroke="none" fill-rule="evenodd" d="M4.2 13.2H19.8A1 1 0 0 1 20.8 14.2V16.4A1 1 0 0 1 19.8 17.4H4.2A1 1 0 0 1 3.2 16.4V14.2A1 1 0 0 1 4.2 13.2Z M4.15 15.3a1.25 1.25 0 1 0 2.5 0a1.25 1.25 0 1 0-2.5 0Z M6.95 15.3a1.25 1.25 0 1 0 2.5 0a1.25 1.25 0 1 0-2.5 0Z M10.25 15.3a0.75 0.75 0 1 0 1.5 0a0.75 0.75 0 1 0-1.5 0Z M11.95 15.3a0.75 0.75 0 1 0 1.5 0a0.75 0.75 0 1 0-1.5 0Z M13.65 15.3a0.75 0.75 0 1 0 1.5 0a0.75 0.75 0 1 0-1.5 0Z M15.35 15.3a0.75 0.75 0 1 0 1.5 0a0.75 0.75 0 1 0-1.5 0Z M17.75 14.55a0.55 0.55 0 1 0 1.1 0a0.55 0.55 0 1 0-1.1 0Z M17.75 16.05a0.55 0.55 0 1 0 1.1 0a0.55 0.55 0 1 0-1.1 0Z"/>')
w("mtch-amp.svg",  # head: centered uniform knob row + 2 small vertical hole pairs at edges
  '<rect x="1.6" y="5.4" width="20.8" height="13.2" rx="1.6"/>'
  '<circle cx="4.4" cy="7.7" r="0.55" fill="#ffffff" stroke="none"/>'
  '<circle cx="4.4" cy="9.3" r="0.55" fill="#ffffff" stroke="none"/>'
  '<circle cx="7.5" cy="8.5" r="0.7" fill="#ffffff" stroke="none"/>'
  '<circle cx="9.3" cy="8.5" r="0.7" fill="#ffffff" stroke="none"/>'
  '<circle cx="11.1" cy="8.5" r="0.7" fill="#ffffff" stroke="none"/>'
  '<circle cx="12.9" cy="8.5" r="0.7" fill="#ffffff" stroke="none"/>'
  '<circle cx="14.7" cy="8.5" r="0.7" fill="#ffffff" stroke="none"/>'
  '<circle cx="16.5" cy="8.5" r="0.7" fill="#ffffff" stroke="none"/>'
  '<circle cx="19.6" cy="7.7" r="0.55" fill="#ffffff" stroke="none"/>'
  '<circle cx="19.6" cy="9.3" r="0.55" fill="#ffffff" stroke="none"/>'
  '<rect x="5.4" y="10.8" width="13.2" height="4.6"/>')
w("plx-amp.svg",  # plexi JMP: white bottom strip - 6 uniform centered knobs + 2x2 small holes
  '<rect x="1.6" y="5.4" width="20.8" height="13.2" rx="1.6"/>'
  '<path fill="#ffffff" stroke="none" fill-rule="evenodd" d="M4.6 12.4H19.4A1 1 0 0 1 20.4 13.4V16.4A1 1 0 0 1 19.4 17.4H4.6A1 1 0 0 1 3.6 16.4V13.4A1 1 0 0 1 4.6 12.4Z M7.45 14.9a0.75 0.75 0 1 0 1.5 0a0.75 0.75 0 1 0-1.5 0Z M9.05 14.9a0.75 0.75 0 1 0 1.5 0a0.75 0.75 0 1 0-1.5 0Z M10.65 14.9a0.75 0.75 0 1 0 1.5 0a0.75 0.75 0 1 0-1.5 0Z M12.25 14.9a0.75 0.75 0 1 0 1.5 0a0.75 0.75 0 1 0-1.5 0Z M13.85 14.9a0.75 0.75 0 1 0 1.5 0a0.75 0.75 0 1 0-1.5 0Z M15.45 14.9a0.75 0.75 0 1 0 1.5 0a0.75 0.75 0 1 0-1.5 0Z M17.85 13.95a0.45 0.45 0 1 0 0.9 0a0.45 0.45 0 1 0-0.9 0Z M19.05 13.95a0.45 0.45 0 1 0 0.9 0a0.45 0.45 0 1 0-0.9 0Z M17.85 15.95a0.45 0.45 0 1 0 0.9 0a0.45 0.45 0 1 0-0.9 0Z M19.05 15.95a0.45 0.45 0 1 0 0.9 0a0.45 0.45 0 1 0-0.9 0Z"/>')
w("an-amp.svg",  # head: 10-dot knob row + big open panel
  '<rect x="1.6" y="5.4" width="20.8" height="13.2" rx="1.6"/>'
  '<circle cx="3.9" cy="9.2" r="0.8" fill="#ffffff" stroke="none"/>'
  '<circle cx="5.9" cy="9.2" r="0.8" fill="#ffffff" stroke="none"/>'
  '<circle cx="7.9" cy="9.2" r="0.8" fill="#ffffff" stroke="none"/>'
  '<circle cx="9.9" cy="9.2" r="0.8" fill="#ffffff" stroke="none"/>'
  '<circle cx="11.9" cy="9.2" r="0.8" fill="#ffffff" stroke="none"/>'
  '<circle cx="13.9" cy="9.2" r="0.8" fill="#ffffff" stroke="none"/>'
  '<circle cx="15.9" cy="9.2" r="0.8" fill="#ffffff" stroke="none"/>'
  '<circle cx="17.9" cy="9.2" r="0.8" fill="#ffffff" stroke="none"/>'
  '<rect x="4" y="11.6" width="16" height="4.8"/>')
w("dmb-amp.svg",  # head: uniform knob row + big open panel
  '<rect x="1.6" y="5.4" width="20.8" height="13.2" rx="1.6"/>'
  '<circle cx="4.2" cy="9" r="0.8" fill="#ffffff" stroke="none"/>'
  '<circle cx="6.4" cy="9" r="0.8" fill="#ffffff" stroke="none"/>'
  '<circle cx="8.6" cy="9" r="0.8" fill="#ffffff" stroke="none"/>'
  '<circle cx="10.8" cy="9" r="0.8" fill="#ffffff" stroke="none"/>'
  '<circle cx="13" cy="9" r="0.8" fill="#ffffff" stroke="none"/>'
  '<circle cx="15.2" cy="9" r="0.8" fill="#ffffff" stroke="none"/>'
  '<circle cx="17.4" cy="9" r="0.8" fill="#ffffff" stroke="none"/>'
  '<circle cx="19.6" cy="9" r="0.8" fill="#ffffff" stroke="none"/>'
  '<rect x="4" y="11.4" width="16" height="4.8"/>')

print("wrote", len(list(OUT.glob('*.svg'))), "icons")

