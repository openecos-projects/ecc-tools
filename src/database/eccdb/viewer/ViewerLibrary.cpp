// The GUI-facing service is compiled into eccdb_service.  This target keeps
// a small shared-library boundary for native clients that load the viewer
// facade independently of the Python extension.
namespace eccdb {
void eccdb_viewer_link_anchor() {}
}  // namespace eccdb
