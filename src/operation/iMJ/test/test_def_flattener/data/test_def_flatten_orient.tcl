set data_directory_path [file dirname [file normalize [info script]]]

tech_lef_init -path ${data_directory_path}/tech.lef
lef_init -path ${data_directory_path}/cell.lef
def_init -path ${data_directory_path}/top_orient.def.in

set hierarchy_def_path_list "\
    ${data_directory_path}/mid.def.in \
    ${data_directory_path}/child.def.in"
flatten_def -hierarchy "${hierarchy_def_path_list}"
def_save -path test_imj_def_flattener_orient.def
