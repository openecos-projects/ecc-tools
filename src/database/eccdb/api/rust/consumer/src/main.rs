use eccdb::*;
use std::{
    error::Error as StdError,
    path::{Path, PathBuf},
};

fn files(dir: &Path, prefix: &str) -> BinaryFiles {
    BinaryFiles::new(
        dir.join(format!("{prefix}tech.bin")),
        dir.join(format!("{prefix}library.bin")),
        dir.join(format!("{prefix}design.bin")),
    )
}

fn main() -> std::result::Result<(), Box<dyn StdError>> {
    let dir = PathBuf::from(
        std::env::args_os()
            .nth(1)
            .expect("pass the C fixture directory"),
    );
    let ids = std::fs::read_to_string(dir.join("ids.txt"))?
        .split_whitespace()
        .map(str::parse::<u64>)
        .collect::<std::result::Result<Vec<_>, _>>()?;
    let (bottom, cut, top, rule, master) = (
        RoutingLayerId::from_packed(ids[0]),
        LayerId::from_packed(ids[1]),
        RoutingLayerId::from_packed(ids[2]),
        ViaRuleId::from_packed(ids[3]),
        CellMasterId::from_packed(ids[4]),
    );
    let mut db = Database::open_binary(files(&dir, ""))?;
    assert!(db.diagnostics()?.is_empty());
    let net = db.create_net(&NetData {
        name: "signal".into(),
        signal_use: SignalUse::Signal,
        ..Default::default()
    })?;
    let power = db.create_special_net(&NetData {
        name: "VDD".into(),
        signal_use: SignalUse::Power,
        ..Default::default()
    })?;
    assert_eq!(db.find_regular_net("signal")?, Some(net));
    assert_eq!(db.find_special_net("VDD")?, Some(power));
    assert!(db.contains_net(net)? && db.is_special_net(power)?);
    assert_eq!(db.net_ids()?.len(), 2);
    assert_eq!(db.regular_net_ids()?, vec![net]);
    assert_eq!(db.special_net_ids()?, vec![power]);
    let mut nd = db.net(net)?.unwrap();
    assert_eq!(nd.tech_non_default_rule, None);
    nd.weight = Some(0);
    nd.source = NetSource::User;
    db.update_net(net, &nd)?;
    assert_eq!(db.net(net)?.unwrap(), nd);
    let options = NetOptions {
        pattern: Some(NetPattern::Balanced),
        frequency: Some(1.0),
        spacing_rules: vec![NetSpacingRule {
            layer: bottom,
            spacing: 2,
            range: Some((1, 4)),
        }],
        ..Default::default()
    };
    // Spacing rules belong to SPECIALNET, as enforced by the C++ API.
    assert_eq!(
        db.set_net_options(net, &options).unwrap_err().kind,
        ErrorKind::InvalidArgument
    );
    db.set_net_options(power, &options)?;
    assert_eq!(db.net_options(power)?, Some(options));
    let instance = db.create_instance(&InstanceData {
        name: "u1".into(),
        master,
        ..Default::default()
    })?;
    let mut inst = db.instance(instance)?.unwrap();
    inst.origin = Point { x: 100, y: 200 };
    inst.placement_status = PlacementStatus::Placed;
    db.update_instance(instance, &inst)?;
    assert_eq!(db.instance(instance)?, Some(inst));
    assert_eq!(db.find_instance("u1")?, Some(instance));
    assert!(db.contains_instance(instance)?);
    assert_eq!(db.instance_ids()?, vec![instance]);
    let pin = db.find_instance_pin(instance, "A")?.unwrap();
    assert!(db.contains_instance_pin(pin)?);
    assert_eq!(db.instance_pin_ids(instance)?, vec![pin]);
    db.connect_instance_pin(pin, net)?;
    assert_eq!(db.instance_pin(pin)?.unwrap().net, Some(net));
    db.disconnect_instance_pin(pin)?;
    db.connect_instance_pin(pin, net)?;
    let io = db.create_io_pin(&IoPinData {
        name: "IN".into(),
        ..Default::default()
    })?;
    assert!(db.contains_io_pin(io)?);
    assert_eq!(db.find_io_pin("IN")?, Some(io));
    assert_eq!(db.io_pin_ids()?, vec![io]);
    db.connect_io_pin(io, net)?;
    let mut io_data = db.io_pin(io)?.unwrap();
    io_data.direction = IoDirection::Input;
    db.update_io_pin(io, &io_data)?;
    db.disconnect_io_pin(io)?;
    db.connect_io_pin(io, net)?;
    assert_eq!(db.net_pin_ids(net)?, (vec![pin], vec![io]));
    let mut via_data = DesignViaData {
        name: "generated".into(),
        generated: Some(GeneratedViaData {
            via_rule: rule,
            bottom_layer: bottom,
            cut_layer: cut,
            top_layer: top,
            cut_size_x: 2,
            cut_size_y: 3,
            cut_spacing_x: 4,
            cut_spacing_y: 5,
            bottom_enclosure_x: 6,
            bottom_enclosure_y: 7,
            top_enclosure_x: 8,
            top_enclosure_y: 9,
            row_column: Some((2, 3)),
            origin: Some(Point { x: 4, y: 5 }),
            offsets: Some((Point { x: 6, y: 7 }, Point { x: 8, y: 9 })),
            cut_pattern: Some("pattern".into()),
        }),
        ..Default::default()
    };
    let via = db.create_design_via(&via_data)?;
    assert_eq!(db.design_via(via)?, Some(via_data.clone()));
    assert!(db.contains_design_via(via)?);
    assert_eq!(db.find_design_via("generated")?, Some(via));
    assert_eq!(db.design_via_ids()?, vec![via]);
    via_data.generated.as_mut().unwrap().origin = Some(Point { x: 42, y: 0 });
    db.update_design_via(via, &via_data)?;
    let metadata = WireMetadata {
        net: power,
        status: WireStatus::Fixed,
        ..Default::default()
    };
    let mut routing = WireRoutingData {
        paths: vec![WirePathData {
            layer: bottom,
            width: Some(20),
            points: vec![
                WirePoint {
                    position: Point { x: 0, y: 0 },
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
    assert!(db.contains_wire(wire)?);
    assert_eq!(db.wire_metadata(wire)?, Some(metadata.clone()));
    assert_eq!(db.wire_routing(wire)?, Some(routing.clone()));
    assert_eq!(db.net_wire_ids(power)?, vec![wire]);
    assert_eq!(db.wire_ids()?, vec![wire]);
    routing.paths[0].points[1].position.x = 200;
    db.update_wire(wire, &metadata, &routing)?;
    assert!(!db.destroy_design_via(via)?);
    let fixed = DesignViaData {
        name: "fixed".into(),
        rectangles: vec![ViaRectangle {
            layer: LayerId::from_packed(bottom.packed()),
            rectangle: Rect {
                ll_x: -2,
                ll_y: -2,
                ur_x: 2,
                ur_y: 2,
            },
            mask: 0,
        }],
        polygons: vec![ViaPolygon {
            layer: LayerId::from_packed(top.packed()),
            points: vec![
                Point { x: -3, y: -3 },
                Point { x: 3, y: -3 },
                Point { x: 3, y: 3 },
                Point { x: -3, y: 3 },
            ],
            mask: 0,
        }],
        ..Default::default()
    };
    let fixed_id = db.create_design_via(&fixed)?;
    assert_eq!(db.design_via(fixed_id)?, Some(fixed.clone()));
    db.update_design_via(fixed_id, &fixed)?;
    assert!(db.destroy_design_via(fixed_id)?);
    assert!(db.find_net("absent")?.is_none());
    assert!(db.net(NetId::INVALID)?.is_none());
    assert_eq!(
        db.find_net("bad\0name").unwrap_err().kind,
        ErrorKind::InvalidArgument
    );
    assert_eq!(
        db.create_net(&nd).unwrap_err().kind,
        ErrorKind::InvalidArgument
    );
    db.write_binary(&files(&dir, "rust-"))?;
    db.write_def(dir.join("rust.def"))?;
    drop(db);
    let mut db = Database::open_binary(files(&dir, "rust-"))?;
    // Re-resolve database-local IDs after reopening.
    let via = db.find_design_via("generated")?.unwrap();
    let wire = db.wire_ids()?[0];
    assert_eq!(db.design_via(via)?, Some(via_data));
    assert_eq!(
        db.wire_routing(wire)?.unwrap().paths[0].points[1]
            .position
            .x,
        200
    );
    assert!(db.destroy_wire(wire)?);
    assert!(db.destroy_design_via(via)?);
    let net = db.find_net("signal")?.unwrap();
    let inst = db.find_instance("u1")?.unwrap();
    let pin = db.find_instance_pin(inst, "A")?.unwrap();
    let io = db.find_io_pin("IN")?.unwrap();
    db.disconnect_instance_pin_from_net(pin, net)?;
    db.disconnect_io_pin_from_net(io, net)?;
    assert!(db.destroy_instance(inst)?);
    assert!(db.destroy_io_pin(io)?);
    assert!(db.destroy_net(net)?);
    println!(
        "Rust consumer: CRUD, relationships, routing, VIA, binary reopen and DEF export passed"
    );
    Ok(())
}
