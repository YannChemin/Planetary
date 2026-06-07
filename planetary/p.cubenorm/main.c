/****************************************************************************
 *
 * MODULE:       p.cubenorm
 * AUTHOR(S):    Yann Chemin - dr.yann.chemin@gmail.com
 * PURPOSE:      Column/row normalisation for pushbroom planetary images.
 *               Normalises each column (or row) by its mean or median,
 *               preserving the overall image statistics.
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

static int dbl_cmp(const void *a, const void *b)
{ return (*(double*)a > *(double*)b) ? 1 : (*(double*)a < *(double*)b) ? -1 : 0; }

int main(int argc, char *argv[])
{
    struct GModule *module;
    struct Option  *opt_input, *opt_output, *opt_stat;
    struct Flag    *flag_row;
    struct History  history;

    G_gisinit(argv[0]);
    module = G_define_module();
    G_add_keyword(_("Planetary"));
    G_add_keyword(_("Noise & Calibration"));
    G_add_keyword(_("normalisation"));
    G_add_keyword(_("pushbroom"));
    G_add_keyword(_("calibration"));
    module->label = _("Column/row normalisation for planetary pushbroom images.");
    module->description = _("Divides each column (or row with -r flag) by its "
                             "mean or median, then multiplies by the global mean "
                             "to preserve overall brightness. Removes systematic "
                             "detector-sensitivity variations.");

    opt_input  = G_define_standard_option(G_OPT_R_INPUT);
    opt_output = G_define_standard_option(G_OPT_R_OUTPUT);
    opt_stat   = G_define_option(); opt_stat->key="statistic";
    opt_stat->type=TYPE_STRING; opt_stat->required=NO;
    opt_stat->answer="mean"; opt_stat->options="mean,median";
    opt_stat->description=_("Statistic to normalise by");
    flag_row = G_define_flag(); flag_row->key='r';
    flag_row->description=_("Normalise by row instead of column");

    if (G_parser(argc, argv)) exit(EXIT_FAILURE);

    int do_rows  = flag_row->answer;
    int do_median = strcmp(opt_stat->answer,"median")==0;

    int fd_in = Rast_open_old(opt_input->answer,"");
    struct Cell_head reg; G_get_window(&reg);
    int nrows=reg.rows, ncols=reg.cols;

    DCELL **data=(DCELL**)G_malloc((size_t)nrows*sizeof(DCELL*));
    DCELL *tmp=Rast_allocate_d_buf();
    for(int r=0;r<nrows;r++){
        data[r]=(DCELL*)G_malloc((size_t)ncols*sizeof(DCELL));
        Rast_get_d_row(fd_in,tmp,r);
        memcpy(data[r],tmp,(size_t)ncols*sizeof(DCELL));
    }
    Rast_close(fd_in); G_free(tmp);

    int nstripes=do_rows?nrows:ncols, nlen=do_rows?ncols:nrows;
    double *stat_arr=(double*)G_malloc((size_t)nstripes*sizeof(double));
    double *vals=(double*)G_malloc((size_t)nlen*sizeof(double));
    double global_mean=0; long global_cnt=0;

    for(int s=0;s<nstripes;s++){
        int vcnt=0;
        for(int i=0;i<nlen;i++){
            DCELL v=do_rows?data[s][i]:data[i][s];
            if(!Rast_is_d_null_value(&v)){vals[vcnt++]=v;global_mean+=v;global_cnt++;}
        }
        if(vcnt==0){stat_arr[s]=1.0;continue;}
        if(do_median){
            qsort(vals,(size_t)vcnt,sizeof(double),dbl_cmp);
            stat_arr[s]=(vcnt%2)?vals[vcnt/2]:(vals[vcnt/2-1]+vals[vcnt/2])*0.5;
        } else {
            double s2=0; for(int i=0;i<vcnt;i++) s2+=vals[i];
            stat_arr[s]=s2/vcnt;
        }
    }
    if(global_cnt>0) global_mean/=global_cnt;
    G_free(vals);

    int fd_out=Rast_open_new(opt_output->answer,DCELL_TYPE);
    DCELL *buf_out=Rast_allocate_d_buf();
    for(int r=0;r<nrows;r++){
        G_percent(r,nrows,2);
        for(int c=0;c<ncols;c++){
            if(Rast_is_d_null_value(&data[r][c])){
                Rast_set_d_null_value(&buf_out[c],1);continue;}
            double s=do_rows?stat_arr[r]:stat_arr[c];
            buf_out[c]=(DCELL)(s>1e-10?data[r][c]*global_mean/s:data[r][c]);
        }
        Rast_put_d_row(fd_out,buf_out);
    }
    G_percent(1,1,2);
    for(int r=0;r<nrows;r++) G_free(data[r]);
    G_free(data);G_free(stat_arr);G_free(buf_out);
    Rast_close(fd_out);
    Rast_short_history(opt_output->answer,"raster",&history);
    Rast_command_history(&history);Rast_write_history(opt_output->answer,&history);
    G_message(_("Normalisation complete: %s"),opt_output->answer);
    return EXIT_SUCCESS;
}
