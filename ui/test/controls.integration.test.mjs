import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import http from 'node:http';
import { randomBytes } from 'node:crypto';
import test from 'node:test';

const base = 'http://127.0.0.1:8190';
const timeoutMs = 10000;

// Exercise the running app, including its main-thread command dispatch, without mocks.
async function connect() {
  const socket = new WebSocket(`${base.replace('http:', 'ws:')}/ws`);
  const pending = new Map();
  const observers = new Set();
  let state, nextId = 0;
  function fail(error) {
    for (const request of pending.values()) request.reject(error);
    for (const observer of observers) observer.reject(error);
  }
  socket.addEventListener('error', () => fail(new Error('Cannot connect to the video app. Run make dev first.')));
  socket.addEventListener('close', () => fail(new Error('The video app disconnected during verification.')));
  socket.addEventListener('message', ({ data }) => {
    let message;
    try { message = JSON.parse(data); } catch (error) { fail(error); return; }
    if (message.type === 'state') {
      state = message;
      for (const observer of [...observers]) {
        if (observer.predicate(state)) observer.resolve(state);
      }
    } else if (message.type === 'result') {
      pending.get(message.id)?.resolve(message);
    }
  });

  function waitForState(predicate = () => true) {
    if (state && predicate(state)) return Promise.resolve(state);
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => observer.reject(new Error('Timed out waiting for a state snapshot.')), timeoutMs);
      const observer = {
        predicate,
        resolve(value) { clearTimeout(timer); observers.delete(observer); resolve(value); },
        reject(error) { clearTimeout(timer); observers.delete(observer); reject(error); },
      };
      observers.add(observer);
    });
  }
  const client = {
    waitForState,
    command(action, payload = {}) {
      const id = ++nextId;
      return new Promise((resolve, reject) => {
        const timer = setTimeout(() => request.reject(new Error(`No confirmation for ${action}.`)), timeoutMs);
        const request = {
          resolve(value) { clearTimeout(timer); pending.delete(id); resolve(value); },
          reject(error) { clearTimeout(timer); pending.delete(id); reject(error); },
        };
        pending.set(id, request);
        try { socket.send(JSON.stringify({ id, action, ...payload })); }
        catch (error) { request.reject(error); }
      });
    },
    close() { socket.close(); },
  };
  try { await waitForState(); } catch (error) { socket.close(); throw error; }
  return client;
}

function foreignOriginStatus() {
  return new Promise((resolve, reject) => {
    const request = http.get(`${base}/ws`, {
      headers: {
        Origin: 'https://example.invalid', Connection: 'Upgrade', Upgrade: 'websocket',
        'Sec-WebSocket-Version': '13', 'Sec-WebSocket-Key': randomBytes(16).toString('base64'),
      },
    }, (response) => { response.resume(); resolve(response.statusCode); });
    request.on('upgrade', (_response, socket) => {
      socket.destroy();
      reject(new Error('A foreign browser origin was allowed to open a WebSocket.'));
    });
    request.on('error', reject);
    request.setTimeout(timeoutMs, () => request.destroy(new Error('Origin check timed out.')));
  });
}

test('live Vue control transport', { timeout: 90000 }, async (t) => {
  const files = ['settings.toml', 'render-specs.toml'];
  const diskBefore = await Promise.all(files.map((file) => readFile(new URL(`../../bin/data/${file}`, import.meta.url))));
  const client = await connect();
  const original = await client.waitForState();
  let observer, effectBefore, effectTouched = false;
  const requireSuccess = (result) => {
    assert.equal(result.ok, true, result.error);
    return result.state;
  };
  try {
    await t.test('serves built Vue assets and keeps configuration paths private', async () => {
      const page = await fetch(`${base}/`);
      assert.equal(page.status, 200);
      const html = await page.text();
      const asset = html.match(/src="(\/assets\/[^" ]+\.js)"/)?.[1];
      assert.ok(asset, 'Build the Vue controls with make controls.');
      const script = await fetch(`${base}${asset}`);
      assert.equal(script.status, 200);
      assert.match(script.headers.get('content-type'), /javascript/);
      for (const path of ['/settings.toml', '/render-specs.toml', '/assets/%2e%2e/settings.toml']) {
        assert.equal((await fetch(`${base}${path}`)).status, 404, path);
      }
    });

    await t.test('rejects invalid settings atomically', async () => {
      for (const values of [
        { frameOffset: 121 }, { frameOffset: '3' }, { frameOffset: 1.5 },
        { nonexistent: true }, { pixelFlightStrength: 1.2, frameOffset: -1 },
      ]) {
        const result = await client.command('settings', { targetMode: original.settings.mode, values });
        assert.equal(result.ok, false, JSON.stringify(values));
        assert.ok(result.error);
        assert.deepEqual(result.state.settings, original.settings, 'Rejected patches must leave all settings intact.');
      }
      assert.equal((await client.command('mode', { mode: 'nonexistent' })).ok, false);
      assert.equal((await client.command('video', { index: original.sources.length })).ok, false);
    });

    await t.test('switches layers, confirms live edits and broadcasts to another client', async () => {
      effectBefore = requireSuccess(await client.command('mode', { mode: 'displacedVideo' })).settings;
      observer = await connect();
      const observed = await observer.waitForState();
      const fade = effectBefore.pixelFadePercent === 1.7 ? 2.3 : 1.7;
      effectTouched = true;
      const edited = requireSuccess(await client.command('settings', {
        targetMode: 'displacedVideo',
        values: { colouredParticles: !effectBefore.colouredParticles, pixelFadePercent: fade },
      }));
      assert.equal(edited.settings.mode, 'displacedVideo');
      assert.equal(edited.settings.colouredParticles, !effectBefore.colouredParticles);
      assert.ok(Math.abs(edited.settings.pixelFadePercent - fade) < 0.00001);
      const broadcast = await observer.waitForState((snapshot) =>
        snapshot.sequence > observed.sequence && snapshot.settings.colouredParticles === !effectBefore.colouredParticles);
      assert.ok(Math.abs(broadcast.settings.pixelFadePercent - fade) < 0.00001);
    });

    await t.test('rejects edits aimed at a layer that is no longer selected', async () => {
      const result = await client.command('settings', { targetMode: 'motionMask', values: { frameOffset: 2 } });
      assert.equal(result.ok, false);
      assert.match(result.error, /Layer changed/);
      assert.equal(result.state.settings.mode, 'displacedVideo');
    });

    await t.test('a new connection receives current authoritative values', async () => {
      const reconnected = await connect();
      try {
        const snapshot = await reconnected.waitForState();
        assert.equal(snapshot.settings.mode, 'displacedVideo');
        assert.equal(snapshot.settings.colouredParticles, !effectBefore.colouredParticles);
        assert.ok(snapshot.sequence > original.sequence);
      } finally { reconnected.close(); }
    });

    await t.test('rejects WebSocket upgrades from foreign browser origins', async () => {
      assert.equal(await foreignOriginStatus(), 403);
    });
  } finally {
    try {
      if (effectTouched) {
        requireSuccess(await client.command('mode', { mode: 'displacedVideo' }));
        requireSuccess(await client.command('settings', {
          targetMode: 'displacedVideo', values: {
            colouredParticles: effectBefore.colouredParticles,
            pixelFadePercent: effectBefore.pixelFadePercent,
            bakeStoppedPixels: effectBefore.bakeStoppedPixels,
          },
        }));
      }
      const restored = requireSuccess(await client.command('mode', { mode: original.settings.mode }));
      assert.deepEqual(restored.settings, original.settings, 'Restore the original layer and all its values.');
      assert.equal(restored.selectedVideo, original.selectedVideo);
      const diskAfter = await Promise.all(files.map((file) => readFile(new URL(`../../bin/data/${file}`, import.meta.url))));
      for (let index = 0; index < files.length; ++index) {
        assert.deepEqual(diskAfter[index], diskBefore[index], `${files[index]} must remain byte-for-byte unchanged.`);
      }
    } finally {
      observer?.close();
      client.close();
    }
  }
});
