# Ordinary Via Rule Storage

`via_rule/` represents a non-GENERATE LEF `VIARULE` as one entity:

```text
TechViaRule entity
  TechViaRule
  TechViaRuleLowerLayer
  TechViaRuleUpperLayer
  TechViaRuleCandidates { vector<TechViaMasterId> }
  TechViaRuleProperties { vector<TechViaRuleProperty> }
```

The two routing-Layer clauses have fixed cardinality and role, so they are two
different Component types on the same entity. Candidate fixed VIAs are an
ordered variable-length list but have no independent lifecycle; their IDs are
stored directly in the `TechViaRuleCandidates` component.

The storage rejects generated VIA candidates. A generated VIA is selected via
`TechViaRuleGenerate`, while an ordinary `VIARULE` selects a concrete fixed
VIA.
