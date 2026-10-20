// Node-view drag e2e (the juce-end-to-end layer, m04873/m04938, extended
// m05916).
//
// This is the REGRESSION GUARD for the user-reported drag bugs:
//   m04679/m04723:  1. quarter ghost visible only for the FIRST node; other
//                     nodes show none;
//                   2. a top-left "box" that appears only while the pointer
//                     is over the signal lines;
//                   3. drop landing in the wrong slot.
//   m05916 (this round):
//                   4. swap thresholds asymmetric (instant right swap, lazy
//                      left) — must be equal: ~1 tile either way;
//                   5. drop target must be visibly highlighted while
//                      dragging;
//                   6. cancellable drag — off-chain release cancels, and a
//                      off-chain release cancels; right-press mid-drag does not.
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
const {
  launchApp, pointerDrag, dragPress, geometry, cells, setView, screenshot,
} = require('./e2e');

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

    const c = cellList; // window coords, LANE order (occupied first: c[0]=slot0...)
    const kCellW = c[1].x - c[0].x; // horizontal pitch of the grid
    const lane = (g) => (g.lane || '').split(',').filter(Boolean);

    const g0 = await geometry(app);
    check('pre-drag: no ghost', parseGhost(g0.ghost) === null, g0.ghost);
    // node-drag-multi loads compressor, chorus, delay, reverb (full kind
    // names — the e2e lane probe reports ChainItem::effectKind verbatim).
    check('pre-drag: lane is compressor,chorus,delay,reverb',
          lane(g0).slice(0, 4).join(',') === 'compressor,chorus,delay,reverb',
          g0.lane);

    // ------------------------------------------------------------------
    // Drag 1 (m04679): press cell 0, drag to c[9] (the far tail slot; the
    // floor law lands the item where the pointer releases), so the path
    // crosses the signal lines and the whole right cascade.
    const log1 = await pointerDrag(app, c[0], c[9], 14);

    // Mid-drag: a ghost must exist and FILL the cell (the quarter-ghost bug
    // reported "bottom quarter only" — w must be the cell width, h the cell
    // height, i.e. the WHOLE opaque cell following the pointer).
    const mid = log1.log[Math.floor(log1.log.length / 2)];
    const mg = parseGhost(mid.ghost);
    check('mid-drag (cell0->5): ghost is alive', mg !== null, mid.ghost);
    if (mg) {
      check('mid-drag: ghost width == cell width', Math.abs(mg.w - kCellW) <= 2,
            `ghost w=${mg.w} cell pitch=${kCellW}`);
      check('mid-drag: ghost follows the pointer (not pinned to origin)',
            mg.x > 20 && mg.y < c[0].y + 60,
            `ghost at ${mg.x},${mg.y} pointer at ${mid.x},${mid.y}`);
      // The top-left-box bug: while over the signal lines the ghost must NOT
      // jump to the window origin (x=0..~10, y=0..~10) mid-drag.
      const nearOrigin = log1.log.filter((l) => {
        const g = parseGhost(l.ghost);
        return g && g.x <= 12 && g.y <= 12;
      });
      check('mid-drag: ghost never snaps to the window origin (top-left box)',
            nearOrigin.length === 0,
            `${nearOrigin.length} steps at origin: ${JSON.stringify(nearOrigin.map((n) => [n.x, n.y, n.ghost]))}`);
    }

    await screenshot(app, path.join(OUT, 'drag-mid-cell0-5.png'));

    // Post-drag: drop committed (drag state idle), no ghost left, and the
    // COMPRESSOR really moved to the far tail; the other three slid left by
    // one (chorus -> 0, delay -> 1, reverb -> 2).
    const g1 = await geometry(app);
    check('post-drag (cell0->far): drag idle', g1.drag === 'idle', g1.drag);
    check('post-drag (cell0->far): no ghost left', parseGhost(g1.ghost) === null, g1.ghost);
    const l1 = lane(g1);
    check('post-drag (cell0->far): compressor committed to the tail',
          l1[l1.length - 1] === 'compressor', `lane=${g1.lane}`);
    check('post-drag (cell0->far): neighbours pushed left (chorus 0, delay 1, reverb 2)',
          l1[0] === 'chorus' && l1[1] === 'delay' && l1[2] === 'reverb', `lane=${g1.lane}`);
    await screenshot(app, path.join(OUT, 'drag-after-cell0-5.png'));

    // ------------------------------------------------------------------
    // Drag 2 (m04679): press the SECOND node (delay, now at slot 1), drag
    // it to the far tail — the OTHER nodes must get a ghost too. The centre
    // list is from the PRE-drag-1 layout; recompute — the blocks moved.
    const c1b = await cells(app);
    const from2 = c1b[1];
    const to2 = c1b[Math.min(9, c1b.length - 1)];
    const res2 = await pointerDrag(app, from2, to2, 14);
    const mid2 = res2.log[Math.floor(res2.log.length / 2)];
    const mg2 = parseGhost(mid2.ghost);
    check('mid-drag (cell1->tail): ghost is alive (OTHER nodes get a ghost too)',
          mg2 !== null, mid2.ghost);
    if (mg2) {
      check('mid-drag (cell1->tail): ghost width == cell width',
            Math.abs(mg2.w - kCellW) <= 2, `ghost w=${mg2.w} cell pitch=${kCellW}`);
      const nearOrigin2 = res2.log.filter((l) => {
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
    // delay (slot 1) moves to the TAIL: the compact lane becomes
    // chorus 0, reverb 1, compressor 2, delay 3 (the visual grid is
    // compact — fillCellsFrom maps lane[i] onto grid slot i, no holes).
    const l2 = lane(g2);
    check('post-drag (cell1->tail): delay committed to the tail',
          l2[l2.length - 1] === 'delay', `lane=${g2.lane}`);
    check('post-drag (cell1->tail): reverb/compressor slid left (reverb 1, compressor 2)',
          l2[0] === 'chorus' && l2[1] === 'reverb' && l2[2] === 'compressor', `lane=${g2.lane}`);

    // ------------------------------------------------------------------
    // m05916-1: SYMMETRIC thresholds. The floor((x - rowX_)/cellW_) law
    // flips at exactly a HALF tile from the ghost's own centre — a 0.4-tile
    // press must NOT change the target, a 1.1-tile press MUST. Both
    // directions, equal margins (the old roundToInt law fired the right
    // swap at 0 tiles and the left swap at 1.5 tiles).
    //
    // Lane math: laneForShot() is the COMPACT model order (4 entries, no
    // grid holes) and fillCellsFrom maps lane[i] onto grid slot i — so the
    // grid the e2e pointer sees IS the compact list. State after drag 2:
    //   S0 = [chorus, reverb, compressor, delay]  (grid slots 0..3).
    // A frame with target=-1 is the 1px pre-placement transient (the ghost
    // is created on first travel, one step before it is inside the grid);
    // in-grid frames are the law under test.
    const inGrid = (l) => {
      const m = l.drag.match(/target=(-?\d+)/);
      return m !== null && m[1] !== '-1';
    };

    // A: 0.4-tile RIGHT from slot 0 (chorus): target stays 0, no commit.
    const cA = await cells(app);
    const rA = await pointerDrag(app, cA[0], { x: Math.round(cA[0].x + 0.4 * kCellW), y: cA[0].y }, 8);
    const rAG = rA.log.filter(inGrid);
    check('symmetric (0.4-tile right): target stays at the source slot',
          rAG.length > 0 && rAG.every((l) => /target=0\b/.test(l.drag)),
          `saw ${[...new Set(rAG.map((l) => l.drag))].join(' | ')}`);
    const gA = await geometry(app);
    check('symmetric (0.4-tile right): release commits NOTHING',
          lane(gA).join(',') === 'chorus,reverb,compressor,delay', gA.lane);

    // B: 1.1-tile RIGHT from slot 0 (chorus): target 1, swap committed.
    const cB = await cells(app);
    const rB = await pointerDrag(app, cB[0], { x: Math.round(cB[0].x + 1.1 * kCellW), y: cB[0].y }, 8);
    check('symmetric (1.1-tile right): target is the right-neighbour slot',
          /target=1\b/.test(rB.log[rB.log.length - 1].drag),
          `saw ${[...new Set(rB.log.filter(inGrid).map((l) => l.drag))].join(' | ')}`);
    const gB = await geometry(app);
    check('symmetric (1.1-tile right): release committed the swap',
          lane(gB).slice(0, 2).join(',') === 'reverb,chorus', gB.lane);

    // C: 0.4-tile LEFT from slot 1 (chorus, after B): target stays 1, no
    // commit — the left margin is as tight as the right was.
    const cC = await cells(app);
    const rC = await pointerDrag(app, cC[1], { x: Math.round(cC[1].x - 0.4 * kCellW), y: cC[1].y }, 8);
    const rCG = rC.log.filter(inGrid);
    check('symmetric (0.4-tile left): target stays at the source slot',
          rCG.length > 0 && rCG.every((l) => /target=1\b/.test(l.drag)),
          `saw ${[...new Set(rCG.map((l) => l.drag))].join(' | ')}`);
    const gC = await geometry(app);
    check('symmetric (0.4-tile left): release commits NOTHING',
          lane(gC).slice(0, 2).join(',') === 'reverb,chorus', gC.lane);

    // D: 1.1-tile LEFT from slot 1 (chorus): target 0, swap committed —
    // round-trip back to S0.
    const cD = await cells(app);
    const rD = await pointerDrag(app, cD[1], { x: Math.round(cD[1].x - 1.1 * kCellW), y: cD[1].y }, 8);
    check('symmetric (1.1-tile left): target is the left-neighbour slot',
          /target=0\b/.test(rD.log[rD.log.length - 1].drag),
          `saw ${[...new Set(rD.log.filter(inGrid).map((l) => l.drag))].join(' | ')}`);
    const gD = await geometry(app);
    check('symmetric (1.1-tile left): release committed the swap (back to S0)',
          lane(gD).slice(0, 2).join(',') === 'chorus,reverb', gD.lane);

    // m05916-3a: OFF-CHAIN release cancels. Drag the first block (slot 0)
    // to the far right, well past the last slot, release there: nothing may
    // move and no ghost may linger.
    const c6 = await cells(app);
    const c6Baseline = lane(await geometry(app)).join(',');
    const offGrid = { x: c6[9].x + kCellW + 20, y: c6[9].y };
    // Begin the press ourselves so we can hold past the edge before release.
    await app.sendCommand({ type: 'node-drag-begin', args: { x: c6[0].x, y: c6[0].y } });
    for (let i = 1; i <= 10; ++i) {
      const x = Math.round(c6[0].x + ((offGrid.x - c6[0].x) * i) / 10);
      const y = c6[0].y;
      await app.sendCommand({ type: 'node-drag-move', args: { x, y, button: 'left' } });
    }
    const gOff = await geometry(app);
    check('off-chain: drag still active off the grid', gOff.drag.startsWith('active'), gOff.drag);
    await app.sendCommand({ type: 'node-drag-end', args: { button: 'left' } });
    const g6 = await geometry(app);
    check('off-chain: release CANCELLED (lane unchanged)',
          lane(g6).join(',') === c6Baseline, g6.lane);
    check('off-chain: no ghost after cancel', parseGhost(g6.ghost) === null, g6.ghost);
    await screenshot(app, path.join(OUT, 'offchain-after.png'));

    // m06828-2: RIGHT-CLICK while dragging does NOT cancel (the off-chain
    // release above is the only cancel path). Press the first block (slot
    // 0), drag two tiles right (target=2, preview reflowed), then a right
    // mouseDown mid-drag: the drag must SURVIVE it (a context menu opens
    // instead), and the LEFT release must COMMIT the reflow, not revert.
    const c7 = await cells(app);
    const src7 = c7[0];
    const base7 = lane(await geometry(app));  // pre-drag compact lane
    const dragId7 = base7[0];                 // the block this drag carries
    const dst7 = { x: Math.round(src7.x + 2 * kCellW), y: src7.y };
    await app.sendCommand({ type: 'node-drag-begin', args: { x: src7.x, y: src7.y } });
    for (let i = 1; i <= 8; ++i) {
      const x = Math.round(src7.x + ((dst7.x - src7.x) * i) / 8);
      await app.sendCommand({ type: 'node-drag-move', args: { x, y: src7.y, button: 'left' } });
    }
    const gMid7 = await geometry(app);
    check('right-press-mid-drag: drag active with target=2 before the press',
          /target=2\b/.test(gMid7.drag), gMid7.drag);
    await screenshot(app, path.join(OUT, 'rightclick-middrag.png'));
    const gPress = await dragPress(app, { x: dst7.x, y: src7.y }, 'right');
    check('right-press-mid-drag: the drag SURVIVES the right mouseDown',
          gPress.drag.startsWith('active'), gPress.drag);
    // (The right release happened WITHIN the press command — the e2e
    // handler mirrors the OS sequence of a right click: down + up. The
    // left drag must now be the only thing in flight.)
    const gMid7b = await geometry(app);
    check('right-press-mid-drag: left drag still active after the right release',
          gMid7b.drag.startsWith('active'), gMid7b.drag);
    // Left release at the same spot: COMMIT (lane reflowed, slot 0 -> 2).
    await app.sendCommand({ type: 'node-drag-end', args: { button: 'left' } });
    const g7 = await geometry(app);
    // The drag ended at the ghost's target slot (2 from the source) —
    // assert the dragged block now sits AT that index of the compact lane,
    // whatever the pre-drag layout is (the cell1->tail drag above may have
    // left inserts in it).
    const postLane7 = lane(g7);
    check('right-press-mid-drag: left release COMMITTED the target-2 reflow',
          !/target=\d/.test(g7.drag) && postLane7.length === base7.length,
          `${g7.drag} / ${g7.lane}`);
    check('right-press-mid-drag: dragged block now at slot 2',
          postLane7[2] === dragId7 && postLane7[0] !== dragId7,
          `dragged=${dragId7} lane=${g7.lane}`);
    check('right-press-mid-drag: drag idle, no ghost',
          g7.drag === 'idle' && parseGhost(g7.ghost) === null,
          `${g7.drag} / ${g7.ghost}`);
    await screenshot(app, path.join(OUT, 'rightclick-after.png'));

    // The conv strip's settings/export band has its OWN e2e layer
    // (conv-strip.js): two AppConnections in one process race on the
    // first app's socket teardown ("Unhandled message").

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
