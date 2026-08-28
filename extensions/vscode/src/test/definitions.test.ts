import assert from "node:assert/strict";
import test from "node:test";

import { maskNonCode, readDefinitions } from "../web/definitions";

test("component props and slots are read back out of the source", () => {
  const definitions = readDefinitions(`@DefineComponent(name: "Metric")
  @Props
    label: string
    value: string
    accent: color = "#2563eb"
  @/Props
  @Slots
    left: required
    right: optional
  @/Slots
  @Template
    <div>@Slot(left);</div>
  @/Template
@/DefineComponent
`);
  assert.equal(definitions.components.length, 1);
  const [component] = definitions.components;
  assert.equal(component.name, "Metric");
  assert.deepEqual(
    component.props.map((prop) => [prop.name, prop.type, prop.defaultValue]),
    [
      ["label", "string", undefined],
      ["value", "string", undefined],
      ["accent", "color", '"#2563eb"'],
    ],
  );
  assert.deepEqual(
    component.slots.map((slot) => [slot.name, slot.required]),
    [
      ["left", true],
      ["right", false],
    ],
  );
  assert.equal(definitions.hasRenderableBody, false);
  assert.equal(definitions.isShell, false, "a @Slot inside a component is not a shell");
});

test("a token sheet is definitions only", () => {
  const definitions = readDefinitions(`@DefineToken(name: "accent", value: "#8E51D0");

@DefineToken(name: "ink", value: "#1A1523");
`);
  assert.deepEqual(
    definitions.tokens.map((token) => [token.name, token.value]),
    [
      ["accent", "#8E51D0"],
      ["ink", "#1A1523"],
    ],
  );
  assert.equal(definitions.hasRenderableBody, false);
  assert.equal(definitions.components.length, 0);
});

test("a document-level slot marks a shell", () => {
  const definitions = readDefinitions(`@Include("project.em");

<html><body><main>@Slot(default);</main></body></html>
`);
  assert.equal(definitions.isShell, true);
  assert.equal(definitions.hasRenderableBody, true);
});

test("a template that renders is not mistaken for a library", () => {
  const definitions = readDefinitions(`@Heading Campaign report @/Heading

<p>Hello @[business.name]</p>
`);
  assert.equal(definitions.hasRenderableBody, true);
  assert.equal(definitions.components.length, 0);
  assert.equal(definitions.tokens.length, 0);
});

test("comments and escaped sigils cannot be mistaken for markup", () => {
  const source = `@* @DefineComponent(name: "Ghost") *@
@// @DefineToken(name: "ghost", value: "#000");
@@DefineComponent
`;
  const definitions = readDefinitions(source);
  assert.equal(definitions.components.length, 0);
  assert.equal(definitions.tokens.length, 0);
  const masked = maskNonCode(source);
  assert.equal(masked.length, source.length, "masking must preserve every offset");
  assert.equal(
    masked.split("\n").length,
    source.split("\n").length,
    "masking must preserve line structure",
  );
});

test("a file that defines components and also renders is treated as a template", () => {
  const definitions = readDefinitions(`@DefineComponent(name: "Notice")
  @Slots
    default: required
  @/Slots
  @Template
    <section>@Slot(default);</section>
  @/Template
@/DefineComponent

@Notice
  Live content
@/Notice
`);
  assert.equal(definitions.components.length, 1);
  assert.equal(definitions.hasRenderableBody, true);
});
