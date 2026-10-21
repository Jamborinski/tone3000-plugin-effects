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
      return r.state;
    };

    // Face chrome at rest: Load IR + Export are visible (right of the
    // CONVOLUTION mode bar, m03044). The strip's band layout runs on the
    // next resize pass — settle before asserting.
    await new Promise((r) => setTimeout(r, 300));
    const s0 = await strip();
    check('conv-strip: conv node selected, strip visible',
          s0 !== 'no-strip', s0);
    check('conv-strip: face chrome visible (Load IR + Export)',
          s0.startsWith('1:1|'), `state=${s0}`);

    // Export with no IR loaded: the guard toast fires (the scenario's
    // conv block has no IR file on disk — TileShot's are in-memory).
    const s1 = await strip('export-click');
    check('conv-strip: export with no IR shows the guard toast',
          s1.includes('No IR loaded in this block'), `state=${s1}`);

    // Chrome unchanged after the guarded click.
    const s2 = await strip();
    check('conv-strip: chrome still visible after guarded export',
          s2.startsWith('1:1|'), `state=${s2}`);

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
