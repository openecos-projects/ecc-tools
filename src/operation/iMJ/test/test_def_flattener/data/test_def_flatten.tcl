set data_directory_path [file dirname [file normalize [info script]]]
set output_def_path [file join [pwd] "test_imj_def_flattener_tcl.def"]

tech_lef_init -path "${data_directory_path}/tech.lef"
lef_init -path "${data_directory_path}/cell.lef"
def_init -path "${data_directory_path}/top.def.in"
set hierarchy_def_path_list "\
    ${data_directory_path}/pass.def.in \
    ${data_directory_path}/mid.def.in \
    ${data_directory_path}/child.def.in"
flatten_def -hierarchy "${hierarchy_def_path_list}"
def_save -path "${output_def_path}"
