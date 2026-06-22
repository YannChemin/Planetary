/*!
 * \file p_meta.h
 * \brief Planetary map metadata — C API.
 *
 * Writes \c planetary.json alongside each GRASS raster or 3-D raster map
 * created by a \c p.in.* C module.  The JSON schema is a strict superset of
 * the i.hyper \c hyper_meta.py schema (GRASS grass-addons), so future GRASS
 * imagery tools can consume shared fields without awareness of the planetary
 * extensions stored under \c extended_metadata.planetary.
 *
 * Storage locations (mirrors i.hyper conventions):
 *   2-D raster : \c $MAPSET/cell_misc/<name>/planetary.json
 *   3-D raster : \c $MAPSET/grid3/<name>/planetary.json
 *
 * Compatible i.hyper top-level keys (identical names and semantics):
 *   schema_version, dataset_id, derived, data_type, sensor,
 *   wavelength_units, radiometric_quantity, radiometric_units,
 *   acquisition_datetime, bands.{count,count_valid,wavelength,fwhm,validity},
 *   processing_history, extended_metadata.
 *
 * Planetary-specific fields live under \c extended_metadata.planetary:
 *   body, mission, pds_product_id, source_file.
 *
 * No external JSON library is required; the file is hand-serialised.
 *
 * \author Yann Chemin
 * \copyright The Unlicense (public domain)
 */

#ifndef P_META_H
#define P_META_H

#ifdef __cplusplus
extern "C" {
#endif

/*! \brief Opaque metadata container. */
typedef struct PMeta PMeta;

/* ------------------------------------------------------------------
 * Lifecycle
 * ------------------------------------------------------------------ */

/*!
 * \brief Allocate a new, zeroed PMeta.
 * \return Heap-allocated PMeta; free with p_meta_free().
 */
PMeta *p_meta_new(void);

/*!
 * \brief Free a PMeta and all owned strings / arrays.
 */
void p_meta_free(PMeta *m);

/* ------------------------------------------------------------------
 * Setters  (all strings are deep-copied)
 * ------------------------------------------------------------------ */

void p_meta_set_data_type(PMeta *m, const char *data_type);
void p_meta_set_sensor(PMeta *m, const char *sensor);
void p_meta_set_mission(PMeta *m, const char *mission);
void p_meta_set_body(PMeta *m, const char *body);
void p_meta_set_acquisition_datetime(PMeta *m, const char *dt);
void p_meta_set_radiometric_quantity(PMeta *m, const char *q);
void p_meta_set_radiometric_units(PMeta *m, const char *u);
void p_meta_set_n_bands(PMeta *m, int n);
void p_meta_set_source_file(PMeta *m, const char *path);
void p_meta_set_pds_product_id(PMeta *m, const char *id);

/*!
 * \brief Set the command string recorded in processing_history.
 *
 * Typically built from \c argv[] by the caller.
 */
void p_meta_set_command(PMeta *m, const char *cmd);

/*!
 * \brief Set per-band centre wavelengths (nm).
 *
 * \param wl  array of \p n doubles; copied internally.
 * \param n   length of \p wl.
 */
void p_meta_set_wavelengths(PMeta *m, const double *wl, int n);

/*!
 * \brief Set per-band FWHM values (nm).
 *
 * \param fwhm  array of \p n doubles; copied internally.
 * \param n     length of \p fwhm.
 */
void p_meta_set_fwhm(PMeta *m, const double *fwhm, int n);

/* ------------------------------------------------------------------
 * Write
 * ------------------------------------------------------------------ */

/*!
 * \brief Write \c planetary.json for a 2-D raster map.
 *
 * Writes to \c $GISDBASE/$LOCATION/$MAPSET/cell_misc/<mapname>/planetary.json.
 * Does nothing (and emits a G_warning) if the \c cell_misc directory
 * does not exist (map not yet created).
 * Does nothing silently if the file already exists (first-write wins).
 *
 * \param m       populated PMeta
 * \param mapname GRASS raster map name (no \c @mapset suffix)
 * \return 0 on success, -1 on error
 */
int p_meta_write(PMeta *m, const char *mapname);

/*!
 * \brief Write \c planetary.json for a 3-D raster map (raster3d / grid3).
 *
 * Writes to \c $GISDBASE/$LOCATION/$MAPSET/grid3/<mapname>/planetary.json.
 *
 * \param m       populated PMeta
 * \param mapname GRASS 3-D raster map name
 * \return 0 on success, -1 on error
 */
int p_meta_write_3d(PMeta *m, const char *mapname);

/*!
 * \brief Read a single top-level string field from a map's planetary.json.
 *
 * A minimal, targeted scanner for exactly this purpose (e.g. reading
 * back \c sensor written by \c p_meta_write()) -- not a general JSON
 * parser. Looks in the *current* mapset, same convention as
 * \c p_meta_write()/p_meta_write_3d().
 *
 * \param mapname  GRASS map name (no \c @mapset suffix)
 * \param map_type "raster" (default; pass NULL) or "raster3d"
 * \param field    top-level JSON key name (e.g. "sensor")
 * \param out      output buffer (caller-allocated)
 * \param outlen   size of \p out
 * \return 0 on success (value copied into \p out), -1 if the file or
 *         field doesn't exist
 */
int p_meta_read_string_field(const char *mapname, const char *map_type,
                              const char *field, char *out, int outlen);

/*!
 * \brief Install \c matter_bands.json into the current mapset's \c Misc/ directory.
 *
 * Copies \c $GISBASE/etc/planetary/matter_bands.json to
 * \c $GISDBASE/$LOCATION/$MAPSET/Misc/matter_bands.json so that
 * \c p.matter.bands can resolve the band database without needing a
 * system-wide installation path.
 *
 * - Creates \c Misc/ if it does not already exist.
 * - First-write wins: an existing file is never overwritten, making the
 *   call idempotent and safe to repeat for every imported band.
 * - If the source file is absent (e.g. development build not yet installed),
 *   the function returns 0 silently rather than raising an error.
 *
 * Called automatically by \c p_meta_write() and \c p_meta_write_3d(), so
 * any \c p.in.* module that uses \c p_meta already installs the database
 * on first import without any extra code.
 *
 * \return 0 on success or if the source is not available, -1 on I/O error
 */
int p_meta_install_matter_bands(void);

#ifdef __cplusplus
}
#endif

#endif /* P_META_H */
