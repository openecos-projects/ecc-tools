# iEMIR static IR/EM inputs

iEMIR reads the loaded LEF/DEF database into its own power network, imports
instance power, builds and solves the network, and writes IR/EM reports. It does
not run iSTA or write its solved network back into the shared design database.

## Tcl interface

Load technology LEF, cell LEF and the design DEF before `init_emir`. The current
comparison driver also loads Liberty. For the shared resistor comparison flow:

```tcl
init_emir \
  -temp_directory_path ./emir_output \
  -ptpx_instance_power_file_path ./design.ptpx.tsv \
  -redhawk_res_network_file_path ./design.res_network \
  -ploc_file_path ./design.ploc \
  -redhawk_tech_file_path ./process.tech \
  -em_violation_threshold_percent 100.0 \
  -thread_number 4
run_emir
destroy_emir
```

| Input | Contract |
| --- | --- |
| `-ptpx_instance_power_file_path` | Required. TSV with the `# iEMIR_PTPX_INSTANCE_POWER_V1` marker and the seven columns below. |
| `-redhawk_res_network_file_path` | Imports wire/via resistances and connection metadata. The comparison driver requires this input. If omitted, the native LEF/DEF geometry path constructs resistances; that is a different extraction model. |
| `-ploc_file_path` | Explicit supply contacts. Without a PLOC file, the graph must have connected DEF PG IO sources. A graph without a source fails; top-layer nodes are never automatically made ideal sources. |
| `-redhawk_tech_file_path` | EM technology rules. |
| `-em_limit_file_path` | Optional rule overrides. EM assessment needs applicable rules; missing rules are not evidence of passing EM. |
| `-em_violation_threshold_percent` | Report threshold; defaults to 100.0. |
| `-temp_directory_path` | Per-run output directory, recreated during initialization. Use a separate directory for each run. |
| `-thread_number` | OpenMP thread count. |

The power TSV header is:

```text
instance_name	voltage_v	internal_power_w	switching_power_w	leakage_power_w	total_power_w	average_current_a
```

Fields use volts, watts and amperes. The reader checks component sums and
`voltage_v * average_current_a == total_power_w` within its numerical tolerance;
instance names are mapped to DEF instances. This is the normalized PTPX export,
not a raw commercial report or an iSTA binary power file.

Golden `.ir.full` and `.em.worst` files belong to the external comparison step;
iEMIR does not read them to solve voltages or currents. Its own reports are
written under `emir_reporter/`, with solver diagnostics under `ir_analyzer/`.

## Module boundary

The public interface owns file import. Graph construction uses `PowerNet` data
and its explicit-parasitic flag, without consulting the input filename. Imported
pin loads retain area-based distribution, and via reporting retains the original
physical location separately from its electrical endpoints.

The supported interface has no `-instance_power_file_path` alias, PG SPEF import
option or generated-source compatibility switch. It does not require a power
SPEF in the shared SPEF reader. iSTA activity configuration is outside this
module's change set.

PLOC/DEF supply-location background: `RedHawk_User_Manual_11.1.pdf`, PDF
pp. 604–605. The interface and module-boundary decisions above describe this
implementation; they are not requirements imposed by that manual.
