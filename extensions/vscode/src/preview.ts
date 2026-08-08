export function protectRemoteImages(html: string): string {
  return html.replace(
    /(<img\b[^>]*?)\ssrc=(['"])(https?:\/\/.*?)\2/gi,
    (_match, before: string, quote: string, source: string) =>
      `${before} data-email-markup-remote-src=${quote}${source}${quote} src=${quote}data:image/gif;base64,R0lGODlhAQABAAD/ACwAAAAAAQABAAACADs=${quote}`,
  );
}

export function restoreRemoteImages(html: string): string {
  return html.replace(
    /(<img\b[^>]*?)\sdata-email-markup-remote-src=(['"])(https?:\/\/.*?)\2\s+src=(['"]).*?\4/gi,
    (_match, before: string, quote: string, source: string) =>
      `${before} src=${quote}${source}${quote}`,
  );
}

export function previewDocument(
  html: string,
  allowRemoteImages: boolean,
): string {
  const content = allowRemoteImages
    ? restoreRemoteImages(html)
    : protectRemoteImages(html);
  const imagePolicy = allowRemoteImages
    ? "img-src https: http: data:;"
    : "img-src data:;";
  return `<!doctype html><html><head><meta charset="utf-8"><meta http-equiv="Content-Security-Policy" content="default-src 'none'; ${imagePolicy} style-src 'unsafe-inline';"><meta name="viewport" content="width=device-width, initial-scale=1"></head><body>${content}</body></html>`;
}
