import assert from "node:assert/strict";
import test from "node:test";

import { literalEmbeddedImages } from "../imageReferences";

test("literal image audit skips explicitly remote and dynamic sources", () => {
  const source = [
    '@Image(src: "https://cdn.test/hero.png", alt: "Hero");',
    '@Image(src: "https://cdn.test/remote.png", embed: false);',
    "@Image(src: recipient.image);",
  ].join("\n");
  const references = literalEmbeddedImages(source);
  assert.deepEqual(references.map(({ url }) => url), [
    "https://cdn.test/hero.png",
  ]);
  assert.equal(source.slice(references[0].start, references[0].end), '"https://cdn.test/hero.png"');
});
