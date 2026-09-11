from ecc_tools_bin import ecc_py


def test_pyplacedb_exposes_m2_pg_rail_geometry_fields():
    assert hasattr(ecc_py.PyPlaceDB, "m2_pg_rail_blockage_rects")
    assert hasattr(ecc_py.PyPlaceDB, "m2_pg_rail_boxes")
    assert hasattr(ecc_py.PyPlaceDB, "m2_pg_rail_density_boxes")
    assert hasattr(ecc_py.PyPlaceDB, "total_fixed_node_area")
    assert hasattr(ecc_py.PyPlaceDB, "total_space_area")


def test_pydb_keeps_rail_collection_arguments_optional():
    signature = ecc_py.pydb.__doc__.splitlines()[0]
    assert "include_m2_pg_rail_blockage: bool = False" in signature
    assert "include_m2_pg_rail_density: bool = True" in signature
