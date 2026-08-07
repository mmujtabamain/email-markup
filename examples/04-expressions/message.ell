@Heading Invoice summary @/Heading

@Paragraph
  Total: $@{invoice.subtotal + invoice.tax}<br>
  Paid: $@{invoice.amount_paid}<br>
  Balance: $@{invoice.subtotal + invoice.tax - invoice.amount_paid}
@/Paragraph

@If(invoice.amount_paid < invoice.subtotal + invoice.tax)
  @Button(url: payment_url) Pay remaining balance @/Button
@/If
