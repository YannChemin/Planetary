#include <string.h>
/****************************************************************************
 *
 * MODULE:       p.dstripe
 * AUTHOR(S):    Yann Chemin - dr.yann.chemin@gmail.com
 * PURPOSE:      Remove column or row stripe noise from a planetary pushbroom
 *               camera image using per-column (or row) statistics computed
 *               from a reference dark-strip or from the image statistics.
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
    struct Option  *opt_input, *opt_output, *opt_method;
    struct Flag    *flag_row;
    struct History  history;

    G_gisinit(argv[0]);
    module = G_define_module();
    G_add_keyword(_("Planetary"));
    G_add_keyword(_("Noise & Calibration"));
    G_add_keyword(_("noise removal"));
    G_add_keyword(_("pushbroom"));
    G_add_keyword(_("stripes"));
    module->label = _("Remove detector-stripe noise from a planetary pushbroom image.");
    module->description = _("Corrects column (or row, with -r flag) stripe noise "
                             "inherent in pushbroom-sensor data. "
                             "Method 'subtract': removes per-column mean; "
                             "Method 'divide': divides by per-column mean (flat-field). "
                             "The global mean is preserved after correction.");

    opt_input  = G_define_standard_option(G_OPT_R_INPUT);
    opt_output = G_define_standard_option(G_OPT_R_OUTPUT);
    opt_method = G_define_option(); opt_method->key="method";
    opt_method->type=TYPE_STRING; opt_method->required=NO;
    opt_method->answer="subtract"; opt_method->options="subtract,divide";
    opt_method->description=_("Destriping method");
    flag_row = G_define_flag(); flag_row->key='r';
    flag_row->description=_("Remove row stripes instead of column stripes");

    if (G_parser(argc, argv)) exit(EXIT_FAILURE);

    int do_divide = (strcmp(opt_method->answer,"divide")==0);
    int do_rows   = flag_row->answer;

    int fd_in = Rast_open_old(opt_input->answer, "");
    struct Cell_head reg; G_get_window(&reg);
    int nrows=reg.rows, ncols=reg.cols;

    /* Load full image */
    DCELL **data = (DCELL**)G_malloc((size_t)nrows*sizeof(DCELL*));
    DCELL *tmp   = Rast_allocate_d_buf();
    for(int r=0;r<nrows;r++){
        data[r]=(DCELL*)G_malloc((size_t)ncols*sizeof(DCELL));
        Rast_get_d_row(fd_in,tmp,r);
        for(int c=0;c<ncols;c++) data[r][c]=tmp[c];
    }
    Rast_close(fd_in); G_free(tmp);

    /* Compute per-column (or row) statistics */
    int nstripes = do_rows ? nrows : ncols;
    int nlen     = do_rows ? ncols : nrows;
    double *stripe_mean = (double*)G_malloc((size_t)nstripes*sizeof(double));
    double global_mean  = 0.0;
    long   global_cnt   = 0;

    for(int s=0;s<nstripes;s++){
        double sum=0; long cnt=0;
        for(int i=0;i<nlen;i++){
            DCELL v = do_rows ? data[s][i] : data[i][s];
            if(!Rast_is_d_null_value(&v)){sum+=v;cnt++;}
        }
        stripe_mean[s] = (cnt>0) ? sum/cnt : 0.0;
        global_mean += (cnt>0 ? sum : 0);
        global_cnt  += cnt;
    }
    if(global_cnt>0) global_mean /= global_cnt;

    /* Apply correction */
    int fd_out = Rast_open_new(opt_output->answer, DCELL_TYPE);
    DCELL *buf_out = Rast_allocate_d_buf();

    for(int r=0;r<nrows;r++){
        G_percent(r,nrows,2);
        for(int c=0;c<ncols;c++){
            if(Rast_is_d_null_value(&data[r][c])){
                Rast_set_d_null_value(&buf_out[c],1); continue;
            }
            int s = do_rows ? r : c;
            double sm = stripe_mean[s];
            if(do_divide){
                buf_out[c] = (DCELL)(sm>1e-10 ? data[r][c]*global_mean/sm : data[r][c]);
            } else {
                buf_out[c] = (DCELL)(data[r][c] - sm + global_mean);
            }
        }
        Rast_put_d_row(fd_out,buf_out);
    }
    G_percent(1,1,2);

    for(int r=0;r<nrows;r++) G_free(data[r]);
    G_free(data); G_free(stripe_mean); G_free(buf_out);
    Rast_close(fd_out);
    Rast_short_history(opt_output->answer,"raster",&history);
    Rast_command_history(&history); Rast_write_history(opt_output->answer,&history);
    G_message(_("Destriping complete: %s"), opt_output->answer);
    return EXIT_SUCCESS;
}
