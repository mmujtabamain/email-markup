import { promises as dns } from "node:dns";
import { isIP } from "node:net";
import * as vscode from "vscode";

import { literalEmbeddedImages } from "./imageReferences";

function privateIp(address: string): boolean {
  if (isIP(address) === 4) {
    const [first, second] = address.split(".").map(Number);
    return (
      first === 0 ||
      first === 10 ||
      first === 127 ||
      first >= 224 ||
      (first === 100 && second >= 64 && second <= 127) ||
      (first === 169 && second === 254) ||
      (first === 172 && second >= 16 && second <= 31) ||
      (first === 192 && second === 168)
    );
  }
  const normalized = address.toLowerCase();
  return (
    normalized === "::" ||
    normalized === "::1" ||
    normalized.startsWith("fc") ||
    normalized.startsWith("fd") ||
    /^fe[89ab]/.test(normalized) ||
    normalized.startsWith("ff")
  );
}

async function publicUrl(raw: string): Promise<URL> {
  const url = new URL(raw);
  if (url.protocol !== "https:" && url.protocol !== "http:")
    throw new Error("unsupported image URL protocol");
  const addresses = await dns.lookup(url.hostname, { all: true });
  if (!addresses.length || addresses.some(({ address }) => privateIp(address)))
    throw new Error("private network image URLs are not fetched");
  return url;
}

export function registerRemoteImageAudit(
  context: vscode.ExtensionContext,
): void {
  const diagnostics = vscode.languages.createDiagnosticCollection(
    "email-markup-images",
  );
  const sizes = new Map<string, number | undefined>();
  const timers = new Map<string, ReturnType<typeof setTimeout>>();
  const controllers = new Map<string, AbortController>();

  const size = async (
    raw: string,
    signal: AbortSignal,
  ): Promise<number | undefined> => {
    if (sizes.has(raw)) return sizes.get(raw);
    try {
      let target = await publicUrl(raw);
      for (let redirects = 0; redirects <= 3; ++redirects) {
        const request = new AbortController();
        const abort = (): void => request.abort();
        signal.addEventListener("abort", abort, { once: true });
        const timeout = setTimeout(abort, 5_000);
        let response: Response;
        try {
          response = await fetch(target, {
            method: "HEAD",
            redirect: "manual",
            signal: request.signal,
          });
        } finally {
          clearTimeout(timeout);
          signal.removeEventListener("abort", abort);
        }
        if (response.status >= 300 && response.status < 400) {
          const location = response.headers.get("location");
          if (!location || redirects === 3) return undefined;
          target = await publicUrl(new URL(location, target).toString());
          continue;
        }
        if (!response.ok) return undefined;
        const value = Number(response.headers.get("content-length"));
        const result =
          Number.isSafeInteger(value) && value >= 0 ? value : undefined;
        if (!signal.aborted) sizes.set(raw, result);
        return result;
      }
    } catch {
      return undefined;
    }
    return undefined;
  };

  const audit = async (document: vscode.TextDocument): Promise<void> => {
    if (document.languageId !== "email-markup" || !vscode.workspace.isTrusted)
      return;
    const key = document.uri.toString();
    controllers.get(key)?.abort();
    const controller = new AbortController();
    controllers.set(key, controller);
    const version = document.version;
    const warningBytes = vscode.workspace
      .getConfiguration("email-markup.images", document.uri)
      .get<number>("base64WarningBytes", 100 * 1024);
    const findings = await Promise.all(
      literalEmbeddedImages(document.getText()).map(async (reference) => ({
        reference,
        bytes: await size(reference.url, controller.signal),
      })),
    );
    if (controller.signal.aborted || document.version !== version) return;
    diagnostics.set(
      document.uri,
      findings
        .filter(({ bytes }) => bytes !== undefined && bytes > warningBytes)
        .map(({ reference, bytes }) => {
          const diagnostic = new vscode.Diagnostic(
            new vscode.Range(
              document.positionAt(reference.start),
              document.positionAt(reference.end),
            ),
            `This ${bytes?.toLocaleString()}-byte image will grow by about one third when Base64 encoded. Use embed: false to retain the remote URL.`,
            vscode.DiagnosticSeverity.Warning,
          );
          diagnostic.code = "EM0816";
          diagnostic.source = "email-markup-image-audit";
          return diagnostic;
        }),
    );
  };

  const schedule = (document: vscode.TextDocument): void => {
    if (document.languageId !== "email-markup") return;
    const key = document.uri.toString();
    const existing = timers.get(key);
    if (existing) clearTimeout(existing);
    timers.set(
      key,
      setTimeout(() => {
        timers.delete(key);
        void audit(document);
      }, 750),
    );
  };

  context.subscriptions.push(
    diagnostics,
    vscode.workspace.onDidOpenTextDocument(schedule),
    vscode.workspace.onDidChangeTextDocument(({ document }) => schedule(document)),
    vscode.workspace.onDidCloseTextDocument((document) => {
      const key = document.uri.toString();
      const timer = timers.get(key);
      if (timer) clearTimeout(timer);
      timers.delete(key);
      controllers.get(key)?.abort();
      controllers.delete(key);
      diagnostics.delete(document.uri);
    }),
  );
  for (const document of vscode.workspace.textDocuments) schedule(document);
}
