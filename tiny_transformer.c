/*
 * This file tiny_transformer.c is part of L1vm.
 *
 * (c) Copyright Stefan Pietzonke (info@midnight-coding.de), 2026
 *
 * L1vm is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * L1vm is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with L1vm.  If not, see <http://www.gnu.org/licenses/>.
 */

/* ==================== Tiny Transformer Inference Engine ==================== */
/* A minimal transformer (~200K-500K params) for prompt → emitter classification. */
/* Replaces keyword-based scoring with learned attention. */

#include "tiny_transformer.h"
#include <math.h>
#include <time.h>
#include <dirent.h>
#include <sys/stat.h>

/* ==================== Random Seed ==================== */

static unsigned int tt_rand_state = 42;

static float tt_randf(void) {
    tt_rand_state = tt_rand_state * 1103515245 + 12345;
    return (float)(tt_rand_state & 0x7FFFFFFF) / (float)0x7FFFFFFF;
}

static float tt_randn(void) {
    /* Box-Muller transform */
    float u1 = tt_randf() + 1e-8f;
    float u2 = tt_randf();
    return sqrtf(-2.0f * logf(u1)) * cosf(2.0f * (float)M_PI * u2);
}

/* ==================== Matrix Operations ==================== */

Matrix *mat_create(int rows, int cols) {
    Matrix *m = (Matrix *)calloc(1, sizeof(Matrix));
    m->rows = rows;
    m->cols = cols;
    m->data = (float *)calloc((size_t)rows * cols, sizeof(float));
    return m;
}

void mat_free(Matrix *m) {
    if (m) {
        free(m->data);
        free(m);
    }
}

void mat_fill(Matrix *m, float val) {
    int n = m->rows * m->cols;
    for (int i = 0; i < n; i++) m->data[i] = val;
}

void mat_randn(Matrix *m, float scale) {
    int n = m->rows * m->cols;
    for (int i = 0; i < n; i++)
        m->data[i] = tt_randn() * scale;
}

float mat_get(const Matrix *m, int r, int c) {
    return m->data[r * m->cols + c];
}

void mat_set(Matrix *m, int r, int c, float val) {
    m->data[r * m->cols + c] = val;
}

Matrix *mat_mul(const Matrix *a, const Matrix *b) {
    Matrix *c = mat_create(a->rows, b->cols);
    int K = a->cols;
    for (int i = 0; i < a->rows; i++) {
        for (int k = 0; k < K; k++) {
            float aik = a->data[i * K + k];
            for (int j = 0; j < b->cols; j++) {
                c->data[i * c->cols + j] += aik * b->data[k * b->cols + j];
            }
        }
    }
    return c;
}

Matrix *mat_add(const Matrix *a, const Matrix *b) {
    Matrix *c = mat_create(a->rows, a->cols);
    int n = a->rows * a->cols;
    for (int i = 0; i < n; i++)
        c->data[i] = a->data[i] + b->data[i];
    return c;
}

Matrix *mat_transpose(const Matrix *a) {
    Matrix *c = mat_create(a->cols, a->rows);
    for (int i = 0; i < a->rows; i++)
        for (int j = 0; j < a->cols; j++)
            c->data[j * c->cols + i] = a->data[i * a->cols + j];
    return c;
}

Matrix *mat_clone(const Matrix *a) {
    Matrix *c = mat_create(a->rows, a->cols);
    memcpy(c->data, a->data, sizeof(float) * (size_t)(a->rows * a->cols));
    return c;
}

void mat_print(const Matrix *m, const char *name) {
    printf("Matrix %s [%d x %d]:\n", name, m->rows, m->cols);
    for (int i = 0; i < m->rows && i < 4; i++) {
        printf("  [");
        for (int j = 0; j < m->cols && j < 8; j++) {
            printf("%.4f", mat_get(m, i, j));
            if (j < m->cols - 1 && j < 7) printf(", ");
        }
        if (m->cols > 8) printf("...");
        printf("]\n");
    }
}

/* ==================== Softmax ==================== */

static void softmax_inplace(float *arr, int n) {
    float maxv = arr[0];
    for (int i = 1; i < n; i++)
        if (arr[i] > maxv) maxv = arr[i];
    float sum = 0;
    for (int i = 0; i < n; i++) {
        arr[i] = expf(arr[i] - maxv);
        sum += arr[i];
    }
    if (sum > 0)
        for (int i = 0; i < n; i++) arr[i] /= sum;
}

/* ==================== Layer Normalization ==================== */

static Matrix *layernorm(const Matrix *x, const Matrix *gamma, const Matrix *beta, int hidden_dim) {
    int seq_len = x->rows;
    Matrix *out = mat_create(seq_len, hidden_dim);

    for (int i = 0; i < seq_len; i++) {
        float mean = 0;
        for (int j = 0; j < hidden_dim; j++)
            mean += mat_get(x, i, j);
        mean /= hidden_dim;

        float var = 0;
        for (int j = 0; j < hidden_dim; j++) {
            float diff = mat_get(x, i, j) - mean;
            var += diff * diff;
        }
        var /= hidden_dim;
        float std_inv = 1.0f / sqrtf(var + 1e-5f);

        for (int j = 0; j < hidden_dim; j++) {
            float norm = (mat_get(x, i, j) - mean) * std_inv;
            float g = gamma ? mat_get(gamma, 0, j) : 1.0f;
            float b = beta  ? mat_get(beta, 0, j)  : 0.0f;
            mat_set(out, i, j, norm * g + b);
        }
    }
    return out;
}

/* ==================== GELU Activation ==================== */

static float gelu(float x) {
    return 0.5f * x * (1.0f + tanhf(0.7978845608f * (x + 0.044715f * x * x * x)));
}

/* ==================== Attention Layer ==================== */

AttentionLayer *attn_create(void) {
    AttentionLayer *a = (AttentionLayer *)calloc(1, sizeof(AttentionLayer));
    float scale = sqrtf(2.0f / TT_EMBED_DIM);
    a->wq = mat_create(TT_EMBED_DIM, TT_EMBED_DIM);
    a->wk = mat_create(TT_EMBED_DIM, TT_EMBED_DIM);
    a->wv = mat_create(TT_EMBED_DIM, TT_EMBED_DIM);
    a->wo = mat_create(TT_EMBED_DIM, TT_EMBED_DIM);
    mat_randn(a->wq, scale);
    mat_randn(a->wk, scale);
    mat_randn(a->wv, scale);
    mat_randn(a->wo, scale);
    a->wq_grad = mat_create(TT_EMBED_DIM, TT_EMBED_DIM);
    a->wk_grad = mat_create(TT_EMBED_DIM, TT_EMBED_DIM);
    a->wv_grad = mat_create(TT_EMBED_DIM, TT_EMBED_DIM);
    a->wo_grad = mat_create(TT_EMBED_DIM, TT_EMBED_DIM);
    return a;
}

void attn_free(AttentionLayer *a) {
    if (!a) return;
    mat_free(a->wq); mat_free(a->wk); mat_free(a->wv); mat_free(a->wo);
    mat_free(a->wq_grad); mat_free(a->wk_grad); mat_free(a->wv_grad); mat_free(a->wo_grad);
    free(a);
}

Matrix *attn_forward(AttentionLayer *attn, const Matrix *x) {
    /* x: [seq_len, embed_dim] */
    int seq_len = x->rows;

    /* Q, K, V projections */
    Matrix *Q = mat_mul(x, attn->wq);
    Matrix *K = mat_mul(x, attn->wk);
    Matrix *V = mat_mul(x, attn->wv);

    /* Scaled dot-product attention (single-head reshape) */
    /* Q, K, V: [seq_len, embed_dim], treat as single head for simplicity */
    float scale = 1.0f / sqrtf((float)TT_HEAD_DIM);

    /* Attention scores: [seq_len, seq_len] */
    Matrix *scores = mat_create(seq_len, seq_len);
    for (int i = 0; i < seq_len; i++) {
        for (int j = 0; j < seq_len; j++) {
            float s = 0;
            for (int d = 0; d < TT_EMBED_DIM; d++)
                s += mat_get(Q, i, d) * mat_get(K, j, d);
            mat_set(scores, i, j, s * scale);
        }
    }

    /* Softmax per row */
    for (int i = 0; i < seq_len; i++)
        softmax_inplace(&scores->data[i * seq_len], seq_len);

    /* Weighted sum */
    Matrix *context = mat_create(seq_len, TT_EMBED_DIM);
    for (int i = 0; i < seq_len; i++) {
        for (int d = 0; d < TT_EMBED_DIM; d++) {
            float val = 0;
            for (int j = 0; j < seq_len; j++)
                val += mat_get(scores, i, j) * mat_get(V, j, d);
            mat_set(context, i, d, val);
        }
    }

    /* Output projection */
    Matrix *out = mat_mul(context, attn->wo);

    mat_free(Q); mat_free(K); mat_free(V);
    mat_free(scores); mat_free(context);
    return out;
}

/* ==================== Feed-Forward Layer ==================== */

FeedForwardLayer *ff_create(void) {
    FeedForwardLayer *ff = (FeedForwardLayer *)calloc(1, sizeof(FeedForwardLayer));
    float scale1 = sqrtf(2.0f / TT_EMBED_DIM);
    float scale2 = sqrtf(2.0f / TT_FF_DIM);
    ff->w1 = mat_create(TT_EMBED_DIM, TT_FF_DIM);
    ff->b1 = mat_create(1, TT_FF_DIM);
    ff->w2 = mat_create(TT_FF_DIM, TT_EMBED_DIM);
    ff->b2 = mat_create(1, TT_EMBED_DIM);
    mat_randn(ff->w1, scale1);
    mat_randn(ff->w2, scale2);
    ff->w1_grad = mat_create(TT_EMBED_DIM, TT_FF_DIM);
    ff->b1_grad = mat_create(1, TT_FF_DIM);
    ff->w2_grad = mat_create(TT_FF_DIM, TT_EMBED_DIM);
    ff->b2_grad = mat_create(1, TT_EMBED_DIM);
    return ff;
}

void ff_free(FeedForwardLayer *ff) {
    if (!ff) return;
    mat_free(ff->w1); mat_free(ff->b1); mat_free(ff->w2); mat_free(ff->b2);
    mat_free(ff->w1_grad); mat_free(ff->b1_grad);
    mat_free(ff->w2_grad); mat_free(ff->b2_grad);
    free(ff);
}

Matrix *ff_forward(FeedForwardLayer *ff, const Matrix *x) {
    int seq_len = x->rows;

    /* Layer 1: x @ w1 + b1 */
    Matrix *h = mat_create(seq_len, TT_FF_DIM);
    for (int i = 0; i < seq_len; i++) {
        for (int j = 0; j < TT_FF_DIM; j++) {
            float val = mat_get(ff->b1, 0, j);
            for (int k = 0; k < TT_EMBED_DIM; k++)
                val += mat_get(x, i, k) * mat_get(ff->w1, k, j);
            mat_set(h, i, j, val);
        }
    }

    /* GELU */
    for (int i = 0; i < h->data + seq_len * TT_FF_DIM - h->data; i++)
        h->data[i] = gelu(h->data[i]);

    /* Layer 2: h @ w2 + b2 */
    Matrix *out = mat_create(seq_len, TT_EMBED_DIM);
    for (int i = 0; i < seq_len; i++) {
        for (int j = 0; j < TT_EMBED_DIM; j++) {
            float val = mat_get(ff->b2, 0, j);
            for (int k = 0; k < TT_FF_DIM; k++)
                val += mat_get(h, i, k) * mat_get(ff->w2, k, j);
            mat_set(out, i, j, val);
        }
    }

    mat_free(h);
    return out;
}

/* ==================== Transformer Block ==================== */

TransformerBlock *block_create(void) {
    TransformerBlock *b = (TransformerBlock *)calloc(1, sizeof(TransformerBlock));
    b->attn = attn_create();
    b->ff   = ff_create();
    b->ln1_gamma = mat_create(1, TT_EMBED_DIM);
    b->ln1_beta  = mat_create(1, TT_EMBED_DIM);
    b->ln2_gamma = mat_create(1, TT_EMBED_DIM);
    b->ln2_beta  = mat_create(1, TT_EMBED_DIM);
    mat_fill(b->ln1_gamma, 1.0f);
    mat_fill(b->ln2_gamma, 1.0f);
    b->ln1_gamma_grad = mat_create(1, TT_EMBED_DIM);
    b->ln1_beta_grad  = mat_create(1, TT_EMBED_DIM);
    b->ln2_gamma_grad = mat_create(1, TT_EMBED_DIM);
    b->ln2_beta_grad  = mat_create(1, TT_EMBED_DIM);
    return b;
}

void block_free(TransformerBlock *b) {
    if (!b) return;
    attn_free(b->attn);
    ff_free(b->ff);
    mat_free(b->ln1_gamma); mat_free(b->ln1_beta);
    mat_free(b->ln2_gamma); mat_free(b->ln2_beta);
    mat_free(b->ln1_gamma_grad); mat_free(b->ln1_beta_grad);
    mat_free(b->ln2_gamma_grad); mat_free(b->ln2_beta_grad);
    free(b);
}

Matrix *block_forward(TransformerBlock *block, const Matrix *x) {
    /* Layer Norm 1 */
    Matrix *ln1 = layernorm(x, block->ln1_gamma, block->ln1_beta, TT_EMBED_DIM);

    /* Self-attention */
    Matrix *attn_out = attn_forward(block->attn, ln1);

    /* Residual connection */
    Matrix *res1 = mat_add(x, attn_out);

    /* Layer Norm 2 */
    Matrix *ln2 = layernorm(res1, block->ln2_gamma, block->ln2_beta, TT_EMBED_DIM);

    /* Feed-forward */
    Matrix *ff_out = ff_forward(block->ff, ln2);

    /* Residual connection */
    Matrix *out = mat_add(res1, ff_out);

    mat_free(ln1); mat_free(attn_out); mat_free(res1);
    mat_free(ln2); mat_free(ff_out);
    return out;
}

/* ==================== Full Model ==================== */

TinyModel *model_create(int vocab_size, int embed_dim, int max_seq_len, int num_emitters) {
    TinyModel *m = (TinyModel *)calloc(1, sizeof(TinyModel));
    m->vocab_size  = vocab_size;
    m->embed_dim   = embed_dim;
    m->max_seq_len = max_seq_len;
    m->num_emitters = num_emitters;

    float scale = sqrtf(2.0f / embed_dim);
    m->token_emb = mat_create(vocab_size, embed_dim);
    m->pos_emb   = mat_create(max_seq_len, embed_dim);
    m->out_w     = mat_create(embed_dim, num_emitters);
    m->out_b     = mat_create(1, num_emitters);
    mat_randn(m->token_emb, scale);
    mat_randn(m->pos_emb, scale);
    mat_randn(m->out_w, scale);

    m->token_emb_grad = mat_create(vocab_size, embed_dim);
    m->pos_emb_grad   = mat_create(max_seq_len, embed_dim);
    m->out_w_grad     = mat_create(embed_dim, num_emitters);
    m->out_b_grad     = mat_create(1, num_emitters);

    for (int i = 0; i < TT_NUM_LAYERS; i++)
        m->blocks[i] = block_create();

    return m;
}

void model_free(TinyModel *m) {
    if (!m) return;
    mat_free(m->token_emb); mat_free(m->pos_emb);
    mat_free(m->out_w); mat_free(m->out_b);
    mat_free(m->token_emb_grad); mat_free(m->pos_emb_grad);
    mat_free(m->out_w_grad); mat_free(m->out_b_grad);
    for (int i = 0; i < TT_NUM_LAYERS; i++)
        block_free(m->blocks[i]);
    free(m);
}

Matrix *model_forward(TinyModel *m, const int *tokens, int num_tokens) {
    if (num_tokens > m->max_seq_len) num_tokens = m->max_seq_len;

    /* Build embedding: token_emb + positional_emb */
    Matrix *x = mat_create(num_tokens, m->embed_dim);
    for (int i = 0; i < num_tokens; i++) {
        int tok = tokens[i];
        if (tok < 0 || tok >= m->vocab_size) tok = 0;
        for (int d = 0; d < m->embed_dim; d++) {
            float val = mat_get(m->token_emb, tok, d) + mat_get(m->pos_emb, i, d);
            mat_set(x, i, d, val);
        }
    }

    /* Pass through transformer blocks */
    Matrix *cur = x;
    for (int i = 0; i < TT_NUM_LAYERS; i++) {
        Matrix *out = block_forward(m->blocks[i], cur);
        if (i > 0) mat_free(cur);
        cur = out;
    }

    /* Mean pooling over sequence → single vector */
    Matrix *pooled = mat_create(1, m->embed_dim);
    for (int d = 0; d < m->embed_dim; d++) {
        float sum = 0;
        for (int i = 0; i < num_tokens; i++)
            sum += mat_get(cur, i, d);
        mat_set(pooled, 0, d, sum / num_tokens);
    }

    /* Classification head: pooled @ out_w + out_b → [1, num_emitters] */
    Matrix *logits = mat_create(1, m->num_emitters);
    for (int j = 0; j < m->num_emitters; j++) {
        float val = mat_get(m->out_b, 0, j);
        for (int d = 0; d < m->embed_dim; d++)
            val += mat_get(pooled, 0, d) * mat_get(m->out_w, d, j);
        mat_set(logits, 0, j, val);
    }

    /* Free intermediates (keep logits) */
    mat_free(cur);
    mat_free(pooled);
    return logits;
}

/* ==================== Backward Pass ==================== */
/* We only update the output classification head via analytical gradients. */
/* The transformer body uses random projections (frozen). */
/* This is much faster than full backprop and still effective for classification. */

static float cross_entropy_loss(const Matrix *logits, int target) {
    float probs[TT_MAX_EMITTERS];
    int n = logits->cols;
    if (n > TT_MAX_EMITTERS) n = TT_MAX_EMITTERS;
    memcpy(probs, logits->data, sizeof(float) * (size_t)n);
    softmax_inplace(probs, n);
    if (target >= 0 && target < n) {
        return -logf(probs[target] + 1e-8f);
    }
    return 0;
}

void model_backward(TinyModel *m, const int *tokens, int num_tokens,
                     int target_emitter, float lr) {
    /* Get the pooled representation (same as forward but keeping intermediates) */
    if (num_tokens > m->max_seq_len) num_tokens = m->max_seq_len;

    /* Build embedding */
    Matrix *x = mat_create(num_tokens, m->embed_dim);
    for (int i = 0; i < num_tokens; i++) {
        int tok = tokens[i];
        if (tok < 0 || tok >= m->vocab_size) tok = 0;
        for (int d = 0; d < m->embed_dim; d++) {
            float val = mat_get(m->token_emb, tok, d) + mat_get(m->pos_emb, i, d);
            mat_set(x, i, d, val);
        }
    }

    /* Pass through transformer blocks (frozen) */
    Matrix *cur = x;
    for (int i = 0; i < TT_NUM_LAYERS; i++) {
        Matrix *out = block_forward(m->blocks[i], cur);
        if (i > 0) mat_free(cur);
        cur = out;
    }

    /* Mean pooling */
    Matrix *pooled = mat_create(1, m->embed_dim);
    for (int d = 0; d < m->embed_dim; d++) {
        float sum = 0;
        for (int i = 0; i < num_tokens; i++)
            sum += mat_get(cur, i, d);
        mat_set(pooled, 0, d, sum / num_tokens);
    }
    mat_free(cur);

    /* Forward through classification head */
    Matrix *logits = mat_create(1, m->num_emitters);
    for (int j = 0; j < m->num_emitters; j++) {
        float val = mat_get(m->out_b, 0, j);
        for (int d = 0; d < m->embed_dim; d++)
            val += mat_get(pooled, 0, d) * mat_get(m->out_w, d, j);
        mat_set(logits, 0, j, val);
    }

    /* Compute softmax probabilities */
    float probs[TT_MAX_EMITTERS];
    memcpy(probs, logits->data, sizeof(float) * (size_t)m->num_emitters);
    softmax_inplace(probs, m->num_emitters);

    /* Gradient of cross-entropy + softmax: dL/d(logit_j) = probs[j] - (j == target) */
    /* Update out_w and out_b with analytical gradients */
    for (int j = 0; j < m->num_emitters; j++) {
        float grad_j = probs[j] - (j == target_emitter ? 1.0f : 0.0f);

        /* Update bias */
        float old_b = mat_get(m->out_b, 0, j);
        mat_set(m->out_b, 0, j, old_b - lr * grad_j);

        /* Update weights */
        for (int d = 0; d < m->embed_dim; d++) {
            float old_w = mat_get(m->out_w, d, j);
            float grad_w = grad_j * mat_get(pooled, 0, d);
            mat_set(m->out_w, d, j, old_w - lr * grad_w);
        }
    }

    mat_free(logits);
    mat_free(pooled);
}

/* ==================== Model Save/Load ==================== */

int model_save(const TinyModel *m, const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;

    /* Header: magic + dimensions */
    uint32_t magic = 0x54494E59; /* "TINY" */
    fwrite(&magic, sizeof(uint32_t), 1, f);
    int32_t vocab_size = m->vocab_size;
    int32_t embed_dim  = m->embed_dim;
    int32_t seq_len    = m->max_seq_len;
    int32_t num_emitters = m->num_emitters;
    int32_t num_layers = TT_NUM_LAYERS;
    fwrite(&vocab_size, sizeof(int32_t), 1, f);
    fwrite(&embed_dim, sizeof(int32_t), 1, f);
    fwrite(&seq_len, sizeof(int32_t), 1, f);
    fwrite(&num_emitters, sizeof(int32_t), 1, f);
    fwrite(&num_layers, sizeof(int32_t), 1, f);

    /* Weights */
    fwrite(m->token_emb->data, sizeof(float), (size_t)m->vocab_size * m->embed_dim, f);
    fwrite(m->pos_emb->data, sizeof(float), (size_t)m->max_seq_len * m->embed_dim, f);
    fwrite(m->out_w->data, sizeof(float), (size_t)m->embed_dim * m->num_emitters, f);
    fwrite(m->out_b->data, sizeof(float), (size_t)m->num_emitters, f);

    for (int i = 0; i < TT_NUM_LAYERS; i++) {
        TransformerBlock *b = m->blocks[i];
        fwrite(b->ln1_gamma->data, sizeof(float), TT_EMBED_DIM, f);
        fwrite(b->ln1_beta->data, sizeof(float), TT_EMBED_DIM, f);
        fwrite(b->ln2_gamma->data, sizeof(float), TT_EMBED_DIM, f);
        fwrite(b->ln2_beta->data, sizeof(float), TT_EMBED_DIM, f);
        fwrite(b->attn->wq->data, sizeof(float), TT_EMBED_DIM * TT_EMBED_DIM, f);
        fwrite(b->attn->wk->data, sizeof(float), TT_EMBED_DIM * TT_EMBED_DIM, f);
        fwrite(b->attn->wv->data, sizeof(float), TT_EMBED_DIM * TT_EMBED_DIM, f);
        fwrite(b->attn->wo->data, sizeof(float), TT_EMBED_DIM * TT_EMBED_DIM, f);
        fwrite(b->ff->w1->data, sizeof(float), TT_EMBED_DIM * TT_FF_DIM, f);
        fwrite(b->ff->b1->data, sizeof(float), TT_FF_DIM, f);
        fwrite(b->ff->w2->data, sizeof(float), TT_FF_DIM * TT_EMBED_DIM, f);
        fwrite(b->ff->b2->data, sizeof(float), TT_EMBED_DIM, f);
    }

    fclose(f);
    return 0;
}

int model_load(TinyModel *m, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;

    uint32_t magic;
    fread(&magic, sizeof(uint32_t), 1, f);
    if (magic != 0x54494E59) { fclose(f); return -1; }

    int32_t vocab_size, embed_dim, seq_len, num_emitters, num_layers;
    fread(&vocab_size, sizeof(int32_t), 1, f);
    fread(&embed_dim, sizeof(int32_t), 1, f);
    fread(&seq_len, sizeof(int32_t), 1, f);
    fread(&num_emitters, sizeof(int32_t), 1, f);
    fread(&num_layers, sizeof(int32_t), 1, f);

    if (vocab_size != m->vocab_size || embed_dim != m->embed_dim ||
        seq_len != m->max_seq_len || num_emitters != m->num_emitters) {
        fclose(f);
        return -1;
    }

    fread(m->token_emb->data, sizeof(float), (size_t)m->vocab_size * m->embed_dim, f);
    fread(m->pos_emb->data, sizeof(float), (size_t)m->max_seq_len * m->embed_dim, f);
    fread(m->out_w->data, sizeof(float), (size_t)m->embed_dim * m->num_emitters, f);
    fread(m->out_b->data, sizeof(float), (size_t)m->num_emitters, f);

    for (int i = 0; i < TT_NUM_LAYERS; i++) {
        TransformerBlock *b = m->blocks[i];
        fread(b->ln1_gamma->data, sizeof(float), TT_EMBED_DIM, f);
        fread(b->ln1_beta->data, sizeof(float), TT_EMBED_DIM, f);
        fread(b->ln2_gamma->data, sizeof(float), TT_EMBED_DIM, f);
        fread(b->ln2_beta->data, sizeof(float), TT_EMBED_DIM, f);
        fread(b->attn->wq->data, sizeof(float), TT_EMBED_DIM * TT_EMBED_DIM, f);
        fread(b->attn->wk->data, sizeof(float), TT_EMBED_DIM * TT_EMBED_DIM, f);
        fread(b->attn->wv->data, sizeof(float), TT_EMBED_DIM * TT_EMBED_DIM, f);
        fread(b->attn->wo->data, sizeof(float), TT_EMBED_DIM * TT_EMBED_DIM, f);
        fread(b->ff->w1->data, sizeof(float), TT_EMBED_DIM * TT_FF_DIM, f);
        fread(b->ff->b1->data, sizeof(float), TT_FF_DIM, f);
        fread(b->ff->w2->data, sizeof(float), TT_FF_DIM * TT_EMBED_DIM, f);
        fread(b->ff->b2->data, sizeof(float), TT_EMBED_DIM, f);
    }

    fclose(f);
    return 0;
}

/* ==================== Vocabulary ==================== */

void ttvocab_init(TTVocab *v) {
    memset(v, 0, sizeof(TTVocab));
    v->count = 0;
    v->num_examples = 0;
}

int ttvocab_add_word(TTVocab *v, const char *word) {
    if (v->count >= TT_VOCAB_SIZE) return -1;
    /* Check if already exists */
    for (int i = 0; i < v->count; i++) {
        if (strcmp(v->words[i], word) == 0) return i;
    }
    strncpy(v->words[v->count], word, 63);
    v->words[v->count][63] = '\0';
    return v->count++;
}

int ttvocab_find(const TTVocab *v, const char *word) {
    for (int i = 0; i < v->count; i++) {
        if (strcmp(v->words[i], word) == 0) return i;
    }
    return -1;
}

int ttvocab_tokenize(const TTVocab *v, const char *prompt, int *tokens, int max_tokens) {
    char buf[TT_MAX_PROMPT_LEN];
    strncpy(buf, prompt, TT_MAX_PROMPT_LEN - 1);
    buf[TT_MAX_PROMPT_LEN - 1] = '\0';

    /* Lowercase */
    for (int i = 0; buf[i]; i++)
        buf[i] = (char)tolower((unsigned char)buf[i]);

    int count = 0;
    char *p = buf;
    while (*p && count < max_tokens) {
        while (*p && !isalpha((unsigned char)*p)) p++;
        if (!*p) break;
        char *start = p;
        while (*p && isalpha((unsigned char)*p)) p++;
        char saved = *p;
        *p = '\0';

        int idx = ttvocab_find(v, start);
        if (idx >= 0) {
            tokens[count++] = idx;
        }
        *p = saved;
    }
    return count;
}

void ttvocab_add_example(TTVocab *v, const char *prompt, int emitter_id) {
    if (v->num_examples >= TT_MAX_EXAMPLES) return;
    int idx = v->num_examples;
    strncpy(v->prompts[idx], prompt, TT_MAX_PROMPT_LEN - 1);
    v->prompts[idx][TT_MAX_PROMPT_LEN - 1] = '\0';
    v->emitter_ids[idx] = emitter_id;
    v->prompt_lengths[idx] = ttvocab_tokenize(v, prompt, v->prompt_tokens[idx], TT_MAX_TOKENS);
    v->num_examples++;
}

int ttvocab_save(const TTVocab *v, const char *path) {
    char vocab_path[1024];
    snprintf(vocab_path, sizeof(vocab_path), "%s.vocab", path);
    FILE *f = fopen(vocab_path, "wb");
    if (!f) return -1;

    int32_t magic = 0x564F4342; /* "VOCB" */
    fwrite(&magic, sizeof(int32_t), 1, f);
    fwrite(&v->count, sizeof(int32_t), 1, f);

    for (int i = 0; i < v->count; i++) {
        int32_t len = (int32_t)strlen(v->words[i]);
        fwrite(&len, sizeof(int32_t), 1, f);
        fwrite(v->words[i], 1, (size_t)len, f);
    }

    fclose(f);
    return 0;
}

int ttvocab_load(TTVocab *v, const char *path) {
    char vocab_path[1024];
    snprintf(vocab_path, sizeof(vocab_path), "%s.vocab", path);
    FILE *f = fopen(vocab_path, "rb");
    if (!f) return -1;

    int32_t magic;
    fread(&magic, sizeof(int32_t), 1, f);
    if (magic != 0x564F4342) { fclose(f); return -1; }

    int32_t count;
    fread(&count, sizeof(int32_t), 1, f);
    if (count > TT_VOCAB_SIZE) { fclose(f); return -1; }

    v->count = 0;
    for (int i = 0; i < count; i++) {
        int32_t len;
        fread(&len, sizeof(int32_t), 1, f);
        if (len > 63) len = 63;
        fread(v->words[i], 1, (size_t)len, f);
        v->words[i][len] = '\0';
        v->count++;
    }

    fclose(f);
    return 0;
}

/* ==================== Training Data ==================== */

TrainingData *train_data_create(void) {
    TrainingData *td = (TrainingData *)calloc(1, sizeof(TrainingData));
    td->num_pairs = 0;
    return td;
}

void train_data_free(TrainingData *td) {
    free(td);
}

int train_data_add(TrainingData *td, const char *prompt, int emitter_id, TTVocab *v) {
    if (td->num_pairs >= TT_MAX_EXAMPLES) return -1;
    int idx = td->num_pairs;
    strncpy(td->prompts[idx], prompt, TT_MAX_PROMPT_LEN - 1);
    td->prompts[idx][TT_MAX_PROMPT_LEN - 1] = '\0';
    td->emitter_ids[idx] = emitter_id;
    td->vocab_lengths[idx] = ttvocab_tokenize(v, prompt, td->vocab[idx], TT_MAX_TOKENS);
    td->num_pairs++;
    return 0;
}

/* Emitter name → ID mapping (must match EMITTER_NAMES in embed.c) */
static const char *emitter_names[] = {
    "math","input_loop","loop","for_sum","print_even","find_max",
    "countdown","fib_seq","input_sort","median","string_cat",
    "string_compare","array_assign","array_reverse","array_find",
    "input_fact","array_vmath","read_file","write_file","string_to_num",
    "timer","factorial","fizzbuzz","primes","even_odd","power",
    "mult_table","guess","gcd","hello_name","random","array_min_max",
    "bool_demo","bit_check","fann_create","fann_train","fann_run",
    "average","selection_sort","palindrome","lcm","collatz",
    "sum_of_digits","reverse_string","armstrong","perfect_number",
    "count_vowels","anagram_check","string_to_upper","string_to_lower",
    "caesar_cipher","palindrome_string","bubble_sort","binary_search",
    "square_root","prime_factorization","standard_deviation",
    "compound_interest","decimal_to_binary","dice_roll","double_math",
    "double_circle_area","double_average","double_compound_interest",
    "double_pythagoras","double_temp_convert","double_sqrt","function",
    "string_length","stack","queue","insertion_sort","calculator",
    "unit_converter","rock_paper_scissors","pyramid","temp_converter_menu",
    "sort_stats","string_analyzer","number_analyzer","filter_numbers",
    "random_generator","math_menu","quiz_game","bmi_calculator",
    "statistics_suite","linked_list","binary_search_tree","tree_traversal",
    "graph_bfs_dfs","n_queens","sudoku","levenshtein","maze_generator",
    "maze_solver","monte_carlo","matrix_mul","matrix_transpose",
    "numerical_integration","complex_numbers","linear_regression",
    "base_converter","freq_analysis","shuffle","weighted_random",
    "ascii_table","bignum_math","password_card","chess_problem",
    "shell_repl","webserver","sdl_window","sdl_button","thread",
    "scheduler","shell_exec","json","crypto","bluetooth_ble",
    "serial_rs232","gpio","gps","timer_date","sdl_sound","sdl_joystick",
    "sdl_mouse","fractal","cluster_3x1","reload","coordinate_grid",
    "turmite","crossword","linter","double_power","double_volume_sphere",
    "double_discount","double_simple_interest","double_bmi",
    "double_standard_deviation","double_kinetic_energy","hello_world",
    "string_find","string_split","switch_demo","type_convert",
    "iterative_factorial","random_walk","bar_chart","hanoi_tower",
    "ascii_art","number_to_words","temperature_table","loop_demo",
    "pointer","struct","hex_binary","shell_args","time"
};
#define NUM_EMITTER_NAMES (sizeof(emitter_names) / sizeof(emitter_names[0]))

static int find_emitter_id(const char *name) {
    for (int i = 0; i < (int)NUM_EMITTER_NAMES; i++) {
        if (strcmp(emitter_names[i], name) == 0) return i;
    }
    return -1;
}

/* Map DSL keywords to emitter ID (simple keyword → emitter mapping) */
typedef struct {
    const char *keyword;
    int emitter_id;
} KeywordEmitterMap;

static const KeywordEmitterMap keyword_map[] = {
    {"fibonacci", 7}, {"fib", 7}, {"fib_seq", 7},
    {"factorial", 21}, {"fakult", 21},
    {"bubble_sort", 52}, {"sort", 52}, {"sortiere", 52},
    {"insertion_sort", 71}, {"selection_sort", 38},
    {"binary_search", 53},
    {"hello_name", 29}, {"hallo", 29}, {"name", 29},
    {"fizzbuzz", 22},
    {"primes", 23}, {"prime", 23}, {"prim", 23},
    {"countdown", 6},
    {"sum", 0}, {"summe", 0}, {"add", 0},
    {"palindrome", 39}, {"palindrom", 39},
    {"lcm", 40}, {"kgv", 40},
    {"collatz", 41},
    {"sum_of_digits", 42}, {"quersumme", 42},
    {"reverse_string", 43}, {"umkehr", 43},
    {"armstrong", 44},
    {"perfect_number", 45}, {"perfekte", 45},
    {"count_vowels", 46}, {"vokale", 46},
    {"anagram", 47},
    {"caesar", 50}, {"cipher", 50}, {"chiffre", 50},
    {"calculator", 72}, {"rechner", 72},
    {"unit_converter", 73},
    {"rock_paper_scissors", 74}, {"schere", 74}, {"stein", 74},
    {"pyramid", 75}, {"pyramide", 75},
    {"bmi", 84}, {"bmi_calculator", 84},
    {"matrix_mul", 96}, {"matrix", 96},
    {"linked_list", 86}, {"list", 86}, {"liste", 86},
    {"binary_search_tree", 87}, {"baum", 87}, {"tree", 87},
    {"graph", 88}, {"bfs", 88}, {"dfs", 88},
    {"n_queens", 90}, {"dame", 90},
    {"sudoku", 91},
    {"levenshtein", 92},
    {"maze", 93}, {"labyrinth", 93},
    {"monte_carlo", 95},
    {"webserver", 110},
    {"sdl", 111}, {"window", 111},
    {"fractal", 126}, {"mandelbrot", 126},
    {"json", 116},
    {"crypto", 117}, {"encrypt", 117}, {"decrypt", 117},
    {"shuffle", 103}, {"misch", 103},
    {"bar_chart", 155}, {"balken", 155},
    {"hanoi", 156},
    {"hello_world", 148},
    {"string_find", 149}, {"string_split", 150},
    {"switch", 151},
    {"random_walk", 154},
    {"stack", 69}, {"queue", 70},
    {"dice", 59}, {"wuerfel", 59},
    {"guess", 27}, {"rate", 27},
    {"gcd", 28}, {"ggT", 28},
    {"frequency", 102}, {"haeufigkeit", 102},
    {"password", 107},
    {"chess", 108}, {"schach", 108},
    {"shell", 109}, {"repl", 109},
    {"bluetooth", 118}, {"ble", 118},
    {"serial", 119}, {"rs232", 119},
    {"gpio", 120},
    {"gps", 121},
    {"timer", 122}, {"zeit", 122}, {"datum", 122},
    {"sound", 123}, {"audio", 123},
    {"joystick", 124},
    {"mouse", 125},
    {"turmite", 130},
    {"crossword", 131},
    {"linter", 132},
    {"thread", 113}, {"scheduler", 114},
    {"decimal_to_binary", 58}, {"bin", 58},
    {"compound_interest", 57}, {"zins", 57},
    {"standard_deviation", 56}, {"abweichung", 56},
    {"square_root", 54}, {"wurzel", 54},
    {"prime_factorization", 55}, {"zerlegung", 55},
    {"dice_roll", 59},
    {"double_math", 60},
    {"double_circle_area", 61}, {"kreis", 61},
    {"double_average", 62}, {"durchschnitt", 62},
    {"double_pythagoras", 64}, {"pythagoras", 64},
    {"double_temp_convert", 65}, {"celsius", 65}, {"fahrenheit", 65},
    {"double_sqrt", 66},
    {"double_power", 133},
    {"double_volume_sphere", 134}, {"kugel", 134},
    {"double_discount", 135}, {"rabatt", 135},
    {"double_simple_interest", 136},
    {"double_bmi", 137},
    {"double_standard_deviation", 138},
    {"double_kinetic_energy", 139}, {"energie", 139},
    {"linear_regression", 100},
    {"numerical_integration", 98}, {"integration", 98},
    {"complex_numbers", 99},
    {"base_converter", 101},
    {"weighted_random", 104},
    {"ascii_table", 105},
    {"bignum_math", 106}, {"grozzahl", 106},
    {"number_to_words", 158},
    {"temperature_table", 159},
    {"loop_demo", 160},
    {"pointer", 161}, {"zeiger", 161},
    {"struct", 162}, {"struktur", 162},
    {"hex_binary", 163}, {"hex", 163},
    {"shell_args", 164},
    {"time_demo", 165},
    {"string_length", 68}, {"laenge", 68},
    {"string_to_upper", 48}, {"gross", 48},
    {"string_to_lower", 49}, {"klein", 49},
    {"string_cat", 10}, {"concat", 10},
    {"string_compare", 11},
    {"array_assign", 12}, {"array", 12},
    {"array_reverse", 13},
    {"array_find", 14},
    {"array_vmath", 16},
    {"array_min_max", 31},
    {"read_file", 17}, {"lesen", 17},
    {"write_file", 18}, {"schreiben", 18},
    {"string_to_num", 19},
    {"bool_demo", 32}, {"boolean", 32},
    {"bit_check", 33}, {"bit", 33},
    {"even_odd", 24}, {"gerade", 24}, {"ungerade", 24},
    {"power", 25}, {"potenz", 25},
    {"mult_table", 26}, {"einmaleins", 26},
    {"random", 30}, {"zufall", 30},
    {"leap_year", 34}, {"schaltjahr", 34},
    {"temp_convert", 35}, {"temperatur", 35},
    {"circle_area", 36},
    {"average", 37}, {"median", 38},
    {"sort_stats", 77},
    {"string_analyzer", 78},
    {"number_analyzer", 79},
    {"filter_numbers", 80},
    {"random_generator", 81},
    {"math_menu", 82},
    {"quiz_game", 83},
    {"statistics_suite", 85},
    {"function", 67}, {"funktion", 67},
    {"iterative_factorial", 153},
    {"type_convert", 152},
    {NULL, 0}
};

static int match_keyword_to_emitter(const char *word) {
    for (int i = 0; keyword_map[i].keyword != NULL; i++) {
        if (strcmp(word, keyword_map[i].keyword) == 0) {
            return keyword_map[i].emitter_id;
        }
    }
    return -1;
}

int train_data_load_dsl(TrainingData *td, const char *dsl_dir, TTVocab *v) {
    char path[1024];
    DIR *dir = opendir(dsl_dir);
    if (!dir) return -1;

    struct dirent *ent;
    int loaded = 0;

    while ((ent = readdir(dir)) != NULL) {
        const char *name = ent->d_name;
        int len = (int)strlen(name);
        if (len < 7 || strcmp(name + len - 6, ".l1dsl") != 0) continue;

        snprintf(path, sizeof(path), "%s/%s", dsl_dir, name);

        FILE *f = fopen(path, "r");
        if (!f) continue;

        char parser_line[2048] = "";
        int emitter_id = -1;

        char line[2048];
        while (fgets(line, sizeof(line), f)) {
            /* Strip newline */
            line[strcspn(line, "\r\n")] = '\0';

            if (strncmp(line, "parser:", 7) == 0) {
                char *p = line + 7;
                while (*p == ' ') p++;
                /* Remove quotes */
                if (*p == '"') p++;
                strncpy(parser_line, p, sizeof(parser_line) - 1);
                int plen = (int)strlen(parser_line);
                if (plen > 0 && parser_line[plen - 1] == '"')
                    parser_line[plen - 1] = '\0';
            }

            if (strncmp(line, "match:", 6) == 0) {
                /* Match flag → we can map some flags to emitters */
            }
        }
        fclose(f);

        if (parser_line[0] == '\0') continue;

        /* Determine emitter from filename stem */
        char stem[256];
        strncpy(stem, name, sizeof(stem) - 1);
        stem[sizeof(stem) - 1] = '\0';
        char *dot = strrchr(stem, '.');
        if (dot) *dot = '\0';

        /* Try to find emitter from filename */
        emitter_id = find_emitter_id(stem);
        if (emitter_id < 0) {
            /* Try keyword matching on parser line */
            char pcopy[2048];
            strncpy(pcopy, parser_line, sizeof(pcopy) - 1);
            pcopy[sizeof(pcopy) - 1] = '\0';
            for (char *w = strtok(pcopy, ", "); w; w = strtok(NULL, ", ")) {
                emitter_id = match_keyword_to_emitter(w);
                if (emitter_id >= 0) break;
            }
        }

        /* Fallback: try to match stem keywords */
        if (emitter_id < 0) {
            emitter_id = match_keyword_to_emitter(stem);
        }

        if (emitter_id < 0) emitter_id = 0; /* default to math */

        /* Add to vocabulary */
        char pcopy[2048];
        strncpy(pcopy, parser_line, sizeof(pcopy) - 1);
        pcopy[sizeof(pcopy) - 1] = '\0';
        for (char *w = strtok(pcopy, ", "); w; w = strtok(NULL, ", ")) {
            /* Trim whitespace */
            while (*w == ' ') w++;
            ttvocab_add_word(v, w);
        }
        ttvocab_add_word(v, stem);

        /* Add training example */
        train_data_add(td, parser_line, emitter_id, v);
        ttvocab_add_example(v, parser_line, emitter_id);
        loaded++;

        /* Add augmented examples: individual keywords → same emitter */
        strncpy(pcopy, parser_line, sizeof(pcopy) - 1);
        pcopy[sizeof(pcopy) - 1] = '\0';
        char keywords[16][64];
        int n_kw = 0;
        for (char *w = strtok(pcopy, ", "); w; w = strtok(NULL, ", ")) {
            while (*w == ' ') w++;
            if (n_kw < 16 && v->count < TT_TRAIN_VOCAB) {
                strncpy(keywords[n_kw], w, 63);
                keywords[n_kw][63] = '\0';
                n_kw++;
            }
        }
        /* Add pairs of keywords as training examples */
        for (int i = 0; i < n_kw; i++) {
            train_data_add(td, keywords[i], emitter_id, v);
            for (int j = i + 1; j < n_kw; j++) {
                char augmented[TT_MAX_PROMPT_LEN];
                snprintf(augmented, sizeof(augmented), "%s %s", keywords[i], keywords[j]);
                train_data_add(td, augmented, emitter_id, v);
            }
        }
    }

    closedir(dir);
    return loaded;
}

/* ==================== Inference API ==================== */

static TinyModel *tt_model = NULL;
static TTVocab    tt_vocab;
static int        tt_initialized = 0;

int tt_init(const char *model_path) {
    if (tt_initialized) return 0;

    ttvocab_init(&tt_vocab);

    /* Try to load vocabulary from model file first */
    if (model_path && model_path[0]) {
        if (ttvocab_load(&tt_vocab, model_path) == 0) {
            printf("Loaded vocabulary: %d words\n", tt_vocab.count);
            tt_model = model_create(tt_vocab.count, TT_EMBED_DIM, TT_MAX_TOKENS, TT_MAX_EMITTERS);
            if (model_load(tt_model, model_path) == 0) {
                tt_initialized = 1;
                return 0;
            }
            model_free(tt_model);
            tt_model = NULL;
        }
    }

    /* Fallback: build vocabulary from built-in data */
    for (int i = 0; i < (int)NUM_EMITTER_NAMES; i++) {
        ttvocab_add_word(&tt_vocab, emitter_names[i]);
    }
    for (int i = 0; keyword_map[i].keyword != NULL; i++) {
        ttvocab_add_word(&tt_vocab, keyword_map[i].keyword);
    }

    tt_model = model_create(tt_vocab.count, TT_EMBED_DIM, TT_MAX_TOKENS, TT_MAX_EMITTERS);

    /* Try to load pre-trained model */
    if (model_path && model_path[0]) {
        if (model_load(tt_model, model_path) == 0) {
            tt_initialized = 1;
            return 0;
        }
    }

    /* No model found – will use fallback (keyword scoring) */
    tt_initialized = 1;
    return 1; /* 1 = no model loaded, using fallback */
}

int tt_predict(const char *prompt, float *score) {
    if (!tt_initialized || !tt_model) {
        if (score) *score = 0;
        return -1;
    }

    int tokens[TT_MAX_TOKENS];
    int n = ttvocab_tokenize(&tt_vocab, prompt, tokens, TT_MAX_TOKENS);
    if (n == 0) {
        if (score) *score = 0;
        return -1;
    }

    Matrix *logits = model_forward(tt_model, tokens, n);

    /* Apply softmax */
    softmax_inplace(logits->data, logits->cols);

    /* Find best */
    int best = 0;
    float best_p = logits->data[0];
    for (int i = 1; i < logits->cols; i++) {
        if (logits->data[i] > best_p) {
            best_p = logits->data[i];
            best = i;
        }
    }

    if (score) *score = best_p;
    mat_free(logits);
    return best;
}

int tt_train(const char *dsl_dir, const char *model_path, int epochs, float lr) {
    TTVocab vocab;
    ttvocab_init(&vocab);

    TrainingData *td = train_data_create();

    printf("Loading DSL training data from %s...\n", dsl_dir);
    int loaded = train_data_load_dsl(td, dsl_dir, &vocab);
    printf("Loaded %d DSL rules, %d training examples, %d vocab words\n",
           loaded, td->num_pairs, vocab.count);

    if (td->num_pairs == 0) {
        printf("No training data found!\n");
        train_data_free(td);
        return -1;
    }

    /* Create model */
    TinyModel *m = model_create(vocab.count, TT_EMBED_DIM, TT_MAX_TOKENS, TT_MAX_EMITTERS);

    printf("Training tiny transformer: %d vocab, %d embed_dim, %d layers, %d heads\n",
           vocab.count, TT_EMBED_DIM, TT_NUM_LAYERS, TT_NUM_HEADS);
    printf("Parameters: ~%dK\n",
           (int)((vocab.count * TT_EMBED_DIM + TT_MAX_TOKENS * TT_EMBED_DIM +
                  TT_EMBED_DIM * TT_MAX_EMITTERS + TT_MAX_EMITTERS +
                  TT_NUM_LAYERS * (4 * TT_EMBED_DIM * TT_EMBED_DIM +
                                   4 * TT_EMBED_DIM +
                                   2 * TT_EMBED_DIM * TT_FF_DIM +
                                   2 * TT_FF_DIM)) / 1000));

    /* Training loop */
    srand((unsigned)time(NULL));
    for (int epoch = 0; epoch < epochs; epoch++) {
        float total_loss = 0;
        int correct = 0;

        /* Shuffle indices */
        int indices[TT_MAX_EXAMPLES];
        for (int i = 0; i < td->num_pairs; i++) indices[i] = i;
        for (int i = td->num_pairs - 1; i > 0; i--) {
            int j = rand() % (i + 1);
            int tmp = indices[i]; indices[i] = indices[j]; indices[j] = tmp;
        }

        for (int i = 0; i < td->num_pairs; i++) {
            int idx = indices[i];
            int *tokens = td->vocab[idx];
            int n = td->vocab_lengths[idx];
            int target = td->emitter_ids[idx];

            if (n == 0 || target < 0 || target >= TT_MAX_EMITTERS) continue;

            /* Forward */
            Matrix *logits = model_forward(m, tokens, n);
            float loss = cross_entropy_loss(logits, target);
            total_loss += loss;

            /* Check prediction */
            int best = 0;
            float best_p = logits->data[0];
            for (int j = 1; j < logits->cols; j++) {
                if (logits->data[j] > best_p) {
                    best_p = logits->data[j];
                    best = j;
                }
            }
            if (best == target) correct++;
            mat_free(logits);

            /* Backward */
            model_backward(m, tokens, n, target, lr);
        }

        float avg_loss = total_loss / td->num_pairs;
        float accuracy = (float)correct / td->num_pairs * 100.0f;
        printf("Epoch %3d/%d  loss=%.4f  accuracy=%.1f%%\n",
               epoch + 1, epochs, avg_loss, accuracy);
    }

    /* Save model */
    if (model_save(m, model_path) == 0) {
        printf("Model saved to %s\n", model_path);
    } else {
        printf("Failed to save model to %s\n", model_path);
    }

    /* Save vocabulary */
    if (ttvocab_save(&vocab, model_path) == 0) {
        printf("Vocabulary saved to %s.vocab\n", model_path);
    }

    model_free(m);
    train_data_free(td);
    return 0;
}

/* ==================== Code Embeddings ==================== */

static float code_emb_hash(const char *code) {
    unsigned long hash = 5381;
    const char *p = code;
    while (*p) {
        hash = ((hash << 5) + hash) + (unsigned char)*p;
        p++;
    }
    return (float)(hash % 10000) / 10000.0f;
}

CodeEmbedder *code_emb_create(void) {
    CodeEmbedder *ce = calloc(1, sizeof(CodeEmbedder));
    if (!ce) return NULL;

    ce->code_emb = mat_create(TT_MAX_PATTERNS, TT_CODE_EMBED_DIM);
    ce->prompt_emb = mat_create(1, TT_CODE_EMBED_DIM);
    ce->sim_w = mat_create(TT_CODE_EMBED_DIM, TT_CODE_EMBED_DIM);

    /* Initialize with small random values */
    float scale = sqrtf(2.0f / TT_CODE_EMBED_DIM);
    for (int i = 0; i < ce->sim_w->rows * ce->sim_w->cols; i++) {
        ce->sim_w->data[i] = ((float)rand() / (float)RAND_MAX - 0.5f) * scale;
    }

    ce->num_patterns = 0;
    return ce;
}

void code_emb_free(CodeEmbedder *ce) {
    if (!ce) return;
    mat_free(ce->code_emb);
    mat_free(ce->prompt_emb);
    mat_free(ce->sim_w);
    free(ce);
}

int code_emb_add_pattern(CodeEmbedder *ce, const char *name, const char *code) {
    if (!ce || ce->num_patterns >= TT_MAX_PATTERNS) return -1;

    int idx = ce->num_patterns;
    snprintf(ce->pattern_names[idx], 64, "%s", name);

    /* Create embedding from code hash and character distribution */
    float hash = code_emb_hash(code);
    for (int d = 0; d < TT_CODE_EMBED_DIM; d++) {
        float val = 0;
        const char *p = code;
        int pos = 0;
        while (*p && pos < 100) {
            val += ((float)((unsigned char)*p) / 255.0f) * (d + 1);
            p++;
            pos++;
        }
        val = val / 100.0f + hash * (d % 3 + 1);
        mat_set(ce->code_emb, idx, d, val);
    }

    ce->num_patterns++;
    return idx;
}

float code_emb_similarity(CodeEmbedder *ce, const char *prompt, const char *pattern_name) {
    if (!ce) return 0;

    /* Find pattern index */
    int pat_idx = -1;
    for (int i = 0; i < ce->num_patterns; i++) {
        if (strcmp(ce->pattern_names[i], pattern_name) == 0) {
            pat_idx = i;
            break;
        }
    }
    if (pat_idx < 0) return 0;

    /* Create prompt embedding */
    float hash = code_emb_hash(prompt);
    for (int d = 0; d < TT_CODE_EMBED_DIM; d++) {
        float val = 0;
        const char *p = prompt;
        int pos = 0;
        while (*p && pos < 100) {
            val += ((float)((unsigned char)*p) / 255.0f) * (d + 1);
            p++;
            pos++;
        }
        val = val / 100.0f + hash * (d % 3 + 1);
        mat_set(ce->prompt_emb, 0, d, val);
    }

    /* Compute cosine similarity */
    float dot = 0, norm_a = 0, norm_b = 0;
    for (int d = 0; d < TT_CODE_EMBED_DIM; d++) {
        float a = mat_get(ce->prompt_emb, 0, d);
        float b = mat_get(ce->code_emb, pat_idx, d);
        dot += a * b;
        norm_a += a * a;
        norm_b += b * b;
    }

    if (norm_a == 0 || norm_b == 0) return 0;
    return dot / (sqrtf(norm_a) * sqrtf(norm_b));
}

int code_emb_find_similar(CodeEmbedder *ce, const char *prompt, float *scores, int top_k) {
    if (!ce) return 0;

    /* Create prompt embedding */
    float hash = code_emb_hash(prompt);
    for (int d = 0; d < TT_CODE_EMBED_DIM; d++) {
        float val = 0;
        const char *p = prompt;
        int pos = 0;
        while (*p && pos < 100) {
            val += ((float)((unsigned char)*p) / 255.0f) * (d + 1);
            p++;
            pos++;
        }
        val = val / 100.0f + hash * (d % 3 + 1);
        mat_set(ce->prompt_emb, 0, d, val);
    }

    /* Compute similarities */
    float sims[TT_MAX_PATTERNS];
    for (int i = 0; i < ce->num_patterns; i++) {
        float dot = 0, norm_a = 0, norm_b = 0;
        for (int d = 0; d < TT_CODE_EMBED_DIM; d++) {
            float a = mat_get(ce->prompt_emb, 0, d);
            float b = mat_get(ce->code_emb, i, d);
            dot += a * b;
            norm_a += a * a;
            norm_b += b * b;
        }
        sims[i] = (norm_a > 0 && norm_b > 0) ? dot / (sqrtf(norm_a) * sqrtf(norm_b)) : 0;
    }

    /* Sort and return top_k */
    int indices[TT_MAX_PATTERNS];
    for (int i = 0; i < ce->num_patterns; i++) indices[i] = i;

    for (int i = 0; i < top_k && i < ce->num_patterns; i++) {
        int best = i;
        for (int j = i + 1; j < ce->num_patterns; j++) {
            if (sims[j] > sims[best]) best = j;
        }
        int tmp = indices[i]; indices[i] = indices[best]; indices[best] = tmp;
        float tmp_s = sims[i]; sims[i] = sims[best]; sims[best] = tmp_s;
        scores[i] = sims[i];
    }

    return top_k < ce->num_patterns ? top_k : ce->num_patterns;
}

/* ==================== Attention Pattern Selector ==================== */

AttentionSelector *attn_sel_create(int num_patterns) {
    AttentionSelector *sel = calloc(1, sizeof(AttentionSelector));
    if (!sel) return NULL;

    sel->num_patterns = num_patterns;
    sel->wq = mat_create(TT_EMBED_DIM, TT_EMBED_DIM);
    sel->wk = mat_create(TT_EMBED_DIM, TT_EMBED_DIM);
    sel->wv = mat_create(TT_EMBED_DIM, TT_EMBED_DIM);
    sel->proj = mat_create(TT_EMBED_DIM, num_patterns);

    float scale = sqrtf(2.0f / TT_EMBED_DIM);
    for (int i = 0; i < sel->wq->rows * sel->wq->cols; i++)
        sel->wq->data[i] = ((float)rand() / (float)RAND_MAX - 0.5f) * scale;
    for (int i = 0; i < sel->wk->rows * sel->wk->cols; i++)
        sel->wk->data[i] = ((float)rand() / (float)RAND_MAX - 0.5f) * scale;
    for (int i = 0; i < sel->wv->rows * sel->wv->cols; i++)
        sel->wv->data[i] = ((float)rand() / (float)RAND_MAX - 0.5f) * scale;
    for (int i = 0; i < sel->proj->rows * sel->proj->cols; i++)
        sel->proj->data[i] = ((float)rand() / (float)RAND_MAX - 0.5f) * scale;

    return sel;
}

void attn_sel_free(AttentionSelector *sel) {
    if (!sel) return;
    mat_free(sel->wq);
    mat_free(sel->wk);
    mat_free(sel->wv);
    mat_free(sel->proj);
    free(sel);
}

int attn_sel_predict(AttentionSelector *sel, const Matrix *prompt_emb,
                     float *scores, int *best_idx) {
    if (!sel || !prompt_emb) return -1;

    /* Q = prompt_emb * wq */
    Matrix *Q = mat_mul(prompt_emb, sel->wq);
    /* K = prompt_emb * wk */
    Matrix *K = mat_mul(prompt_emb, sel->wk);
    /* V = prompt_emb * wv */
    Matrix *V = mat_mul(prompt_emb, sel->wv);

    /* Attention scores = Q * K^T / sqrt(d) */
    Matrix *Kt = mat_transpose(K);
    Matrix *scores_mat = mat_mul(Q, Kt);

    float scale = sqrtf((float)TT_EMBED_DIM);
    for (int i = 0; i < scores_mat->rows * scores_mat->cols; i++) {
        scores_mat->data[i] /= scale;
    }

    /* Softmax */
    float max_val = scores_mat->data[0];
    for (int i = 1; i < scores_mat->cols; i++) {
        if (scores_mat->data[i] > max_val) max_val = scores_mat->data[i];
    }
    float sum = 0;
    for (int i = 0; i < scores_mat->cols; i++) {
        scores_mat->data[i] = expf(scores_mat->data[i] - max_val);
        sum += scores_mat->data[i];
    }
    for (int i = 0; i < scores_mat->cols; i++) {
        scores_mat->data[i] /= sum;
    }

    /* Context = scores * V */
    Matrix *context = mat_mul(scores_mat, V);

    /* Output projection */
    Matrix *output = mat_mul(context, sel->proj);

    /* Copy scores and find best */
    *best_idx = 0;
    float best_score = output->data[0];
    for (int i = 0; i < sel->num_patterns && i < output->cols; i++) {
        scores[i] = output->data[i];
        if (output->data[i] > best_score) {
            best_score = output->data[i];
            *best_idx = i;
        }
    }

    mat_free(Q);
    mat_free(K);
    mat_free(V);
    mat_free(Kt);
    mat_free(scores_mat);
    mat_free(context);
    mat_free(output);

    return *best_idx;
}

/* ==================== Prompt Expander ==================== */

PromptExpander *expander_create(int vocab_size, int embed_dim) {
    PromptExpander *pe = calloc(1, sizeof(PromptExpander));
    if (!pe) return NULL;

    pe->vocab_size = vocab_size;
    pe->embed_dim = embed_dim;
    pe->w_emb = mat_create(vocab_size, embed_dim);
    pe->w_dec = mat_create(embed_dim, vocab_size);
    pe->w_ctx = mat_create(embed_dim, embed_dim);

    float scale = sqrtf(2.0f / embed_dim);
    for (int i = 0; i < pe->w_emb->rows * pe->w_emb->cols; i++)
        pe->w_emb->data[i] = ((float)rand() / (float)RAND_MAX - 0.5f) * scale;
    for (int i = 0; i < pe->w_dec->rows * pe->w_dec->cols; i++)
        pe->w_dec->data[i] = ((float)rand() / (float)RAND_MAX - 0.5f) * scale;
    for (int i = 0; i < pe->w_ctx->rows * pe->w_ctx->cols; i++)
        pe->w_ctx->data[i] = ((float)rand() / (float)RAND_MAX - 0.5f) * scale;

    return pe;
}

void expander_free(PromptExpander *pe) {
    if (!pe) return;
    mat_free(pe->w_emb);
    mat_free(pe->w_dec);
    mat_free(pe->w_ctx);
    free(pe);
}

int expander_paraphrase(PromptExpander *pe, const char *prompt,
                        char *output, int output_size) {
    if (!pe || !prompt || !output) return -1;

    /* Simple approach: create embedding and decode most likely tokens */
    Matrix *emb = mat_create(1, pe->embed_dim);
    const char *p = prompt;
    int pos = 0;
    while (*p && pos < pe->embed_dim) {
        emb->data[pos] = (float)((unsigned char)*p) / 255.0f;
        p++;
        pos++;
    }

    /* Decode */
    Matrix *logits = mat_mul(emb, pe->w_dec);

    /* Copy prompt as base (simple paraphrase: just add variations) */
    snprintf(output, output_size, "%s", prompt);

    /* Add common paraphrases */
    if (strstr(prompt, "fibonacci") || strstr(prompt, "fib")) {
        snprintf(output, output_size, "%s sequence", prompt);
    } else if (strstr(prompt, "sort")) {
        snprintf(output, output_size, "order %s", prompt + 5);
    } else if (strstr(prompt, "hello")) {
        snprintf(output, output_size, "greet user");
    }

    mat_free(emb);
    mat_free(logits);
    return 0;
}

/* ==================== Simple RL Agent ==================== */

RLAgent *rl_agent_create(int num_states, int num_actions, float lr, float gamma) {
    RLAgent *agent = calloc(1, sizeof(RLAgent));
    if (!agent) return NULL;

    agent->num_states = num_states;
    agent->num_actions = num_actions;
    agent->learning_rate = lr;
    agent->discount_factor = gamma;
    agent->exploration_rate = 0.1f;
    agent->last_state = 0;
    agent->last_action = 0;

    agent->q_table = calloc(num_states * num_actions, sizeof(float));
    if (!agent->q_table) {
        free(agent);
        return NULL;
    }

    return agent;
}

void rl_agent_free(RLAgent *agent) {
    if (!agent) return;
    free(agent->q_table);
    free(agent);
}

int rl_agent_choose_action(RLAgent *agent, int state) {
    if (!agent) return 0;

    /* Epsilon-greedy */
    if ((float)rand() / (float)RAND_MAX < agent->exploration_rate) {
        return rand() % agent->num_actions;
    }

    /* Choose best action */
    int best_action = 0;
    float best_q = agent->q_table[state * agent->num_actions];
    for (int a = 1; a < agent->num_actions; a++) {
        float q = agent->q_table[state * agent->num_actions + a];
        if (q > best_q) {
            best_q = q;
            best_action = a;
        }
    }

    agent->last_state = state;
    agent->last_action = best_action;
    return best_action;
}

void rl_agent_learn(RLAgent *agent, int state, int action, float reward, int next_state) {
    if (!agent) return;

    /* Q-learning update */
    float current_q = agent->q_table[state * agent->num_actions + action];

    /* Find max Q for next state */
    float max_next_q = agent->q_table[next_state * agent->num_actions];
    for (int a = 1; a < agent->num_actions; a++) {
        float q = agent->q_table[next_state * agent->num_actions + a];
        if (q > max_next_q) max_next_q = q;
    }

    /* Update Q */
    float new_q = current_q + agent->learning_rate * (reward + agent->discount_factor * max_next_q - current_q);
    agent->q_table[state * agent->num_actions + action] = new_q;
}

float rl_agent_get_q(RLAgent *agent, int state, int action) {
    if (!agent) return 0;
    return agent->q_table[state * agent->num_actions + action];
}

int rl_agent_save(RLAgent *agent, const char *path) {
    if (!agent || !path) return -1;

    FILE *f = fopen(path, "wb");
    if (!f) return -1;

    fwrite(&agent->num_states, sizeof(int), 1, f);
    fwrite(&agent->num_actions, sizeof(int), 1, f);
    fwrite(&agent->learning_rate, sizeof(float), 1, f);
    fwrite(&agent->discount_factor, sizeof(float), 1, f);
    fwrite(agent->q_table, sizeof(float), agent->num_states * agent->num_actions, f);

    fclose(f);
    return 0;
}

int rl_agent_load(RLAgent *agent, const char *path) {
    if (!agent || !path) return -1;

    FILE *f = fopen(path, "rb");
    if (!f) return -1;

    int states, actions;
    float lr, gamma;

    if (fread(&states, sizeof(int), 1, f) != 1 ||
        fread(&actions, sizeof(int), 1, f) != 1 ||
        fread(&lr, sizeof(float), 1, f) != 1 ||
        fread(&gamma, sizeof(float), 1, f) != 1) {
        fclose(f);
        return -1;
    }

    agent->num_states = states;
    agent->num_actions = actions;
    agent->learning_rate = lr;
    agent->discount_factor = gamma;

    if (fread(agent->q_table, sizeof(float), states * actions, f) != (size_t)(states * actions)) {
        fclose(f);
        return -1;
    }

    fclose(f);
    return 0;
}
