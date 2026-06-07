/*!
 * \file p_pds.h
 *
 * \brief Planetary library - PDS3/PDS4 label and image I/O.
 *
 * Provides a minimal, dependency-free C API for reading PDS3 IMAGE and
 * QUBE products and querying their PVL (Parameter Value Language) labels.
 * PDS4 support reads the XML label and locates the referenced binary data
 * file.  The library requires only GRASS GIS (for G_malloc / G_fatal_error)
 * and standard C99.
 *
 * Supported PDS3 SAMPLE_TYPE values
 * ----------------------------------
 *  MSB_UNSIGNED_INTEGER  (8, 16, 32-bit)
 *  LSB_UNSIGNED_INTEGER  (8, 16, 32-bit)
 *  MSB_INTEGER           (8, 16, 32-bit)
 *  LSB_INTEGER           (8, 16, 32-bit)
 *  IEEE_REAL             (32-bit = float, 64-bit = double)
 *  PC_REAL               (LSB 32 or 64-bit float)
 *  UNSIGNED_INTEGER      (alias MSB_UNSIGNED_INTEGER)
 *  SIGNED_INTEGER        (alias MSB_INTEGER)
 *
 * Supported BAND_STORAGE_TYPE / organisation
 * ------------------------------------------
 *  BAND_SEQUENTIAL   (BSQ)
 *  LINE_INTERLEAVED  (BIL)
 *  SAMPLE_INTERLEAVED (BIP)
 *
 * (C) 2026 by the GRASS Development Team
 *
 * (>=v2).  Read the file COPYING that comes with GRASS for details.
 *
 * \author Yann Chemin - dr.yann.chemin@gmail.com
 */

#ifndef GRASS_P_PDS_H
#define GRASS_P_PDS_H

#include <stddef.h>  /* size_t */

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* PVL node types                                                       */
/* ------------------------------------------------------------------ */

/*! \brief PVL token types returned by the parser. */
typedef enum {
    P_PVL_SCALAR  = 0, /*!< keyword = value              */
    P_PVL_OBJECT  = 1, /*!< OBJECT = name … END_OBJECT   */
    P_PVL_GROUP   = 2  /*!< GROUP  = name … END_GROUP    */
} PPvlType;

/*! \brief A single PVL node (keyword or container). */
typedef struct PPvlNode {
    PPvlType  type;          /*!< scalar, object, or group                 */
    char     *key;           /*!< keyword name (upper-case, heap-owned)     */
    char     *value;         /*!< raw value string (heap-owned, may be NULL for containers) */
    char     *unit;          /*!< unit string from <…> (NULL if absent)     */

    struct PPvlNode *children; /*!< first child (for OBJECT/GROUP)          */
    struct PPvlNode *next;     /*!< next sibling                             */
} PPvlNode;

/* ------------------------------------------------------------------ */
/* Pixel / storage types                                               */
/* ------------------------------------------------------------------ */

/*! \brief Native data type of the image pixels on disk. */
typedef enum {
    P_PDS_DTYPE_UNKNOWN  = 0,
    P_PDS_DTYPE_UINT8    = 1,  /*!< 8-bit unsigned byte                    */
    P_PDS_DTYPE_INT16    = 2,  /*!< 16-bit signed integer                  */
    P_PDS_DTYPE_UINT16   = 3,  /*!< 16-bit unsigned integer                */
    P_PDS_DTYPE_INT32    = 4,  /*!< 32-bit signed integer                  */
    P_PDS_DTYPE_UINT32   = 5,  /*!< 32-bit unsigned integer                */
    P_PDS_DTYPE_FLOAT32  = 6,  /*!< 32-bit IEEE float                      */
    P_PDS_DTYPE_FLOAT64  = 7   /*!< 64-bit IEEE double                     */
} PPdsDataType;

/*! \brief Band interleave organisation on disk. */
typedef enum {
    P_PDS_ORG_BSQ = 0, /*!< band-sequential (default)                     */
    P_PDS_ORG_BIL = 1, /*!< band-interleaved-by-line                      */
    P_PDS_ORG_BIP = 2  /*!< band-interleaved-by-pixel                     */
} PPdsOrganization;

/* ------------------------------------------------------------------ */
/* Main image-label descriptor                                         */
/* ------------------------------------------------------------------ */

/*!
 * \brief Complete descriptor of one PDS image object.
 *
 * Filled by p_pds_open_image().  The caller reads pixel data via
 * p_pds_read_row() or p_pds_read_band().  Call p_pds_close() when done.
 */
typedef struct PPdsImage {
    /* --- dimensions ------------------------------------------------- */
    int lines;          /*!< number of image rows (LINES)                  */
    int samples;        /*!< number of image columns (LINE_SAMPLES)        */
    int bands;          /*!< number of spectral bands (BANDS, default 1)   */

    /* --- pixel encoding --------------------------------------------- */
    PPdsDataType dtype;         /*!< data type on disk                     */
    int          bytes_per_pixel; /*!< bytes per sample (SAMPLE_BITS / 8) */
    int          is_msb;          /*!< 1 = big-endian on disk              */
    double       offset;          /*!< OFFSET (additive, applied first)    */
    double       scaling_factor;  /*!< SCALING_FACTOR (multiplicative)     */

    /* --- special pixel DN values ------------------------------------ */
    double dn_null;       /*!< CORE_NULL (or NULL_CONSTANT)               */
    double dn_lrs;        /*!< CORE_LOW_REPR_SATURATION                   */
    double dn_lis;        /*!< CORE_LOW_INSTR_SATURATION                  */
    double dn_hrs;        /*!< CORE_HIGH_REPR_SATURATION                  */
    double dn_his;        /*!< CORE_HIGH_INSTR_SATURATION                 */

    /* --- layout ----------------------------------------------------- */
    PPdsOrganization organization; /*!< BSQ / BIL / BIP                   */
    long             data_offset;  /*!< byte offset from start of data file to first pixel */

    /* --- file handle ------------------------------------------------ */
    char *data_path;    /*!< heap-owned path to binary data file           */
    void *_fp;          /*!< opaque FILE* (do not access directly)         */

    /* --- full PVL tree (root) --------------------------------------- */
    PPvlNode *label;    /*!< heap-owned PVL tree; free with p_pvl_free()  */
} PPdsImage;

/* ================================================================== */
/* PVL parsing API                                                      */
/* ================================================================== */

/*!
 * \brief Parse a PDS3 PVL label from an open text stream.
 *
 * Reads until "END" token or EOF.  Returns a heap-allocated PPvlNode tree.
 * The caller must free it with p_pvl_free().
 *
 * \param path   path to the label file (used only for error messages)
 * \param fp     open FILE* positioned at start of label text
 * \return root PPvlNode*, or NULL on parse error
 */
PPvlNode *p_pvl_parse(const char *path, void *fp);

/*!
 * \brief Find a direct-child scalar node by key.
 *
 * Case-insensitive key match.
 *
 * \param parent  starting node (search its children)
 * \param key     keyword to find
 * \return matching PPvlNode* or NULL
 */
PPvlNode *p_pvl_find(const PPvlNode *parent, const char *key);

/*!
 * \brief Find a named OBJECT or GROUP among children.
 *
 * Matches nodes where type == P_PVL_OBJECT (or GROUP) and value == name.
 *
 * \param parent  parent node
 * \param name    object name (e.g. "IMAGE", "QUBE")
 * \return matching PPvlNode* or NULL
 */
PPvlNode *p_pvl_find_object(const PPvlNode *parent, const char *name);

/*!
 * \brief Return the scalar string value of a keyword (or NULL).
 *
 * Strips surrounding quotes.  Pointer is valid as long as the PPvlNode tree
 * exists.
 */
const char *p_pvl_value(const PPvlNode *parent, const char *key);

/*!
 * \brief Return double value of a keyword; sets *ok=0 on failure.
 */
double p_pvl_value_double(const PPvlNode *parent, const char *key, int *ok);

/*!
 * \brief Return int value of a keyword; sets *ok=0 on failure.
 */
int p_pvl_value_int(const PPvlNode *parent, const char *key, int *ok);

/*!
 * \brief Recursively free a PPvlNode tree.
 */
void p_pvl_free(PPvlNode *root);

/* ================================================================== */
/* PDS3 image API                                                       */
/* ================================================================== */

/*!
 * \brief Open and parse a PDS3 product file.
 *
 * Handles both attached labels (label + data in the same .img/.lbl file)
 * and detached labels (separate .lbl + .img files).  The ^IMAGE or ^QUBE
 * pointer keyword is followed automatically.
 *
 * \param path  path to the PDS3 label file (.lbl or combined .img)
 * \return heap-allocated PPdsImage, or NULL on error (calls G_warning)
 */
PPdsImage *p_pds_open_image(const char *path);

/*!
 * \brief Read one row of one band into a caller-provided DCELL buffer.
 *
 * Applies OFFSET + SCALING_FACTOR so the returned values are physical DN
 * equivalents scaled to double.  Special pixels are mapped to G_*_VAL
 * (Null → GRASS NULL, LRS → GRASS LRS, etc.) when grass_special != 0.
 *
 * \param img            open PPdsImage
 * \param band           0-based band index
 * \param row            0-based row index
 * \param buf            output buffer of length img->samples (DCELL)
 * \param grass_special  if non-zero, convert ISIS special DN values to GRASS special values
 * \return 0 on success, -1 on error
 */
int p_pds_read_row(PPdsImage *img, int band, int row,
                    double *buf, int grass_special);

/*!
 * \brief Read an entire band into a flat double array [row * samples].
 *
 * Convenience wrapper around p_pds_read_row().
 * Caller must pre-allocate buf of size img->lines * img->samples * sizeof(double).
 */
int p_pds_read_band(PPdsImage *img, int band, double *buf, int grass_special);

/*!
 * \brief Close a PPdsImage and free all resources.
 */
void p_pds_close(PPdsImage *img);

/* ================================================================== */
/* Utility                                                              */
/* ================================================================== */

/*!
 * \brief Return 1 if the running CPU is little-endian.
 */
int p_pds_is_little_endian(void);

/*!
 * \brief Swap bytes in-place for n elements of elem_size bytes each.
 */
void p_pds_swap_bytes(void *buf, int n, int elem_size);

#ifdef __cplusplus
}
#endif

#endif /* GRASS_P_PDS_H */
