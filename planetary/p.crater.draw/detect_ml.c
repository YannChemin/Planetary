/****************************************************************************
 * MODULE:       p.crater.draw (detect_ml.c)
 * PURPOSE:      Shallow-ML rescoring of candidates from P1+P2 classical
 *               detectors. Implements the design described in
 *               detect_ml.h: stack the per-candidate features (including
 *               classical confidences) and apply a learned logistic
 *               linear model. Uniform-weight fallback when no model is
 *               loaded.
 *
 * AUTHOR(S):    Yann Chemin - dr.yann.chemin@gmail.com
 * LICENSE:      The Unlicense (SPDX-License-Identifier: Unlicense)
 ****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <grass/gis.h>
#include <grass/glocale.h>

#include "p_crater_draw.h"
#include "detect_ml.h"

/* ------------------------------------------------------------------ */
MLModel ml_load_model(const char *path)
{
    MLModel m;
    /* Baseline: uniform 1/6 weights, zero bias. */
    for (int i = 0; i < P_CRATER_DRAW_ML_NFEATURES; i++)
        m.w[i] = 1.0f / P_CRATER_DRAW_ML_NFEATURES;
    m.bias    = 0.0f;
    m.trained = 0;

    if (!path || !path[0]) return m;
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        G_message(_("ML model file '%s' not found - using uniform-weight "
                     "baseline"), path);
        return m;
    }

    char magic[4];
    if (fread(magic, 1, 4, fp) != 4 || memcmp(magic, "PCDM", 4) != 0) {
        G_warning(_("ML model '%s' has wrong magic - using baseline"),
                  path);
        fclose(fp); return m;
    }
    int version = 0;
    if (fread(&version, sizeof(int), 1, fp) != 1 || version != 1) {
        G_warning(_("ML model '%s' has unsupported version %d "
                     "- using baseline"), path, version);
        fclose(fp); return m;
    }
    if (fread(m.w,    sizeof(float), P_CRATER_DRAW_ML_NFEATURES, fp)
            != P_CRATER_DRAW_ML_NFEATURES
        || fread(&m.bias, sizeof(float), 1, fp) != 1) {
        G_warning(_("ML model '%s' truncated - using baseline"), path);
        for (int i = 0; i < P_CRATER_DRAW_ML_NFEATURES; i++)
            m.w[i] = 1.0f / P_CRATER_DRAW_ML_NFEATURES;
        m.bias = 0.0f;
        fclose(fp); return m;
    }
    fclose(fp);
    m.trained = 1;
    G_message(_("Loaded trained ML model '%s' (v%d)"), path, version);
    return m;
}

/* ------------------------------------------------------------------ */
/* Disk-IoU on a candidate pair (centre + radius).                     */
/* ------------------------------------------------------------------ */
static double pair_iou(const CraterCandidate *a, const CraterCandidate *b)
{
    double dx = a->cx - b->cx, dy = a->cy - b->cy;
    double d  = sqrt(dx*dx + dy*dy);
    double ra = a->radius_m, rb = b->radius_m;
    if (d >= ra + rb)        return 0.0;
    if (d + fmin(ra,rb) <= fmax(ra,rb)) {
        double small = M_PI * fmin(ra,rb) * fmin(ra,rb);
        double big   = M_PI * fmax(ra,rb) * fmax(ra,rb);
        return small / big;
    }
    double ra2 = ra*ra, rb2 = rb*rb;
    double alpha = 2.0 * acos((d*d + ra2 - rb2) / (2.0 * d * ra));
    double beta  = 2.0 * acos((d*d + rb2 - ra2) / (2.0 * d * rb));
    double inter = 0.5 * ra2 * (alpha - sin(alpha))
                 + 0.5 * rb2 * (beta  - sin(beta));
    double uni   = M_PI * (ra2 + rb2) - inter;
    return inter / uni;
}

static inline float sigmoid(float x)
{
    return 1.0f / (1.0f + expf(-x));
}

/* ------------------------------------------------------------------ */
/* Build the 6-feature vector for a "merged" hypothesis of P1 + P2.    */
/* When a partner is absent, fill the missing channels with 0.         */
/* ------------------------------------------------------------------ */
static void make_features(const CraterCandidate *p1,
                            const CraterCandidate *p2,
                            float f[P_CRATER_DRAW_ML_NFEATURES])
{
    double cP1 = p1 ? p1->confidence : 0.0;
    double cP2 = p2 ? p2->confidence : 0.0;
    /* The remaining four features are placeholders that downstream
     * training would fill - in v1 we don't have access to the per-
     * candidate radial-profile statistics after the detectors are
     * done. Use simple proxies derived from the confidences.        */
    f[0] = (float)cP1;
    f[1] = (float)cP2;
    f[2] = (float)(cP1 * cP1);          /* rim-std proxy             */
    f[3] = (float)(cP2 * cP2);          /* inner-std proxy           */
    f[4] = (float)fabs(cP1 - cP2);      /* bright-shadow contrast    */
    f[5] = (float)(0.5 * (cP1 + cP2));  /* rim/interior contrast     */
}

/* ------------------------------------------------------------------ */
int detect_ml_rescore(const CandidateList *in,
                       const MLModel *model,
                       CandidateList *out)
{
    if (!in || in->n == 0) return 0;

    /* Pair P1 (dem) and P2 (image) candidates by IoU. A given P1
     * candidate gets paired with the highest-IoU P2 partner (if any)
     * with IoU >= 0.30. Both lonely candidates and paired ones go
     * through the model.                                              */
    int *pair_of = G_malloc((size_t)in->n * sizeof(int));
    for (int i = 0; i < in->n; i++) pair_of[i] = -1;

    for (int i = 0; i < in->n; i++) {
        if (strcmp(in->data[i].method, "dem") != 0) continue;
        double best_iou = 0.30;
        int    best_j = -1;
        for (int j = 0; j < in->n; j++) {
            if (strcmp(in->data[j].method, "image") != 0) continue;
            if (pair_of[j] >= 0) continue;   /* already paired         */
            double iou = pair_iou(&in->data[i], &in->data[j]);
            if (iou > best_iou) { best_iou = iou; best_j = j; }
        }
        if (best_j >= 0) {
            pair_of[i]     = best_j;
            pair_of[best_j]= i;
        }
    }

    int n_emit = 0;
    char *consumed = G_calloc((size_t)in->n, 1);
    for (int i = 0; i < in->n; i++) {
        if (consumed[i]) continue;
        const CraterCandidate *p1 = NULL, *p2 = NULL;
        const CraterCandidate *base = &in->data[i];
        if (strcmp(base->method, "dem") == 0) {
            p1 = base;
            if (pair_of[i] >= 0) p2 = &in->data[pair_of[i]];
        } else if (strcmp(base->method, "image") == 0) {
            p2 = base;
            if (pair_of[i] >= 0) p1 = &in->data[pair_of[i]];
        } else {
            /* "merged" or other - treat as full pair via its tags. */
            p1 = base; p2 = base;
        }

        float f[P_CRATER_DRAW_ML_NFEATURES];
        make_features(p1, p2, f);
        float z = model->bias;
        for (int k = 0; k < P_CRATER_DRAW_ML_NFEATURES; k++)
            z += model->w[k] * f[k];
        float conf_ml = sigmoid(z);

        /* Compose the output candidate: centre/radius from the
         * higher-confidence partner; carry the ML score.              */
        CraterCandidate cand = *base;
        if (p1 && p2) {
            if (p2->confidence > p1->confidence) cand = *p2;
            else                                  cand = *p1;
        }
        cand.confidence = (double)conf_ml;
        snprintf(cand.method, sizeof(cand.method),
                 model->trained ? "ml" : "ml-baseline");
        cand.n_methods  = (p1 && p2) ? 2 : 1;
        cand.dD_simple  = base->dD_simple;
        cand.basin_id   = 0;
        cand.ring_index = 0;
        cl_push(out, &cand);
        n_emit++;

        consumed[i] = 1;
        if (pair_of[i] >= 0) consumed[pair_of[i]] = 1;
    }

    G_free(consumed);
    G_free(pair_of);
    G_message(_("ML rescore: %d -> %d candidates "
                 "(%s)"),
              in->n, n_emit,
              model->trained ? "trained model" : "uniform baseline");
    return n_emit;
}
