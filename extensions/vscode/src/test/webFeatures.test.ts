import assert from "node:assert/strict";
import test from "node:test";

import {
  cssProjection,
  htmlLinkedRanges,
  htmlProjection,
  isEmmetContext,
  localClasses,
  webCompletionLanguage,
} from "../webProjection";

test("CSS projection preserves style content and source offsets", () => {
  const source = "<p>Hi</p>\n<style>.card { color: red; }</style>";
  const projection = cssProjection(source);
  assert.equal(projection.text.length, source.length);
  assert.match(projection.text, /\.card \{ color: red; \}/);
  assert.equal(projection.text.slice(0, source.indexOf(".card")).trim(), "");
});

test("local CSS classes are available to HTML class attributes", () => {
  const source =
    '<style>.card{} .card-title{} .card{}</style><div class=""></div>';
  assert.deepEqual(localClasses(source), ["card", "card-title"]);
});

test("CSS projection covers style tags, inline declarations, styles, and media", () => {
  const source = [
    "<style>.from-tag { color: red; }</style>",
    '<div style="display: block; color: blue"></div>',
    '@DefineStyle(name: "card")',
    "  padding: 12px;",
    "@/DefineStyle",
    '@Media("(max-width: 600px)")',
    "  .from-media { display: block; }",
    "@/Media",
  ].join("\n");
  const projection = cssProjection(source);
  assert.equal(projection.text.length, source.length);
  assert.match(projection.text, /\.from-tag \{ color: red; \}/);
  assert.match(projection.text, /display: block; color: blue/);
  assert.match(projection.text, /padding: 12px/);
  assert.match(projection.text, /\.from-media \{ display: block; \}/);
  assert.equal(projection.ranges.length, 4);
});

test("CSS projection ignores commented blocks and masks Email Markup expressions", () => {
  const source = [
    "@*",
    '@Media("(max-width: 600px)") .ignored { display: block; } @/Media',
    '<span style="color: red">ignored too</span>',
    "*@",
    "@DefineStyle(",
    '  name: "card") color: @{token.accent}; @/DefineStyle',
  ].join("\n");
  const projection = cssProjection(source);
  assert.equal(projection.text.length, source.length);
  assert.doesNotMatch(projection.text, /ignored/);
  assert.doesNotMatch(projection.text, /token\.accent|@\{/);
  assert.match(projection.text, /color:\s+0\s+;/);
  assert.equal(projection.ranges.length, 1);
});

test("projections preserve UTF-16 offsets and hide Email Markup from HTML", () => {
  const source =
    '😀 @Heading Hello @/Heading <p title="@{account.name}">Zażółć</p>';
  const projection = htmlProjection(source);
  assert.equal(projection.length, source.length);
  assert.equal(projection.indexOf("<p"), source.indexOf("<p"));
  assert.doesNotMatch(projection, /@Heading|account\.name/);
  assert.match(projection, /<p title="x\s*">Zażółć<\/p>/);
});

test("strict completion contexts separate prose, HTML, CSS, and Emmet", () => {
  const prose = "Choose a plan to restore access.";
  assert.equal(webCompletionLanguage(prose, prose.length), undefined);
  assert.equal(webCompletionLanguage("<", 1), "html");
  assert.equal(webCompletionLanguage("  .card", 7), "html");
  assert.equal(webCompletionLanguage("ordinary sentence.", 18), undefined);

  const css = '@DefineStyle(name: "card")\n  color: red;\n@/DefineStyle';
  assert.equal(webCompletionLanguage(css, css.indexOf("red")), "css");
  assert.equal(isEmmetContext("ul>li.item", 10), true);
});

test("HTML linked ranges pair nested opening and closing tags", () => {
  const source = "<section><section>Hi</section></section>";
  const inner = source.indexOf("section", 2);
  const ranges = htmlLinkedRanges(source, inner);
  assert.deepEqual(ranges, [
    [inner, inner + "section".length],
    [
      source.indexOf("section", inner + 1),
      source.indexOf("section", inner + 1) + "section".length,
    ],
  ]);
});
