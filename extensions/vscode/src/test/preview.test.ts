import assert from "node:assert/strict";
import test from "node:test";

import {
  previewDocument,
  protectRemoteImages,
  restoreRemoteImages,
} from "../preview";

test("remote images are inert by default", () => {
  const protectedHtml = protectRemoteImages(
    '<img src="https://tracker.test/pixel.png" alt="">',
  );
  assert.match(protectedHtml, /data-email-markup-remote-src/);
  assert.doesNotMatch(protectedHtml, /(?:^|\s)src="https:/);
  assert.match(previewDocument(protectedHtml, false), /default-src 'none'/);
  assert.match(previewDocument(protectedHtml, false), /img-src data:/);
});

test("explicit action restores remote images for the current preview", () => {
  const source = '<img src="https://cdn.test/image.png" alt="">';
  assert.equal(restoreRemoteImages(protectRemoteImages(source)), source);
  assert.match(
    previewDocument(protectRemoteImages(source), true),
    /img-src https: http: data:/,
  );
});
