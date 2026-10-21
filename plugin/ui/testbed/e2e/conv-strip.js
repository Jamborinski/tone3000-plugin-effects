// Conv strip e2e: the node-view strip's face chrome (m01741 — the
// strip reaches every tile; m03044 — Load IR + Export sit right of the
// CONVOLUTION mode bar). Pins the chrome visibility + the no-IR export
// GUARD toast.
//
// The save dialog can't be driven headless, so this asserts on the
// toast string + knob visibility, not on a file write.
//
// Run:  xvfb-run -a node plugin/ui/testbed/e2e/conv-strip.js

'use strict';

const path = require('path');
const os = require('os');
const fs = require('fs');
const { launchApp, setView, screenshot } = require('./e2e');

const OUT = path.join(os.tmpdir(), 't3k-e2e');
fs.mkdirSync(OUT, { recursive: true });

let passed = 0;
let failed = 0;
function check(name, cond, detail) {
  if (cond) {
    ++passed;
    console.log(`ok   ${name}`);
  } else {
    ++failed;
    console.log(`FAIL ${name}: ${detail}`);
  }
}

(async () => {
  const app = await launchApp('node-knob-change');
  try {
    await setView(app, true);

    // sendCommand resolves the response DATA object (it rejects on
    // failure), so r is already the {state: ...} payload.
    const strip = async (op = 'probe') => {
      const r = await app.sendCommand({
        type: 'node-conv-strip',
        args: { blockId: 'fx-conv', op },
      });
      return { state: r.state, band: r.band };
    };

    // Face chrome at rest: Load IR + Export are visible BESIDE the
    // CONVOLUTION mode bar (m04482 — their old top-right position is gone),
    // the strip's band layout runs on the next resize pass — settle before
    // asserting. State: <loadVis>:<exportVis>|<toast>|<convIr>|<labels>|<loadX>,<exportX>
    await new Promise((r) => setTimeout(r, 300));
    const p0 = await strip();
    const s0 = p0.state;
    check('conv-strip: conv node selected, strip visible',
          s0 !== 'no-strip', s0);
    check('conv-strip: face chrome visible (Load IR + Export)',
          s0.startsWith('1:1|'), `state=${s0}`);
    check('conv-strip: no curve band open at rest',
          p0.band === 'closed', `band=${p0.band}`);

    // The strip's acceptance surface (m04350): the conv face renamed
    // "In" -> "Dwell" (m04334 item 1) and the Load IR / Export buttons sit
    // beside the CONVOLUTION bar, not right-aligned (m04482). The labels
    // section is comma-joined knob labels; the tail is <loadX>,<exportX>.
    const parts = s0.split('|');
    const labels = parts[4] || '';
    check('conv-strip: conv face shows Dwell (not In)',
          labels.includes(',Dwell,'), `labels=${labels}`);
    check('conv-strip: conv face no longer has a bare In',
          !labels.includes(',In,'), `labels=${labels}`);
    const coords = (parts[5] || '').split(',');
    const loadX = parseInt(coords[0], 10);
    const exportX = parseInt(coords[1], 10);
    // Beside the CONVOLUTION bar = left-of-centre (nameW caps at 320, so
    // loadX <= 14+320+8=342); right-aligned would be stripWidth-148, far
    // to the right. Assert both are well clear of the right edge.
    check('conv-strip: Load IR sits beside the CONVOLUTION bar (left)',
          Number.isFinite(loadX) && loadX >= 14 && loadX <= 360, `loadX=${loadX}`);
    check('conv-strip: Export sits right of Load IR (beside the bar)',
          Number.isFinite(exportX) && exportX > loadX && exportX <= 500,
          `loadX=${loadX} exportX=${exportX}`);

    // Export with no IR loaded: the guard toast fires (the scenario's
    // conv block has no IR file on disk — TileShot's are in-memory).
    const p1 = await strip('export-click');
    check('conv-strip: export with no IR shows the guard toast',
          p1.state.includes('No IR loaded in this block'), `state=${p1.state}`);

    // F In / F Out curve bands (m04334 item 2): right-clicking the strip's
    // F In opens the InCrv band — and opening F Out dismisses F In
    // (one band at a time, the gate-deck pattern). Headless has no
    // right-button, so the e2e drives the opener directly (curve-in/out
    // call the same openConvCurveBand the secondary press routes to).
    const p2 = await strip('curve-in');
    check('conv-strip: F In right-click opens the InCrv band',
          p2.band === 'in', `band=${p2.band}`);
    await screenshot(app, path.join(OUT, 'conv-strip-band-in.png'));
    const p3 = await strip('curve-out');
    check('conv-strip: F Out opens OutCrv and dismisses InCrv',
          p3.band === 'out', `band=${p3.band}`);

    // Chrome unchanged after the guarded click (visible flags only; the
    // label/coord tail is asserted above).
    const p2b = await strip();
    check('conv-strip: chrome still visible after guarded export',
          p2b.state.startsWith('1:1|'), `state=${p2b.state}`);

    await screenshot(app, path.join(OUT, 'conv-strip-face.png'));
  } finally {
    try { await app.quit(); } catch { /* quit can race exit */ }
  }
  console.log(`\nconv-strip e2e: ${passed} passed, ${failed} failed`);
  process.exit(failed === 0 ? 0 : 1);
})().catch((e) => {
  console.error('conv-strip e2e crashed:', e);
  process.exit(2);
});
