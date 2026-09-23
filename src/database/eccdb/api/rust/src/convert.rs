//! Raw pointers stay confined to this module and database call sites.
use crate::{Error, ErrorKind, Result, ffi, types::*};
use std::{
    any::Any,
    ffi::{CStr, CString, c_char},
    path::Path,
};
#[derive(Default)]
pub(crate) struct Arena {
    strings: Vec<CString>,
    buffers: Vec<Box<dyn Any>>,
}
impl Arena {
    pub fn string(&mut self, v: &str) -> Result<*mut c_char> {
        let s = CString::new(v)?;
        let p = s.as_ptr().cast_mut();
        self.strings.push(s);
        Ok(p)
    }
    pub fn path(&mut self, v: &Path) -> Result<*const c_char> {
        #[cfg(unix)]
        let bytes = {
            use std::os::unix::ffi::OsStrExt;
            v.as_os_str().as_bytes()
        };
        #[cfg(not(unix))]
        let bytes = v
            .to_str()
            .ok_or_else(|| Error::argument("non-UTF8 path"))?
            .as_bytes();
        let s = CString::new(bytes)?;
        let p = s.as_ptr();
        self.strings.push(s);
        Ok(p)
    }
    pub fn array<T: 'static>(&mut self, v: Vec<T>) -> *mut T {
        let mut b = v.into_boxed_slice();
        let p = b.as_mut_ptr();
        self.buffers.push(Box::new(b));
        p
    }
}
pub(crate) trait Codec: Sized {
    type Raw: Default;
    fn encode(&self, a: &mut Arena) -> Result<Self::Raw>;
    /// All referenced allocations must remain valid for this call; pointers
    /// must describe initialized values in the matching ECCDB C ABI layout.
    unsafe fn decode(r: &Self::Raw) -> Result<Self>;
}
// Only use on C-owned outputs while their allocation guard is alive.
pub(crate) unsafe fn slice<'a, T>(p: *const T, n: usize) -> Result<&'a [T]> {
    if n == 0 {
        return Ok(&[]);
    }
    if p.is_null() || n > isize::MAX as usize / std::mem::size_of::<T>() {
        return Err(Error::argument("invalid native array"));
    }
    Ok(unsafe { std::slice::from_raw_parts(p, n) })
}
unsafe fn string(p: *const c_char) -> Result<String> {
    if p.is_null() {
        return Err(Error::argument("null native string"));
    }
    unsafe { CStr::from_ptr(p) }
        .to_str()
        .map(str::to_owned)
        .map_err(|e| Error {
            kind: ErrorKind::InvalidUtf8,
            message: e.to_string(),
        })
}
pub(crate) struct Owned<T> {
    pub raw: T,
    free: unsafe extern "C" fn(*mut T),
}
impl<T: Default> Owned<T> {
    pub fn new(free: unsafe extern "C" fn(*mut T)) -> Self {
        Self {
            raw: T::default(),
            free,
        }
    }
}
impl<T> Drop for Owned<T> {
    fn drop(&mut self) {
        unsafe { (self.free)(&mut self.raw) }
    }
}
impl Codec for Point {
    type Raw = ffi::eccdb_point_t;
    #[allow(unused_variables)]
    fn encode(&self, a: &mut Arena) -> Result<Self::Raw> {
        let r = Self::Raw {
            x: self.x,
            y: self.y,
        };
        Ok(r)
    }
    #[allow(unused_unsafe)]
    unsafe fn decode(r: &Self::Raw) -> Result<Self> {
        unsafe { Ok(Self { x: r.x, y: r.y }) }
    }
}
impl Codec for Rect {
    type Raw = ffi::eccdb_rect_t;
    #[allow(unused_variables)]
    fn encode(&self, a: &mut Arena) -> Result<Self::Raw> {
        let r = Self::Raw {
            ll_x: self.ll_x,
            ll_y: self.ll_y,
            ur_x: self.ur_x,
            ur_y: self.ur_y,
        };
        Ok(r)
    }
    #[allow(unused_unsafe)]
    unsafe fn decode(r: &Self::Raw) -> Result<Self> {
        unsafe {
            Ok(Self {
                ll_x: r.ll_x,
                ll_y: r.ll_y,
                ur_x: r.ur_x,
                ur_y: r.ur_y,
            })
        }
    }
}
impl Codec for NetData {
    type Raw = ffi::eccdb_net_data_t;
    #[allow(unused_variables)]
    fn encode(&self, a: &mut Arena) -> Result<Self::Raw> {
        let mut r = Self::Raw {
            name: a.string(&self.name)?,
            signal_use: self.signal_use as u8,
            source: self.source as u8,
            ..Default::default()
        };
        if let Some(v) = &self.weight {
            r.has_weight = 1;
            r.weight = *v;
        }
        r.tech_non_default_rule = self
            .tech_non_default_rule
            .map(|id| id.0)
            .unwrap_or(u64::MAX);
        r.design_non_default_rule = self
            .design_non_default_rule
            .map(|id| id.0)
            .unwrap_or(u64::MAX);
        Ok(r)
    }
    #[allow(unused_unsafe)]
    unsafe fn decode(r: &Self::Raw) -> Result<Self> {
        unsafe {
            Ok(Self {
                name: string(r.name)?,
                signal_use: SignalUse::try_from(r.signal_use)?,
                source: NetSource::try_from(r.source)?,
                weight: if r.has_weight != 0 {
                    Some(r.weight)
                } else {
                    None
                },
                tech_non_default_rule: (r.tech_non_default_rule != u64::MAX)
                    .then_some(TechRuleId(r.tech_non_default_rule)),
                design_non_default_rule: (r.design_non_default_rule != u64::MAX)
                    .then_some(DesignRuleId(r.design_non_default_rule)),
            })
        }
    }
}
impl Codec for InstanceData {
    type Raw = ffi::eccdb_instance_data_t;
    #[allow(unused_variables)]
    fn encode(&self, a: &mut Arena) -> Result<Self::Raw> {
        let r = Self::Raw {
            name: a.string(&self.name)?,
            master: self.master.0,
            origin_x: self.origin.x,
            origin_y: self.origin.y,
            orientation: self.orientation as u8,
            placement_status: self.placement_status as u8,
            source: self.source as u8,
        };
        Ok(r)
    }
    #[allow(unused_unsafe)]
    unsafe fn decode(r: &Self::Raw) -> Result<Self> {
        unsafe {
            Ok(Self {
                name: string(r.name)?,
                master: CellMasterId(r.master),
                origin: Point {
                    x: r.origin_x,
                    y: r.origin_y,
                },
                orientation: Orientation::try_from(r.orientation)?,
                placement_status: PlacementStatus::try_from(r.placement_status)?,
                source: InstanceSource::try_from(r.source)?,
            })
        }
    }
}
impl Codec for InstancePinData {
    type Raw = ffi::eccdb_instance_pin_data_t;
    #[allow(unused_variables)]
    fn encode(&self, a: &mut Arena) -> Result<Self::Raw> {
        let mut r = Self::Raw::default();
        r.instance.value = self.instance.0;
        r.master_term = self.master_term.0;
        r.net.value = self.net.map(|id| id.0).unwrap_or(u64::MAX);
        r.special_net.value = self.special_net.map(|id| id.0).unwrap_or(u64::MAX);
        Ok(r)
    }
    #[allow(unused_unsafe)]
    unsafe fn decode(r: &Self::Raw) -> Result<Self> {
        unsafe {
            Ok(Self {
                instance: InstanceId(r.instance.value),
                master_term: MasterTermId(r.master_term),
                net: (r.net.value != u64::MAX).then_some(NetId(r.net.value)),
                special_net: (r.special_net.value != u64::MAX)
                    .then_some(NetId(r.special_net.value)),
            })
        }
    }
}
impl Codec for IoPinData {
    type Raw = ffi::eccdb_io_pin_data_t;
    #[allow(unused_variables)]
    fn encode(&self, a: &mut Arena) -> Result<Self::Raw> {
        let mut r = Self::Raw {
            name: a.string(&self.name)?,
            direction: self.direction as u8,
            ..Default::default()
        };
        r.r#use = self.signal_use as u8;
        r.net.value = self.net.map(|id| id.0).unwrap_or(u64::MAX);
        r.special_net.value = self.special_net.map(|id| id.0).unwrap_or(u64::MAX);
        Ok(r)
    }
    #[allow(unused_unsafe)]
    unsafe fn decode(r: &Self::Raw) -> Result<Self> {
        unsafe {
            Ok(Self {
                name: string(r.name)?,
                direction: IoDirection::try_from(r.direction)?,
                signal_use: SignalUse::try_from(r.r#use)?,
                net: (r.net.value != u64::MAX).then_some(NetId(r.net.value)),
                special_net: (r.special_net.value != u64::MAX)
                    .then_some(NetId(r.special_net.value)),
            })
        }
    }
}
impl Codec for NetSpacingRule {
    type Raw = ffi::eccdb_net_spacing_rule_t;
    #[allow(unused_variables)]
    fn encode(&self, a: &mut Arena) -> Result<Self::Raw> {
        let mut r = Self::Raw::default();
        r.layer.value = self.layer.0;
        r.spacing = self.spacing;
        if let Some((x, y)) = &self.range {
            r.has_range = 1;
            r.range_min = *x;
            r.range_max = *y;
        }
        Ok(r)
    }
    #[allow(unused_unsafe)]
    unsafe fn decode(r: &Self::Raw) -> Result<Self> {
        unsafe {
            Ok(Self {
                layer: RoutingLayerId(r.layer.value),
                spacing: r.spacing,
                range: if r.has_range != 0 {
                    Some((r.range_min, r.range_max))
                } else {
                    None
                },
            })
        }
    }
}
impl Codec for NetOptions {
    type Raw = ffi::eccdb_net_options_t;
    #[allow(unused_variables)]
    fn encode(&self, a: &mut Arena) -> Result<Self::Raw> {
        let mut r = Self::Raw::default();
        if let Some(v) = &self.original {
            r.has_original = 1;
            r.original = a.string(v)?;
        }
        if let Some(v) = &self.pattern {
            r.has_pattern = 1;
            r.pattern = *v as u8;
        }
        if let Some(v) = &self.estimated_capacitance {
            r.has_estimated_capacitance = 1;
            r.estimated_capacitance = *v;
        }
        if let Some(v) = &self.frequency {
            r.has_frequency = 1;
            r.frequency = *v;
        }
        if let Some(v) = &self.xtalk {
            r.has_xtalk = 1;
            r.xtalk = *v;
        }
        if let Some(v) = &self.style {
            r.has_style = 1;
            r.style = *v;
        }
        if let Some(v) = &self.voltage {
            r.has_voltage = 1;
            r.voltage = *v;
        }
        let values = self
            .spacing_rules
            .iter()
            .map(|v| v.encode(a))
            .collect::<Result<Vec<_>>>()?;
        r.spacing_rule_count = values.len();
        r.spacing_rules = a.array(values);
        Ok(r)
    }
    #[allow(unused_unsafe)]
    unsafe fn decode(r: &Self::Raw) -> Result<Self> {
        unsafe {
            Ok(Self {
                original: if r.has_original != 0 {
                    Some(string(r.original)?)
                } else {
                    None
                },
                pattern: if r.has_pattern != 0 {
                    Some(NetPattern::try_from(r.pattern)?)
                } else {
                    None
                },
                estimated_capacitance: if r.has_estimated_capacitance != 0 {
                    Some(r.estimated_capacitance)
                } else {
                    None
                },
                frequency: if r.has_frequency != 0 {
                    Some(r.frequency)
                } else {
                    None
                },
                xtalk: if r.has_xtalk != 0 {
                    Some(r.xtalk)
                } else {
                    None
                },
                style: if r.has_style != 0 {
                    Some(r.style)
                } else {
                    None
                },
                voltage: if r.has_voltage != 0 {
                    Some(r.voltage)
                } else {
                    None
                },
                spacing_rules: slice(r.spacing_rules, r.spacing_rule_count)?
                    .iter()
                    .map(|v| NetSpacingRule::decode(v))
                    .collect::<Result<Vec<_>>>()?,
            })
        }
    }
}
impl Codec for WirePoint {
    type Raw = ffi::eccdb_wire_point_t;
    #[allow(unused_variables)]
    fn encode(&self, a: &mut Arena) -> Result<Self::Raw> {
        let mut r = Self::Raw {
            position: self.position.encode(a)?,
            ..Default::default()
        };
        if let Some(v) = &self.extension {
            r.has_extension = 1;
            r.extension = *v;
        }
        r.virtual_point = self.virtual_point as u8;
        Ok(r)
    }
    #[allow(unused_unsafe)]
    unsafe fn decode(r: &Self::Raw) -> Result<Self> {
        unsafe {
            Ok(Self {
                position: Point::decode(&r.position)?,
                extension: if r.has_extension != 0 {
                    Some(r.extension)
                } else {
                    None
                },
                virtual_point: r.virtual_point != 0,
            })
        }
    }
}
impl Codec for WireMask {
    type Raw = ffi::eccdb_wire_mask_t;
    #[allow(unused_variables)]
    fn encode(&self, a: &mut Arena) -> Result<Self::Raw> {
        let r = Self::Raw {
            top: self.top,
            cut: self.cut,
            bottom: self.bottom,
        };
        Ok(r)
    }
    #[allow(unused_unsafe)]
    unsafe fn decode(r: &Self::Raw) -> Result<Self> {
        unsafe {
            Ok(Self {
                top: r.top,
                cut: r.cut,
                bottom: r.bottom,
            })
        }
    }
}
impl Codec for ViaPlacementData {
    type Raw = ffi::eccdb_via_placement_t;
    #[allow(unused_variables)]
    fn encode(&self, a: &mut Arena) -> Result<Self::Raw> {
        let mut r = Self::Raw {
            point_index: self.point_index,
            ..Default::default()
        };
        match self.definition {
            ViaDefinitionId::Tech(id) => {
                r.definition_kind = 0;
                r.definition = id.0;
            }
            ViaDefinitionId::Design(id) => {
                r.definition_kind = 1;
                r.definition = id.0;
            }
        }
        r.orientation = self.orientation as u8;
        if let Some(v) = &self.mask {
            r.has_mask = 1;
            r.mask = v.encode(a)?;
        }
        if let Some((x, y)) = &self.rows_columns {
            r.has_rows_columns = 1;
            r.rows = *x;
            r.columns = *y;
        }
        r.step_x = self.step_x;
        r.step_y = self.step_y;
        Ok(r)
    }
    #[allow(unused_unsafe)]
    unsafe fn decode(r: &Self::Raw) -> Result<Self> {
        unsafe {
            Ok(Self {
                point_index: r.point_index,
                definition: match r.definition_kind {
                    0 => ViaDefinitionId::Tech(TechViaId(r.definition)),
                    1 => ViaDefinitionId::Design(ViaId(r.definition)),
                    _ => return Err(Error::argument("invalid via definition tag")),
                },
                orientation: Orientation::try_from(r.orientation)?,
                mask: if r.has_mask != 0 {
                    Some(WireMask::decode(&r.mask)?)
                } else {
                    None
                },
                rows_columns: if r.has_rows_columns != 0 {
                    Some((r.rows, r.columns))
                } else {
                    None
                },
                step_x: r.step_x,
                step_y: r.step_y,
            })
        }
    }
}
impl Codec for WireRectangle {
    type Raw = ffi::eccdb_wire_rectangle_t;
    #[allow(unused_variables)]
    fn encode(&self, a: &mut Arena) -> Result<Self::Raw> {
        let r = Self::Raw {
            point_index: self.point_index,
            delta: self.delta.encode(a)?,
        };
        Ok(r)
    }
    #[allow(unused_unsafe)]
    unsafe fn decode(r: &Self::Raw) -> Result<Self> {
        unsafe {
            Ok(Self {
                point_index: r.point_index,
                delta: Rect::decode(&r.delta)?,
            })
        }
    }
}
impl Codec for WireMetadata {
    type Raw = ffi::eccdb_wire_metadata_t;
    #[allow(unused_variables)]
    fn encode(&self, a: &mut Arena) -> Result<Self::Raw> {
        let mut r = Self::Raw::default();
        r.net.value = self.net.0;
        r.status = self.status as u8;
        r.shield_net = a.string(&self.shield_net)?;
        Ok(r)
    }
    #[allow(unused_unsafe)]
    unsafe fn decode(r: &Self::Raw) -> Result<Self> {
        unsafe {
            Ok(Self {
                net: NetId(r.net.value),
                status: WireStatus::try_from(r.status)?,
                shield_net: string(r.shield_net)?,
            })
        }
    }
}
impl Codec for WirePathData {
    type Raw = ffi::eccdb_wire_path_t;
    #[allow(unused_variables)]
    fn encode(&self, a: &mut Arena) -> Result<Self::Raw> {
        let mut r = Self::Raw::default();
        r.layer.value = self.layer.0;
        if let Some(v) = &self.width {
            r.has_width = 1;
            r.width = *v;
        }
        if let Some(v) = &self.mask {
            r.has_mask = 1;
            r.mask = *v;
        }
        r.taper = self.taper as u8;
        if let Some(v) = &self.taper_rule {
            r.has_taper_rule = 1;
            r.taper_rule = a.string(v)?;
        }
        if let Some(v) = &self.shape {
            r.has_shape = 1;
            r.shape = a.string(v)?;
        }
        if let Some(v) = &self.style {
            r.has_style = 1;
            r.style = *v;
        }
        let values = self
            .points
            .iter()
            .map(|v| v.encode(a))
            .collect::<Result<Vec<_>>>()?;
        r.point_count = values.len();
        r.points = a.array(values);
        let values = self
            .vias
            .iter()
            .map(|v| v.encode(a))
            .collect::<Result<Vec<_>>>()?;
        r.via_count = values.len();
        r.vias = a.array(values);
        let values = self
            .rectangles
            .iter()
            .map(|v| v.encode(a))
            .collect::<Result<Vec<_>>>()?;
        r.rectangle_count = values.len();
        r.rectangles = a.array(values);
        Ok(r)
    }
    #[allow(unused_unsafe)]
    unsafe fn decode(r: &Self::Raw) -> Result<Self> {
        unsafe {
            Ok(Self {
                layer: RoutingLayerId(r.layer.value),
                width: if r.has_width != 0 {
                    Some(r.width)
                } else {
                    None
                },
                mask: if r.has_mask != 0 { Some(r.mask) } else { None },
                taper: r.taper != 0,
                taper_rule: if r.has_taper_rule != 0 {
                    Some(string(r.taper_rule)?)
                } else {
                    None
                },
                shape: if r.has_shape != 0 {
                    Some(string(r.shape)?)
                } else {
                    None
                },
                style: if r.has_style != 0 {
                    Some(r.style)
                } else {
                    None
                },
                points: slice(r.points, r.point_count)?
                    .iter()
                    .map(|v| WirePoint::decode(v))
                    .collect::<Result<Vec<_>>>()?,
                vias: slice(r.vias, r.via_count)?
                    .iter()
                    .map(|v| ViaPlacementData::decode(v))
                    .collect::<Result<Vec<_>>>()?,
                rectangles: slice(r.rectangles, r.rectangle_count)?
                    .iter()
                    .map(|v| WireRectangle::decode(v))
                    .collect::<Result<Vec<_>>>()?,
            })
        }
    }
}
impl Codec for WireRoutingData {
    type Raw = ffi::eccdb_wire_routing_t;
    #[allow(unused_variables)]
    fn encode(&self, a: &mut Arena) -> Result<Self::Raw> {
        let mut r = Self::Raw::default();
        let values = self
            .paths
            .iter()
            .map(|v| v.encode(a))
            .collect::<Result<Vec<_>>>()?;
        r.path_count = values.len();
        r.paths = a.array(values);
        Ok(r)
    }
    #[allow(unused_unsafe)]
    unsafe fn decode(r: &Self::Raw) -> Result<Self> {
        unsafe {
            Ok(Self {
                paths: slice(r.paths, r.path_count)?
                    .iter()
                    .map(|v| WirePathData::decode(v))
                    .collect::<Result<Vec<_>>>()?,
            })
        }
    }
}
impl Codec for ViaRectangle {
    type Raw = ffi::eccdb_via_rectangle_t;
    #[allow(unused_variables)]
    fn encode(&self, a: &mut Arena) -> Result<Self::Raw> {
        let mut r = Self::Raw::default();
        r.layer.value = self.layer.0;
        r.rectangle = self.rectangle.encode(a)?;
        r.mask = self.mask;
        Ok(r)
    }
    #[allow(unused_unsafe)]
    unsafe fn decode(r: &Self::Raw) -> Result<Self> {
        unsafe {
            Ok(Self {
                layer: LayerId(r.layer.value),
                rectangle: Rect::decode(&r.rectangle)?,
                mask: r.mask,
            })
        }
    }
}
impl Codec for ViaPolygon {
    type Raw = ffi::eccdb_via_polygon_t;
    #[allow(unused_variables)]
    fn encode(&self, a: &mut Arena) -> Result<Self::Raw> {
        let mut r = Self::Raw::default();
        r.layer.value = self.layer.0;
        let values = self
            .points
            .iter()
            .map(|v| v.encode(a))
            .collect::<Result<Vec<_>>>()?;
        r.point_count = values.len();
        r.points = a.array(values);
        r.mask = self.mask;
        Ok(r)
    }
    #[allow(unused_unsafe)]
    unsafe fn decode(r: &Self::Raw) -> Result<Self> {
        unsafe {
            Ok(Self {
                layer: LayerId(r.layer.value),
                points: slice(r.points, r.point_count)?
                    .iter()
                    .map(|v| Point::decode(v))
                    .collect::<Result<Vec<_>>>()?,
                mask: r.mask,
            })
        }
    }
}
impl Codec for GeneratedViaData {
    type Raw = ffi::eccdb_generated_via_data_t;
    #[allow(unused_variables)]
    fn encode(&self, a: &mut Arena) -> Result<Self::Raw> {
        let mut r = Self::Raw {
            via_rule: self.via_rule.0,
            bottom_layer: self.bottom_layer.0,
            cut_layer: self.cut_layer.0,
            top_layer: self.top_layer.0,
            cut_size_x: self.cut_size_x,
            cut_size_y: self.cut_size_y,
            cut_spacing_x: self.cut_spacing_x,
            cut_spacing_y: self.cut_spacing_y,
            bottom_enclosure_x: self.bottom_enclosure_x,
            bottom_enclosure_y: self.bottom_enclosure_y,
            top_enclosure_x: self.top_enclosure_x,
            top_enclosure_y: self.top_enclosure_y,
            ..Default::default()
        };
        if let Some((x, y)) = &self.row_column {
            r.has_row_column = 1;
            r.rows = *x;
            r.columns = *y;
        }
        if let Some(v) = &self.origin {
            r.has_origin = 1;
            r.origin = v.encode(a)?;
        }
        if let Some((x, y)) = &self.offsets {
            r.has_offsets = 1;
            r.bottom_offset = x.encode(a)?;
            r.top_offset = y.encode(a)?;
        }
        if let Some(v) = &self.cut_pattern {
            r.has_cut_pattern = 1;
            r.cut_pattern = a.string(v)?;
        }
        Ok(r)
    }
    #[allow(unused_unsafe)]
    unsafe fn decode(r: &Self::Raw) -> Result<Self> {
        unsafe {
            Ok(Self {
                via_rule: ViaRuleId(r.via_rule),
                bottom_layer: RoutingLayerId(r.bottom_layer),
                cut_layer: LayerId(r.cut_layer),
                top_layer: RoutingLayerId(r.top_layer),
                cut_size_x: r.cut_size_x,
                cut_size_y: r.cut_size_y,
                cut_spacing_x: r.cut_spacing_x,
                cut_spacing_y: r.cut_spacing_y,
                bottom_enclosure_x: r.bottom_enclosure_x,
                bottom_enclosure_y: r.bottom_enclosure_y,
                top_enclosure_x: r.top_enclosure_x,
                top_enclosure_y: r.top_enclosure_y,
                row_column: if r.has_row_column != 0 {
                    Some((r.rows, r.columns))
                } else {
                    None
                },
                origin: if r.has_origin != 0 {
                    Some(Point::decode(&r.origin)?)
                } else {
                    None
                },
                offsets: if r.has_offsets != 0 {
                    Some((
                        Point::decode(&r.bottom_offset)?,
                        Point::decode(&r.top_offset)?,
                    ))
                } else {
                    None
                },
                cut_pattern: if r.has_cut_pattern != 0 {
                    Some(string(r.cut_pattern)?)
                } else {
                    None
                },
            })
        }
    }
}
impl Codec for DesignViaData {
    type Raw = ffi::eccdb_design_via_data_t;
    #[allow(unused_variables)]
    fn encode(&self, a: &mut Arena) -> Result<Self::Raw> {
        let mut r = Self::Raw {
            name: a.string(&self.name)?,
            ..Default::default()
        };
        if let Some(v) = &self.pattern_name {
            r.has_pattern_name = 1;
            r.pattern_name = a.string(v)?;
        }
        let values = self
            .rectangles
            .iter()
            .map(|v| v.encode(a))
            .collect::<Result<Vec<_>>>()?;
        r.rectangle_count = values.len();
        r.rectangles = a.array(values);
        let values = self
            .polygons
            .iter()
            .map(|v| v.encode(a))
            .collect::<Result<Vec<_>>>()?;
        r.polygon_count = values.len();
        r.polygons = a.array(values);
        if let Some(v) = &self.generated {
            r.has_generated = 1;
            r.generated = v.encode(a)?;
        }
        Ok(r)
    }
    #[allow(unused_unsafe)]
    unsafe fn decode(r: &Self::Raw) -> Result<Self> {
        unsafe {
            Ok(Self {
                name: string(r.name)?,
                pattern_name: if r.has_pattern_name != 0 {
                    Some(string(r.pattern_name)?)
                } else {
                    None
                },
                rectangles: slice(r.rectangles, r.rectangle_count)?
                    .iter()
                    .map(|v| ViaRectangle::decode(v))
                    .collect::<Result<Vec<_>>>()?,
                polygons: slice(r.polygons, r.polygon_count)?
                    .iter()
                    .map(|v| ViaPolygon::decode(v))
                    .collect::<Result<Vec<_>>>()?,
                generated: if r.has_generated != 0 {
                    Some(GeneratedViaData::decode(&r.generated)?)
                } else {
                    None
                },
            })
        }
    }
}
impl Codec for ImportDiagnostic {
    type Raw = ffi::eccdb_import_diagnostic_t;
    #[allow(unused_variables)]
    fn encode(&self, a: &mut Arena) -> Result<Self::Raw> {
        let r = Self::Raw {
            source: a.string(&self.source)?,
            statement: a.string(&self.statement)?,
            occurrence_count: self.occurrence_count,
        };
        Ok(r)
    }
    #[allow(unused_unsafe)]
    unsafe fn decode(r: &Self::Raw) -> Result<Self> {
        unsafe {
            Ok(Self {
                source: string(r.source)?,
                statement: string(r.statement)?,
                occurrence_count: r.occurrence_count,
            })
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::atomic::{AtomicUsize, Ordering};

    #[test]
    fn optional_zero_and_empty_values_are_not_absent() {
        let mut arena = Arena::default();
        let value = NetOptions {
            original: Some(String::new()),
            pattern: Some(NetPattern::None),
            estimated_capacitance: Some(0.0),
            frequency: Some(0.0),
            xtalk: Some(0),
            style: Some(0),
            voltage: Some(0),
            spacing_rules: vec![],
        };
        let raw = value.encode(&mut arena).unwrap();
        assert_eq!(unsafe { NetOptions::decode(&raw) }.unwrap(), value);
        let absent = NetOptions::default().encode(&mut arena).unwrap();
        assert_eq!(
            unsafe { NetOptions::decode(&absent) }.unwrap(),
            NetOptions::default()
        );
        let pin = IoPinData::default().encode(&mut arena).unwrap();
        assert_eq!(pin.net.value, u64::MAX);
        assert_eq!(pin.special_net.value, u64::MAX);
    }

    #[test]
    fn rejects_nul_invalid_utf8_and_unknown_enum() {
        let mut a = Arena::default();
        assert!(a.string("bad\0name").is_err());
        let invalid = CString::new(vec![0xff]).unwrap();
        let raw = ffi::eccdb_net_data_t {
            name: invalid.as_ptr().cast_mut(),
            ..Default::default()
        };
        assert_eq!(
            unsafe { NetData::decode(&raw) }.unwrap_err().kind,
            ErrorKind::InvalidUtf8
        );
        let name = CString::new("ok").unwrap();
        let raw = ffi::eccdb_net_data_t {
            name: name.as_ptr().cast_mut(),
            signal_use: 255,
            ..Default::default()
        };
        assert_eq!(
            unsafe { NetData::decode(&raw) }.unwrap_err().kind,
            ErrorKind::InvalidArgument
        );
        assert!(unsafe { slice::<u64>(std::ptr::null(), 1) }.is_err());
        assert!(
            unsafe { slice::<u64>(std::ptr::null(), 0) }
                .unwrap()
                .is_empty()
        );
    }

    #[test]
    fn guards_release_on_error_and_unwind() {
        static DROPS: AtomicUsize = AtomicUsize::new(0);
        unsafe extern "C" fn release(_: *mut u32) {
            DROPS.fetch_add(1, Ordering::SeqCst);
        }
        fn failed() -> Result<()> {
            let _output = Owned::<u32>::new(release);
            Err(Error::argument("conversion failed"))
        }
        assert!(failed().is_err());
        assert_eq!(DROPS.load(Ordering::SeqCst), 1);
        let panic = std::panic::catch_unwind(|| {
            let _output = Owned::<u32>::new(release);
            panic!("conversion panic");
        });
        assert!(panic.is_err());
        assert_eq!(DROPS.load(Ordering::SeqCst), 2);
    }

    #[test]
    fn input_pointers_survive_arena_growth_and_nested_arrays() {
        let mut a = Arena::default();
        let first = a.string("first").unwrap();
        for _ in 0..1000 {
            a.string("grow").unwrap();
            a.array(vec![1_u32, 2, 3]);
        }
        assert_eq!(unsafe { string(first) }.unwrap(), "first");
        let value = WireRoutingData {
            paths: vec![WirePathData {
                layer: RoutingLayerId::from_packed(7),
                width: Some(0),
                mask: Some(0),
                taper_rule: Some("wide".into()),
                shape: Some("STRIPE".into()),
                style: Some(0),
                points: vec![WirePoint {
                    position: Point { x: 2, y: 3 },
                    extension: Some(0),
                    virtual_point: true,
                }],
                vias: vec![
                    ViaPlacementData {
                        definition: ViaDefinitionId::Tech(TechViaId::from_packed(9)),
                        mask: Some(WireMask {
                            top: 1,
                            cut: 2,
                            bottom: 3,
                        }),
                        rows_columns: Some((2, 4)),
                        step_x: 5,
                        step_y: 6,
                        ..Default::default()
                    },
                    ViaPlacementData {
                        definition: ViaDefinitionId::Design(ViaId::from_packed(10)),
                        ..Default::default()
                    },
                ],
                rectangles: vec![WireRectangle {
                    point_index: 0,
                    delta: Rect {
                        ll_x: -1,
                        ll_y: -2,
                        ur_x: 3,
                        ur_y: 4,
                    },
                }],
                ..Default::default()
            }],
        };
        let raw = value.encode(&mut a).unwrap();
        assert_eq!(unsafe { WireRoutingData::decode(&raw) }.unwrap(), value);
    }
}
