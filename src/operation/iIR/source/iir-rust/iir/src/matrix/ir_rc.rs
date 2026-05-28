use log;
use sprs::TriMat;
use sprs::TriMatI;
use std::cell::RefCell;
use std::collections::HashMap;
use std::io::Write;

use super::c_str_to_r_str;
use super::RustIRPGNetlist;

pub const POWER_INNER_RESISTANCE: f64 = 1e-3;
pub const RC_COEFF: f64 = 3.0; // RC coefficient, used to scale the resistance value from SPEF.

pub struct SpefConnInput {
    pub name: String,
    pub is_external: bool,
}

pub struct SpefResCapInput {
    pub node1: String,
    pub node2: String,
    pub value: f64,
}

pub struct SpefNetInput {
    pub name: String,
    pub conns: Vec<SpefConnInput>,
    pub caps: Vec<SpefResCapInput>,
    pub ress: Vec<SpefResCapInput>,
}

/// RC node of the spef network.
pub struct RCNode {
    name: String,
    cap: f64,          // The node capacitance
    is_bump: bool,     // Whether power bump.
    is_inst_pin: bool, // Whether instance pin.
}

impl RCNode {
    pub fn new(name: String) -> RCNode {
        RCNode { name, cap: 0.0, is_bump: false, is_inst_pin: false }
    }

    pub fn get_name(&self) -> &str {
        &self.name
    }

    pub fn get_node_name(&self) -> &String {
        &self.name
    }
    #[allow(dead_code)]
    pub fn get_cap(&self) -> f64 {
        self.cap
    }
    pub fn set_cap(&mut self, cap: f64) {
        self.cap = cap;
    }

    pub fn set_is_bump(&mut self) {
        self.is_bump = true;
    }
    pub fn get_is_bump(&self) -> bool {
        self.is_bump
    }

    pub fn set_is_inst_pin(&mut self) {
        self.is_inst_pin = true;
    }
    pub fn get_is_inst_pin(&self) -> bool {
        self.is_inst_pin
    }
}

/// RC resistance.
#[derive(Default)]
pub struct RCResistance {
    pub from_node_id: usize,
    pub to_node_id: usize,
    pub resistance: f64,
}

/// One power net rc data.
#[derive(Default)]
pub struct RCOneNetData {
    name: String,
    node_name_to_node_id: HashMap<String, usize>,
    node_id_to_node_name: HashMap<usize, String>,
    nodes: RefCell<Vec<RCNode>>,
    resistances: Vec<RCResistance>,
}

impl RCOneNetData {
    pub fn new(name: String) -> RCOneNetData {
        RCOneNetData {
            name,
            node_name_to_node_id: HashMap::new(),
            node_id_to_node_name: HashMap::new(),
            nodes: RefCell::new(Vec::new()),
            resistances: Vec::new(),
        }
    }
    pub fn get_name(&self) -> &str {
        &self.name
    }
    pub fn add_node(&mut self, one_node: RCNode) -> usize {
        let node_id = self.nodes.borrow().len();
        self.node_name_to_node_id.insert(String::from(one_node.get_name()), node_id);
        self.node_id_to_node_name.insert(node_id, String::from(one_node.get_name()));
        self.nodes.borrow_mut().push(one_node);
        node_id
    }

    pub fn get_nodes(&self) -> &RefCell<Vec<RCNode>> {
        &self.nodes
    }
    pub fn get_node_id(&self, node_name: &String) -> Option<usize> {
        self.node_name_to_node_id.get(node_name).cloned()
    }
    pub fn get_node_name(&self, node_id: usize) -> Option<&String> {
        self.node_id_to_node_name.get(&node_id)
    }

    pub fn set_node_cap(&self, node_id: usize, cap_value: f64) {
        if let Some(node) = self.nodes.borrow_mut().get_mut(node_id) {
            node.set_cap(cap_value);
        }
    }

    pub fn add_resistance(&mut self, one_resistance: RCResistance) {
        self.resistances.push(one_resistance);
    }

    pub fn get_resistances(&self) -> &Vec<RCResistance> {
        &self.resistances
    }

    pub fn print_to_yaml(&self, yaml_file_path: &str) {
        let mut file = std::fs::File::create(yaml_file_path).unwrap();
        for (index, node) in self.nodes.borrow().iter().enumerate() {
            let node_name = node.get_name();
            let node_id = format!("node_{}", index);

            writeln!(file, "{}:\n  {}", node_id, node_name).unwrap();
        }

        for (index, resistance) in self.resistances.iter().enumerate() {
            let edge_id = format!("edge_{}", index);

            writeln!(file, "{}:", edge_id).unwrap();
            writeln!(file, "  node1: {}", resistance.from_node_id).unwrap();
            writeln!(file, "  node2: {}", resistance.to_node_id).unwrap();

            writeln!(file, "  resistance: {}", resistance.resistance).unwrap();
        }
    }
}

/// All power net rc data.
#[derive(Default)]
pub struct RCData {
    rc_nets_data: HashMap<String, RCOneNetData>,
}

impl RCData {
    pub fn add_one_net_data(&mut self, one_net_data: RCOneNetData) {
        self.rc_nets_data.insert(String::from(one_net_data.get_name()), one_net_data);
    }
    pub fn get_nets_data(&self) -> &HashMap<String, RCOneNetData> {
        &self.rc_nets_data
    }

    pub fn get_one_net_data(&self, name: &str) -> &RCOneNetData {
        self.rc_nets_data.get(name).unwrap()
    }
    pub fn is_contain_net_data(&self, name: &str) -> bool {
        self.rc_nets_data.contains_key(name)
    }
}

pub fn create_rc_data_from_spef_nets(nets: &[SpefNetInput]) -> RCData {
    let mut rc_data = RCData::default();

    for spef_net in nets {
        let mut one_net_data = RCOneNetData::new(spef_net.name.clone());

        for conn in &spef_net.conns {
            let mut rc_node = RCNode::new(conn.name.clone());
            if conn.is_external {
                rc_node.set_is_bump();
            } else {
                rc_node.set_is_inst_pin();
            }
            one_net_data.add_node(rc_node);
        }

        for cap in &spef_net.caps {
            if !cap.node2.is_empty() {
                continue;
            }

            let node_id = one_net_data.get_node_id(&cap.node1);
            if let Some(node_id) = node_id {
                one_net_data.set_node_cap(node_id, cap.value);
            } else {
                let mut rc_node = RCNode::new(cap.node1.clone());
                rc_node.set_cap(cap.value);
                one_net_data.add_node(rc_node);
            }
        }

        for resistance in &spef_net.ress {
            let node1_id = if let Some(node_id) = one_net_data.get_node_id(&resistance.node1) {
                node_id
            } else {
                let rc_node = RCNode::new(resistance.node1.clone());
                one_net_data.add_node(rc_node)
            };

            let node2_id = if let Some(node_id) = one_net_data.get_node_id(&resistance.node2) {
                node_id
            } else {
                let rc_node = RCNode::new(resistance.node2.clone());
                one_net_data.add_node(rc_node)
            };

            let mut rc_resistance = RCResistance::default();
            rc_resistance.from_node_id = node1_id;
            rc_resistance.to_node_id = node2_id;
            rc_resistance.resistance = resistance.value * RC_COEFF;
            one_net_data.add_resistance(rc_resistance);
        }

        rc_data.add_one_net_data(one_net_data);
    }

    log::info!("build net rc data finish");
    rc_data
}

/// build rc data, rc node from pg node, rc edge from pg edge.
pub fn create_rc_data_from_topo(pg_netlist: &RustIRPGNetlist) -> RCOneNetData {
    let net_name = c_str_to_r_str(pg_netlist.net_name);
    let mut one_net_data = RCOneNetData::new(net_name.clone());

    for pg_node in pg_netlist.nodes.iter() {
        let node_id = pg_node.node_id;
        if pg_node.is_instance_pin || pg_node.is_bump {
            let node_name = c_str_to_r_str(pg_node.node_name);
            let mut rc_node = RCNode::new(node_name);

            if pg_node.is_bump {
                rc_node.set_is_bump();
            } else {
                rc_node.set_is_inst_pin();
            }

            one_net_data.add_node(rc_node);
        } else {
            let node_name = format!("{}:{}", net_name, node_id);
            let rc_node = RCNode::new(node_name);
            one_net_data.add_node(rc_node);
        }
    }

    for pg_edge in pg_netlist.edges.iter() {
        let node1_id = pg_edge.node1 as usize;
        let node2_id = pg_edge.node2 as usize;
        let mut rc_resistance = RCResistance::default();
        rc_resistance.from_node_id = node1_id;
        rc_resistance.to_node_id = node2_id;
        rc_resistance.resistance = pg_edge.resistance;
        one_net_data.add_resistance(rc_resistance);
    }

    one_net_data
}

/// Build conductance matrix from one net rc data.
pub fn build_conductance_matrix(rc_one_net_data: &RCOneNetData) -> TriMatI<f64, usize> {
    let nodes = rc_one_net_data.get_nodes();
    let resistances = rc_one_net_data.get_resistances();

    let matrix_size = nodes.borrow().len();
    let net_name = rc_one_net_data.get_name();
    log::info!("{} matrix size {}", net_name, matrix_size);

    let sum_resistance: f64 = resistances.iter().map(|x| x.resistance).sum();
    log::info!("{} sum resistance {}", net_name, sum_resistance);

    let mut g_matrix = TriMat::new((matrix_size, matrix_size));

    for node in nodes.borrow().iter() {
        if node.get_is_bump() {
            let node_name = node.get_node_name();
            let node_id = rc_one_net_data.get_node_id(node_name).unwrap();
            g_matrix.add_triplet(node_id, node_id, 1.0 / POWER_INNER_RESISTANCE);
        }
    }

    for rc_resistance in resistances {
        let node1_id = rc_resistance.from_node_id;
        let node2_id = rc_resistance.to_node_id;
        let resistance_val = rc_resistance.resistance;

        g_matrix.add_triplet(node1_id, node2_id, -1.0 / resistance_val);
        g_matrix.add_triplet(node2_id, node1_id, -1.0 / resistance_val);
        g_matrix.add_triplet(node1_id, node1_id, 1.0 / resistance_val);
        g_matrix.add_triplet(node2_id, node2_id, 1.0 / resistance_val);
    }

    g_matrix
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn create_rc_data_from_spef_nets_preserves_iir_rc_semantics() {
        let nets = vec![SpefNetInput {
            name: "VDD".to_string(),
            conns: vec![
                SpefConnInput { name: "VDD".to_string(), is_external: true },
                SpefConnInput { name: "U1/VDD".to_string(), is_external: false },
            ],
            caps: vec![
                SpefResCapInput { node1: "VDD".to_string(), node2: String::new(), value: 0.25 },
                SpefResCapInput { node1: "VDD:3".to_string(), node2: String::new(), value: 0.5 },
                SpefResCapInput {
                    node1: "COUPLED_ONLY_A".to_string(),
                    node2: "COUPLED_ONLY_B".to_string(),
                    value: 0.75,
                },
            ],
            ress: vec![SpefResCapInput { node1: "VDD".to_string(), node2: "U1/VDD".to_string(), value: 2.0 }],
        }];

        let rc_data = create_rc_data_from_spef_nets(&nets);
        let one_net = rc_data.get_one_net_data("VDD");

        let vdd = "VDD".to_string();
        let inst = "U1/VDD".to_string();
        let cap_only = "VDD:3".to_string();
        let coupled_only = "COUPLED_ONLY_A".to_string();

        let vdd_id = one_net.get_node_id(&vdd).unwrap();
        let inst_id = one_net.get_node_id(&inst).unwrap();
        let cap_only_id = one_net.get_node_id(&cap_only).unwrap();

        {
            let nodes = one_net.get_nodes().borrow();
            assert!(nodes[vdd_id].get_is_bump());
            assert_eq!(nodes[vdd_id].get_cap(), 0.25);
            assert!(nodes[inst_id].get_is_inst_pin());
            assert_eq!(nodes[cap_only_id].get_cap(), 0.5);
        }

        assert!(one_net.get_node_id(&coupled_only).is_none());
        assert_eq!(one_net.get_resistances().len(), 1);
        assert_eq!(one_net.get_resistances()[0].resistance, 2.0 * RC_COEFF);
    }
}
