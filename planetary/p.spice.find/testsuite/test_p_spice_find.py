"""
Testsuite for p.spice.find.

Offline tests: parsing and selection logic, no network.
Network tests: live NAIF directory listing + selection (marked with
  @pytest.mark.network — skipped unless -m network is passed or the
  NAIF server is reachable).

Run offline only:
    python -m pytest p.spice.find/testsuite/test_p_spice_find.py -v

Run all including network:
    python -m pytest p.spice.find/testsuite/test_p_spice_find.py -v -m network
"""

import datetime
import re
import sys
import os
import urllib.error

import pytest

# Minimal grass shim for offline testing (must add both parent and child)
import types
_grass_pkg = types.ModuleType("grass")
gs = types.ModuleType("grass.script")
_grass_pkg.script = gs
gs.message = lambda x: None
gs.warning = lambda x: None
gs.fatal = lambda x: (_ for _ in ()).throw(SystemExit(x))
gs.verbose = lambda x: None
gs.percent = lambda *a: None
gs.parser = lambda: ({}, {})
sys.modules["grass"] = _grass_pkg
sys.modules["grass.script"] = gs
_p_spice_shim = types.ModuleType("p_spice")
_p_spice_shim.mapset_spice_dir = lambda: "/tmp/test_spice"
sys.modules["p_spice"] = _p_spice_shim

import importlib.util
_spec = importlib.util.spec_from_file_location(
    "p_spice_find",
    os.path.join(os.path.dirname(__file__), "..", "p.spice.find.py"),
)
_mod = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_mod)


def test_yydoy_ck():
    r = _mod._file_date_range("04183_04185ra.bc")
    assert r == (datetime.date(2004, 7, 1), datetime.date(2004, 7, 3))


def test_yymmdd_ck():
    r = _mod._file_date_range("040514_040730pe_port3_soi.bc")
    assert r == (datetime.date(2004, 5, 14), datetime.date(2004, 7, 30))


def test_spk_scpse():
    r = _mod._file_date_range("040701AP_SCPSE_04173_04236.bsp")
    assert r == (datetime.date(2004, 6, 21), datetime.date(2004, 8, 23))


def test_coverage_false():
    r = _mod._file_date_range("04153_04182ca_ISS.bc")
    target = datetime.date(2004, 7, 1)
    assert r is not None
    assert not (r[0] <= target <= r[1])


def test_best_ck_selects_shortest_ra():
    files = [
        "04183_04185ra.bc",        # span 2d, ra → should win
        "04180_04191ra.bc",        # span 11d, ra
        "04183_04213ca_ISS.bc",    # span 30d, ca
        "04183_04183ra_drpc_scale_factor.bc",  # skipped by _CK_SKIP
    ]
    target = datetime.date(2004, 7, 1)
    best = _mod._best_ck(files, target, "ra")
    assert best == "04183_04185ra.bc"


def test_best_spk_prefers_scpse():
    files = [
        "040629AP_SCPSE_04179_04185.bsp",  # 7d SCPSE → should win
        "040701AP_SCPSE_04173_04236.bsp",  # 63d SCPSE
    ]
    target = datetime.date(2004, 7, 1)
    best = _mod._best_spk(files, target)
    assert best == "040629AP_SCPSE_04179_04185.bsp"


def test_latest_file_prefix():
    files = ["cas_v40.tf", "cas_v41.tf", "cas_v43.tf", "cas_dyn_v03.tf"]
    result = _mod._latest_file(files, ".tf", "cas_v*")
    assert result == "cas_v43.tf"


def test_latest_file_exact():
    files = ["naif0012.tls", "naif0011.tls"]
    result = _mod._latest_file(files, ".tls", "naif0012.tls")
    assert result == "naif0012.tls"


# ---------------------------------------------------------------------------
# Network integration tests — require live NAIF server.
# Run with:  pytest ... -m network
# ---------------------------------------------------------------------------

def _naif_reachable():
    try:
        import urllib.request
        urllib.request.urlopen(
            "https://naif.jpl.nasa.gov/pub/naif/CASSINI/kernels/ck/",
            timeout=10,
        )
        return True
    except Exception:
        return False


@pytest.mark.network
@pytest.mark.skipif(not _naif_reachable(), reason="NAIF server not reachable")
class TestNetworkCassiniSOI:
    """Verify live kernel selection for Cassini SOI (2004-07-01T03:11:40)."""

    TARGET = datetime.date(2004, 7, 1)
    NAIF = "https://naif.jpl.nasa.gov/pub/naif"

    def _ls(self, path):
        return _mod._list_dir(f"{self.NAIF}/{path}", timeout=30)

    def test_ck_selects_ra_covering_soi(self):
        files = self._ls("CASSINI/kernels/ck/")
        best = _mod._best_ck(files, self.TARGET, "ra")
        assert best is not None, "No CK found for 2004-07-01"
        r = _mod._file_date_range(best)
        assert r[0] <= self.TARGET <= r[1], f"{best} does not cover {self.TARGET}"
        # Must be reconstructed-actual (ra suffix on second token)
        stem = best.rsplit(".", 1)[0].split("_")
        m = _mod._RE_CK_TYPE.match(stem[1])
        assert m and m.group(2).lower().startswith("ra"), \
            f"Expected ra-type CK, got: {best}"

    def test_spk_selects_scpse_covering_soi(self):
        files = self._ls("CASSINI/kernels/spk/")
        best = _mod._best_spk(files, self.TARGET)
        assert best is not None, "No SPK found for 2004-07-01"
        r = _mod._file_date_range(best)
        assert r[0] <= self.TARGET <= r[1], f"{best} does not cover {self.TARGET}"
        assert "SCPSE" in best.upper(), f"Expected SCPSE SPK, got: {best}"

    def test_lsk_naif0012_present(self):
        files = self._ls("generic_kernels/lsk/")
        result = _mod._latest_file(files, ".tls", "naif0012.tls")
        assert result == "naif0012.tls"

    def test_sclk_cas00172_present(self):
        files = self._ls("CASSINI/kernels/sclk/")
        result = _mod._latest_file(files, ".tsc", "cas00172.tsc")
        assert result == "cas00172.tsc"

    def test_ik_cas_iss_present(self):
        files = self._ls("CASSINI/kernels/ik/")
        result = _mod._latest_file(files, ".ti", "cas_iss_v10.ti")
        assert result == "cas_iss_v10.ti"

    def test_fk_cas_v_latest(self):
        files = self._ls("CASSINI/kernels/fk/")
        result = _mod._latest_file(files, ".tf", "cas_v*")
        assert result is not None
        assert result.startswith("cas_v") and result.endswith(".tf")
