// Wave / Volume add-path e2e (user m07658: "adding a wave or volume effect
// crashes the application"). The crash was the EffectTile ctor reading
// ps[0..2] unguarded on a 1-2 param face; this drives the REAL add path in
// the running testbed — palette add -> strip sync -> face render — for both
// kinds and asserts the strip shows the right face without dying.
//
// Run:  xvfb-run -a node plugin/ui/testbed/e2e/wave-volume-add.js

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
    await new Promise((r) => setTimeout(r, 300));

    const add = async (kind) => {
      const r = await app.sendCommand({
        type: 'node-add',
        args: { kind, index: 2 },
      });
      return { id: r.id, labels: r.labels, state: r.state };
    };

    // Wave: the strip face is Mix/In/Out + the two wave params.
    const w = await add('wave');
    check('wave: add returned a block id', !!w.id, JSON.stringify(w));
    check('wave: strip visible after add', w.state !== 'no-strip', w.state);
    check('wave: face = Mix,In,Out,Level,Shape',
          w.labels === 'Mix,In,Out,Level,Shape', `labels=${w.labels}`);

    // Volume: the strip face is Mix/In/Out + the single volume param.
    const v = await add('volume');
    check('volume: add returned a block id', !!v.id, JSON.stringify(v));
    check('volume: strip visible after add', v.state !== 'no-strip', v.state);
    check('volume: face = Mix,In,Out,Volume',
          v.labels === 'Mix,In,Out,Volume', `labels=${v.labels}`);

    // Both faces rendered, no crash: the app must still answer, and the
    // lane must now carry BOTH kinds (they inserted at index 2 — mid-lane).
    const ok = await app.sendCommand({ type: 'node-geometry', args: {} });
    check('app alive after both adds', !!ok.lane, JSON.stringify(ok));
    const kinds = ok.lane.split(',');
    check('lane carries wave + volume',
          kinds.includes('wave') && kinds.includes('volume'),
          `lane=${ok.lane}`);

    await screenshot(app, path.join(OUT, 'wave-volume-strip.png'));
  } finally {
    app.kill();
  }
  console.log(`\n${passed} passed, ${failed} failed`);
  process.exit(failed === 0 ? 0 : 1);
})().catch((e) => {
  console.error('FATAL', e);
  process.exit(2);
});
