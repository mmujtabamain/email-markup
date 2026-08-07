import { mkdir, readFile, writeFile } from "node:fs/promises";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const here = dirname(fileURLToPath(import.meta.url));
const extension = resolve(here, "..");
const lexical = JSON.parse(await readFile(resolve(extension, "../../syntax/lexical.json"), "utf8"));

const grammar = {
  $schema: "https://raw.githubusercontent.com/martinring/tmlanguage/master/tmlanguage.json",
  name: "Email Markup",
  scopeName: "source.email-markup",
  patterns: [
    { include: "#comments" },
    { include: "#interpolation" },
    { include: "#props" },
    { include: "#slots" },
    { include: "#defineStyle" },
    { include: "#media" },
    { include: "#close" },
    { include: "#invocation" },
    { include: "#construct" },
    { include: "#escaped" },
    { include: "text.html.basic" }
  ],
  repository: {
    comments: {
      patterns: [
        { name: "comment.block.email-markup", begin: "@\\*", end: "\\*@" },
        { name: "comment.line.double-slash.email-markup", begin: "@//", end: "$" }
      ]
    },
    interpolation: {
      name: "meta.interpolation.email-markup",
      begin: "@\\{",
      beginCaptures: { 0: { name: "punctuation.section.interpolation.begin.email-markup" } },
      end: "\\}",
      endCaptures: { 0: { name: "punctuation.section.interpolation.end.email-markup" } },
      patterns: [
        { name: "keyword.operator.word.email-markup", match: "\\b(?:and|or|not|in)\\b" },
        { name: "constant.language.email-markup", match: "\\b(?:true|false|null)\\b" },
        { name: "constant.numeric.email-markup", match: "-?\\b\\d+(?:\\.\\d+)?\\b" },
        { name: "string.quoted.double.email-markup", begin: "\"", end: "\"" },
        { name: "variable.other.email-markup", match: "[A-Za-z_][A-Za-z0-9_.]*" }
      ]
    },
    props: {
      name: "meta.block.props.email-markup",
      begin: "(@)(Props)\\b",
      beginCaptures: {
        1: { name: "punctuation.definition.tag.email-markup" },
        2: { name: "keyword.control.email-markup" },
      },
      end: "(@/)(Props)\\b",
      endCaptures: {
        1: { name: "punctuation.definition.tag.email-markup" },
        2: { name: "keyword.control.email-markup" },
      },
      patterns: [
        { include: "#comments" },
        { include: "#interpolation" },
        {
          match: "\\b([A-Za-z_][A-Za-z0-9_]*)(\\??)(\\s*)(:)(\\s*)([A-Za-z_][A-Za-z0-9_]*(?:\\([^)]*\\))?)",
          captures: {
            1: { name: "support.type.property-name.email-markup" },
            2: { name: "keyword.operator.optional.email-markup" },
            4: { name: "punctuation.separator.key-value.email-markup" },
            6: { name: "storage.type.email-markup" },
          },
        },
        { name: "keyword.operator.assignment.email-markup", match: "=" },
        { name: "constant.language.email-markup", match: "\\b(?:true|false|null)\\b" },
        { name: "constant.numeric.email-markup", match: "-?\\b\\d+(?:\\.\\d+)?\\b" },
        { name: "string.quoted.double.email-markup", begin: "\"", end: "\"" },
        { name: "variable.other.email-markup", match: "[A-Za-z_][A-Za-z0-9_.]*" },
      ],
    },
    slots: {
      name: "meta.block.slots.email-markup",
      begin: "(@)(Slots)\\b",
      beginCaptures: {
        1: { name: "punctuation.definition.tag.email-markup" },
        2: { name: "keyword.control.email-markup" },
      },
      end: "(@/)(Slots)\\b",
      endCaptures: {
        1: { name: "punctuation.definition.tag.email-markup" },
        2: { name: "keyword.control.email-markup" },
      },
      patterns: [{
        match: "\\b([A-Za-z_][A-Za-z0-9_]*)(\\s*)(:)(\\s*)(required|optional)\\b",
        captures: {
          1: { name: "variable.parameter.email-markup" },
          3: { name: "punctuation.separator.key-value.email-markup" },
          5: { name: "storage.modifier.email-markup" },
        },
      }],
    },
    defineStyle: {
      name: "meta.embedded.block.css.email-markup",
      begin: "(@)(DefineStyle)\\b",
      beginCaptures: {
        1: { name: "punctuation.definition.tag.email-markup" },
        2: { name: "keyword.control.email-markup" },
      },
      end: "(@/)(DefineStyle)\\b",
      endCaptures: {
        1: { name: "punctuation.definition.tag.email-markup" },
        2: { name: "keyword.control.email-markup" },
      },
      patterns: [
        { include: "#namedDefinitionArguments" },
        { include: "#interpolation" },
        { include: "source.css#rule-list-innards" },
      ],
    },
    media: {
      name: "meta.embedded.block.css.email-markup",
      begin: "(@)(Media)\\b",
      beginCaptures: {
        1: { name: "punctuation.definition.tag.email-markup" },
        2: { name: "keyword.control.email-markup" },
      },
      end: "(@/)(Media)\\b",
      endCaptures: {
        1: { name: "punctuation.definition.tag.email-markup" },
        2: { name: "keyword.control.email-markup" },
      },
      patterns: [
        { include: "#positionalStringArguments" },
        { include: "#interpolation" },
        { include: "source.css" },
      ],
    },
    namedDefinitionArguments: {
      name: "meta.arguments.email-markup",
      begin: "\\G(\\s*)(\\()",
      beginCaptures: { 2: { name: "punctuation.section.arguments.begin.email-markup" } },
      end: "\\)",
      endCaptures: { 0: { name: "punctuation.section.arguments.end.email-markup" } },
      patterns: [
        {
          match: "\\b(name)(\\s*)(:)",
          captures: {
            1: { name: "support.type.property-name.email-markup" },
            3: { name: "punctuation.separator.key-value.email-markup" },
          },
        },
        { name: "string.quoted.double.email-markup", begin: "\"", end: "\"" },
      ],
    },
    positionalStringArguments: {
      name: "meta.arguments.email-markup",
      begin: "\\G(\\s*)(\\()",
      beginCaptures: { 2: { name: "punctuation.section.arguments.begin.email-markup" } },
      end: "\\)",
      endCaptures: { 0: { name: "punctuation.section.arguments.end.email-markup" } },
      patterns: [{ name: "string.quoted.double.email-markup", begin: "\"", end: "\"" }],
    },
    close: {
      match: "@/([A-Z][A-Za-z0-9_]*)",
      captures: {
        0: { name: "punctuation.definition.tag.email-markup" },
        1: { name: "entity.name.tag.email-markup" }
      }
    },
    invocation: {
      name: "meta.function-call.email-markup",
      begin: "(@)([A-Z][A-Za-z0-9_]*)(\\s*)(\\()",
      beginCaptures: {
        1: { name: "punctuation.definition.tag.email-markup" },
        2: { name: "entity.name.function.em entity.name.tag.email-markup" },
        4: { name: "punctuation.section.arguments.begin.email-markup" },
      },
      end: "\\)",
      endCaptures: { 0: { name: "punctuation.section.arguments.end.email-markup" } },
      patterns: [
        { include: "#comments" },
        { include: "#interpolation" },
        {
          match: "\\b([A-Za-z_][A-Za-z0-9_]*)(\\s*)(:)",
          captures: {
            1: { name: "support.type.property-name.email-markup" },
            3: { name: "punctuation.separator.key-value.email-markup" },
          },
        },
        { name: "punctuation.separator.arguments.email-markup", match: "," },
        { name: "keyword.operator.word.email-markup", match: "\\b(?:and|or|not|in)\\b" },
        { name: "keyword.operator.email-markup", match: "(?:==|!=|<=|>=|[+\\-*/%<>])" },
        { name: "constant.language.email-markup", match: "\\b(?:true|false|null)\\b" },
        { name: "constant.numeric.email-markup", match: "-?\\b\\d+(?:\\.\\d+)?\\b" },
        { name: "string.quoted.double.email-markup", begin: "\"", end: "\"" },
        { name: "variable.other.readwrite.email-markup", match: "[A-Za-z_][A-Za-z0-9_.]*" },
      ],
    },
    construct: {
      match: "@([A-Z][A-Za-z0-9_]*)",
      captures: {
        0: { name: "punctuation.definition.tag.email-markup" },
        1: { name: "entity.name.tag.email-markup" }
      }
    },
    escaped: { name: "constant.character.escape.email-markup", match: "@@" }
  },
  metadata: { generatedFrom: "syntax/lexical.json", version: lexical.version }
};

const languageConfiguration = {
  comments: { lineComment: lexical.lineComment, blockComment: lexical.blockComment },
  brackets: [["(", ")"], ["{", "}"], ["<", ">"]],
  autoClosingPairs: [
    { open: "(", close: ")" },
    { open: "{", close: "}" },
    { open: "<", close: ">" },
    { open: "\"", close: "\"", notIn: ["string", "comment"] }
  ],
  surroundingPairs: [["(", ")"], ["{", "}"], ["<", ">"], ["\"", "\""]],
  folding: { markers: { start: "^\\s*@[A-Z][A-Za-z0-9_]*(?:\\([^)]*\\))?\\s*$", end: "^\\s*@/[A-Z][A-Za-z0-9_]*\\s*$" } },
  indentationRules: {
    increaseIndentPattern: "^.*@[A-Z][A-Za-z0-9_]*(?:\\([^)]*\\))?\\s*$",
    decreaseIndentPattern: "^\\s*@/[A-Z][A-Za-z0-9_]*"
  }
};

const injectionGrammar = {
  $schema: grammar.$schema,
  name: "Email Markup in HTML and CSS",
  scopeName: "email-markup.injection",
  injectionSelector: "L:text.html.basic, L:source.css",
  patterns: [
    { include: "source.email-markup#comments" },
    { include: "source.email-markup#interpolation" },
    { include: "source.email-markup#props" },
    { include: "source.email-markup#slots" },
    { include: "source.email-markup#defineStyle" },
    { include: "source.email-markup#media" },
    { include: "source.email-markup#close" },
    { include: "source.email-markup#invocation" },
    { include: "source.email-markup#construct" },
    { include: "source.email-markup#escaped" },
  ],
  metadata: { generatedFrom: "syntax/lexical.json", version: lexical.version },
};

await mkdir(resolve(extension, "syntaxes"), { recursive: true });
await writeFile(resolve(extension, "syntaxes/email-markup.tmLanguage.json"), `${JSON.stringify(grammar, null, 2)}\n`);
await writeFile(resolve(extension, "syntaxes/email-markup.injection.tmLanguage.json"), `${JSON.stringify(injectionGrammar, null, 2)}\n`);
await writeFile(resolve(extension, "language-configuration.json"), `${JSON.stringify(languageConfiguration, null, 2)}\n`);
