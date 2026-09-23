use crate::{
    Error, Result,
    convert::{Arena, Codec, Owned, slice},
    error::check,
    ffi,
    types::*,
};
use std::{marker::PhantomData, path::Path, ptr::NonNull, rc::Rc};
/// Owns one ECCDB database. Mutations require exclusive access.
/// This handle is intentionally neither Send nor Sync.
///
/// ```compile_fail
/// fn require_send<T: Send>() {}
/// require_send::<eccdb::Database>();
/// ```
/// ```compile_fail
/// fn require_sync<T: Sync>() {}
/// require_sync::<eccdb::Database>();
/// ```
/// ```compile_fail
/// fn wrong_id(db: &eccdb::Database, id: eccdb::NetId) {
///     db.instance(id).unwrap();
/// }
/// ```
pub struct Database {
    raw: NonNull<ffi::eccdb_database_t>,
    _thread: PhantomData<Rc<()>>,
}
impl Drop for Database {
    fn drop(&mut self) {
        unsafe { ffi::eccdb_database_free(self.raw.as_ptr()) }
    }
}
fn files(v: &BinaryFiles, a: &mut Arena) -> Result<ffi::eccdb_binary_files_t> {
    Ok(ffi::eccdb_binary_files_t {
        technology: a.path(&v.technology)?,
        library: a.path(&v.library)?,
        design: a.path(&v.design)?,
    })
}
impl Database {
    pub fn supports_lef_def() -> Result<bool> {
        let mut v = 0;
        check(unsafe { ffi::eccdb_supports_lef_def(&mut v) })?;
        Ok(v != 0)
    }
    pub fn open(config: &Config) -> Result<Self> {
        let mut a = Arena::default();
        let mut raw = ffi::eccdb_config_t {
            polygon_mode: config.polygon_mode as u8,
            ..Default::default()
        };
        match &config.input {
            DatabaseInput::Binary(v) => {
                raw.input_kind = 1;
                raw.binary = files(v, &mut a)?;
            }
            DatabaseInput::LefDef {
                lef_files,
                def_file,
            } => {
                let paths = lef_files
                    .iter()
                    .map(|v| a.path(v))
                    .collect::<Result<Vec<_>>>()?;
                raw.lef_count = paths.len();
                raw.lef_files = a.array(paths);
                raw.def_file = a.path(def_file)?;
            }
        }
        if let Some(cache) = &config.binary_cache {
            raw.has_binary_cache = 1;
            raw.binary_cache = files(cache, &mut a)?;
        }
        let mut ptr = std::ptr::null_mut();
        let result = check(unsafe { ffi::eccdb_open(&raw, &mut ptr) });
        if let Err(e) = result {
            if !ptr.is_null() {
                unsafe { ffi::eccdb_database_free(ptr) }
            }
            return Err(e);
        }
        Ok(Self {
            raw: NonNull::new(ptr).ok_or_else(|| Error::argument("native open returned null"))?,
            _thread: PhantomData,
        })
    }
    pub fn open_binary(files: BinaryFiles) -> Result<Self> {
        Self::open(&Config::new(DatabaseInput::Binary(files)))
    }
    pub fn open_lef_def(
        lef_files: Vec<std::path::PathBuf>,
        def_file: impl Into<std::path::PathBuf>,
    ) -> Result<Self> {
        Self::open(&Config::new(DatabaseInput::LefDef {
            lef_files,
            def_file: def_file.into(),
        }))
    }
    pub fn write_binary(&self, output: &BinaryFiles) -> Result<()> {
        let mut a = Arena::default();
        let v = files(output, &mut a)?;
        check(unsafe {
            ffi::eccdb_write_binary(self.raw.as_ptr(), v.technology, v.library, v.design)
        })
    }
    pub fn write_def(&self, path: impl AsRef<Path>) -> Result<()> {
        let mut a = Arena::default();
        let p = a.path(path.as_ref())?;
        check(unsafe { ffi::eccdb_write_def(self.raw.as_ptr(), p) })
    }
    pub fn contains_net(&self, id: NetId) -> Result<bool> {
        let mut v = 0;
        check(unsafe {
            ffi::eccdb_net_contains(
                self.raw.as_ptr(),
                ffi::eccdb_net_id_t { value: id.0 },
                &mut v,
            )
        })?;
        Ok(v != 0)
    }
    pub fn destroy_net(&mut self, id: NetId) -> Result<bool> {
        let mut v = 0;
        check(unsafe {
            ffi::eccdb_net_destroy(
                self.raw.as_ptr(),
                ffi::eccdb_net_id_t { value: id.0 },
                &mut v,
            )
        })?;
        Ok(v != 0)
    }
    pub fn contains_instance(&self, id: InstanceId) -> Result<bool> {
        let mut v = 0;
        check(unsafe {
            ffi::eccdb_instance_contains(
                self.raw.as_ptr(),
                ffi::eccdb_instance_id_t { value: id.0 },
                &mut v,
            )
        })?;
        Ok(v != 0)
    }
    pub fn destroy_instance(&mut self, id: InstanceId) -> Result<bool> {
        let mut v = 0;
        check(unsafe {
            ffi::eccdb_instance_destroy(
                self.raw.as_ptr(),
                ffi::eccdb_instance_id_t { value: id.0 },
                &mut v,
            )
        })?;
        Ok(v != 0)
    }
    pub fn contains_instance_pin(&self, id: InstancePinId) -> Result<bool> {
        let mut v = 0;
        check(unsafe {
            ffi::eccdb_instance_pin_contains(
                self.raw.as_ptr(),
                ffi::eccdb_instance_pin_id_t { value: id.0 },
                &mut v,
            )
        })?;
        Ok(v != 0)
    }
    pub fn contains_io_pin(&self, id: IoPinId) -> Result<bool> {
        let mut v = 0;
        check(unsafe {
            ffi::eccdb_io_pin_contains(
                self.raw.as_ptr(),
                ffi::eccdb_io_pin_id_t { value: id.0 },
                &mut v,
            )
        })?;
        Ok(v != 0)
    }
    pub fn destroy_io_pin(&mut self, id: IoPinId) -> Result<bool> {
        let mut v = 0;
        check(unsafe {
            ffi::eccdb_io_pin_destroy(
                self.raw.as_ptr(),
                ffi::eccdb_io_pin_id_t { value: id.0 },
                &mut v,
            )
        })?;
        Ok(v != 0)
    }
    pub fn contains_wire(&self, id: WireId) -> Result<bool> {
        let mut v = 0;
        check(unsafe {
            ffi::eccdb_wire_contains(
                self.raw.as_ptr(),
                ffi::eccdb_wire_id_t { value: id.0 },
                &mut v,
            )
        })?;
        Ok(v != 0)
    }
    pub fn destroy_wire(&mut self, id: WireId) -> Result<bool> {
        let mut v = 0;
        check(unsafe {
            ffi::eccdb_wire_destroy(
                self.raw.as_ptr(),
                ffi::eccdb_wire_id_t { value: id.0 },
                &mut v,
            )
        })?;
        Ok(v != 0)
    }
    pub fn contains_design_via(&self, id: ViaId) -> Result<bool> {
        let mut v = 0;
        check(unsafe {
            ffi::eccdb_design_via_contains(
                self.raw.as_ptr(),
                ffi::eccdb_via_id_t { value: id.0 },
                &mut v,
            )
        })?;
        Ok(v != 0)
    }
    pub fn destroy_design_via(&mut self, id: ViaId) -> Result<bool> {
        let mut v = 0;
        check(unsafe {
            ffi::eccdb_design_via_destroy(
                self.raw.as_ptr(),
                ffi::eccdb_via_id_t { value: id.0 },
                &mut v,
            )
        })?;
        Ok(v != 0)
    }
    pub fn find_net(&self, name: &str) -> Result<Option<NetId>> {
        let mut a = Arena::default();
        let n = a.string(name)?;
        let mut id = ffi::eccdb_net_id_t::default();
        let mut found = 0;
        check(unsafe { ffi::eccdb_net_find(self.raw.as_ptr(), n, &mut id, &mut found) })?;
        Ok((found != 0).then_some(NetId(id.value)))
    }
    pub fn find_regular_net(&self, name: &str) -> Result<Option<NetId>> {
        let mut a = Arena::default();
        let n = a.string(name)?;
        let mut id = ffi::eccdb_net_id_t::default();
        let mut found = 0;
        check(unsafe { ffi::eccdb_net_find_regular(self.raw.as_ptr(), n, &mut id, &mut found) })?;
        Ok((found != 0).then_some(NetId(id.value)))
    }
    pub fn find_special_net(&self, name: &str) -> Result<Option<NetId>> {
        let mut a = Arena::default();
        let n = a.string(name)?;
        let mut id = ffi::eccdb_net_id_t::default();
        let mut found = 0;
        check(unsafe { ffi::eccdb_net_find_special(self.raw.as_ptr(), n, &mut id, &mut found) })?;
        Ok((found != 0).then_some(NetId(id.value)))
    }
    pub fn find_instance(&self, name: &str) -> Result<Option<InstanceId>> {
        let mut a = Arena::default();
        let n = a.string(name)?;
        let mut id = ffi::eccdb_instance_id_t::default();
        let mut found = 0;
        check(unsafe { ffi::eccdb_instance_find(self.raw.as_ptr(), n, &mut id, &mut found) })?;
        Ok((found != 0).then_some(InstanceId(id.value)))
    }
    pub fn find_io_pin(&self, name: &str) -> Result<Option<IoPinId>> {
        let mut a = Arena::default();
        let n = a.string(name)?;
        let mut id = ffi::eccdb_io_pin_id_t::default();
        let mut found = 0;
        check(unsafe { ffi::eccdb_io_pin_find(self.raw.as_ptr(), n, &mut id, &mut found) })?;
        Ok((found != 0).then_some(IoPinId(id.value)))
    }
    pub fn find_design_via(&self, name: &str) -> Result<Option<ViaId>> {
        let mut a = Arena::default();
        let n = a.string(name)?;
        let mut id = ffi::eccdb_via_id_t::default();
        let mut found = 0;
        check(unsafe { ffi::eccdb_design_via_find(self.raw.as_ptr(), n, &mut id, &mut found) })?;
        Ok((found != 0).then_some(ViaId(id.value)))
    }
    pub fn net_ids(&self) -> Result<Vec<NetId>> {
        let mut out = Owned::new(ffi::eccdb_net_id_list_free);
        check(unsafe { ffi::eccdb_net_ids(self.raw.as_ptr(), &mut out.raw) })?;
        Ok(unsafe { slice(out.raw.items, out.raw.count) }?
            .iter()
            .map(|x| NetId(x.value))
            .collect())
    }
    pub fn regular_net_ids(&self) -> Result<Vec<NetId>> {
        let mut out = Owned::new(ffi::eccdb_net_id_list_free);
        check(unsafe { ffi::eccdb_net_regular_ids(self.raw.as_ptr(), &mut out.raw) })?;
        Ok(unsafe { slice(out.raw.items, out.raw.count) }?
            .iter()
            .map(|x| NetId(x.value))
            .collect())
    }
    pub fn special_net_ids(&self) -> Result<Vec<NetId>> {
        let mut out = Owned::new(ffi::eccdb_net_id_list_free);
        check(unsafe { ffi::eccdb_net_special_ids(self.raw.as_ptr(), &mut out.raw) })?;
        Ok(unsafe { slice(out.raw.items, out.raw.count) }?
            .iter()
            .map(|x| NetId(x.value))
            .collect())
    }
    pub fn instance_ids(&self) -> Result<Vec<InstanceId>> {
        let mut out = Owned::new(ffi::eccdb_instance_id_list_free);
        check(unsafe { ffi::eccdb_instance_ids(self.raw.as_ptr(), &mut out.raw) })?;
        Ok(unsafe { slice(out.raw.items, out.raw.count) }?
            .iter()
            .map(|x| InstanceId(x.value))
            .collect())
    }
    pub fn io_pin_ids(&self) -> Result<Vec<IoPinId>> {
        let mut out = Owned::new(ffi::eccdb_io_pin_id_list_free);
        check(unsafe { ffi::eccdb_io_pin_ids(self.raw.as_ptr(), &mut out.raw) })?;
        Ok(unsafe { slice(out.raw.items, out.raw.count) }?
            .iter()
            .map(|x| IoPinId(x.value))
            .collect())
    }
    pub fn wire_ids(&self) -> Result<Vec<WireId>> {
        let mut out = Owned::new(ffi::eccdb_wire_id_list_free);
        check(unsafe { ffi::eccdb_wire_ids(self.raw.as_ptr(), &mut out.raw) })?;
        Ok(unsafe { slice(out.raw.items, out.raw.count) }?
            .iter()
            .map(|x| WireId(x.value))
            .collect())
    }
    pub fn design_via_ids(&self) -> Result<Vec<ViaId>> {
        let mut out = Owned::new(ffi::eccdb_via_id_list_free);
        check(unsafe { ffi::eccdb_design_via_ids(self.raw.as_ptr(), &mut out.raw) })?;
        Ok(unsafe { slice(out.raw.items, out.raw.count) }?
            .iter()
            .map(|x| ViaId(x.value))
            .collect())
    }
    pub fn instance_pin_ids(&self, instance: InstanceId) -> Result<Vec<InstancePinId>> {
        let mut out = Owned::new(ffi::eccdb_instance_pin_id_list_free);
        check(unsafe {
            ffi::eccdb_instance_pin_ids(
                self.raw.as_ptr(),
                ffi::eccdb_instance_id_t { value: instance.0 },
                &mut out.raw,
            )
        })?;
        Ok(unsafe { slice(out.raw.items, out.raw.count) }?
            .iter()
            .map(|x| InstancePinId(x.value))
            .collect())
    }
    pub fn net_wire_ids(&self, net: NetId) -> Result<Vec<WireId>> {
        let mut out = Owned::new(ffi::eccdb_wire_id_list_free);
        check(unsafe {
            ffi::eccdb_net_wire_ids(
                self.raw.as_ptr(),
                ffi::eccdb_net_id_t { value: net.0 },
                &mut out.raw,
            )
        })?;
        Ok(unsafe { slice(out.raw.items, out.raw.count) }?
            .iter()
            .map(|x| WireId(x.value))
            .collect())
    }
    pub fn create_net(&mut self, value: &NetData) -> Result<NetId> {
        let mut a = Arena::default();
        let v = value.encode(&mut a)?;
        let mut id = ffi::eccdb_net_id_t::default();
        check(unsafe { ffi::eccdb_net_create(self.raw.as_ptr(), &v, &mut id) })?;
        Ok(NetId(id.value))
    }
    pub fn update_net(&mut self, id: NetId, value: &NetData) -> Result<()> {
        let mut a = Arena::default();
        let v = value.encode(&mut a)?;
        check(unsafe {
            ffi::eccdb_net_update(self.raw.as_ptr(), ffi::eccdb_net_id_t { value: id.0 }, &v)
        })
    }
    pub fn create_instance(&mut self, value: &InstanceData) -> Result<InstanceId> {
        let mut a = Arena::default();
        let v = value.encode(&mut a)?;
        let mut id = ffi::eccdb_instance_id_t::default();
        check(unsafe { ffi::eccdb_instance_create(self.raw.as_ptr(), &v, &mut id) })?;
        Ok(InstanceId(id.value))
    }
    pub fn update_instance(&mut self, id: InstanceId, value: &InstanceData) -> Result<()> {
        let mut a = Arena::default();
        let v = value.encode(&mut a)?;
        check(unsafe {
            ffi::eccdb_instance_update(
                self.raw.as_ptr(),
                ffi::eccdb_instance_id_t { value: id.0 },
                &v,
            )
        })
    }
    pub fn create_io_pin(&mut self, value: &IoPinData) -> Result<IoPinId> {
        let mut a = Arena::default();
        let v = value.encode(&mut a)?;
        let mut id = ffi::eccdb_io_pin_id_t::default();
        check(unsafe { ffi::eccdb_io_pin_create(self.raw.as_ptr(), &v, &mut id) })?;
        Ok(IoPinId(id.value))
    }
    pub fn update_io_pin(&mut self, id: IoPinId, value: &IoPinData) -> Result<()> {
        let mut a = Arena::default();
        let v = value.encode(&mut a)?;
        check(unsafe {
            ffi::eccdb_io_pin_update(
                self.raw.as_ptr(),
                ffi::eccdb_io_pin_id_t { value: id.0 },
                &v,
            )
        })
    }
    pub fn create_design_via(&mut self, value: &DesignViaData) -> Result<ViaId> {
        let mut a = Arena::default();
        let v = value.encode(&mut a)?;
        let mut id = ffi::eccdb_via_id_t::default();
        check(unsafe { ffi::eccdb_design_via_create(self.raw.as_ptr(), &v, &mut id) })?;
        Ok(ViaId(id.value))
    }
    pub fn update_design_via(&mut self, id: ViaId, value: &DesignViaData) -> Result<()> {
        let mut a = Arena::default();
        let v = value.encode(&mut a)?;
        check(unsafe {
            ffi::eccdb_design_via_update(self.raw.as_ptr(), ffi::eccdb_via_id_t { value: id.0 }, &v)
        })
    }
    pub fn net(&self, id: NetId) -> Result<Option<NetData>> {
        let mut out = Owned::new(ffi::eccdb_net_data_free);
        let mut found = 0;
        check(unsafe {
            ffi::eccdb_net_get(
                self.raw.as_ptr(),
                ffi::eccdb_net_id_t { value: id.0 },
                &mut out.raw,
                &mut found,
            )
        })?;
        if found == 0 {
            return Ok(None);
        }
        Ok(Some(unsafe { NetData::decode(&out.raw) }?))
    }
    pub fn instance(&self, id: InstanceId) -> Result<Option<InstanceData>> {
        let mut out = Owned::new(ffi::eccdb_instance_data_free);
        let mut found = 0;
        check(unsafe {
            ffi::eccdb_instance_get(
                self.raw.as_ptr(),
                ffi::eccdb_instance_id_t { value: id.0 },
                &mut out.raw,
                &mut found,
            )
        })?;
        if found == 0 {
            return Ok(None);
        }
        Ok(Some(unsafe { InstanceData::decode(&out.raw) }?))
    }
    pub fn io_pin(&self, id: IoPinId) -> Result<Option<IoPinData>> {
        let mut out = Owned::new(ffi::eccdb_io_pin_data_free);
        let mut found = 0;
        check(unsafe {
            ffi::eccdb_io_pin_get(
                self.raw.as_ptr(),
                ffi::eccdb_io_pin_id_t { value: id.0 },
                &mut out.raw,
                &mut found,
            )
        })?;
        if found == 0 {
            return Ok(None);
        }
        Ok(Some(unsafe { IoPinData::decode(&out.raw) }?))
    }
    pub fn design_via(&self, id: ViaId) -> Result<Option<DesignViaData>> {
        let mut out = Owned::new(ffi::eccdb_design_via_data_free);
        let mut found = 0;
        check(unsafe {
            ffi::eccdb_design_via_get(
                self.raw.as_ptr(),
                ffi::eccdb_via_id_t { value: id.0 },
                &mut out.raw,
                &mut found,
            )
        })?;
        if found == 0 {
            return Ok(None);
        }
        Ok(Some(unsafe { DesignViaData::decode(&out.raw) }?))
    }
    pub fn net_options(&self, id: NetId) -> Result<Option<NetOptions>> {
        let mut out = Owned::new(ffi::eccdb_net_options_free);
        let mut found = 0;
        check(unsafe {
            ffi::eccdb_net_options_get(
                self.raw.as_ptr(),
                ffi::eccdb_net_id_t { value: id.0 },
                &mut out.raw,
                &mut found,
            )
        })?;
        if found == 0 {
            return Ok(None);
        }
        Ok(Some(unsafe { NetOptions::decode(&out.raw) }?))
    }
    pub fn wire_metadata(&self, id: WireId) -> Result<Option<WireMetadata>> {
        let mut out = Owned::new(ffi::eccdb_wire_metadata_free);
        let mut found = 0;
        check(unsafe {
            ffi::eccdb_wire_metadata_get(
                self.raw.as_ptr(),
                ffi::eccdb_wire_id_t { value: id.0 },
                &mut out.raw,
                &mut found,
            )
        })?;
        if found == 0 {
            return Ok(None);
        }
        Ok(Some(unsafe { WireMetadata::decode(&out.raw) }?))
    }
    pub fn wire_routing(&self, id: WireId) -> Result<Option<WireRoutingData>> {
        let mut out = Owned::new(ffi::eccdb_wire_routing_free);
        let mut found = 0;
        check(unsafe {
            ffi::eccdb_wire_routing_get(
                self.raw.as_ptr(),
                ffi::eccdb_wire_id_t { value: id.0 },
                &mut out.raw,
                &mut found,
            )
        })?;
        if found == 0 {
            return Ok(None);
        }
        Ok(Some(unsafe { WireRoutingData::decode(&out.raw) }?))
    }
    pub fn create_special_net(&mut self, value: &NetData) -> Result<NetId> {
        let mut a = Arena::default();
        let v = value.encode(&mut a)?;
        let mut id = ffi::eccdb_net_id_t::default();
        check(unsafe { ffi::eccdb_net_create_special(self.raw.as_ptr(), &v, &mut id) })?;
        Ok(NetId(id.value))
    }
    pub fn is_special_net(&self, id: NetId) -> Result<bool> {
        let mut v = 0;
        check(unsafe {
            ffi::eccdb_net_is_special(
                self.raw.as_ptr(),
                ffi::eccdb_net_id_t { value: id.0 },
                &mut v,
            )
        })?;
        Ok(v != 0)
    }
    pub fn set_net_options(&mut self, id: NetId, value: &NetOptions) -> Result<()> {
        let mut a = Arena::default();
        let v = value.encode(&mut a)?;
        check(unsafe {
            ffi::eccdb_net_options_set(self.raw.as_ptr(), ffi::eccdb_net_id_t { value: id.0 }, &v)
        })
    }
    pub fn net_pin_ids(&self, id: NetId) -> Result<(Vec<InstancePinId>, Vec<IoPinId>)> {
        let mut inst = Owned::new(ffi::eccdb_instance_pin_id_list_free);
        let mut io = Owned::new(ffi::eccdb_io_pin_id_list_free);
        check(unsafe {
            ffi::eccdb_net_pin_ids(
                self.raw.as_ptr(),
                ffi::eccdb_net_id_t { value: id.0 },
                &mut inst.raw,
                &mut io.raw,
            )
        })?;
        unsafe {
            Ok((
                slice(inst.raw.items, inst.raw.count)?
                    .iter()
                    .map(|x| InstancePinId(x.value))
                    .collect(),
                slice(io.raw.items, io.raw.count)?
                    .iter()
                    .map(|x| IoPinId(x.value))
                    .collect(),
            ))
        }
    }
    pub fn find_instance_pin(
        &self,
        instance: InstanceId,
        name: &str,
    ) -> Result<Option<InstancePinId>> {
        let mut a = Arena::default();
        let n = a.string(name)?;
        let mut id = ffi::eccdb_instance_pin_id_t::default();
        let mut found = 0;
        check(unsafe {
            ffi::eccdb_instance_pin_find(
                self.raw.as_ptr(),
                ffi::eccdb_instance_id_t { value: instance.0 },
                n,
                &mut id,
                &mut found,
            )
        })?;
        Ok((found != 0).then_some(InstancePinId(id.value)))
    }
    pub fn instance_pin(&self, id: InstancePinId) -> Result<Option<InstancePinData>> {
        let mut v = ffi::eccdb_instance_pin_data_t::default();
        let mut found = 0;
        check(unsafe {
            ffi::eccdb_instance_pin_get(
                self.raw.as_ptr(),
                ffi::eccdb_instance_pin_id_t { value: id.0 },
                &mut v,
                &mut found,
            )
        })?;
        if found == 0 {
            Ok(None)
        } else {
            Ok(Some(unsafe { InstancePinData::decode(&v) }?))
        }
    }
    pub fn connect_instance_pin(&mut self, pin: InstancePinId, net: NetId) -> Result<()> {
        check(unsafe {
            ffi::eccdb_instance_pin_connect(
                self.raw.as_ptr(),
                ffi::eccdb_instance_pin_id_t { value: pin.0 },
                ffi::eccdb_net_id_t { value: net.0 },
            )
        })
    }
    pub fn disconnect_instance_pin(&mut self, pin: InstancePinId) -> Result<()> {
        check(unsafe {
            ffi::eccdb_instance_pin_disconnect(
                self.raw.as_ptr(),
                ffi::eccdb_instance_pin_id_t { value: pin.0 },
            )
        })
    }
    pub fn disconnect_instance_pin_from_net(
        &mut self,
        pin: InstancePinId,
        net: NetId,
    ) -> Result<()> {
        check(unsafe {
            ffi::eccdb_instance_pin_disconnect_from_net(
                self.raw.as_ptr(),
                ffi::eccdb_instance_pin_id_t { value: pin.0 },
                ffi::eccdb_net_id_t { value: net.0 },
            )
        })
    }
    pub fn connect_io_pin(&mut self, pin: IoPinId, net: NetId) -> Result<()> {
        check(unsafe {
            ffi::eccdb_io_pin_connect(
                self.raw.as_ptr(),
                ffi::eccdb_io_pin_id_t { value: pin.0 },
                ffi::eccdb_net_id_t { value: net.0 },
            )
        })
    }
    pub fn disconnect_io_pin(&mut self, pin: IoPinId) -> Result<()> {
        check(unsafe {
            ffi::eccdb_io_pin_disconnect(self.raw.as_ptr(), ffi::eccdb_io_pin_id_t { value: pin.0 })
        })
    }
    pub fn disconnect_io_pin_from_net(&mut self, pin: IoPinId, net: NetId) -> Result<()> {
        check(unsafe {
            ffi::eccdb_io_pin_disconnect_from_net(
                self.raw.as_ptr(),
                ffi::eccdb_io_pin_id_t { value: pin.0 },
                ffi::eccdb_net_id_t { value: net.0 },
            )
        })
    }
    pub fn create_wire(
        &mut self,
        metadata: &WireMetadata,
        routing: &WireRoutingData,
    ) -> Result<WireId> {
        let mut a = Arena::default();
        let m = metadata.encode(&mut a)?;
        let r = routing.encode(&mut a)?;
        let mut id = ffi::eccdb_wire_id_t::default();
        check(unsafe { ffi::eccdb_wire_create(self.raw.as_ptr(), &m, &r, &mut id) })?;
        Ok(WireId(id.value))
    }
    pub fn update_wire(
        &mut self,
        id: WireId,
        metadata: &WireMetadata,
        routing: &WireRoutingData,
    ) -> Result<()> {
        let mut a = Arena::default();
        let m = metadata.encode(&mut a)?;
        let r = routing.encode(&mut a)?;
        check(unsafe {
            ffi::eccdb_wire_update(
                self.raw.as_ptr(),
                ffi::eccdb_wire_id_t { value: id.0 },
                &m,
                &r,
            )
        })
    }
    pub fn diagnostics(&self) -> Result<Vec<ImportDiagnostic>> {
        struct Guard {
            ptr: *mut ffi::eccdb_import_diagnostic_t,
            count: usize,
        }
        impl Drop for Guard {
            fn drop(&mut self) {
                unsafe { ffi::eccdb_diagnostics_free(self.ptr, self.count) }
            }
        }
        let mut g = Guard {
            ptr: std::ptr::null_mut(),
            count: 0,
        };
        check(unsafe { ffi::eccdb_diagnostics(self.raw.as_ptr(), &mut g.ptr, &mut g.count) })?;
        unsafe {
            slice(g.ptr, g.count)?
                .iter()
                .map(|x| ImportDiagnostic::decode(x))
                .collect()
        }
    }
}
