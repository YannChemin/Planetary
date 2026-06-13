"""
p_meta.py — planetary map metadata (JSON sidecar).

Writes/reads ``planetary.json`` alongside each GRASS map created by a p.in.*
module, using a schema that is a strict superset of the i.hyper JSON schema
(i_hyper_lib/hyper_meta.py).  Fields shared with i.hyper use identical key
names so that future i.hyper tools can consume planetary data without a
translation layer.

Storage locations (mirrors i.hyper conventions):
  2-D raster   →  $MAPSET/cell_misc/<name>/planetary.json
  3-D raster   →  $MAPSET/grid3/<name>/planetary.json

Schema compatibility with i.hyper 1.0
--------------------------------------
All top-level i.hyper keys that are present here carry the same semantics:
  schema_version, dataset_id, derived, data_type, sensor,
  wavelength_units, radiometric_quantity, radiometric_units,
  acquisition_datetime, bands (count/count_valid/wavelength/fwhm/validity),
  processing_history, extended_metadata.

Planetary-specific fields live under extended_metadata.planetary:
  body, mission, pds_product_id, source_file, spice_kernels.
"""

from __future__ import annotations

import json
import uuid
from dataclasses import dataclass, field
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

import grass.script as gs

SCHEMA_VERSION = "1.0"
METADATA_FILENAME = "planetary.json"


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _mapset_path() -> Path:
    env = gs.gisenv()
    return Path(env["GISDBASE"]) / env["LOCATION_NAME"] / env["MAPSET"]


def _meta_path(map_name: str, map_type: str = "raster") -> Path:
    """Return the Path where planetary.json should be stored."""
    base = _mapset_path()
    if map_type == "raster3d":
        return base / "grid3" / map_name / METADATA_FILENAME
    return base / "cell_misc" / map_name / METADATA_FILENAME


def _now_iso() -> str:
    return datetime.now(timezone.utc).isoformat()


# ---------------------------------------------------------------------------
# Data class
# ---------------------------------------------------------------------------

@dataclass
class PlanetaryMetadata:
    """
    Metadata container for a single GRASS map produced by a p.in.* module.

    Compatible with the i.hyper HyperMetadata JSON schema so that downstream
    GRASS imagery tools can read shared fields (sensor, radiometric_quantity,
    bands.wavelength, bands.fwhm …) without being aware of the planetary
    extensions stored under extended_metadata.planetary.
    """

    # --- i.hyper-compatible top-level fields ---
    schema_version: str = SCHEMA_VERSION
    dataset_id: str = field(default_factory=lambda: uuid.uuid4().hex)
    derived: bool = False
    data_type: str = "image"          # image|dem|radiance|reflectance|thermal|ancillary|rings|component
    sensor: str | None = None         # instrument name, e.g. "CASSINI_ISS_NAC"
    wavelength_units: str = "nm"
    radiometric_quantity: str | None = None   # raw_dn|toa_radiance|surface_reflectance|elevation|…
    radiometric_units: str | None = None      # DN|W/(m^2 sr um)|m|…
    acquisition_datetime: str | None = None   # ISO-8601 UTC

    # --- band metadata (i.hyper-compatible) ---
    n_bands: int = 1
    wavelengths: list[float] | None = None
    fwhm: list[float] | None = None
    validity: list[bool] | None = None

    # --- planetary-specific (stored under extended_metadata.planetary) ---
    body: str | None = None           # target body: MOON, MARS, SATURN, …
    mission: str | None = None        # Cassini, LRO, MRO, …
    pds_product_id: str | None = None
    source_file: str | None = None
    spice_kernels: list[str] | None = None
    projection: str | None = None     # ring-plane projection mode: radlong|polar

    # --- provenance ---
    processing_history: list[dict[str, Any]] = field(default_factory=list)
    extended_metadata: dict[str, Any] = field(default_factory=dict)

    # ------------------------------------------------------------------
    # Factory
    # ------------------------------------------------------------------

    @classmethod
    def new(cls, **kwargs) -> "PlanetaryMetadata":
        """Create a fresh PlanetaryMetadata; keyword args override defaults."""
        meta = cls()
        for k, v in kwargs.items():
            if hasattr(meta, k):
                setattr(meta, k, v)
        return meta

    # ------------------------------------------------------------------
    # Processing history
    # ------------------------------------------------------------------

    def add_history_entry(
        self,
        command: str,
        *,
        timestamp: str | None = None,
    ) -> None:
        self.processing_history.append({
            "command": command,
            "timestamp": timestamp or _now_iso(),
            "inputs": [],
            "outputs": [],
        })

    # ------------------------------------------------------------------
    # Serialisation helpers
    # ------------------------------------------------------------------

    def _build_planetary_block(self) -> dict[str, Any]:
        block: dict[str, Any] = {}
        if self.body:
            block["body"] = self.body
        if self.mission:
            block["mission"] = self.mission
        if self.pds_product_id:
            block["pds_product_id"] = self.pds_product_id
        if self.source_file:
            block["source_file"] = self.source_file
        if self.spice_kernels:
            block["spice_kernels"] = list(self.spice_kernels)
        if self.projection:
            block["projection"] = self.projection
        return block

    def _build_bands(self) -> dict[str, Any]:
        n = self.n_bands or 1
        valid = self.validity if self.validity is not None else [True] * n
        bands: dict[str, Any] = {
            "count": n,
            "count_valid": int(sum(bool(v) for v in valid)),
            "validity": [bool(v) for v in valid],
        }
        if self.wavelengths is not None:
            bands["wavelength"] = list(self.wavelengths)
        if self.fwhm is not None:
            bands["fwhm"] = list(self.fwhm)
        return bands

    def to_dict(self) -> dict[str, Any]:
        ext = dict(self.extended_metadata)
        planetary = self._build_planetary_block()
        if planetary:
            ext.setdefault("planetary", {}).update(planetary)
        return {
            "schema_version": self.schema_version,
            "dataset_id": self.dataset_id,
            "derived": bool(self.derived),
            "data_type": self.data_type,
            "sensor": self.sensor,
            "wavelength_units": self.wavelength_units,
            "radiometric_quantity": self.radiometric_quantity,
            "radiometric_units": self.radiometric_units,
            "acquisition_datetime": self.acquisition_datetime,
            "bands": self._build_bands(),
            "processing_history": list(self.processing_history),
            "extended_metadata": ext,
        }

    # ------------------------------------------------------------------
    # Save / load / exists
    # ------------------------------------------------------------------

    def save(self, map_name: str, map_type: str = "raster") -> None:
        """Write planetary.json into the map's cell_misc (or grid3) directory."""
        path = _meta_path(map_name, map_type)
        if not path.parent.exists():
            gs.warning(
                f"p_meta: directory '{path.parent}' does not exist; "
                f"planetary.json not written for '{map_name}'."
            )
            return
        with open(path, "w") as fh:
            json.dump(self.to_dict(), fh, indent=2)
        gs.verbose(f"p_meta: wrote {path}")

    @classmethod
    def load(cls, map_name: str, map_type: str = "raster") -> "PlanetaryMetadata":
        path = _meta_path(map_name, map_type)
        if not path.exists():
            raise FileNotFoundError(
                f"planetary.json not found for map '{map_name}' at '{path}'."
            )
        with open(path) as fh:
            data = json.load(fh)
        return cls._from_dict(data)

    @classmethod
    def exists(cls, map_name: str, map_type: str = "raster") -> bool:
        return _meta_path(map_name, map_type).exists()

    @classmethod
    def _from_dict(cls, data: dict[str, Any]) -> "PlanetaryMetadata":
        meta = cls()
        meta.schema_version = data.get("schema_version", SCHEMA_VERSION)
        meta.dataset_id = data.get("dataset_id") or uuid.uuid4().hex
        meta.derived = bool(data.get("derived", False))
        meta.data_type = data.get("data_type", "image")
        meta.sensor = data.get("sensor")
        meta.wavelength_units = data.get("wavelength_units", "nm")
        meta.radiometric_quantity = data.get("radiometric_quantity")
        meta.radiometric_units = data.get("radiometric_units")
        meta.acquisition_datetime = data.get("acquisition_datetime")
        meta.processing_history = data.get("processing_history") or []
        meta.extended_metadata = data.get("extended_metadata") or {}

        bands = data.get("bands") or {}
        meta.n_bands = bands.get("count", 1)
        meta.wavelengths = bands.get("wavelength")
        meta.fwhm = bands.get("fwhm")
        meta.validity = bands.get("validity")

        planetary = meta.extended_metadata.get("planetary") or {}
        meta.body = planetary.get("body")
        meta.mission = planetary.get("mission")
        meta.pds_product_id = planetary.get("pds_product_id")
        meta.source_file = planetary.get("source_file")
        meta.spice_kernels = planetary.get("spice_kernels")
        meta.projection = planetary.get("projection")
        return meta


# ---------------------------------------------------------------------------
# Convenience function (the one p.in.* modules call)
# ---------------------------------------------------------------------------

def write_planetary_metadata(
    map_name: str,
    *,
    map_type: str = "raster",
    module: str = "p.in",
    command: str | None = None,
    **kwargs,
) -> None:
    """
    Create and save planetary.json for *map_name*.

    Parameters
    ----------
    map_name     : GRASS map name (no @mapset)
    map_type     : "raster" (default) or "raster3d"
    module       : calling module name, used in processing_history
    command      : full command string for the history entry (optional)
    **kwargs     : any PlanetaryMetadata field: data_type, sensor, body,
                   mission, radiometric_quantity, radiometric_units,
                   acquisition_datetime, wavelengths, fwhm, n_bands,
                   pds_product_id, source_file, spice_kernels, …

    If planetary.json already exists for *map_name*, the existing file is
    left intact (this function only writes on first creation).
    """
    if PlanetaryMetadata.exists(map_name, map_type):
        gs.verbose(f"p_meta: planetary.json already exists for '{map_name}', skipping.")
        return
    meta = PlanetaryMetadata.new(**kwargs)
    cmd = command or module
    meta.add_history_entry(cmd)
    try:
        meta.save(map_name, map_type)
    except Exception as exc:
        gs.warning(f"p_meta: could not write planetary.json for '{map_name}': {exc}")
