@Engine("${EMAIL_MARKUP_LIB}/engines/django.emt");

<h1>Hello @[recipient.name]</h1>
@If[recipient.active]
  <ul>
    @For[collection: recipient.items, binding: item, limit: 2]
      <li>@[item.label]</li>
    @/For
  </ul>
@Slot(else)
  <p>This account is inactive.</p>
@/Slot
@/If
