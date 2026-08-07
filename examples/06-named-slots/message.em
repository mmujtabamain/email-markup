@DefineComponent(name: "DetailPair")
  @Slots
    left: required
    right: required
  @/Slots
  @Template
    <table role="presentation" cellpadding="0" cellspacing="0" border="0" style="width:100%;margin:0 0 20px;border-collapse:collapse;">
      <tr>
        <td style="width:50%;padding:16px;background:#f8fafc;vertical-align:top;">@Slot(left);</td>
        <td style="width:50%;padding:16px;background:#eef2ff;vertical-align:top;">@Slot(right);</td>
      </tr>
    </table>
  @/Template
@/DefineComponent

@Heading Upcoming workshop @/Heading

@DetailPair
  @Slot(left) <strong>Date</strong><br>@{event.date} @/Slot
  @Slot(right) <strong>Time</strong><br>@{event.time} @/Slot
@/DetailPair
