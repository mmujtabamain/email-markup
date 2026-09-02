@DefineComponent(name: "Signoff")
  @Props
    author: string
  @/Props
  @Template
    @Paragraph — @{author} @/Paragraph
  @/Template
@/DefineComponent

@Heading Hello @{business.name} @/Heading
@Paragraph You are rated @{business.rating}. @/Paragraph
@Signoff(author: "Sam");
