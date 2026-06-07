/****************************************************************************
 *
 * MODULE:       p.rings.stats
 * AUTHOR(S):    Yann Chemin - dr.yann.chemin@gmail.com
 * PURPOSE:      Compute azimuthal and radial statistics for a ring-plane raster.
 *               Produces azimuthal profiles (mean over all longitudes per radius)
 *               and radial profiles (mean over all radii per longitude).
 *
 * LICENSE:      The Unlicense - public domain dedication.
 *               SPDX-License-Identifier: Unlicense
 *               See https://unlicense.org for the full text and LICENSE
 *               file in this directory.
 *
 *****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <grass/gis.h>
#include <grass/raster.h>
#include <grass/glocale.h>

int main(int argc, char *argv[])
{
    struct GModule *module;
    struct Option  *opt_input, *opt_radial, *opt_azimuth, *opt_output;
    struct History  history;

    G_gisinit(argv[0]);
    module = G_define_module();
    G_add_keyword(_("Planetary"));
    G_add_keyword(_("Ring Plane Analysis"));
    G_add_keyword(_("ring plane"));
    G_add_keyword(_("statistics"));
    G_add_keyword(_("Saturn"));
    module->label = _("Compute azimuthal and radial statistics for a ring-plane raster.");
    module->description = _("Given a ring-plane raster (north=ring_radius, east=ring_lon), "
                             "computes radial profile (mean DN vs ring radius) and azimuthal "
                             "profile (mean DN vs ring longitude). "
                             "Profiles are written to text files.");

    opt_input   = G_define_standard_option(G_OPT_R_INPUT);
    opt_output  = G_define_standard_option(G_OPT_R_OUTPUT);
    opt_output->required = NO;
    opt_output->description = _("Output azimuthal-mean raster (optional, fills each row with azimuthal mean)");
    opt_radial  = G_define_option(); opt_radial->key="radial_profile";
    opt_radial->type=TYPE_STRING; opt_radial->required=NO;
    opt_radial->description=_("Output text file: ring_radius mean_DN [km, DN]");
    opt_azimuth = G_define_option(); opt_azimuth->key="azimuth_profile";
    opt_azimuth->type=TYPE_STRING; opt_azimuth->required=NO;
    opt_azimuth->description=_("Output text file: ring_lon mean_DN [deg, DN]");

    if (G_parser(argc, argv)) exit(EXIT_FAILURE);

    int fd_in = Rast_open_old(opt_input->answer,"");
    struct Cell_head reg; G_get_window(&reg);
    int nrows=reg.rows, ncols=reg.cols;

    /* Load raster */
    DCELL **data=(DCELL**)G_malloc((size_t)nrows*sizeof(DCELL*));
    DCELL *tmp=Rast_allocate_d_buf();
    for(int r=0;r<nrows;r++){
        data[r]=(DCELL*)G_malloc((size_t)ncols*sizeof(DCELL));
        Rast_get_d_row(fd_in,tmp,r);
        for(int c=0;c<ncols;c++) data[r][c]=tmp[c];
    }
    Rast_close(fd_in); G_free(tmp);

    /* Radial profile: one entry per row (ring radius increases S→N) */
    if(opt_radial->answer){
        FILE *fp=fopen(opt_radial->answer,"w");
        if(!fp) G_warning(_("Cannot write radial profile '%s'"),opt_radial->answer);
        else {
            fprintf(fp,"# ring_radius_km mean_DN count\n");
            for(int r=0;r<nrows;r++){
                double rad = reg.north - (r+0.5)*reg.ns_res;
                double sum=0; long cnt=0;
                for(int c=0;c<ncols;c++)
                    if(!Rast_is_d_null_value(&data[r][c])){sum+=data[r][c];cnt++;}
                if(cnt>0)
                    fprintf(fp,"%.4f %.8g %ld\n",rad,sum/cnt,cnt);
            }
            fclose(fp);
            G_message(_("Radial profile: %s"),opt_radial->answer);
        }
    }

    /* Azimuthal profile: one entry per column */
    if(opt_azimuth->answer){
        FILE *fp=fopen(opt_azimuth->answer,"w");
        if(!fp) G_warning(_("Cannot write azimuth profile '%s'"),opt_azimuth->answer);
        else {
            fprintf(fp,"# ring_lon_deg mean_DN count\n");
            for(int c=0;c<ncols;c++){
                double lon = reg.west + (c+0.5)*reg.ew_res;
                double sum=0; long cnt=0;
                for(int r=0;r<nrows;r++)
                    if(!Rast_is_d_null_value(&data[r][c])){sum+=data[r][c];cnt++;}
                if(cnt>0)
                    fprintf(fp,"%.4f %.8g %ld\n",lon,sum/cnt,cnt);
            }
            fclose(fp);
            G_message(_("Azimuthal profile: %s"),opt_azimuth->answer);
        }
    }

    /* Optional: output raster filled with azimuthal mean per column */
    if(opt_output->answer){
        /* Compute col means */
        double *col_mean=(double*)G_malloc((size_t)ncols*sizeof(double));
        for(int c=0;c<ncols;c++){
            double sum=0; long cnt=0;
            for(int r=0;r<nrows;r++)
                if(!Rast_is_d_null_value(&data[r][c])){sum+=data[r][c];cnt++;}
            col_mean[c]=(cnt>0)?sum/cnt:0.0;
        }
        int fd_out=Rast_open_new(opt_output->answer,DCELL_TYPE);
        DCELL *buf_out=Rast_allocate_d_buf();
        for(int r=0;r<nrows;r++){
            for(int c=0;c<ncols;c++)
                buf_out[c]=(DCELL)col_mean[c];
            Rast_put_d_row(fd_out,buf_out);
        }
        Rast_close(fd_out); G_free(buf_out); G_free(col_mean);
        Rast_short_history(opt_output->answer,"raster",&history);
        Rast_command_history(&history);Rast_write_history(opt_output->answer,&history);
    }

    for(int r=0;r<nrows;r++) G_free(data[r]);
    G_free(data);
    G_message(_("Ring-plane statistics complete."));
    return EXIT_SUCCESS;
}
