# Architecture Decision Records

Short, durable records of decisions that are expensive to reverse or whose
reasoning is not obvious from the code. Each one states the context, the
decision, and the consequences — including what was rejected and why, so a
future reader does not re-litigate a settled question from scratch.

This project already keeps *lessons* in `.clinerules/` (how to build, test and
not get bitten). ADRs are for *choices*: which way a design went and what it
cost. When a decision in here is superseded, add a new ADR that says so rather
than editing history — the old record is how you learn why the first answer
stopped being right.

| ADR | Title | Status |
|-----|-------|--------|
| [0001](0001-config-partition-and-flasher-configurator.md) | Runtime config partition + web-flasher configurator | Proposed |
| [0002](0002-serial-schema-versioning.md) | Version the serial/JSON wire format | Accepted |
