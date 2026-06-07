/****************************************************************************
 *
 * MODULE:       p.mineral.indices
 * AUTHOR(S):    Yann Chemin - dr.yann.chemin@gmail.com
 * PURPOSE:      Compute planetary mineral spectral indices from multi-band data.
 *               Supported indices: olivine absorption band depth at 1.05 µm,
 *               pyroxene band depth at 2.0 µm, TiO2 index (Clementine/Kaguya),
 *               FeO index, mafic index.
 *
 * LICENSE:      The Unlicense - public domain dedication.
 *               SPDX-License-Identifier: Unlicense
 *               See https://unlicense.org for the full text and LICENSE
 *               file in this directory.
 *
 *****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <grass/gis.h>
#include <grass/raster.h>
#include <grass/glocale.h>
#include "../../libs/p_spectra/p_spectra.h"

/* Index definitions: center wavelength, left shoulder, right shoulder [µm] */
typedef struct {
    const char *name;
    const char *description;
    double wl_center;
    double wl_left;
    double wl_right;
    int    is_ratio;   /* 1 = band ratio (wl_center/wl_right), 0 = band depth */
} MineralIndex;

static const MineralIndex INDICES[] = {
    { "olivine",  "Olivine absorption at 1.05 µm",           1.05, 0.75, 1.55, 0 },
    { "pyroxene", "Pyroxene absorption at 2.0 µm",           2.0,  1.5,  2.5,  0 },
    { "tio2",     "TiO2 index (Clementine 415/750 nm ratio)", 0.415, 0.0,  0.750, 1 },
    { "feo",      "FeO index (Clementine 950/750 nm ratio)",  0.950, 0.0,  0.750, 1 },
    { "mafic",    "Mafic index: sum of olivine + pyroxene BD", 1.5, 0.75, 2.5,  0 },
    { NULL, NULL, 0, 0, 0, 0 }
};

int main(int argc, char *argv[])
{
    struct GModule *module;
    struct Option  *opt_input, *opt_output, *opt_index, *opt_wcsv;
    struct History  history;

    G_gisinit(argv[0]);
    module = G_define_module();
    G_add_keyword(_("Planetary"));
    G_add_keyword(_("Spectral & Mineral Mapping"));
    G_add_keyword(_("spectral"));
    G_add_keyword(_("mineralogy"));
    G_add_keyword(_("indices"));
    module->label = _("Compute planetary mineral spectral indices.");
    module->description = _("Calculates band-depth or band-ratio spectral indices "
                             "for mineral identification from multi-band planetary "
                             "imagery. Available indices: olivine (1.05 µm), "
                             "pyroxene (2.0 µm), TiO2 (415/750 nm), FeO (950/750 nm), "
                             "mafic. Input bands named input.1, input.2, etc.");

    opt_input = G_define_option(); opt_input->key="input";
    opt_input->type=TYPE_STRING; opt_input->required=YES;
    opt_input->description=_("Base name of input band rasters");

    opt_output = G_define_standard_option(G_OPT_R_OUTPUT);

    opt_index = G_define_option(); opt_index->key="index";
    opt_index->type=TYPE_STRING; opt_index->required=YES;
    opt_index->options="olivine,pyroxene,tio2,feo,mafic";
    opt_index->description=_("Mineral spectral index to compute");

    opt_wcsv = G_define_option(); opt_wcsv->key="wavelengths";
    opt_wcsv->type=TYPE_STRING; opt_wcsv->required=NO;
    opt_wcsv->description=_("CSV file with wavelength,width per band");

    if (G_parser(argc, argv)) exit(EXIT_FAILURE);

    const char *inbase = opt_input->answer;
    const char *idx_name = opt_index->answer;

    /* Find the index spec */
    const MineralIndex *midx = NULL;
    for (int i = 0; INDICES[i].name; i++)
        if (strcmp(INDICES[i].name, idx_name)==0) { midx = &INDICES[i]; break; }
    if (!midx) G_fatal_error(_("Unknown index '%s'"), idx_name);

    G_message(_("Index: %s — %s"), midx->name, midx->description);

    /* Count bands */
    int nbands = 0;
    char mapname[512];
    for (int b = 1; b <= 10000; b++) {
        snprintf(mapname, sizeof(mapname), "%s.%d", inbase, b);
        if (!G_find_raster(mapname, "")) break;
        nbands++;
    }
    if (nbands < 2) G_fatal_error(_("Need at least 2 bands; found %d for '%s'"),nbands,inbase);

    /* Spectral definition */
    PSpectraDef *sd;
    if (opt_wcsv->answer) {
        sd = p_spectra_def_read_csv(opt_wcsv->answer);
    } else {
        /* 1-nm steps from 0.4 to (0.4+nbands*0.001) µm — rough default */
        double *wl=(double*)G_malloc((size_t)nbands*sizeof(double));
        double *wd=(double*)G_malloc((size_t)nbands*sizeof(double));
        for(int b=0;b<nbands;b++){wl[b]=0.4+b*0.001;wd[b]=0.001;}
        sd=p_spectra_def_create(nbands,wl,wd);
        G_free(wl); G_free(wd);
    }
    if (!sd) G_fatal_error(_("Cannot create spectral definition"));

    /* Open inputs */
    int *fd_in = (int*)G_malloc((size_t)nbands*sizeof(int));
    for(int b=0;b<nbands;b++){
        snprintf(mapname,sizeof(mapname),"%s.%d",inbase,b+1);
        fd_in[b]=Rast_open_old(mapname,"");
    }
    int fd_out = Rast_open_new(opt_output->answer, DCELL_TYPE);
    struct Cell_head reg; G_get_window(&reg);
    int nrows=reg.rows, ncols=reg.cols;

    DCELL **bufs=(DCELL**)G_malloc((size_t)nbands*sizeof(DCELL*));
    DCELL  *buf_out=Rast_allocate_d_buf();
    for(int b=0;b<nbands;b++) bufs[b]=Rast_allocate_d_buf();
    double *spec=(double*)G_malloc((size_t)ncols*nbands*sizeof(double));
    double *out_d=(double*)G_malloc((size_t)ncols*sizeof(double));

    for(int row=0;row<nrows;row++){
        G_percent(row,nrows,2);
        for(int b=0;b<nbands;b++){
            Rast_get_d_row(fd_in[b],bufs[b],row);
            for(int c=0;c<ncols;c++)
                spec[(size_t)c*nbands+b]=Rast_is_d_null_value(&bufs[b][c])?NAN:bufs[b][c];
        }
        if(midx->is_ratio)
            p_spectra_apply_row_band_ratio(sd,ncols,nbands,spec,
                midx->wl_center,midx->wl_right,0,out_d);
        else
            p_spectra_apply_row_band_depth(sd,ncols,nbands,spec,
                midx->wl_center,midx->wl_left,midx->wl_right,0,out_d);
        for(int c=0;c<ncols;c++){
            if(out_d[c]!=out_d[c]) Rast_set_d_null_value(&buf_out[c],1);
            else buf_out[c]=(DCELL)out_d[c];
        }
        Rast_put_d_row(fd_out,buf_out);
    }
    G_percent(1,1,2);
    for(int b=0;b<nbands;b++){Rast_close(fd_in[b]);G_free(bufs[b]);}
    G_free(fd_in);G_free(bufs);G_free(buf_out);G_free(spec);G_free(out_d);
    Rast_close(fd_out); p_spectra_def_free(sd);
    Rast_short_history(opt_output->answer,"raster",&history);
    Rast_command_history(&history);Rast_write_history(opt_output->answer,&history);
    G_message(_("Index '%s' written: %s"),midx->name,opt_output->answer);
    return EXIT_SUCCESS;
}
