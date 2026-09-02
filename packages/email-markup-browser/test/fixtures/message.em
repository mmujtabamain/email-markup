<p>Hi @[business.name] team,</p>
@If[business.rating]
  <p>You have <b>@[business.rating]★ from @[business.review_count] reviews</b>.</p>
@Slot(else)
  <p>Your customers clearly rate you.</p>
@/Slot
@/If
@Notice
  <p>We build fast, modern sites for @[business.category] businesses.</p>
@/Notice
<p><a href="https://example.invalid/mockup">See a free homepage mockup</a></p>
