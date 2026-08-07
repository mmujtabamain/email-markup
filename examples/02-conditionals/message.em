@Heading Your trial status @/Heading

@If(account.trial_days > 0)
  @Paragraph
    You have @{account.trial_days} days left to explore the workspace.
  @/Paragraph
  @Button(url: billing_url) Choose a plan @/Button
@Else
  @Callout(background: "#fef2f2", accent: "#dc2626")
    Your trial has ended. Choose a plan to restore access.
  @/Callout
@/If
