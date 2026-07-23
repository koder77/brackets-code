/*
 * This file tiny_transformer.h is part of L1vm.
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

#ifndef TINY_TRANSFORMER_H
#define TINY_TRANSFORMER_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <ctype.h>

/* ==================== Configuration ==================== */

#define TT_MAX_PROMPT_LEN   256
#define TT_MAX_TOKENS        32
#define TT_VOCAB_SIZE       512
#define TT_MAX_EMITTERS     166
#define TT_MAX_EXAMPLES    4096
#define TT_MAX_KEYWORDS      16
#define TT_EMBED_DIM         32
#define TT_NUM_HEADS          2
#define TT_NUM_LAYERS         2
#define TT_HIDDEN_DIM        64
#define TT_FF_DIM           128

/* Code Embedding Configuration */
#define TT_MAX_PATTERNS     128
#define TT_CODE_EMBED_DIM    32

/* Derived constants */
#define TT_HEAD_DIM    (TT_EMBED_DIM / TT_NUM_HEADS)
#define TT_TRAIN_VOCAB 128  /* Smaller vocab for faster training */

/* ==================== Matrix Type ==================== */

typedef struct {
    int rows;
    int cols;
    float *data;
} Matrix;

Matrix *mat_create(int rows, int cols);
void    mat_free(Matrix *m);
void    mat_fill(Matrix *m, float val);
void    mat_randn(Matrix *m, float scale);
float   mat_get(const Matrix *m, int r, int c);
void    mat_set(Matrix *m, int r, int c, float val);
Matrix *mat_mul(const Matrix *a, const Matrix *b);
Matrix *mat_add(const Matrix *a, const Matrix *b);
Matrix *mat_transpose(const Matrix *a);
Matrix *mat_clone(const Matrix *a);
void    mat_print(const Matrix *m, const char *name);

/* ==================== Attention Layer ==================== */

typedef struct {
    Matrix *wq;    /* [embed_dim, embed_dim] */
    Matrix *wk;    /* [embed_dim, embed_dim] */
    Matrix *wv;    /* [embed_dim, embed_dim] */
    Matrix *wo;    /* [embed_dim, embed_dim] */

    /* Gradients */
    Matrix *wq_grad;
    Matrix *wk_grad;
    Matrix *wv_grad;
    Matrix *wo_grad;
} AttentionLayer;

AttentionLayer *attn_create(void);
void            attn_free(AttentionLayer *attn);
Matrix         *attn_forward(AttentionLayer *attn, const Matrix *x);

/* ==================== Feed-Forward Layer ==================== */

typedef struct {
    Matrix *w1;    /* [embed_dim, ff_dim] */
    Matrix *b1;    /* [1, ff_dim] */
    Matrix *w2;    /* [ff_dim, embed_dim] */
    Matrix *b2;    /* [1, embed_dim] */

    Matrix *w1_grad;
    Matrix *b1_grad;
    Matrix *w2_grad;
    Matrix *b2_grad;
} FeedForwardLayer;

FeedForwardLayer *ff_create(void);
void              ff_free(FeedForwardLayer *ff);
Matrix           *ff_forward(FeedForwardLayer *ff, const Matrix *x);

/* ==================== Transformer Block ==================== */

typedef struct {
    AttentionLayer    *attn;
    FeedForwardLayer  *ff;
    Matrix            *ln1_gamma;   /* LayerNorm params */
    Matrix            *ln1_beta;
    Matrix            *ln2_gamma;
    Matrix            *ln2_beta;

    Matrix *ln1_gamma_grad;
    Matrix *ln1_beta_grad;
    Matrix *ln2_gamma_grad;
    Matrix *ln2_beta_grad;

    /* Cached intermediates for backprop */
    Matrix *x_attn_in;
    Matrix *x_attn_out;
    Matrix *x_ff_in;
    Matrix *x_ff_out;
    Matrix *attn_weights;
} TransformerBlock;

TransformerBlock *block_create(void);
void              block_free(TransformerBlock *block);
Matrix           *block_forward(TransformerBlock *block, const Matrix *x);

/* ==================== Full Model ==================== */

typedef struct {
    Matrix *token_emb;    /* [vocab_size, embed_dim] */
    Matrix *pos_emb;      /* [max_seq_len, embed_dim] */
    Matrix *out_w;        /* [embed_dim, num_emitters] */
    Matrix *out_b;        /* [1, num_emitters] */

    Matrix *token_emb_grad;
    Matrix *pos_emb_grad;
    Matrix *out_w_grad;
    Matrix *out_b_grad;

    TransformerBlock *blocks[TT_NUM_LAYERS];

    int    vocab_size;
    int    embed_dim;
    int    max_seq_len;
    int    num_emitters;

    /* Cached forward intermediates */
    Matrix *embedding;
    Matrix *block_outputs[TT_NUM_LAYERS];
    Matrix *pooled;
    Matrix *logits;
} TinyModel;

TinyModel *model_create(int vocab_size, int embed_dim, int max_seq_len, int num_emitters);
void       model_free(TinyModel *m);

/* Forward pass: returns logits [1, num_emitters] */
Matrix    *model_forward(TinyModel *m, const int *tokens, int num_tokens);

/* Backward pass + update: computes gradients and applies SGD */
void       model_backward(TinyModel *m, const int *tokens, int num_tokens,
                           int target_emitter, float lr);

/* Save / load model weights */
int        model_save(const TinyModel *m, const char *path);
int        model_load(TinyModel *m, const char *path);

/* ==================== Vocabulary ==================== */

typedef struct {
    char  words[TT_VOCAB_SIZE][64];
    int   count;
    int   emitter_ids[TT_MAX_EXAMPLES];
    int   num_examples;
    char  prompts[TT_MAX_EXAMPLES][TT_MAX_PROMPT_LEN];
    int   prompt_tokens[TT_MAX_EXAMPLES][TT_MAX_TOKENS];
    int   prompt_lengths[TT_MAX_EXAMPLES];
} TTVocab;

void     ttvocab_init(TTVocab *v);
int      ttvocab_add_word(TTVocab *v, const char *word);
int      ttvocab_find(const TTVocab *v, const char *word);
int      ttvocab_tokenize(const TTVocab *v, const char *prompt, int *tokens, int max_tokens);
void     ttvocab_add_example(TTVocab *v, const char *prompt, int emitter_id);
int      ttvocab_save(const TTVocab *v, const char *path);
int      ttvocab_load(TTVocab *v, const char *path);

/* ==================== Training Data ==================== */

typedef struct {
    int    num_pairs;
    char   prompts[TT_MAX_EXAMPLES][TT_MAX_PROMPT_LEN];
    int    emitter_ids[TT_MAX_EXAMPLES];
    int    vocab[TT_MAX_EXAMPLES][TT_MAX_TOKENS];
    int    vocab_lengths[TT_MAX_EXAMPLES];
} TrainingData;

TrainingData *train_data_create(void);
void          train_data_free(TrainingData *td);
int           train_data_add(TrainingData *td, const char *prompt, int emitter_id, TTVocab *v);
int           train_data_load_dsl(TrainingData *td, const char *dsl_dir, TTVocab *v);

/* ==================== Code Embeddings ==================== */

typedef struct {
    Matrix *code_emb;      /* [TT_MAX_PATTERNS, TT_CODE_EMBED_DIM] */
    Matrix *prompt_emb;    /* [1, TT_CODE_EMBED_DIM] */
    Matrix *sim_w;         /* [TT_CODE_EMBED_DIM, TT_CODE_EMBED_DIM] */
    int     num_patterns;
    char    pattern_names[TT_MAX_PATTERNS][64];
} CodeEmbedder;

CodeEmbedder *code_emb_create(void);
void          code_emb_free(CodeEmbedder *ce);
int           code_emb_add_pattern(CodeEmbedder *ce, const char *name, const char *code);
int           code_emb_find_similar(CodeEmbedder *ce, const char *prompt, float *scores, int top_k);
float         code_emb_similarity(CodeEmbedder *ce, const char *prompt, const char *pattern_name);

/* ==================== Attention Pattern Selector ==================== */

typedef struct {
    Matrix *wq;    /* [embed_dim, embed_dim] */
    Matrix *wk;    /* [embed_dim, embed_dim] */
    Matrix *wv;    /* [embed_dim, embed_dim] */
    Matrix *proj;  /* [embed_dim, num_patterns] */
    int     num_patterns;
} AttentionSelector;

AttentionSelector *attn_sel_create(int num_patterns);
void               attn_sel_free(AttentionSelector *sel);
int                attn_sel_predict(AttentionSelector *sel, const Matrix *prompt_emb,
                                    float *scores, int *best_idx);

/* ==================== Prompt Expander ==================== */

typedef struct {
    Matrix *w_emb;    /* [vocab, embed_dim] */
    Matrix *w_dec;    /* [embed_dim, vocab] */
    Matrix *w_ctx;    /* [embed_dim, embed_dim] */
    int     vocab_size;
    int     embed_dim;
} PromptExpander;

PromptExpander *expander_create(int vocab_size, int embed_dim);
void            expander_free(PromptExpander *pe);
int             expander_paraphrase(PromptExpander *pe, const char *prompt,
                                   char *output, int output_size);

/* ==================== Simple RL Agent ==================== */

typedef struct {
    float *q_table;           /* [num_states, num_actions] */
    float  learning_rate;
    float  discount_factor;
    float  exploration_rate;
    int    num_states;
    int    num_actions;
    int    last_state;
    int    last_action;
} RLAgent;

RLAgent *rl_agent_create(int num_states, int num_actions, float lr, float gamma);
void     rl_agent_free(RLAgent *agent);
int      rl_agent_choose_action(RLAgent *agent, int state);
void     rl_agent_learn(RLAgent *agent, int state, int action, float reward, int next_state);
float    rl_agent_get_q(RLAgent *agent, int state, int action);
int      rl_agent_save(RLAgent *agent, const char *path);
int      rl_agent_load(RLAgent *agent, const char *path);

/* ==================== Inference API ==================== */

/* Initialize transformer (load model or create new) */
int  tt_init(const char *model_path);

/* Predict best emitter for a prompt. Returns emitter index, score in *score */
int  tt_predict(const char *prompt, float *score);

/* Train model from DSL directory */
int  tt_train(const char *dsl_dir, const char *model_path, int epochs, float lr);

#endif /* TINY_TRANSFORMER_H */
