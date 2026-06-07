/****************************************************************************
 *
 * MODULE:       p.bandnorm
 * AUTHOR(S):    Yann Chemin - dr.yann.chemin@gmail.com
 * PURPOSE:      Band-by-band normalisation for multi-spectral/hyperspectral
 *               planetary cubes.  Normalises each band to a reference band
 *               or to a solar irradiance spectrum from a CSV file.
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

int main(int argc, char *argv[])
{
    struct GModule *module;
    struct Option  *opt_input, *opt_output, *opt_refband, *opt_irradiance;
    struct History  history;

    G_gisinit(argv[0]);
    module = G_define_module();
    G_add_keyword(_("Planetary"));
    G_add_keyword(_("Noise & Calibration"));
    G_add_keyword(_("normalisation"));
    G_add_keyword(_("multi-band"));
    G_add_keyword(_("spectral"));
    module->label = _("Band-by-band normalisation for planetary hyperspectral cubes.");
    module->description = _("Divides each band of a multi-band planetary raster cube "
                             "(input.1, .2, ...) by a reference band or by a solar "
                             "irradiance value from a CSV file. "
                             "Used to convert radiance to reflectance factor.");

    opt_input = G_define_option(); opt_input->key="input";
    opt_input->type=TYPE_STRING; opt_input->required=YES;
    opt_input->description=_("Base name of input band rasters");
    opt_output = G_define_option(); opt_output->key="output";
    opt_output->type=TYPE_STRING; opt_output->required=YES;
    opt_output->description=_("Base name for output band rasters");
    opt_refband = G_define_option(); opt_refband->key="refband";
    opt_refband->type=TYPE_INTEGER; opt_refband->required=NO;
    opt_refband->answer="0"; opt_refband->description=_("Reference band number (0=use band means)");
    opt_irradiance = G_define_option(); opt_irradiance->key="irradiance";
    opt_irradiance->type=TYPE_STRING; opt_irradiance->required=NO;
    opt_irradiance->description=_("CSV file with solar irradiance per band (overrides refband)");

    if (G_parser(argc, argv)) exit(EXIT_FAILURE);

    const char *inbase = opt_input->answer;
    const char *outbase = opt_output->answer;
    int refband = atoi(opt_refband->answer);

    /* Count bands */
    int nbands=0; char mapname[512];
    for(int b=1;b<=10000;b++){
        snprintf(mapname,sizeof(mapname),"%s.%d",inbase,b);
        if(!G_find_raster(mapname,"")) break;
        nbands++;
    }
    if(nbands<1) G_fatal_error(_("No bands found for '%s'"),inbase);
    G_message(_("Normalising %d bands of '%s' → '%s'"),nbands,inbase,outbase);

    /* Compute per-band means */
    double *band_mean=(double*)G_malloc((size_t)nbands*sizeof(double));
    struct Cell_head reg; G_get_window(&reg);
    int nrows=reg.rows, ncols=reg.cols;
    DCELL *tmp=Rast_allocate_d_buf();

    for(int b=0;b<nbands;b++){
        snprintf(mapname,sizeof(mapname),"%s.%d",inbase,b+1);
        int fd=Rast_open_old(mapname,"");
        double sum=0; long cnt=0;
        for(int r=0;r<nrows;r++){
            Rast_get_d_row(fd,tmp,r);
            for(int c=0;c<ncols;c++)
                if(!Rast_is_d_null_value(&tmp[c])){sum+=tmp[c];cnt++;}
        }
        band_mean[b]=(cnt>0)?sum/cnt:1.0;
        Rast_close(fd);
    }

    /* Determine scale factors */
    double *scale=(double*)G_malloc((size_t)nbands*sizeof(double));
    if(opt_irradiance->answer){
        FILE *fp=fopen(opt_irradiance->answer,"r");
        if(!fp) G_fatal_error(_("Cannot open irradiance CSV '%s'"),opt_irradiance->answer);
        char line[256]; int bi=0;
        while(fgets(line,sizeof(line),fp) && bi<nbands){
            if(line[0]=='#') continue;
            double irr=atof(line);
            scale[bi++]=(irr>1e-10)?irr:1.0;
        }
        fclose(fp);
        for(int b=bi;b<nbands;b++) scale[b]=1.0;
    } else if(refband>0 && refband<=nbands){
        double ref=band_mean[refband-1];
        for(int b=0;b<nbands;b++) scale[b]=(ref>1e-10)?ref:1.0;
    } else {
        for(int b=0;b<nbands;b++) scale[b]=band_mean[b];
    }

    /* Apply normalisation */
    DCELL *buf_in=Rast_allocate_d_buf();
    DCELL *buf_out=Rast_allocate_d_buf();

    for(int b=0;b<nbands;b++){
        G_message(_("  Band %d/%d  mean=%.4g  scale=%.4g"),b+1,nbands,band_mean[b],scale[b]);
        snprintf(mapname,sizeof(mapname),"%s.%d",inbase,b+1);
        int fd_in=Rast_open_old(mapname,"");
        char outname[512]; snprintf(outname,sizeof(outname),"%s.%d",outbase,b+1);
        int fd_out=Rast_open_new(outname,DCELL_TYPE);
        for(int r=0;r<nrows;r++){
            G_percent(r,nrows,5);
            Rast_get_d_row(fd_in,buf_in,r);
            for(int c=0;c<ncols;c++){
                if(Rast_is_d_null_value(&buf_in[c]))
                    Rast_set_d_null_value(&buf_out[c],1);
                else
                    buf_out[c]=(DCELL)(buf_in[c]/scale[b]);
            }
            Rast_put_d_row(fd_out,buf_out);
        }
        Rast_close(fd_in); Rast_close(fd_out);
        Rast_short_history(outname,"raster",&history);
        Rast_command_history(&history); Rast_write_history(outname,&history);
    }
    G_free(tmp);G_free(buf_in);G_free(buf_out);
    G_free(band_mean);G_free(scale);
    G_message(_("Band normalisation complete: %s.1 … %s.%d"),outbase,outbase,nbands);
    return EXIT_SUCCESS;
}
