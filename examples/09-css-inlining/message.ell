@DefineStyle(name: "release-card")
  background: #eff6ff;
  border: 1px solid #93c5fd;
  padding: 20px;
@/DefineStyle

@DefineComponent(name: "ReleaseCard")
  @Props
    version: string
  @/Props
  @Slots
    default: required
  @/Slots
  @Template
    <section>
      <div style="color:#1d4ed8;font-size:18px;font-weight:700;">Version @{version}</div>
      <div style="margin-top:8px;">@Slot(default);</div>
    </section>
  @/Template
@/DefineComponent

@Heading Product update @/Heading

@ReleaseCard(version: release.version, style: "release-card")
  @{release.summary}
@/ReleaseCard
