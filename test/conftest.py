import pytest
from support import TestRoots


@pytest.fixture(scope="session")
def test_roots() -> TestRoots:
    return TestRoots.from_environment()
