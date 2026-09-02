@Include("project.em");

<!doctype html>
<html>
  <head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
  </head>
  <body style="margin:0;padding:0;background:@{token.surface};">
    <main style="padding:32px;color:@{token.ink};font-size:15px;line-height:1.65;">
      @Slot(default);
    </main>
    <footer style="border-top:1px solid @{token.hairline};font-size:12px;">
      <a href="@[message.unsubscribe_url]" style="color:@{token.accent};">Unsubscribe</a>
    </footer>
  </body>
</html>
