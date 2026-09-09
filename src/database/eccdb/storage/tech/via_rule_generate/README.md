# Via Rule Generate Storage

`via_rule_generate/` represents one LEF `VIARULE ... GENERATE` template as
one entity in the shared `TechRegistry`.

```text
TechViaRuleGenerate entity
  TechViaRuleGenerate
  TechViaRuleGenerateBottomLayer
  TechViaRuleGenerateCutLayer
  TechViaRuleGenerateTopLayer
```

The three clauses are mandatory and have fixed roles, so they do not receive
child entity IDs, owner fields, or an ordered child vector. A generated
ViaMaster references this entity through `TechViaRuleGenerateId`.

`ViaRuleGenerateStorage` owns no registry, string pool, or hash index. It uses
the shared registry owned by `TechStore`. Name and Layer queries scan the
corresponding EnTT component view; the technology Rule count is small enough
that this is the persistent-data-first default.
