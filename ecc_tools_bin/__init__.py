"""Runtime preparation for the native ECC extension."""

import sys


# libglog, which is a transitive dependency of ecc_py on Linux, must be loaded
# after matplotlib's pybind extension.  Loading matplotlib first prevents its
# module-level ``__getattr__`` exception handling from being interrupted by the
# native logging runtime.  Consumers still import ecc_py directly:
# ``from ecc_tools_bin import ecc_py``.
if sys.platform.startswith("linux"):
    import matplotlib.ft2font  # noqa: F401
