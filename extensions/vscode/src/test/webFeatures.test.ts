import assert from "node:assert/strict";
import test from "node:test";

import { cssProjection, localClasses } from "../webProjection";

test("CSS projection preserves style content and source offsets", () => {
  const source = "<p>Hi</p>\n<style>.card { color: red; }</style>";
  const projection = cssProjection(source);
  assert.equal(projection.text.length, source.length);
  assert.match(projection.text, /\.card \{ color: red; \}/);
  assert.equal(projection.text.slice(0, source.indexOf(".card")).trim(), "");
});

test("local CSS classes are available to HTML class attributes", () => {
  const source = "<style>.card{} .card-title{} .card{}</style><div class=\"\"></div>";
  assert.deepEqual(localClasses(source), ["card", "card-title"]);
});
