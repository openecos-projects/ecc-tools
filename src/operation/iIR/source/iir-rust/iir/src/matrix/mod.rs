pub mod ir_inst_power;
pub mod ir_rc;


use sprs::TriMatI;
use std::collections::HashMap;

use std::ffi::c_void;
use std::ffi::CString;
use std::os::raw::c_char;

use self::ir_inst_power::build_instance_current_vector;
use self::ir_inst_power::read_instance_pwr_csv;
use self::ir_inst_power::InstancePowerRecord;
use self::ir_rc::{RCData, SpefConnInput, SpefNetInput, SpefResCapInput};

/// RC matrix used for C interface.
#[repr(C)]
pub struct RustMatrix {
    // val at (row,col)
    data: f64,
    row: usize,
    col: usize,
}

/// RC vector used for C interface.
#[repr(C)]
#[allow(dead_code)]
pub struct RustVector {
    // val at position
    data: f64,
    index: usize,
}

/// Rust vec to C vec
#[repr(C)]
pub struct RustVec {
    data: *mut c_void,
    len: usize,
    cap: usize,
    type_size: usize,
}

/// IR instance power data for interaction with ipower.
#[repr(C)]
pub struct IRInstancePower {
    instance_name: *const c_char,
    nominal_voltage: f64,
    internal_power: f64,
    switch_power: f64,
    leakage_power: f64,
    total_power: f64,
}

/// IR PG node of the PG netlist.
#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct RustIRPGNode {
    coord: (i64, i64),
    layer_id: i32,
    node_id: i32,
    is_instance_pin: bool,
    is_bump: bool,
    is_via: bool,
    node_name: *const c_char,
}

/// IR PG edge of the PG netlist.
#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct RustIRPGEdge {
    node1: i64,
    node2: i64,
    resistance: f64,
}

/// IR PG netlist.
pub struct RustIRPGNetlist {
    nodes: Vec<RustIRPGNode>,
    edges: Vec<RustIRPGEdge>,
    net_name: *const c_char,
}

#[repr(C)]
pub struct RustSpefConn {
    name: *const c_char,
    is_external: bool,
}

#[repr(C)]
pub struct RustSpefResCap {
    node1: *const c_char,
    node2: *const c_char,
    value: f64,
}

#[repr(C)]
pub struct RustSpefNet {
    name: *const c_char,
    conns: RustVec,
    caps: RustVec,
    ress: RustVec,
}

/// One Net conductance matrix data.
#[allow(dead_code)]
struct IRNetConductanceData {
    net_name: String,
    conductance_matrix: Vec<RustMatrix>,
}

/// One net conductance matrix data for C.
#[repr(C)]
pub struct RustNetConductanceData {
    net_name: *const c_char,
    node_num: usize,
    g_matrix_vec: RustVec,
    ir_net_raw_ptr: *const c_void,
}

/// One net equation data.
#[repr(C)]
#[allow(dead_code)]
pub struct RustNetEquationData {
    net_name: *const c_char,
    g_matrix_vec: RustVec,
    j_vec: RustVec,
}

fn rust_vec_to_c_array<T>(vec: &Vec<T>) -> RustVec {
    RustVec {
        data: vec.as_ptr() as *mut c_void,
        len: vec.len(),
        cap: vec.capacity(),
        type_size: std::mem::size_of::<T>(),
    }
}

pub fn string_to_c_char(s: &str) -> *mut c_char {
    let cs = CString::new(s).unwrap();
    cs.into_raw()
}

/// Rust convert rc matrix to C matrix.
fn rust_convert_rc_matrix(rc_matrix: &TriMatI<f64, usize>) -> Vec<RustMatrix> {
    let mut rust_matrix_vec = vec![];

    for (val, (row, col)) in rc_matrix.triplet_iter() {
        rust_matrix_vec.push(RustMatrix { row, col, data: *val });
    }

    rust_matrix_vec
}

/// c str to rust string.
pub fn c_str_to_r_str(str: *const c_char) -> String {
    let c_str = unsafe { std::ffi::CStr::from_ptr(str) };
    let r_str = c_str.to_string_lossy().into_owned();
    r_str
}

/// The iterator for Rust hash map, temporarily write here.
#[repr(C)]
pub struct HashMapIterator {
    hashmap: *mut HashMap<usize, f64>,
    iter: std::collections::hash_map::Iter<'static, usize, f64>,
}

#[no_mangle]
pub extern "C" fn create_hashmap_iterator(hashmap: *mut HashMap<usize, f64>) -> *mut HashMapIterator {
    let iter = unsafe { (*hashmap).iter() };
    Box::into_raw(Box::new(HashMapIterator { hashmap, iter }))
}

#[no_mangle]
pub extern "C" fn hashmap_iterator_next(
    iterator: *mut HashMapIterator,
    out_key: *mut usize,
    out_value: *mut f64,
) -> bool {
    if let Some(iterator) = unsafe { iterator.as_mut() } {
        if let Some((key, value)) = iterator.iter.next() {
            unsafe {
                *out_key = *key;
                *out_value = *value;
            }
            true
        } else {
            false
        }
    } else {
        false
    }
}

#[no_mangle]
pub extern "C" fn destroy_hashmap_iterator(iterator: *mut HashMapIterator) {
    let _ = unsafe { Box::from_raw(iterator) };
}

/// create power ground node.
#[no_mangle]
pub extern "C" fn create_pg_node(c_pg_netlist: *mut c_void, c_pg_node: *const RustIRPGNode) -> *const c_void {
    let pg_node = unsafe { *c_pg_node };
    // println!("{:?}", pg_node);
    let mut pg_netlist = unsafe { Box::from_raw(c_pg_netlist as *mut RustIRPGNetlist) };
    pg_netlist.nodes.push(pg_node);
    Box::into_raw(pg_netlist) as *const c_void
}

/// create power ground edge.
#[no_mangle]
pub extern "C" fn create_pg_edge(c_pg_netlist: *const c_void, c_pg_edge: *const RustIRPGEdge) -> *const c_void {
    let pg_edge = unsafe { *c_pg_edge };
    // println!("{:?}", pg_edge);

    let mut pg_netlist = unsafe { Box::from_raw(c_pg_netlist as *mut RustIRPGNetlist) };
    pg_netlist.edges.push(pg_edge);
    Box::into_raw(pg_netlist) as *const c_void
}

/// create power ground netlist.
#[no_mangle]
pub extern "C" fn create_pg_netlist(c_power_net_name: *const c_char) -> *const c_void {
    let pg_netlist = RustIRPGNetlist { nodes: vec![], edges: vec![], net_name: c_power_net_name };
    let c_pg_netlist = Box::new(pg_netlist);
    Box::into_raw(c_pg_netlist) as *const c_void
}


/// estimate all pg netlist rc data.
#[no_mangle]
pub extern "C" fn create_rc_data(c_pg_netlist_ptr: *const c_void, len: usize) -> *const c_void {
    let mut rc_data = RCData::default();

    let pg_netlist_vec: Vec<Box<RustIRPGNetlist>> = unsafe {
            Vec::from_raw_parts(c_pg_netlist_ptr as *mut Box<RustIRPGNetlist>, len, len)
    };
    for pg_netlist in pg_netlist_vec.iter() {
        let one_rc_data = ir_rc::create_rc_data_from_topo(pg_netlist);
        rc_data.add_one_net_data(one_rc_data);
    }

    std::mem::forget(pg_netlist_vec);

    let mv_rc_data = Box::new(rc_data);
    Box::into_raw(mv_rc_data) as *const c_void
}

fn rust_spef_conns_to_inputs(c_conns: &RustVec) -> Vec<SpefConnInput> {
    let mut conns = Vec::with_capacity(c_conns.len);
    let c_conn_ptr = c_conns.data as *const RustSpefConn;
    for i in 0..c_conns.len {
        let c_conn = unsafe { &*c_conn_ptr.add(i) };
        conns.push(SpefConnInput {
            name: c_str_to_r_str(c_conn.name),
            is_external: c_conn.is_external,
        });
    }
    conns
}

fn rust_spef_res_caps_to_inputs(c_res_caps: &RustVec) -> Vec<SpefResCapInput> {
    let mut res_caps = Vec::with_capacity(c_res_caps.len);
    let c_res_cap_ptr = c_res_caps.data as *const RustSpefResCap;
    for i in 0..c_res_caps.len {
        let c_res_cap = unsafe { &*c_res_cap_ptr.add(i) };
        res_caps.push(SpefResCapInput {
            node1: c_str_to_r_str(c_res_cap.node1),
            node2: c_str_to_r_str(c_res_cap.node2),
            value: c_res_cap.value,
        });
    }
    res_caps
}

#[no_mangle]
pub extern "C" fn create_rc_data_from_spef(c_spef_nets: RustVec) -> *const c_void {
    let mut nets = Vec::with_capacity(c_spef_nets.len);
    let c_net_ptr = c_spef_nets.data as *const RustSpefNet;
    for i in 0..c_spef_nets.len {
        let c_net = unsafe { &*c_net_ptr.add(i) };
        nets.push(SpefNetInput {
            name: c_str_to_r_str(c_net.name),
            conns: rust_spef_conns_to_inputs(&c_net.conns),
            caps: rust_spef_res_caps_to_inputs(&c_net.caps),
            ress: rust_spef_res_caps_to_inputs(&c_net.ress),
        });
    }

    let mv_rc_data = Box::new(ir_rc::create_rc_data_from_spef_nets(&nets));
    Box::into_raw(mv_rc_data) as *const c_void
}

#[no_mangle]
pub extern "C" fn get_sum_resistance(c_rc_data: *const c_void,
    c_net_name: *const c_char) -> f64 {
    let rc_data = unsafe { &*(c_rc_data as *const RCData) };

    let one_net_name = c_str_to_r_str(c_net_name);
    if !(rc_data.is_contain_net_data(&one_net_name)) {
        panic!("The net {} is not exist.", one_net_name);
    }

    let one_net_rc_data = rc_data.get_one_net_data(&one_net_name);
    let resistances = one_net_rc_data.get_resistances();

    let sum_resistance = resistances.iter().map(|x| x.resistance).sum::<f64>();
    sum_resistance
}

#[no_mangle]
pub extern "C" fn build_one_net_conductance_matrix_data(
    c_rc_data: *const c_void,
    c_net_name: *const c_char,
) -> RustNetConductanceData {
    let rc_data = unsafe { &*(c_rc_data as *const RCData) };

    let one_net_name = c_str_to_r_str(c_net_name);
    if !(rc_data.is_contain_net_data(&one_net_name)) {
        panic!("The net {} is not exist.", one_net_name);
    }
    
    let one_net_rc_data = rc_data.get_one_net_data(&one_net_name);

    let conductance_matrix_triplet = ir_rc::build_conductance_matrix(one_net_rc_data);
    let rust_matrix = rust_convert_rc_matrix(&conductance_matrix_triplet);
    let rust_matrix_vec = rust_vec_to_c_array(&rust_matrix);

    let one_net_conductance_data =
        Box::from(IRNetConductanceData { net_name: one_net_name, conductance_matrix: rust_matrix });

    // Need free the memory after use.
    let ir_net_raw_ptr = Box::into_raw(one_net_conductance_data);

    // The C image of rust data.
    
    RustNetConductanceData {
        net_name: c_net_name,
        node_num: conductance_matrix_triplet.shape().0,
        g_matrix_vec: rust_matrix_vec,
        ir_net_raw_ptr: ir_net_raw_ptr as *const c_void,
    }
}

/// Read instance power csv file for C.
#[no_mangle]
pub extern "C" fn read_inst_pwr_csv(file_path: *const c_char) -> *mut c_void {
    let inst_power_path_cstr = unsafe { std::ffi::CStr::from_ptr(file_path) };
    let inst_power_path = inst_power_path_cstr.to_str().unwrap();

    let records = read_instance_pwr_csv(inst_power_path).expect("error reading instance power csv file");
    Box::into_raw(Box::new(records)) as *mut c_void
}

#[no_mangle]
pub extern "C" fn set_instance_power_data(c_instance_power_data: RustVec) -> *mut c_void {
    let mut records = Vec::new();
    for i in 0..c_instance_power_data.len {
        let instance_power_data_ptr = c_instance_power_data.data as *const IRInstancePower;
        let instance_power_data = unsafe { &*instance_power_data_ptr.add(i) };
        let instance_name = c_str_to_r_str(instance_power_data.instance_name);

        let instance_power_record = InstancePowerRecord {
            instance_name,
            nominal_voltage: instance_power_data.nominal_voltage,
            internal_power: instance_power_data.internal_power,
            switch_power: instance_power_data.switch_power,
            leakage_power: instance_power_data.leakage_power,
            total_power: instance_power_data.total_power,
        };

        records.push(instance_power_record);
    }

    Box::into_raw(Box::new(records)) as *mut c_void
}

/// Build one net instance current vector.
#[no_mangle]
pub extern "C" fn build_one_net_instance_current_vector(
    c_instance_power_data: *const c_void,
    c_rc_data: *const c_void,
    c_net_name: *const c_char,
) -> *mut c_void {
    let inst_power_data = unsafe { &*(c_instance_power_data as *const Vec<InstancePowerRecord>) };

    let rc_data = unsafe { &*(c_rc_data as *const RCData) };

    let one_net_name = c_str_to_r_str(c_net_name);
    let one_net_rc_data = rc_data.get_one_net_data(&one_net_name);

    let instance_current_data = build_instance_current_vector(inst_power_data, one_net_rc_data).unwrap();

    Box::into_raw(Box::new(instance_current_data)) as *mut c_void
}

/// Get one net bump node id.
#[no_mangle]
pub extern "C" fn get_bump_node_ids(c_rc_data: *const c_void, c_net_name: *const c_char) -> RustVec {
    let rc_data = unsafe { &*(c_rc_data as *const RCData) };
    let one_net_name = c_str_to_r_str(c_net_name);
    let one_net_rc_data = rc_data.get_one_net_data(&one_net_name);

    let nodes = one_net_rc_data.get_nodes();
    let mut bump_node_ids = Box::new(Vec::new());
    for node in nodes.borrow().iter() {
        if node.get_is_bump() {
            let node_name = node.get_node_name();
            let node_id = one_net_rc_data.get_node_id(node_name).unwrap();
            bump_node_ids.push(node_id);
        }
    }

    let rust_bump_node_id_vec = rust_vec_to_c_array(&bump_node_ids);
    let _ = Box::into_raw(bump_node_ids);
    rust_bump_node_id_vec
}

#[no_mangle]
pub extern "C" fn get_instance_node_ids(c_rc_data: *const c_void, c_net_name: *const c_char) -> RustVec {
    let rc_data = unsafe { &*(c_rc_data as *const RCData) };
    let one_net_name = c_str_to_r_str(c_net_name);
    let one_net_rc_data = rc_data.get_one_net_data(&one_net_name);

    let nodes = one_net_rc_data.get_nodes();
    let mut instance_node_ids = Box::new(Vec::new());
    for node in nodes.borrow().iter() {
        if node.get_is_inst_pin() {
            let node_name = node.get_node_name();
            let node_id = one_net_rc_data.get_node_id(node_name).unwrap();
            instance_node_ids.push(node_id);
        }
    }

    let rust_instance_node_id_vec = rust_vec_to_c_array(&instance_node_ids);
    let _ = Box::into_raw(instance_node_ids);
    rust_instance_node_id_vec
}

#[no_mangle]
pub extern "C" fn get_instance_name(
    c_rc_data: *const c_void,
    c_net_name: *const c_char,
    node_id: usize,
) -> *const c_char {
    let rc_data = unsafe { &*(c_rc_data as *const RCData) };
    let one_net_name = c_str_to_r_str(c_net_name);
    let one_net_rc_data = rc_data.get_one_net_data(&one_net_name);

    let instance_name = one_net_rc_data.get_node_name(node_id);
    
    (string_to_c_char(instance_name.unwrap())) as _
}

#[no_mangle]
pub extern "C" fn build_matrices_from_rc_data(
    c_inst_power_path: *const c_char,
    c_rc_data: *const c_void,
) {
    let inst_power_path = c_str_to_r_str(c_inst_power_path);
    let rc_data = unsafe { &*(c_rc_data as *const RCData) };

    let instance_power_data =
        ir_inst_power::read_instance_pwr_csv(&inst_power_path).expect("error reading instance power csv file");

    for (net_name, one_net_data) in rc_data.get_nets_data() {
        log::info!("construct power net {} matrix start", net_name);

        let conductance_matrix_triplet = ir_rc::build_conductance_matrix(one_net_data);
        let _rust_matrix = rust_convert_rc_matrix(&conductance_matrix_triplet);

        let current_vector_result = ir_inst_power::build_instance_current_vector(&instance_power_data, one_net_data);
        if current_vector_result.is_err() {
            panic!("current vector is none");
        }
        log::info!("construct power net {} matrix finish", net_name);
    }
}

#[cfg(test)]
mod tests {
    use super::ir_rc;
    use crate::matrix::{
        create_rc_data_from_spef, rust_convert_rc_matrix, rust_vec_to_c_array, string_to_c_char,
        RustNetEquationData, RustSpefConn, RustSpefNet, RustSpefResCap, RustVec, RustVector,
    };
    use std::ffi::{c_void, CString};

    fn rust_vec_from_mut_vec<T>(vec: &mut Vec<T>) -> RustVec {
        RustVec {
            data: vec.as_mut_ptr() as *mut c_void,
            len: vec.len(),
            cap: vec.capacity(),
            type_size: std::mem::size_of::<T>(),
        }
    }

    #[test]
    fn create_rc_data_from_spef_copies_c_abi_inputs() {
        let rc_data_ptr = {
            let net_name = CString::new("VDD").unwrap();
            let bump_name = CString::new("VDD").unwrap();
            let inst_name = CString::new("U1/VDD").unwrap();
            let empty_name = CString::new("").unwrap();
            let cap_node_name = CString::new("VDD:3").unwrap();

            let mut conns = vec![
                RustSpefConn { name: bump_name.as_ptr(), is_external: true },
                RustSpefConn { name: inst_name.as_ptr(), is_external: false },
            ];
            let mut caps = vec![RustSpefResCap {
                node1: cap_node_name.as_ptr(),
                node2: empty_name.as_ptr(),
                value: 0.5,
            }];
            let mut ress = vec![RustSpefResCap {
                node1: bump_name.as_ptr(),
                node2: inst_name.as_ptr(),
                value: 2.0,
            }];

            let mut nets = vec![RustSpefNet {
                name: net_name.as_ptr(),
                conns: rust_vec_from_mut_vec(&mut conns),
                caps: rust_vec_from_mut_vec(&mut caps),
                ress: rust_vec_from_mut_vec(&mut ress),
            }];

            create_rc_data_from_spef(rust_vec_from_mut_vec(&mut nets))
        };

        let rc_data = unsafe { Box::from_raw(rc_data_ptr as *mut ir_rc::RCData) };
        let one_net = rc_data.get_one_net_data("VDD");
        let bump = "VDD".to_string();
        let inst = "U1/VDD".to_string();
        let cap_node = "VDD:3".to_string();

        let bump_id = one_net.get_node_id(&bump).unwrap();
        let inst_id = one_net.get_node_id(&inst).unwrap();
        let cap_id = one_net.get_node_id(&cap_node).unwrap();

        let nodes = one_net.get_nodes().borrow();
        assert!(nodes[bump_id].get_is_bump());
        assert!(nodes[inst_id].get_is_inst_pin());
        assert_eq!(nodes[cap_id].get_cap(), 0.5);
        assert_eq!(one_net.get_resistances()[0].resistance, 2.0 * ir_rc::RC_COEFF);
    }

    #[test]
    fn test_build_matrix() {
        let nets = vec![ir_rc::SpefNetInput {
            name: "VDD".to_string(),
            conns: vec![
                ir_rc::SpefConnInput { name: "VDD".to_string(), is_external: true },
                ir_rc::SpefConnInput { name: "U1/VDD".to_string(), is_external: false },
            ],
            caps: vec![],
            ress: vec![ir_rc::SpefResCapInput {
                node1: "VDD".to_string(),
                node2: "U1/VDD".to_string(),
                value: 2.0,
            }],
        }];

        let rc_data = ir_rc::create_rc_data_from_spef_nets(&nets);

        for (net_name, one_net_data) in rc_data.get_nets_data() {
            log::info!("construct power net {} matrix start", net_name);

            // Build rc matrix
            let conductance_matrix_triplet = ir_rc::build_conductance_matrix(one_net_data);
            let rust_matrix = rust_convert_rc_matrix(&conductance_matrix_triplet);

            let current_vec: Vec<RustVector> = Vec::new();

            let rust_matrix_vec = rust_vec_to_c_array(&rust_matrix);
            let rust_current_vec = rust_vec_to_c_array(&current_vec);

            let mut net_matrix_data: Vec<RustNetEquationData> = Vec::new();
            let one_net_rc_net = RustNetEquationData {
                net_name: string_to_c_char(net_name),
                g_matrix_vec: rust_matrix_vec,
                j_vec: rust_current_vec,
            };
            net_matrix_data.push(one_net_rc_net);
        }
    }
}
