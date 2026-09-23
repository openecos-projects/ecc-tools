use std::path::PathBuf;
/// A database-local ID. Packed IDs from another database must not be used here.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub struct NetId(pub(crate) u64);
impl NetId {
    pub const INVALID: Self = Self(u64::MAX);
    pub fn from_packed(value: u64) -> Self {
        Self(value)
    }
    pub fn packed(self) -> u64 {
        self.0
    }
    pub fn is_valid(self) -> bool {
        self.0 != u64::MAX
    }
}
impl Default for NetId {
    fn default() -> Self {
        Self::INVALID
    }
}
/// A database-local ID. Packed IDs from another database must not be used here.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub struct InstanceId(pub(crate) u64);
impl InstanceId {
    pub const INVALID: Self = Self(u64::MAX);
    pub fn from_packed(value: u64) -> Self {
        Self(value)
    }
    pub fn packed(self) -> u64 {
        self.0
    }
    pub fn is_valid(self) -> bool {
        self.0 != u64::MAX
    }
}
impl Default for InstanceId {
    fn default() -> Self {
        Self::INVALID
    }
}
/// A database-local ID. Packed IDs from another database must not be used here.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub struct InstancePinId(pub(crate) u64);
impl InstancePinId {
    pub const INVALID: Self = Self(u64::MAX);
    pub fn from_packed(value: u64) -> Self {
        Self(value)
    }
    pub fn packed(self) -> u64 {
        self.0
    }
    pub fn is_valid(self) -> bool {
        self.0 != u64::MAX
    }
}
impl Default for InstancePinId {
    fn default() -> Self {
        Self::INVALID
    }
}
/// A database-local ID. Packed IDs from another database must not be used here.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub struct IoPinId(pub(crate) u64);
impl IoPinId {
    pub const INVALID: Self = Self(u64::MAX);
    pub fn from_packed(value: u64) -> Self {
        Self(value)
    }
    pub fn packed(self) -> u64 {
        self.0
    }
    pub fn is_valid(self) -> bool {
        self.0 != u64::MAX
    }
}
impl Default for IoPinId {
    fn default() -> Self {
        Self::INVALID
    }
}
/// A database-local ID. Packed IDs from another database must not be used here.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub struct WireId(pub(crate) u64);
impl WireId {
    pub const INVALID: Self = Self(u64::MAX);
    pub fn from_packed(value: u64) -> Self {
        Self(value)
    }
    pub fn packed(self) -> u64 {
        self.0
    }
    pub fn is_valid(self) -> bool {
        self.0 != u64::MAX
    }
}
impl Default for WireId {
    fn default() -> Self {
        Self::INVALID
    }
}
/// A database-local ID. Packed IDs from another database must not be used here.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub struct ViaId(pub(crate) u64);
impl ViaId {
    pub const INVALID: Self = Self(u64::MAX);
    pub fn from_packed(value: u64) -> Self {
        Self(value)
    }
    pub fn packed(self) -> u64 {
        self.0
    }
    pub fn is_valid(self) -> bool {
        self.0 != u64::MAX
    }
}
impl Default for ViaId {
    fn default() -> Self {
        Self::INVALID
    }
}
/// A database-local ID. Packed IDs from another database must not be used here.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub struct LayerId(pub(crate) u64);
impl LayerId {
    pub const INVALID: Self = Self(u64::MAX);
    pub fn from_packed(value: u64) -> Self {
        Self(value)
    }
    pub fn packed(self) -> u64 {
        self.0
    }
    pub fn is_valid(self) -> bool {
        self.0 != u64::MAX
    }
}
impl Default for LayerId {
    fn default() -> Self {
        Self::INVALID
    }
}
/// A database-local ID. Packed IDs from another database must not be used here.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub struct RoutingLayerId(pub(crate) u64);
impl RoutingLayerId {
    pub const INVALID: Self = Self(u64::MAX);
    pub fn from_packed(value: u64) -> Self {
        Self(value)
    }
    pub fn packed(self) -> u64 {
        self.0
    }
    pub fn is_valid(self) -> bool {
        self.0 != u64::MAX
    }
}
impl Default for RoutingLayerId {
    fn default() -> Self {
        Self::INVALID
    }
}
/// A database-local ID. Packed IDs from another database must not be used here.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub struct CellMasterId(pub(crate) u64);
impl CellMasterId {
    pub const INVALID: Self = Self(u64::MAX);
    pub fn from_packed(value: u64) -> Self {
        Self(value)
    }
    pub fn packed(self) -> u64 {
        self.0
    }
    pub fn is_valid(self) -> bool {
        self.0 != u64::MAX
    }
}
impl Default for CellMasterId {
    fn default() -> Self {
        Self::INVALID
    }
}
/// A database-local ID. Packed IDs from another database must not be used here.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub struct MasterTermId(pub(crate) u64);
impl MasterTermId {
    pub const INVALID: Self = Self(u64::MAX);
    pub fn from_packed(value: u64) -> Self {
        Self(value)
    }
    pub fn packed(self) -> u64 {
        self.0
    }
    pub fn is_valid(self) -> bool {
        self.0 != u64::MAX
    }
}
impl Default for MasterTermId {
    fn default() -> Self {
        Self::INVALID
    }
}
/// A database-local ID. Packed IDs from another database must not be used here.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub struct TechRuleId(pub(crate) u64);
impl TechRuleId {
    pub const INVALID: Self = Self(u64::MAX);
    pub fn from_packed(value: u64) -> Self {
        Self(value)
    }
    pub fn packed(self) -> u64 {
        self.0
    }
    pub fn is_valid(self) -> bool {
        self.0 != u64::MAX
    }
}
impl Default for TechRuleId {
    fn default() -> Self {
        Self::INVALID
    }
}
/// A database-local ID. Packed IDs from another database must not be used here.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub struct DesignRuleId(pub(crate) u64);
impl DesignRuleId {
    pub const INVALID: Self = Self(u64::MAX);
    pub fn from_packed(value: u64) -> Self {
        Self(value)
    }
    pub fn packed(self) -> u64 {
        self.0
    }
    pub fn is_valid(self) -> bool {
        self.0 != u64::MAX
    }
}
impl Default for DesignRuleId {
    fn default() -> Self {
        Self::INVALID
    }
}
/// A database-local ID. Packed IDs from another database must not be used here.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub struct TechViaId(pub(crate) u64);
impl TechViaId {
    pub const INVALID: Self = Self(u64::MAX);
    pub fn from_packed(value: u64) -> Self {
        Self(value)
    }
    pub fn packed(self) -> u64 {
        self.0
    }
    pub fn is_valid(self) -> bool {
        self.0 != u64::MAX
    }
}
impl Default for TechViaId {
    fn default() -> Self {
        Self::INVALID
    }
}
/// A database-local ID. Packed IDs from another database must not be used here.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub struct ViaRuleId(pub(crate) u64);
impl ViaRuleId {
    pub const INVALID: Self = Self(u64::MAX);
    pub fn from_packed(value: u64) -> Self {
        Self(value)
    }
    pub fn packed(self) -> u64 {
        self.0
    }
    pub fn is_valid(self) -> bool {
        self.0 != u64::MAX
    }
}
impl Default for ViaRuleId {
    fn default() -> Self {
        Self::INVALID
    }
}
#[repr(u8)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum Orientation {
    #[default]
    N = 0,
    S = 1,
    E = 2,
    W = 3,
    FN = 4,
    FS = 5,
    FE = 6,
    FW = 7,
}
impl TryFrom<u8> for Orientation {
    type Error = crate::Error;
    fn try_from(v: u8) -> crate::Result<Self> {
        match v {
            0 => Ok(Self::N),
            1 => Ok(Self::S),
            2 => Ok(Self::E),
            3 => Ok(Self::W),
            4 => Ok(Self::FN),
            5 => Ok(Self::FS),
            6 => Ok(Self::FE),
            7 => Ok(Self::FW),
            _ => Err(crate::Error::argument(format!("invalid Orientation: {v}"))),
        }
    }
}
#[repr(u8)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum PlacementStatus {
    #[default]
    Unplaced = 0,
    Placed = 1,
    Fixed = 2,
    Cover = 3,
}
impl TryFrom<u8> for PlacementStatus {
    type Error = crate::Error;
    fn try_from(v: u8) -> crate::Result<Self> {
        match v {
            0 => Ok(Self::Unplaced),
            1 => Ok(Self::Placed),
            2 => Ok(Self::Fixed),
            3 => Ok(Self::Cover),
            _ => Err(crate::Error::argument(format!(
                "invalid PlacementStatus: {v}"
            ))),
        }
    }
}
#[repr(u8)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum SignalUse {
    #[default]
    None = 0,
    Signal = 1,
    Analog = 2,
    Power = 3,
    Ground = 4,
    Clock = 5,
    TieOff = 6,
    Scan = 7,
    Reset = 8,
}
impl TryFrom<u8> for SignalUse {
    type Error = crate::Error;
    fn try_from(v: u8) -> crate::Result<Self> {
        match v {
            0 => Ok(Self::None),
            1 => Ok(Self::Signal),
            2 => Ok(Self::Analog),
            3 => Ok(Self::Power),
            4 => Ok(Self::Ground),
            5 => Ok(Self::Clock),
            6 => Ok(Self::TieOff),
            7 => Ok(Self::Scan),
            8 => Ok(Self::Reset),
            _ => Err(crate::Error::argument(format!("invalid SignalUse: {v}"))),
        }
    }
}
#[repr(u8)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum NetSource {
    #[default]
    None = 0,
    Netlist = 1,
    Dist = 2,
    User = 3,
    Timing = 4,
    Test = 5,
}
impl TryFrom<u8> for NetSource {
    type Error = crate::Error;
    fn try_from(v: u8) -> crate::Result<Self> {
        match v {
            0 => Ok(Self::None),
            1 => Ok(Self::Netlist),
            2 => Ok(Self::Dist),
            3 => Ok(Self::User),
            4 => Ok(Self::Timing),
            5 => Ok(Self::Test),
            _ => Err(crate::Error::argument(format!("invalid NetSource: {v}"))),
        }
    }
}
#[repr(u8)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum InstanceSource {
    #[default]
    None = 0,
    Netlist = 1,
    Dist = 2,
    User = 3,
    Timing = 4,
}
impl TryFrom<u8> for InstanceSource {
    type Error = crate::Error;
    fn try_from(v: u8) -> crate::Result<Self> {
        match v {
            0 => Ok(Self::None),
            1 => Ok(Self::Netlist),
            2 => Ok(Self::Dist),
            3 => Ok(Self::User),
            4 => Ok(Self::Timing),
            _ => Err(crate::Error::argument(format!(
                "invalid InstanceSource: {v}"
            ))),
        }
    }
}
#[repr(u8)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum IoDirection {
    #[default]
    None = 0,
    Input = 1,
    Output = 2,
    InOut = 3,
    FeedThru = 4,
}
impl TryFrom<u8> for IoDirection {
    type Error = crate::Error;
    fn try_from(v: u8) -> crate::Result<Self> {
        match v {
            0 => Ok(Self::None),
            1 => Ok(Self::Input),
            2 => Ok(Self::Output),
            3 => Ok(Self::InOut),
            4 => Ok(Self::FeedThru),
            _ => Err(crate::Error::argument(format!("invalid IoDirection: {v}"))),
        }
    }
}
#[repr(u8)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum NetPattern {
    #[default]
    None = 0,
    Balanced = 1,
    Steiner = 2,
    Trunk = 3,
    WiredLogic = 4,
}
impl TryFrom<u8> for NetPattern {
    type Error = crate::Error;
    fn try_from(v: u8) -> crate::Result<Self> {
        match v {
            0 => Ok(Self::None),
            1 => Ok(Self::Balanced),
            2 => Ok(Self::Steiner),
            3 => Ok(Self::Trunk),
            4 => Ok(Self::WiredLogic),
            _ => Err(crate::Error::argument(format!("invalid NetPattern: {v}"))),
        }
    }
}
#[repr(u8)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum WireStatus {
    #[default]
    Routed = 0,
    Fixed = 1,
    Cover = 2,
    Shield = 3,
    NoShield = 4,
}
impl TryFrom<u8> for WireStatus {
    type Error = crate::Error;
    fn try_from(v: u8) -> crate::Result<Self> {
        match v {
            0 => Ok(Self::Routed),
            1 => Ok(Self::Fixed),
            2 => Ok(Self::Cover),
            3 => Ok(Self::Shield),
            4 => Ok(Self::NoShield),
            _ => Err(crate::Error::argument(format!("invalid WireStatus: {v}"))),
        }
    }
}
#[repr(u8)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum PolygonMode {
    #[default]
    Native = 0,
    Rectangularized = 1,
}
impl TryFrom<u8> for PolygonMode {
    type Error = crate::Error;
    fn try_from(v: u8) -> crate::Result<Self> {
        match v {
            0 => Ok(Self::Native),
            1 => Ok(Self::Rectangularized),
            _ => Err(crate::Error::argument(format!("invalid PolygonMode: {v}"))),
        }
    }
}
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct BinaryFiles {
    pub technology: PathBuf,
    pub library: PathBuf,
    pub design: PathBuf,
}
impl BinaryFiles {
    pub fn new(
        technology: impl Into<PathBuf>,
        library: impl Into<PathBuf>,
        design: impl Into<PathBuf>,
    ) -> Self {
        Self {
            technology: technology.into(),
            library: library.into(),
            design: design.into(),
        }
    }
}
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum DatabaseInput {
    Binary(BinaryFiles),
    LefDef {
        lef_files: Vec<PathBuf>,
        def_file: PathBuf,
    },
}
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Config {
    pub input: DatabaseInput,
    pub polygon_mode: PolygonMode,
    pub binary_cache: Option<BinaryFiles>,
}
impl Config {
    pub fn new(input: DatabaseInput) -> Self {
        Self {
            input,
            polygon_mode: PolygonMode::Native,
            binary_cache: None,
        }
    }
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ViaDefinitionId {
    Tech(TechViaId),
    Design(ViaId),
}
impl Default for ViaDefinitionId {
    fn default() -> Self {
        Self::Tech(TechViaId::INVALID)
    }
}
#[derive(Clone, Debug, Default, PartialEq)]
pub struct Point {
    pub x: i32,
    pub y: i32,
}
#[derive(Clone, Debug, Default, PartialEq)]
pub struct Rect {
    pub ll_x: i32,
    pub ll_y: i32,
    pub ur_x: i32,
    pub ur_y: i32,
}
#[derive(Clone, Debug, Default, PartialEq)]
pub struct NetData {
    pub name: String,
    pub signal_use: SignalUse,
    pub source: NetSource,
    pub weight: Option<i32>,
    pub tech_non_default_rule: Option<TechRuleId>,
    pub design_non_default_rule: Option<DesignRuleId>,
}
#[derive(Clone, Debug, Default, PartialEq)]
pub struct InstanceData {
    pub name: String,
    pub master: CellMasterId,
    pub origin: Point,
    pub orientation: Orientation,
    pub placement_status: PlacementStatus,
    pub source: InstanceSource,
}
#[derive(Clone, Debug, Default, PartialEq)]
pub struct InstancePinData {
    pub instance: InstanceId,
    pub master_term: MasterTermId,
    pub net: Option<NetId>,
    pub special_net: Option<NetId>,
}
#[derive(Clone, Debug, Default, PartialEq)]
pub struct IoPinData {
    pub name: String,
    pub direction: IoDirection,
    pub signal_use: SignalUse,
    pub net: Option<NetId>,
    pub special_net: Option<NetId>,
}
#[derive(Clone, Debug, Default, PartialEq)]
pub struct NetSpacingRule {
    pub layer: RoutingLayerId,
    pub spacing: i32,
    pub range: Option<(i32, i32)>,
}
#[derive(Clone, Debug, Default, PartialEq)]
pub struct NetOptions {
    pub original: Option<String>,
    pub pattern: Option<NetPattern>,
    pub estimated_capacitance: Option<f64>,
    pub frequency: Option<f64>,
    pub xtalk: Option<i32>,
    pub style: Option<i32>,
    pub voltage: Option<i32>,
    pub spacing_rules: Vec<NetSpacingRule>,
}
#[derive(Clone, Debug, Default, PartialEq)]
pub struct WirePoint {
    pub position: Point,
    pub extension: Option<i32>,
    pub virtual_point: bool,
}
#[derive(Clone, Debug, Default, PartialEq)]
pub struct WireMask {
    pub top: u32,
    pub cut: u32,
    pub bottom: u32,
}
#[derive(Clone, Debug, Default, PartialEq)]
pub struct ViaPlacementData {
    pub point_index: u32,
    pub definition: ViaDefinitionId,
    pub orientation: Orientation,
    pub mask: Option<WireMask>,
    pub rows_columns: Option<(u32, u32)>,
    pub step_x: i32,
    pub step_y: i32,
}
#[derive(Clone, Debug, Default, PartialEq)]
pub struct WireRectangle {
    pub point_index: u32,
    pub delta: Rect,
}
#[derive(Clone, Debug, Default, PartialEq)]
pub struct WireMetadata {
    pub net: NetId,
    pub status: WireStatus,
    pub shield_net: String,
}
#[derive(Clone, Debug, Default, PartialEq)]
pub struct WirePathData {
    pub layer: RoutingLayerId,
    pub width: Option<i32>,
    pub mask: Option<u32>,
    pub taper: bool,
    pub taper_rule: Option<String>,
    pub shape: Option<String>,
    pub style: Option<i32>,
    pub points: Vec<WirePoint>,
    pub vias: Vec<ViaPlacementData>,
    pub rectangles: Vec<WireRectangle>,
}
#[derive(Clone, Debug, Default, PartialEq)]
pub struct WireRoutingData {
    pub paths: Vec<WirePathData>,
}
#[derive(Clone, Debug, Default, PartialEq)]
pub struct ViaRectangle {
    pub layer: LayerId,
    pub rectangle: Rect,
    pub mask: u32,
}
#[derive(Clone, Debug, Default, PartialEq)]
pub struct ViaPolygon {
    pub layer: LayerId,
    pub points: Vec<Point>,
    pub mask: u32,
}
#[derive(Clone, Debug, Default, PartialEq)]
pub struct GeneratedViaData {
    pub via_rule: ViaRuleId,
    pub bottom_layer: RoutingLayerId,
    pub cut_layer: LayerId,
    pub top_layer: RoutingLayerId,
    pub cut_size_x: i32,
    pub cut_size_y: i32,
    pub cut_spacing_x: i32,
    pub cut_spacing_y: i32,
    pub bottom_enclosure_x: i32,
    pub bottom_enclosure_y: i32,
    pub top_enclosure_x: i32,
    pub top_enclosure_y: i32,
    pub row_column: Option<(u32, u32)>,
    pub origin: Option<Point>,
    pub offsets: Option<(Point, Point)>,
    pub cut_pattern: Option<String>,
}
#[derive(Clone, Debug, Default, PartialEq)]
pub struct DesignViaData {
    pub name: String,
    pub pattern_name: Option<String>,
    pub rectangles: Vec<ViaRectangle>,
    pub polygons: Vec<ViaPolygon>,
    pub generated: Option<GeneratedViaData>,
}
#[derive(Clone, Debug, Default, PartialEq)]
pub struct ImportDiagnostic {
    pub source: String,
    pub statement: String,
    pub occurrence_count: usize,
}
