# p_pds Library

PDS3 and PDS4 label parsing and image I/O for planetary science applications.

## Overview

The p_pds library provides C functions to read PDS3 (Planetary Data System version 3) and PDS4 image products, handling the diverse label formats, image encodings, and special data values used in planetary science.

## Features

- **PDS3 Label Parsing**: Recursive descent parser for Parameter Value Language (PVL) labels
- **Attached/Detached Label Support**: Reads standalone `.lbl` files and detached labels with separate image files
- **Multiple Image Formats**: IMAGE, QUBE, SPECTRAL_QUBE object types
- **Encoding Support**: UINT8, MSB_INTEGER, LSB_INTEGER, IEEE754, REAL encodings; byte/half-word/word unit support
- **Organization Support**: BSQ (Band Sequential), BIL (Band Interleaved by Line), BIP (Band Interleaved by Pixel)
- **Stale Offset Handling**: Automatic detection and correction of invalid `^IMAGE` offsets via `scan_past_ascii()`
- **BYTES vs RECORDS Units**: Transparent handling of offset specifications in BYTES or RECORDS

## API

### Core Functions

```c
PPdsImage *p_pds_open_image(const char *label_path);
void p_pds_close(PPdsImage *img);
int p_pds_read_row(PPdsImage *img, int band, int row, double *buffer, 
                   int null_special);
int p_pds_read_sample(PPdsImage *img, int band, int row, int col, 
                      double *value, int null_special);
```

### Data Structures

```c
typedef struct {
    int lines;
    int samples;
    int bands;
    int sample_bytes;
    double offset;
    double scaling_factor;
    double minimum;
    double maximum;
    PPdsOrganization organization;
    PPdsSampleType sample_type;
    void *internal_state;
} PPdsImage;
```

## Compilation

### Standalone Compilation

```bash
gcc -DP_PDS_STANDALONE -fopenmp -I. -c p_pds.c -o p_pds.o
gcc -DP_PDS_STANDALONE -fopenmp -o test_pds test_pds.c p_pds.o -lm
```

### Integration with GRASS

The library is compiled and linked as part of p.in.pds3 and p.in.pds4 modules via the Makefile's `$(PDS_OBJ)` dependency.

## Implementation Details

### PVL Parsing Strategy

The parser uses recursive descent to handle nested PVL structures:

```
OBJECT = ImageObject
  LINES = 512
  LINE_SAMPLES = 512
  SAMPLE_BITS = 32
  ^IMAGE = ("image.img", 1 <RECORDS>)
END_OBJECT = ImageObject
```

Special handling for:
- Quoted strings: `"value"` or `'value'`
- Numeric scalars: integers, floats, scientific notation
- Units: `<RECORDS>`, `<BYTES>`, `<KILOBYTES>`, etc.
- Pointers: `^IMAGE = (file, offset, unit)`

### Offset Resolution

The `^IMAGE` pointer specifies where image data starts. For detached labels:
1. If offset unit is `<RECORDS>`, multiply by 512 bytes (PDS3 standard record size)
2. If unit is `<BYTES>`, use offset directly
3. If offset appears corrupted (e.g., beyond file size), `scan_past_ascii()` searches for binary data boundary

### Sample Type Mapping

| PDS3 SAMPLE_BITS | PDS3 ENCODING | Internal Type | IEEE754 Range |
|---|---|---|---|
| 8 | UNSIGNED_INTEGER | uint8_t | 0–255 |
| 16 | MSB_INTEGER | int16_t | −32768–32767 |
| 16 | LSB_INTEGER | int16_t | −32768–32767 |
| 32 | IEEE754 | float | ±3.4e38 |
| 32 | MSB_REAL | float | ±3.4e38 |
| 64 | IEEE754 | double | ±1.7e308 |

### Special DN Values

In ISIS3-derived products, DN 0, negative, and specific ranges carry special meaning (NULL, Low Representation Saturation, etc.). The `null_special` flag controls whether these are mapped to GRASS NULL.

## Scientific References

- **PDS3 Standards**: NASA Planetary Data System Standards, JPL Document D-7669, https://pds.nasa.gov/datastandards/pds3/
- **PDS4 Standards**: NASA Planetary Data System Standards V4, JPL Document D-96008, https://pds.nasa.gov/datastandards/pds4/
- **PDS4 Information Model**: https://pds.nasa.gov/datastandards/pds4/information-model/
- Sides, S. H., Elassar, S., Hare, T. M., & Neumann, G. A. (2017). "Integration of LOLA and LROC Data for Improved Lunar Topography Modeling". *Lunar and Planetary Institute Contribution* No. 1986, LPI, Houston, TX.

## Author

Yann Chemin (dr.yann.chemin@gmail.com)

## License

The Unlicense (https://unlicense.org) — released into the public domain. See the LICENSE file in this directory.
