#!/usr/bin/env python3
"""
MODULE:       p.out.pds4
AUTHOR:       Yann Chemin
PURPOSE:      Export a GRASS raster to a PDS4 GeoTIFF product (data file +
              companion XML label).  The label captures the planetary body's
              radii, the geographic/cartographic bounding box, and basic
              observational provenance read from the p_meta sidecar (if
              present).
LICENSE:      The Unlicense - public domain
"""

# %module
# % description: Export a GRASS raster map to a PDS4 GeoTIFF + companion XML label.
# % keyword: raster
# % keyword: export
# % keyword: PDS4
# % keyword: GeoTIFF
# % keyword: planetary
# % keyword: cartography
# %end

# %option G_OPT_R_INPUT
# % key: input
# % description: Input GRASS raster map to export
# % required: yes
# %end

# %option
# % key: output
# % type: string
# % description: Output file base path (without extension; .tif and .xml will be added)
# % required: yes
# %end

# %option
# % key: body
# % type: string
# % description: Target planetary body name (used for radii and CRS label)
# % required: no
# % answer: mars
# %end

# %option
# % key: title
# % type: string
# % description: Product title for the PDS4 label
# % required: no
# %end

# %option
# % key: lid
# % type: string
# % description: PDS4 Logical Identifier (LID) excluding urn:nasa:pds: prefix
# % required: no
# %end

# %option
# % key: type
# % type: string
# % description: GDAL output data type
# % required: no
# % answer: Float32
# % options: Byte,Int16,UInt16,Int32,Float32,Float64
# %end

import sys
import os
import json
import datetime
from pathlib import Path

import grass.script as gs


def _find_body_json(body):
    """Locate the body JSON — installed etc/planetary/ or dev tree."""
    candidates = []
    gisbase = os.environ.get("GISBASE", "")
    if gisbase:
        candidates.append(Path(gisbase) / "bodies" / f"{body}.json")
    candidates.append(Path(__file__).parent.parent.parent / "bodies" / f"{body}.json")
    for p in candidates:
        if p.exists():
            return p
    return None


def _load_body(body):
    p = _find_body_json(body.lower())
    if p is None:
        gs.fatal(f"Body '{body}' not found. Available JSON files checked in "
                 f"$GISBASE/bodies/ and dev tree.")
    with open(p) as f:
        return json.load(f)


def _read_sidecar(map_name):
    """Return p_meta sidecar dict for map_name, or {} if absent."""
    mapset = gs.gisenv()["MAPSET"]
    gisdbase = gs.gisenv()["GISDBASE"]
    location = gs.gisenv()["LOCATION_NAME"]
    sidecar = (Path(gisdbase) / location / mapset / "planetary" /
               f"{map_name}.json")
    if sidecar.exists():
        with open(sidecar) as f:
            return json.load(f)
    return {}


def _build_pds4_xml(geotiff_name, map_name, title, lid,
                    body_info, region, meta, dtype_str):
    """Return the PDS4 XML label as a string."""
    now = datetime.datetime.utcnow().strftime("%Y-%m-%dT%H:%M:%SZ")
    body_name = body_info.get("name", map_name)
    a_m = body_info.get("a", 3396190.0)
    b_m = body_info.get("b", a_m)

    west  = region["w"]
    east  = region["e"]
    north = region["n"]
    south = region["s"]
    nsres = region["nsres"]
    ewres = region["ewres"]

    if not title:
        title = f"{map_name} — {body_name} planetary map"
    if not lid:
        safe = map_name.replace(".", "_").replace(" ", "_").lower()
        lid = f"planetary_grass:data:{safe}"

    # Sensor/mission provenance from sidecar
    sensor = meta.get("sensor", "")
    mission = meta.get("mission", "")
    start_time = meta.get("start_time", "")
    stop_time = meta.get("stop_time", "")
    target = meta.get("target", body_name)

    obs_block = ""
    if mission or sensor or start_time:
        obs_parts = []
        if mission:
            obs_parts.append(f"      <Investigation_Area>\n"
                             f"        <name>{mission}</name>\n"
                             f"        <type>Mission</type>\n"
                             f"        <Internal_Reference>\n"
                             f"          <lid_reference>urn:nasa:pds:{mission.lower().replace(' ', '_')}</lid_reference>\n"
                             f"          <reference_type>ancillary_to_investigation</reference_type>\n"
                             f"        </Internal_Reference>\n"
                             f"      </Investigation_Area>")
        if sensor:
            obs_parts.append(f"      <Observing_System>\n"
                             f"        <Observing_System_Component>\n"
                             f"          <name>{sensor}</name>\n"
                             f"          <type>Instrument</type>\n"
                             f"        </Observing_System_Component>\n"
                             f"      </Observing_System>")
        time_parts = []
        if start_time:
            time_parts.append(f"        <start_date_time>{start_time}</start_date_time>")
        if stop_time:
            time_parts.append(f"        <stop_date_time>{stop_time}</stop_date_time>")
        if time_parts:
            obs_parts.append("      <Time_Coordinates>\n" +
                             "\n".join(time_parts) +
                             "\n      </Time_Coordinates>")
        if target:
            obs_parts.append(f"      <Target_Identification>\n"
                             f"        <name>{target.capitalize()}</name>\n"
                             f"        <type>Planet</type>\n"
                             f"      </Target_Identification>")
        obs_block = (
            "  <Context_Area>\n" +
            "\n".join(obs_parts) +
            "\n  </Context_Area>\n"
        )

    xml = f"""<?xml version="1.0" encoding="UTF-8"?>
<Product_Ancillary xmlns="http://pds.nasa.gov/pds4/pds/v1"
    xmlns:cart="http://pds.nasa.gov/pds4/cart/v1"
    xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance"
    xsi:schemaLocation="http://pds.nasa.gov/pds4/pds/v1
        https://pds.nasa.gov/pds4/pds/v1/PDS4_PDS_1L00.xsd
        http://pds.nasa.gov/pds4/cart/v1
        https://pds.nasa.gov/pds4/cart/v1/PDS4_CART_1L00_1970.xsd">
  <Identification_Area>
    <logical_identifier>urn:nasa:pds:{lid}</logical_identifier>
    <version_id>1.0</version_id>
    <title>{title}</title>
    <information_model_version>1.21.0.0</information_model_version>
    <product_class>Product_Ancillary</product_class>
    <Modification_History>
      <Modification_Detail>
        <modification_date>{now[:10]}</modification_date>
        <version_id>1.0</version_id>
        <description>Exported by GRASS Planetary Addons p.out.pds4</description>
      </Modification_Detail>
    </Modification_History>
  </Identification_Area>
{obs_block}  <File_Area_Ancillary>
    <File>
      <file_name>{geotiff_name}</file_name>
      <creation_date_time>{now}</creation_date_time>
    </File>
    <Encoded_Image>
      <name>{map_name}</name>
      <offset unit="byte">0</offset>
      <encoding_standard_id>GeoTIFF</encoding_standard_id>
      <description>GeoTIFF raster exported from GRASS GIS planetary location.
GDAL data type: {dtype_str}. NODATA = NaN (Float32/64) or -9999 (integer types).
Source map: {map_name}. Exported {now}.</description>
    </Encoded_Image>
  </File_Area_Ancillary>
  <cart:Cartography>
    <cart:Spatial_Domain>
      <cart:Bounding_Coordinates>
        <cart:west_bounding_coordinate unit="deg">{west:.6f}</cart:west_bounding_coordinate>
        <cart:east_bounding_coordinate unit="deg">{east:.6f}</cart:east_bounding_coordinate>
        <cart:north_bounding_coordinate unit="deg">{north:.6f}</cart:north_bounding_coordinate>
        <cart:south_bounding_coordinate unit="deg">{south:.6f}</cart:south_bounding_coordinate>
      </cart:Bounding_Coordinates>
    </cart:Spatial_Domain>
    <cart:Spatial_Reference_Information>
      <cart:Horizontal_Coordinate_System_Definition>
        <cart:Geographic>
          <cart:latitude_resolution unit="deg/pixel">{nsres:.8f}</cart:latitude_resolution>
          <cart:longitude_resolution unit="deg/pixel">{ewres:.8f}</cart:longitude_resolution>
          <cart:geographic_coordinate_system_name>{body_name.capitalize()} Geographic</cart:geographic_coordinate_system_name>
          <cart:longitude_direction>Positive East</cart:longitude_direction>
        </cart:Geographic>
        <cart:Geodetic_Model>
          <cart:latitude_type>Planetocentric</cart:latitude_type>
          <cart:a_axis_radius unit="m">{a_m:.3f}</cart:a_axis_radius>
          <cart:b_axis_radius unit="m">{a_m:.3f}</cart:b_axis_radius>
          <cart:c_axis_radius unit="m">{b_m:.3f}</cart:c_axis_radius>
          <cart:longitude_direction>Positive East</cart:longitude_direction>
        </cart:Geodetic_Model>
      </cart:Horizontal_Coordinate_System_Definition>
    </cart:Spatial_Reference_Information>
  </cart:Cartography>
</Product_Ancillary>
"""
    return xml


def main():
    options, flags = gs.parser()

    map_name  = options["input"]
    out_base  = options["output"]
    body      = options["body"]
    title     = options["title"]
    lid       = options["lid"]
    dtype_str = options["type"]

    geotiff_path = out_base if out_base.endswith(".tif") else out_base + ".tif"
    xml_path     = Path(geotiff_path).with_suffix(".xml")

    # Resolve body radii
    body_info = _load_body(body)

    # Build CRS PROJ string (spherical lat/lon using body equatorial radius)
    a_m = body_info.get("a", 3396190.0)
    b_m = body_info.get("b", a_m)
    proj4 = (f"+proj=longlat +a={a_m:.3f} +b={b_m:.3f} +no_defs")

    # Export GeoTIFF via r.out.gdal with GDAL CRS override after export
    nodata_val = "nan" if dtype_str in ("Float32", "Float64") else "-9999"
    gs.run_command("r.out.gdal",
                   input=map_name,
                   output=geotiff_path,
                   format="GTiff",
                   type=dtype_str,
                   nodata=nodata_val,
                   createopt="COMPRESS=DEFLATE,TILED=YES,BIGTIFF=IF_SAFER",
                   flags="f",
                   overwrite=True)

    # Overwrite the GeoTIFF's CRS with the planetary PROJ string using GDAL
    try:
        from osgeo import gdal, osr
        gdal.UseExceptions()
        ds = gdal.Open(geotiff_path, gdal.GA_Update)
        if ds is None:
            gs.warning("GDAL could not open output GeoTIFF to set planetary CRS.")
        else:
            srs = osr.SpatialReference()
            srs.ImportFromProj4(proj4)
            ds.SetProjection(srs.ExportToWkt())
            ds.FlushCache()
            ds = None
            gs.verbose(f"Set planetary CRS: {proj4}")
    except ImportError:
        gs.warning("python3-gdal not found; GeoTIFF CRS not set to planetary ellipsoid. "
                   "Install with: apt install python3-gdal")

    # Read GRASS region for label bounding box
    region = gs.region()

    # Read sidecar metadata
    meta = _read_sidecar(map_name)

    # Write PDS4 XML label
    geotiff_name = Path(geotiff_path).name
    xml_content = _build_pds4_xml(
        geotiff_name=geotiff_name,
        map_name=map_name,
        title=title,
        lid=lid,
        body_info=body_info,
        region=region,
        meta=meta,
        dtype_str=dtype_str,
    )
    xml_path.write_text(xml_content, encoding="utf-8")

    gs.message(f"GeoTIFF written: {geotiff_path}")
    gs.message(f"PDS4 label:      {xml_path}")


if __name__ == "__main__":
    main()
