# Tool Tcl commands

This directory contains Tool extensions used by source-style constraint
scripts. These are not built-in Tcl language commands. Tcl_Init installs language
commands such as `set`, `if`, `foreach`, `proc`, and `source`.

Confirmed extensions are grouped here, including their unregistered placeholder
files:

- Collection operations (`remove_from_collection`, `foreach_in_collection`, etc.).
- Extra object queries (`get_generated_clocks`, name and attribute queries,
  fanin/fanout traversal, etc.).
- Constraint editing (`remove_*`, `unset_*`, `delete_*`, and `reset_path`).
- Script import/export command placeholders (`read_sdc`, `write_sdc`).

Implemented declarations live in `ToolCommands.hpp`, `CollectionCommands.hpp`,
`GetFan.hpp`, and `GetName.hpp`. Registration remains in `SdcCommandRegistry.cpp`.
Moving a placeholder here does not implement or register it. The main-program
`output_sdc` command remains outside this internal interpreter.

Standard object access (`get_cells`, `get_pins`, `get_clocks`, etc.) remains in
`object/`; constraint commands remain in their existing functional directories.
Shared interpreter/query infrastructure also remains in its original directory.

Classification follows the supported SDC command subset and separately identified
source-script extensions. A command's absence from a reference list alone is not
sufficient evidence to reclassify it. Shared infrastructure and commands with
unresolved compatibility details retain their existing locations.

Directory placement is not a complete command/option acceptance whitelist for
`read_sdc` and does not change `output_sdc` compatibility guarantees.
