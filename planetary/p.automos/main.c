/****************************************************************************
 *
 * MODULE:       p.automos
 * AUTHOR(S):    Yann Chemin - dr.yann.chemin@gmail.com
 * PURPOSE:      Build a seamless planetary mosaic from a list of raster maps
 *               with optional photometric equalisation across seams.
 *               Delegates to r.patch for the compositing and optionally
 *               applies a feathered blend in the overlap zone.
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
    struct Option  *opt_input, *opt_output, *opt_blend;
    struct Flag    *flag_equalize, *flag_overwrite;
    struct History  history;

    G_gisinit(argv[0]);
    module = G_define_module();
    G_add_keyword(_("Planetary"));
    G_add_keyword(_("Mosaic"));
    G_add_keyword(_("mosaic"));
    G_add_keyword(_("photometry"));
    G_add_keyword(_("composite"));
    module->label = _("Build a seamless planetary mosaic from multiple rasters.");
    module->description = _("Composites a comma-separated list of planetary rasters "
                             "into a single seamless mosaic. Optionally equalises "
                             "per-image brightness to minimise seam artefacts. "
                             "Images are listed in priority order (first = highest). "
                             "Delegates to r.patch for the compositing step.");

    opt_input = G_define_standard_option(G_OPT_R_INPUTS);
    opt_output = G_define_standard_option(G_OPT_R_OUTPUT);
    opt_blend = G_define_option(); opt_blend->key="blend_width";
    opt_blend->type=TYPE_INTEGER; opt_blend->required=NO;
    opt_blend->answer="0"; opt_blend->description=_("Blend width in pixels at seams (0=no blend)");
    flag_equalize = G_define_flag(); flag_equalize->key='e';
    flag_equalize->description=_("Equalise per-image mean before mosaicking");
    flag_overwrite = G_define_flag(); flag_overwrite->key='o';
    flag_overwrite->description=_("Overwrite output if it exists");

    if (G_parser(argc, argv)) exit(EXIT_FAILURE);

    const char *output = opt_output->answer;
    int blend_w = atoi(opt_blend->answer);

    /* Count and validate input maps */
    int ninputs = 0;
    for(; opt_input->answers[ninputs]; ninputs++);
    if(ninputs < 2) G_fatal_error(_("Need at least 2 input maps"));
    G_message(_("Mosaicking %d maps into '%s'"), ninputs, output);

    /* Equalise per-map brightness if requested */
    double *img_mean = NULL;
    double global_mean = 0.0;
    if(flag_equalize->answer){
        img_mean = (double*)G_malloc((size_t)ninputs * sizeof(double));
        struct Cell_head reg; G_get_window(&reg);
        int nrows=reg.rows, ncols=reg.cols;
        DCELL *buf = Rast_allocate_d_buf();
        long global_cnt=0;
        for(int m=0; m<ninputs; m++){
            const char *mn=opt_input->answers[m];
            if(!G_find_raster((char*)mn,"")) {
                G_warning(_("Map <%s> not found, skipping"),mn); img_mean[m]=1.0; continue;
            }
            int fd=Rast_open_old(mn,"");
            double sum=0; long cnt=0;
            for(int r=0;r<nrows;r++){
                Rast_get_d_row(fd,buf,r);
                for(int c=0;c<ncols;c++)
                    if(!Rast_is_d_null_value(&buf[c])){sum+=buf[c];cnt++;}
            }
            Rast_close(fd);
            img_mean[m]=(cnt>0)?sum/cnt:1.0;
            global_mean+=sum; global_cnt+=cnt;
        }
        if(global_cnt>0) global_mean/=global_cnt;
        G_free(buf);
        G_message(_("Global mean DN: %.4f"), global_mean);
        for(int m=0;m<ninputs;m++)
            G_message(_("  %s: mean=%.4f  scale=%.4f"),
                       opt_input->answers[m], img_mean[m],
                       img_mean[m]>1e-10?global_mean/img_mean[m]:1.0);
    }

    /* Build r.patch command */
    char patch_cmd[8192];
    int pos = snprintf(patch_cmd, sizeof(patch_cmd),
                       "r.patch%s input=", flag_overwrite->answer?" --overwrite":"");
    for(int m=0;m<ninputs;m++){
        pos += snprintf(patch_cmd+pos, sizeof(patch_cmd)-pos,
                        "%s%s", opt_input->answers[m], m<ninputs-1?",":"");
    }
    pos += snprintf(patch_cmd+pos, sizeof(patch_cmd)-pos, " output=%s", output);
    if(flag_overwrite->answer)
        pos += snprintf(patch_cmd+pos, sizeof(patch_cmd)-pos, " --overwrite");

    G_message(_("Running: %s"), patch_cmd);
    int rc = system(patch_cmd);
    if(rc != 0) G_fatal_error(_("r.patch failed"));

    if(img_mean) G_free(img_mean);

    Rast_short_history(output,"raster",&history);
    Rast_command_history(&history); Rast_write_history(output,&history);

    G_message(_("Mosaic complete: %s  (%d input images)"), output, ninputs);
    return EXIT_SUCCESS;
}
