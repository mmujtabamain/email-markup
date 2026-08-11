@* Email Markup 1 standard library. All fifteen built-ins are ordinary Email Markup components. *@

@DefineComponent(name: "Paragraph")
  @Slots
    default: required
  @/Slots
  @Template
    <p style="margin:0 0 18px;">
      @Slot(default);
    </p>
  @/Template
@/DefineComponent

@DefineComponent(name: "Heading")
  @Slots
    default: required
  @/Slots
  @Template
    <h2 style="margin:26px 0 12px;font-size:19px;line-height:1.35;font-weight:700;">
      @Slot(default);
    </h2>
  @/Template
@/DefineComponent

@DefineComponent(name: "Bullets")
  @Slots
    default: required
  @/Slots
  @Template
    <ul style="margin:0 0 20px;padding-left:22px;">
      @Slot(default);
    </ul>
  @/Template
@/DefineComponent

@DefineComponent(name: "Numbered")
  @Slots
    default: required
  @/Slots
  @Template
    <ol style="margin:0 0 20px;padding-left:22px;">
      @Slot(default);
    </ol>
  @/Template
@/DefineComponent

@DefineComponent(name: "Item")
  @Slots
    default: required
  @/Slots
  @Template
    <li style="margin:9px 0;">
      @Slot(default);
    </li>
  @/Template
@/DefineComponent

@DefineComponent(name: "Callout")
  @Props
    background: color = "#f3f0ff"
    accent: color = token.accent
  @/Props
  @Slots
    default: required
  @/Slots
  @Template
    <div style="margin:6px 0 22px;padding:18px 20px;background:@{background};border-left:4px solid @{accent};border-radius:10px;font-size:15px;line-height:1.65;">
      @Slot(default);
    </div>
  @/Template
@/DefineComponent

@DefineComponent(name: "Quote")
  @Props
    attribution?: string
  @/Props
  @Slots
    default: required
  @/Slots
  @Template
    <blockquote style="margin:6px 0 22px;padding:4px 0 4px 18px;border-left:3px solid @{token.border};font-size:15px;line-height:1.65;font-style:italic;">
      @Slot(default);
      @If(attribution)
        <div style="margin-top:8px;font-size:13px;color:@{token.muted};">
          &mdash; @{attribution}
        </div>
      @/If
    </blockquote>
  @/Template
@/DefineComponent

@DefineComponent(name: "Button")
  @Props
    url: url
  @/Props
  @Slots
    default: required
  @/Slots
  @Template
    <div style="margin:8px 0 28px;">
      <a href="@{url}" style="display:inline-block;background:@{token.accent};color:#ffffff;text-decoration:none;padding:14px 28px;border-radius:11px;font-weight:700;">
      @Slot(default);
      </a>
    </div>
  @/Template
@/DefineComponent

@DefineComponent(name: "Image")
  @Props
    src: url
    alt: string = ""
    href?: url
    embed: bool = true
  @/Props
  @Template
    <div style="margin:6px 0 22px;">
      @If(href)
        <a href="@{href}" style="text-decoration:none;">
      @/If
      <img data-email-markup-embed="@{embed}" src="@{src}" alt="@{alt}" style="display:block;border:0;width:100%;max-width:528px;height:auto;border-radius:10px;" />
      @If(href)
        </a>
      @/If
    </div>
  @/Template
@/DefineComponent

@DefineComponent(name: "Divider")
  @Template
    <div style="margin:24px 0;border-top:1px solid @{token.hairline};font-size:0;line-height:0;">
      &nbsp;
    </div>
  @/Template
@/DefineComponent

@DefineComponent(name: "Spacer")
  @Props
    height: int(4..120) = 16
  @/Props
  @Template
    <div style="height:@{height}px;font-size:0;line-height:0;">
      &nbsp;
    </div>
  @/Template
@/DefineComponent

@DefineComponent(name: "Panel")
  @Props
    background: color = "#faf9fd"
    accent?: color
  @/Props
  @Slots
    default: required
  @/Slots
  @Template
    <div style="margin:6px 0 22px;padding:20px 22px;background:@{background};
      @If(accent)
        border-left:4px solid @{accent};
      @/If
      border-radius:12px;">
      @Slot(default);
    </div>
  @/Template
@/DefineComponent

@DefineComponent(name: "Columns")
  @Props
    gap: int(0..80) = 20
  @/Props
  @Slots
    left: required
    right: required
  @/Slots
  @Template
    <table role="presentation" cellpadding="0" cellspacing="0" border="0" style="width:100%;margin:6px 0 22px;border-collapse:collapse;">
      <tr>
        <td class="stack-column" style="vertical-align:top;width:50%;padding:0;">
          <div style="max-width:calc(50% - @{gap / 2}px);padding-right:@{gap / 2}px;">
            @Slot(left);
          </div>
        </td>
        <td class="stack-column" style="vertical-align:top;width:50%;padding:0;">
          <div style="max-width:calc(50% - @{gap / 2}px);padding-left:@{gap / 2}px;">
            @Slot(right);
          </div>
        </td>
      </tr>
    </table>
  @/Template
@/DefineComponent

@DefineComponent(name: "Unsubscribe")
  @Props
    url: url = unsubscribe_url
    text: string = "You're receiving this because it may be relevant to @{business.name}. Not interested?"
    label: string = "Unsubscribe"
    color: color = token.accent
  @/Props
  @Template
    <div style="margin:10px 0 0;">
      @{text} <a href="@{url}" style="color:@{color};font-weight:600;">@{label}</a> &mdash; one click, no hard feelings.
    </div>
  @/Template
@/DefineComponent

@DefineComponent(name: "Shell")
  @Slots
    default: required
  @/Slots
  @Template
    <!doctype html>
    <html>
      <head>
        <meta charset="utf-8">
      </head>
      <body>
        @Slot(default);
      </body>
    </html>
  @/Template
@/DefineComponent
