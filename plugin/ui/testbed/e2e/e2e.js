// Shared e2e plumbing: launch the testbed on a given display and hand back a
// connected AppConnection (C++ TestCentre dials out to the JS-side port that
// the framework's launch() binds).
//
// The app runs a REAL OS window (a live pointer path — that is the whole
// point of this layer), so the caller must have a display to put it on: a
// real X, or xvfb-run in CI/headless. DISPLAY is inherited from the
// environment; LD_LIBRARY_PATH is added for the staged toolchain libs.
'use strict';

const path = require('path');
const { AppConnection } = require('@focusritegroup/juce-end-to-end');

const REPO_ROOT = path.resolve(__dirname, '..', '..', '..', '..');
const APP = path.join(REPO_ROOT, 'build-ui', 'UiTestbed_artefacts', 'Release', 'UiTestbed');
const STAGE_LIB = '/home/jambo/buildkit/stage/usr/lib/x86_64-linux-gnu';

async function launchApp(scenario, timeoutMs = 15000) {
  const app = new AppConnection({ appPath: APP });
  const env = {
    ...process.env,
    LD_LIBRARY_PATH: `${STAGE_LIB}${process.env.LD_LIBRARY_PATH ? ':' + process.env.LD_LIBRARY_PATH : ''}`,
  };
  await Promise.race([
    app.launch(['--scenario', scenario], env),
    new Promise((_, rej) =>
      setTimeout(() => rej(new Error(`timed out waiting for the app to connect (${scenario})`)), timeoutMs)
    ),
  ]);
  return app;
}

// One real pointer drag: a press at the source cell centre, `steps` moves (so
// the drag stream is seen, like a user's hand), and a release at the final
// point. Returns every intermediate geometry probe, for the assertions and
// the screenshots taken between steps.
async function pointerDrag(app, from, to, steps = 10) {
  const log = [];
  await app.sendCommand({ type: 'node-drag-begin', args: { x: from.x, y: from.y } });
  for (let i = 1; i <= steps; ++i) {
    const x = Math.round(from.x + ((to.x - from.x) * i) / steps);
    const y = Math.round(from.y + ((to.y - from.y) * i) / steps);
    await app.sendCommand({ type: 'node-drag-move', args: { x, y } });
    const g = await app.sendCommand({ type: 'node-geometry', args: {} });
    log.push({ step: i, x, y, ghost: g.ghost, drag: g.drag });
  }
  await app.sendCommand({ type: 'node-drag-end', args: {} });
  return log;
}

async function geometry(app) {
  return app.sendCommand({ type: 'node-geometry', args: {} });
}

async function setView(app, nodeView) {
  const r = await app.sendCommand({ type: 'node-set-view', args: { nodeView } });
  return r.nodeView;
}

async function cells(app) {
  const r = await app.sendCommand({ type: 'node-cells', args: {} });
  // C++ returns "x1;y1,x2;y2,..." (window coords, lane order).
  return r.cells.split(',').filter(Boolean).map((p) => {
    const [x, y] = p.split(';').map(Number);
    return { x, y };
  });
}

async function screenshot(app, out) {
  return app.sendCommand({ type: 'node-screenshot', args: { out } });
}

module.exports = { launchApp, pointerDrag, geometry, cells, setView, screenshot, APP };
