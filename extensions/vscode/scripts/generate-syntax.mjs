import { mkdir, readFile, writeFile } from "node:fs/promises";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const here = dirname(fileURLToPath(import.meta.url));
const extension = resolve(here, "..");
const lexical = JSON.parse(await readFile(resolve(extension, "../../syntax/lexical.json"), "utf8"));

const grammar = {
  $schema: "https://raw.githubusercontent.com/martinring/tmlanguage/master/tmlanguage.json",
  name: "ELL",
  scopeName: "source.ell",
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
        { name: "comment.block.ell", begin: "@\\*", end: "\\*@" },
        { name: "comment.line.double-slash.ell", begin: "@//", end: "$" }
      ]
    },
    interpolation: {
      name: "meta.interpolation.ell",
      begin: "@\\{",
      beginCaptures: { 0: { name: "punctuation.section.interpolation.begin.ell" } },
      end: "\\}",
      endCaptures: { 0: { name: "punctuation.section.interpolation.end.ell" } },
      patterns: [
        { name: "keyword.operator.word.ell", match: "\\b(?:and|or|not|in)\\b" },
        { name: "constant.language.ell", match: "\\b(?:true|false|null)\\b" },
        { name: "constant.numeric.ell", match: "-?\\b\\d+(?:\\.\\d+)?\\b" },
        { name: "string.quoted.double.ell", begin: "\"", end: "\"" },
        { name: "variable.other.ell", match: "[A-Za-z_][A-Za-z0-9_.]*" }
      ]
    },
    props: {
      name: "meta.block.props.ell",
      begin: "(@)(Props)\\b",
      beginCaptures: {
        1: { name: "punctuation.definition.tag.ell" },
        2: { name: "keyword.control.ell" },
      },
      end: "(@/)(Props)\\b",
      endCaptures: {
        1: { name: "punctuation.definition.tag.ell" },
        2: { name: "keyword.control.ell" },
      },
      patterns: [
        { include: "#comments" },
        { include: "#interpolation" },
        {
          match: "\\b([A-Za-z_][A-Za-z0-9_]*)(\\??)(\\s*)(:)(\\s*)([A-Za-z_][A-Za-z0-9_]*(?:\\([^)]*\\))?)",
          captures: {
            1: { name: "support.type.property-name.ell" },
            2: { name: "keyword.operator.optional.ell" },
            4: { name: "punctuation.separator.key-value.ell" },
            6: { name: "storage.type.ell" },
          },
        },
        { name: "keyword.operator.assignment.ell", match: "=" },
        { name: "constant.language.ell", match: "\\b(?:true|false|null)\\b" },
        { name: "constant.numeric.ell", match: "-?\\b\\d+(?:\\.\\d+)?\\b" },
        { name: "string.quoted.double.ell", begin: "\"", end: "\"" },
        { name: "variable.other.ell", match: "[A-Za-z_][A-Za-z0-9_.]*" },
      ],
    },
    slots: {
      name: "meta.block.slots.ell",
      begin: "(@)(Slots)\\b",
      beginCaptures: {
        1: { name: "punctuation.definition.tag.ell" },
        2: { name: "keyword.control.ell" },
      },
      end: "(@/)(Slots)\\b",
      endCaptures: {
        1: { name: "punctuation.definition.tag.ell" },
        2: { name: "keyword.control.ell" },
      },
      patterns: [{
        match: "\\b([A-Za-z_][A-Za-z0-9_]*)(\\s*)(:)(\\s*)(required|optional)\\b",
        captures: {
          1: { name: "variable.parameter.ell" },
          3: { name: "punctuation.separator.key-value.ell" },
          5: { name: "storage.modifier.ell" },
        },
      }],
    },
    defineStyle: {
      name: "meta.embedded.block.css.ell",
      begin: "(@)(DefineStyle)\\b",
      beginCaptures: {
        1: { name: "punctuation.definition.tag.ell" },
        2: { name: "keyword.control.ell" },
      },
      end: "(@/)(DefineStyle)\\b",
      endCaptures: {
        1: { name: "punctuation.definition.tag.ell" },
        2: { name: "keyword.control.ell" },
      },
      patterns: [
        { include: "#namedDefinitionArguments" },
        { include: "#interpolation" },
        { include: "source.css#rule-list-innards" },
      ],
    },
    media: {
      name: "meta.embedded.block.css.ell",
      begin: "(@)(Media)\\b",
      beginCaptures: {
        1: { name: "punctuation.definition.tag.ell" },
        2: { name: "keyword.control.ell" },
      },
      end: "(@/)(Media)\\b",
      endCaptures: {
        1: { name: "punctuation.definition.tag.ell" },
        2: { name: "keyword.control.ell" },
      },
      patterns: [
        { include: "#positionalStringArguments" },
        { include: "#interpolation" },
        { include: "source.css" },
      ],
    },
    namedDefinitionArguments: {
      name: "meta.arguments.ell",
      begin: "\\G(\\s*)(\\()",
      beginCaptures: { 2: { name: "punctuation.section.arguments.begin.ell" } },
      end: "\\)",
      endCaptures: { 0: { name: "punctuation.section.arguments.end.ell" } },
      patterns: [
        {
          match: "\\b(name)(\\s*)(:)",
          captures: {
            1: { name: "support.type.property-name.ell" },
            3: { name: "punctuation.separator.key-value.ell" },
          },
        },
        { name: "string.quoted.double.ell", begin: "\"", end: "\"" },
      ],
    },
    positionalStringArguments: {
      name: "meta.arguments.ell",
      begin: "\\G(\\s*)(\\()",
      beginCaptures: { 2: { name: "punctuation.section.arguments.begin.ell" } },
      end: "\\)",
      endCaptures: { 0: { name: "punctuation.section.arguments.end.ell" } },
      patterns: [{ name: "string.quoted.double.ell", begin: "\"", end: "\"" }],
    },
    close: {
      match: "@/([A-Z][A-Za-z0-9_]*)",
      captures: {
        0: { name: "punctuation.definition.tag.ell" },
        1: { name: "entity.name.tag.ell" }
      }
    },
    invocation: {
      name: "meta.function-call.ell",
      begin: "(@)([A-Z][A-Za-z0-9_]*)(\\s*)(\\()",
      beginCaptures: {
        1: { name: "punctuation.definition.tag.ell" },
        2: { name: "entity.name.function.ell entity.name.tag.ell" },
        4: { name: "punctuation.section.arguments.begin.ell" },
      },
      end: "\\)",
      endCaptures: { 0: { name: "punctuation.section.arguments.end.ell" } },
      patterns: [
        { include: "#comments" },
        { include: "#interpolation" },
        {
          match: "\\b([A-Za-z_][A-Za-z0-9_]*)(\\s*)(:)",
          captures: {
            1: { name: "support.type.property-name.ell" },
            3: { name: "punctuation.separator.key-value.ell" },
          },
        },
        { name: "punctuation.separator.arguments.ell", match: "," },
        { name: "keyword.operator.word.ell", match: "\\b(?:and|or|not|in)\\b" },
        { name: "keyword.operator.ell", match: "(?:==|!=|<=|>=|[+\\-*/%<>])" },
        { name: "constant.language.ell", match: "\\b(?:true|false|null)\\b" },
        { name: "constant.numeric.ell", match: "-?\\b\\d+(?:\\.\\d+)?\\b" },
        { name: "string.quoted.double.ell", begin: "\"", end: "\"" },
        { name: "variable.other.readwrite.ell", match: "[A-Za-z_][A-Za-z0-9_.]*" },
      ],
    },
    construct: {
      match: "@([A-Z][A-Za-z0-9_]*)",
      captures: {
        0: { name: "punctuation.definition.tag.ell" },
        1: { name: "entity.name.tag.ell" }
      }
    },
    escaped: { name: "constant.character.escape.ell", match: "@@" }
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
  name: "ELL in HTML and CSS",
  scopeName: "ell.injection",
  injectionSelector: "L:text.html.basic, L:source.css",
  patterns: [
    { include: "source.ell#comments" },
    { include: "source.ell#interpolation" },
    { include: "source.ell#props" },
    { include: "source.ell#slots" },
    { include: "source.ell#defineStyle" },
    { include: "source.ell#media" },
    { include: "source.ell#close" },
    { include: "source.ell#invocation" },
    { include: "source.ell#construct" },
    { include: "source.ell#escaped" },
  ],
  metadata: { generatedFrom: "syntax/lexical.json", version: lexical.version },
};

await mkdir(resolve(extension, "syntaxes"), { recursive: true });
await writeFile(resolve(extension, "syntaxes/ell.tmLanguage.json"), `${JSON.stringify(grammar, null, 2)}\n`);
await writeFile(resolve(extension, "syntaxes/ell.injection.tmLanguage.json"), `${JSON.stringify(injectionGrammar, null, 2)}\n`);
await writeFile(resolve(extension, "language-configuration.json"), `${JSON.stringify(languageConfiguration, null, 2)}\n`);
