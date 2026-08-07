@Include("components/notice.em");

@Heading Service notice @/Heading

@Notice(title: notice.title)
  The maintenance window is @{notice.window}.
@/Notice
