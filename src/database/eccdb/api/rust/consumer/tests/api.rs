//! Black-box tests: this independent consumer can only access the public crate.
use eccdb::*;
use std::path::PathBuf;

fn fixture() -> PathBuf {
    PathBuf::from(
        std::env::var_os("ECCDB_TEST_DATA")
            .expect("Set ECCDB_TEST_DATA to the directory produced by eccdb_c_fixture"),
    )
}
fn binary(dir: &std::path::Path) -> BinaryFiles {
    BinaryFiles::new(
        dir.join("tech.bin"),
        dir.join("library.bin"),
        dir.join("design.bin"),
    )
}
fn open() -> Result<Database> {
    Database::open_binary(binary(&fixture()))
}
fn ids() -> Vec<u64> {
    std::fs::read_to_string(fixture().join("ids.txt"))
        .unwrap()
        .split_whitespace()
        .map(|v| v.parse().unwrap())
        .collect()
}
struct Output(PathBuf);
impl Output {
    fn new() -> Self {
        let stamp = std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap()
            .as_nanos();
        let dir =
            std::env::temp_dir().join(format!("eccdb-rust-caller-{}-{stamp}", std::process::id()));
        std::fs::create_dir(&dir).unwrap();
        Self(dir)
    }
}
impl Drop for Output {
    fn drop(&mut self) {
        // This directory was created exclusively by this test.
        let _ = std::fs::remove_dir_all(&self.0);
    }
}

#[test]
fn snapshots_are_detached_and_survive_database_drop() -> Result<()> {
    let mut db = open()?;
    let id = db.create_net(&NetData {
        name: "original".into(),
        ..Default::default()
    })?;
    let mut snapshot = db.net(id)?.unwrap();
    snapshot.name = "renamed".into();
    snapshot.weight = Some(0);
    assert_eq!(db.net(id)?.unwrap().name, "original");
    db.update_net(id, &snapshot)?;
    assert_eq!(db.find_net("original")?, None);
    assert_eq!(db.find_net("renamed")?, Some(id));
    assert_eq!(db.net(id)?, Some(snapshot.clone()));
    drop(db);
    assert_eq!(snapshot.name, "renamed");
    assert_eq!(snapshot.weight, Some(0));
    Ok(())
}

#[test]
fn failed_updates_do_not_change_existing_state() -> Result<()> {
    let mut db = open()?;
    let first = db.create_net(&NetData {
        name: "first".into(),
        ..Default::default()
    })?;
    let second = db.create_net(&NetData {
        name: "second".into(),
        ..Default::default()
    })?;
    let original = db.net(first)?.unwrap();
    let mut bad = original.clone();
    bad.name = "second".into();
    assert_eq!(
        db.update_net(first, &bad).unwrap_err().kind,
        ErrorKind::InvalidArgument
    );
    assert_eq!(db.net(first)?, Some(original));
    assert_eq!(db.find_net("second")?, Some(second));
    assert_eq!(
        db.find_net("invalid\0name").unwrap_err().kind,
        ErrorKind::InvalidArgument
    );
    assert!(db.net(NetId::INVALID)?.is_none());
    assert!(!db.contains_net(NetId::INVALID)?);
    let options = NetOptions {
        voltage: Some(10),
        ..Default::default()
    };
    assert_eq!(
        db.set_net_options(first, &options).unwrap_err().kind,
        ErrorKind::InvalidArgument
    );
    assert_eq!(db.net_options(first)?, None);
    let inst = db.create_instance(&InstanceData {
        name: "u".into(),
        master: CellMasterId::from_packed(ids()[4]),
        ..Default::default()
    })?;
    let original = db.instance(inst)?.unwrap();
    let mut bad = original.clone();
    bad.master = CellMasterId::INVALID;
    assert!(db.update_instance(inst, &bad).is_err());
    assert_eq!(db.instance(inst)?, Some(original));
    Ok(())
}

#[test]
fn connections_are_bidirectional_and_deleted_ids_stay_invalid() -> Result<()> {
    let mut db = open()?;
    let net = db.create_net(&NetData {
        name: "signal".into(),
        ..Default::default()
    })?;
    let inst = db.create_instance(&InstanceData {
        name: "u".into(),
        master: CellMasterId::from_packed(ids()[4]),
        ..Default::default()
    })?;
    let pin = db.find_instance_pin(inst, "A")?.unwrap();
    let io = db.create_io_pin(&IoPinData {
        name: "IN".into(),
        ..Default::default()
    })?;
    db.connect_instance_pin(pin, net)?;
    db.connect_io_pin(io, net)?;
    assert_eq!(db.instance_pin(pin)?.unwrap().net, Some(net));
    assert_eq!(db.io_pin(io)?.unwrap().net, Some(net));
    assert_eq!(db.net_pin_ids(net)?, (vec![pin], vec![io]));
    assert!(!db.destroy_net(net)?);
    db.disconnect_instance_pin_from_net(pin, net)?;
    db.disconnect_io_pin(io)?;
    assert_eq!(db.instance_pin(pin)?.unwrap().net, None);
    assert_eq!(db.io_pin(io)?.unwrap().net, None);
    assert_eq!(db.net_pin_ids(net)?, (vec![], vec![]));
    assert!(db.destroy_instance(inst)?);
    assert!(!db.contains_instance_pin(pin)?);
    assert!(db.instance_pin(pin)?.is_none());
    assert!(db.destroy_io_pin(io)?);
    assert!(db.destroy_net(net)?);
    assert!(!db.contains_net(net)?);
    assert!(db.net(net)?.is_none());
    let replacement = db.create_net(&NetData {
        name: "replacement".into(),
        ..Default::default()
    })?;
    assert_ne!(replacement, net);
    assert!(!db.contains_net(net)?);
    assert!(!db.contains_instance(inst)?);
    assert!(!db.contains_io_pin(io)?);
    Ok(())
}

#[test]
fn generated_via_wire_and_options_survive_binary_reopen() -> Result<()> {
    let mut db = open()?;
    let ids = ids();
    let power = db.create_special_net(&NetData {
        name: "VDD".into(),
        signal_use: SignalUse::Power,
        ..Default::default()
    })?;
    let options = NetOptions {
        voltage: Some(0),
        frequency: Some(0.0),
        spacing_rules: vec![NetSpacingRule {
            layer: RoutingLayerId::from_packed(ids[0]),
            spacing: 2,
            range: Some((0, 10)),
        }],
        ..Default::default()
    };
    db.set_net_options(power, &options)?;
    let via_data = DesignViaData {
        name: "generated".into(),
        generated: Some(GeneratedViaData {
            via_rule: ViaRuleId::from_packed(ids[3]),
            bottom_layer: RoutingLayerId::from_packed(ids[0]),
            cut_layer: LayerId::from_packed(ids[1]),
            top_layer: RoutingLayerId::from_packed(ids[2]),
            cut_size_x: 2,
            cut_size_y: 3,
            cut_spacing_x: 4,
            cut_spacing_y: 5,
            bottom_enclosure_x: 6,
            bottom_enclosure_y: 7,
            top_enclosure_x: 8,
            top_enclosure_y: 9,
            row_column: Some((2, 3)),
            origin: Some(Point { x: 0, y: 0 }),
            offsets: Some((Point { x: 1, y: 2 }, Point { x: 3, y: 4 })),
            cut_pattern: Some("PATTERN".into()),
        }),
        ..Default::default()
    };
    let via = db.create_design_via(&via_data)?;
    let metadata = WireMetadata {
        net: power,
        status: WireStatus::Fixed,
        ..Default::default()
    };
    let mut routing = WireRoutingData {
        paths: vec![WirePathData {
            layer: RoutingLayerId::from_packed(ids[0]),
            width: Some(20),
            points: vec![
                WirePoint {
                    extension: Some(0),
                    ..Default::default()
                },
                WirePoint {
                    position: Point { x: 100, y: 0 },
                    ..Default::default()
                },
            ],
            vias: vec![ViaPlacementData {
                point_index: 1,
                definition: ViaDefinitionId::Design(via),
                ..Default::default()
            }],
            ..Default::default()
        }],
    };
    let wire = db.create_wire(&metadata, &routing)?;
    assert!(!db.destroy_design_via(via)?);
    routing.paths[0].points[1].position.x = 200;
    assert_eq!(
        db.wire_routing(wire)?.unwrap().paths[0].points[1]
            .position
            .x,
        100
    );
    db.update_wire(wire, &metadata, &routing)?;
    let mut invalid = routing.clone();
    invalid.paths[0].vias[0].point_index = 999;
    assert!(db.update_wire(wire, &metadata, &invalid).is_err());
    assert_eq!(db.wire_routing(wire)?, Some(routing.clone()));
    let output = Output::new();
    db.write_binary(&binary(&output.0))?;
    db.write_def(output.0.join("design.def"))?;
    let def = std::fs::read_to_string(output.0.join("design.def")).unwrap();
    assert!(def.contains("VDD") && def.contains("generated"));
    drop(db);
    let mut db = Database::open(&Config::new(DatabaseInput::Binary(binary(&output.0))))?;
    let power = db.find_special_net("VDD")?.unwrap();
    let via = db.find_design_via("generated")?.unwrap();
    let wire = db.net_wire_ids(power)?[0];
    assert_eq!(db.net_options(power)?, Some(options));
    assert_eq!(db.design_via(via)?, Some(via_data));
    // Re-resolve database-local references before comparing entire snapshots.
    routing.paths[0].vias[0].definition = ViaDefinitionId::Design(via);
    assert_eq!(db.wire_routing(wire)?, Some(routing));
    assert_eq!(db.wire_metadata(wire)?.unwrap().net, power);
    assert!(db.destroy_wire(wire)?);
    assert!(!db.contains_wire(wire)?);
    assert!(db.wire_routing(wire)?.is_none());
    assert!(db.destroy_design_via(via)?);
    assert!(!db.contains_design_via(via)?);
    Ok(())
}

#[test]
fn missing_files_and_unsupported_import_are_reported_as_errors() -> Result<()> {
    let output = Output::new();
    assert!(Database::open_binary(binary(&output.0)).is_err());
    if !Database::supports_lef_def()? {
        assert!(
            Database::open_lef_def(vec![output.0.join("tech.lef")], output.0.join("design.def"))
                .is_err()
        );
    }
    let db = open()?;
    assert!(
        db.write_def(output.0.join("missing-parent/out.def"))
            .is_err()
    );
    assert!(db.net_ids()?.is_empty());
    Ok(())
}
