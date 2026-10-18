// Node-view drag e2e (the juce-end-to-end layer, m04873/m04938).
//
// This is the REGRESSION GUARD for the user-reported drag bugs (m04679/m04723):
//   1. quarter ghost visible only for the FIRST node; other nodes show none;
//   2. a top-left "box" that appears only while the pointer is over the
//      signal lines;
//   3. drop landing in the wrong slot.
//
// What makes this layer different from the in-process drive (Scenarios.cpp):
// the pointer is delivered to the DEEPEST visible child under the cursor, so
// crossing a signal-line gap changes the receiving component mid-drag — the
// exact path the in-process drive cannot express.
//
// Run:  xvfb-run -a node plugin/ui/testbed/e2e/node-drag.js
//       (use node directly on a live display to watch it in flight)

'use strict';

const path = require('path');
const os = require('os');
const fs = require('fs');
const { launchApp, pointerDrag, geometry, cells, setView, screenshot } = require('./e2e');

const OUT = path.join(os.tmpdir(), 't3k-e2e');
fs.mkdirSync(OUT, { recursive: true });

// Tiny assertion bookkeeping (no test-runner dep; the layer deliberately
// stays at one npm package).
let passed = 0;
let failed = 0;
function check(name, cond, detail = '') {
  if (cond) {
    ++passed;
    console.log(`ok   ${name}`);
  } else {
    ++failed;
    console.log(`FAIL ${name} — ${detail}`);
  }
}

function parseGhost(s) {
  // "x=N y=N w=N h=N grab=(X,Y)" or "none"
  if (s === 'none') return null;
  const m = {};
  for (const k of ['x', 'y', 'w', 'h']) {
    const mm = s.match(new RegExp(`\\b${k}=(\\d+)`));
    m[k] = mm ? parseInt(mm[1], 10) : -1;
  }
  const g = s.match(/grab=\((-?\d+),(-?\d+)\)/);
  m.grabX = g ? parseInt(g[1], 10) : -999;
  m.grabY = g ? parseInt(g[2], 10) : -999;
  return m;
}

(async () => {
  const app = await launchApp('node-drag-multi');
  try {
    // Settle: the chain loads async on the message thread.
    await new Promise((r) => setTimeout(r, 800));

    // The node-drag-* scenarios load the chain in the GALLERY (cold start,
    // m03562) — the drag lives in the node view, so flip it first.
    check('node view activated', (await setView(app, true)) === true);
    await new Promise((r) => setTimeout(r, 400));

    const cellList = await cells(app);
    check('node view is live (cells reported)', cellList.length >= 8, `got ${cellList.length}`);
    if (cellList.length < 8) throw new Error('no node grid');

    const c = cellList; // window coords, lane order
    const kCellW = c[1].x - c[0].x; // horizontal pitch of the grid

    // ---- Drag 1: cell 0 -> cell 5 (forward, crosses the right signal line)
    const g0 = await geometry(app);
    check('pre-drag: no ghost', parseGhost(g0.ghost) === null, g0.ghost);

    const log1 = await pointerDrag(app, c[0], c[5], 14);

    // Mid-drag: a ghost must exist and FILL the cell (the quarter-ghost bug
    // reported "bottom quarter only" — w must be the cell width, h the cell
    // height, i.e. the WHOLE opaque cell following the pointer).
    const mid = log1[Math.floor(log1.length / 2)];
    const mg = parseGhost(mid.ghost);
    check('mid-drag (cell0->5): ghost is alive', mg !== null, mid.ghost);
    if (mg) {
      check('mid-drag: ghost width == cell width', Math.abs(mg.w - kCellW) <= 2, `ghost w=${mg.w} cell pitch=${kCellW}`);
      check('mid-drag: ghost follows the pointer (not pinned to origin)',
            mg.x > 20 && mg.y < c[0].y + 60,
            `ghost at ${mg.x},${mg.y} pointer at ${mid.x},${mid.y}`);
      // The top-left-box bug: while over the signal lines the ghost must NOT
      // jump to the window origin (x=0..~10, y=0..~10) mid-drag.
      const nearOrigin = log1.filter((l) => {
        const g = parseGhost(l.ghost);
        return g && g.x <= 12 && g.y <= 12;
      });
      check('mid-drag: ghost never snaps to the window origin (top-left box)',
            nearOrigin.length === 0,
            `${nearOrigin.length} steps at origin: ${JSON.stringify(nearOrigin.map((n) => [n.x, n.y, n.ghost]))}`);
    }

    await screenshot(app, path.join(OUT, 'drag-mid-cell0-5.png'));

    // Post-drag: drop committed (drag state idle), no ghost left.
    const g1 = await geometry(app);
    check('post-drag (cell0->5): drag idle', g1.drag === 'idle', g1.drag);
    check('post-drag (cell0->5): no ghost left', parseGhost(g1.ghost) === null, g1.ghost);
    await screenshot(app, path.join(OUT, 'drag-after-cell0-5.png'));

    // ---- Drag 2: cell 1 -> cell 9/10 (backward across the LEFT signal line)
    const from2 = c[1];
    const to2 = c[Math.min(9, c.length - 1)];
    const log2 = await pointerDrag(app, from2, to2, 14);
    const mid2 = log2[Math.floor(log2.length / 2)];
    const mg2 = parseGhost(mid2.ghost);
    check('mid-drag (cell1->tail): ghost is alive (OTHER nodes get a ghost too)',
          mg2 !== null, mid2.ghost);
    if (mg2) {
      check('mid-drag (cell1->tail): ghost width == cell width',
            Math.abs(mg2.w - kCellW) <= 2, `ghost w=${mg2.w} cell pitch=${kCellW}`);
      const nearOrigin2 = log2.filter((l) => {
        const g = parseGhost(l.ghost);
        return g && g.x <= 12 && g.y <= 12;
      });
      check('mid-drag (cell1->tail): ghost never snaps to the window origin',
            nearOrigin2.length === 0,
            `${nearOrigin2.length} steps at origin: ${JSON.stringify(nearOrigin2.map((n) => [n.x, n.y, n.ghost]))}`);
    }
    await screenshot(app, path.join(OUT, 'drag-mid-cell1-tail.png'));
    const g2 = await geometry(app);
    check('post-drag (cell1->tail): drag idle, no ghost',
          g2.drag === 'idle' && parseGhost(g2.ghost) === null,
          `${g2.drag} / ${g2.ghost}`);

    console.log(`\nnode-drag e2e: ${passed} passed, ${failed} failed (shots in ${OUT})`);
  } finally {
    try {
      await app.quit();
    } catch {
      // quit can race the process exit; the summary below is the truth.
    }
  }
  process.exit(failed === 0 ? 0 : 1);
})().catch((e) => {
  console.error('node-drag e2e crashed:', e);
  process.exit(2);
});
