@DefineComponent(name: "Metric")
  @Props
    label: string
    value: string
    accent: color = "#2563eb"
  @/Props
  @Template
    <div style="margin:0 0 12px;padding:16px;border-left:4px solid @{accent};background:#f8fafc;">
      <div style="color:#62666d;font-size:12px;">@{label}</div>
      <div style="font-size:24px;font-weight:700;">@{value}</div>
    </div>
  @/Template
@/DefineComponent

@Heading Campaign report @/Heading

@Metric(label: "Open rate", value: report.open_rate);
@Metric(label: "Click rate", value: report.click_rate, accent: "#059669");
