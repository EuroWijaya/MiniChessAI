#include <utility>
#include "114000271_state.hpp"
#include "114000271_AlphaBeta.hpp"

/* ========================================================================
 * ALPHABETA — EVAL_CTX
 *
 * Negamax dengan Alpha-Beta Pruning untuk pemotongan cabang tidak potensial.
 * ======================================================================== */
int AlphaBeta::eval_ctx(
    State *state,
    int depth,
    int alpha,
    int beta,
    GameHistory& history,
    int ply,
    SearchContext& ctx,
    const ABParams& p
) {
    ctx.nodes++;
    if (ply > ctx.seldepth) {
        ctx.seldepth = ply;
    }
    if (ctx.stop) {
        return 0;
    }

    /* === Lazy move generation (sets game_state) === */
    if (state->legal_actions.empty() && state->game_state == UNKNOWN) {
        state->get_legal_actions();
    }

    /* === Terminal / leaf checks === */
    if (state->game_state == WIN) {
        return P_MAX - ply; // Mengutamakan kemenangan tercepat
    }

    if (state->game_state == DRAW) {
        return 0;
    }

    /* === Repetition check (game-specific) === */
    int rep_score;
    if (state->check_repetition(history, rep_score)) {
        return rep_score;
    }
    history.push(state->hash());

    // Batas kedalaman tercapai, lakukan evaluasi papan statis
    if (depth <= 0) {
        int score = state->evaluate(p.use_kp_eval, p.use_eval_mobility, &history); 
        history.pop(state->hash());
        return score;
    }

    /* === Negamax Alpha-Beta Loop === */
    int best_score = M_MAX; // Nilai terbawah absolut sebagai inisialisasi

    for (auto& action : state->legal_actions) {
        State* next = state->next_state(action);
        bool same = next->same_player_as_parent();

        // Menentukan jendela pencarian alpha-beta berdasarkan perspektif pemain berikutnya
        int next_alpha = same ? alpha : -beta;
        int next_beta  = same ? beta  : -alpha;

        // Cari satu tingkat lebih dalam
        int raw_score = eval_ctx(next, depth - 1, next_alpha, next_beta, history, ply + 1, ctx, p);

        // Sesuaikan skor ke sudut pandang (perspektif) pemain aktif saat ini
        int score = same ? raw_score : -raw_score;
        delete next;

        // Perbarui skor terbaik untuk simpul (node) ini
        if (score > best_score) {
            best_score = score;
        }

        // Perbarui batas bawah (alpha) jika menemukan langkah yang lebih menguntungkan
        if (best_score > alpha) {
            alpha = best_score;
        }

        // Beta Cut-off: Potong cabang jika langkah ini terlalu bagus untuk dibiarkan oleh lawan
        if (alpha >= beta) {
            break; 
        }
    }

    history.pop(state->hash());
    return best_score;
}


/* ========================================================================
 * ALPHABETA — SEARCH (Root Interface)
 * ======================================================================== */
SearchResult AlphaBeta::search(
    State *state,
    int depth,
    GameHistory& history,
    SearchContext& ctx
) {
    ctx.reset();
    ABParams p = ABParams::from_map(ctx.params);
    
    SearchResult result;
    result.depth = depth;

    if (!state->legal_actions.size()) {
        state->get_legal_actions();
    }

    // Inisialisasi alpha-beta awal dari root menggunakan batas ekstrem permainan
    int alpha = M_MAX; // Batas bawah terburuk
    int beta  = P_MAX; // Batas atas terbaik
    
    int best_score = M_MAX - 10;
    int move_index = 0;
    int total_moves = (int)state->legal_actions.size();

    for (auto& action : state->legal_actions) {
        State *next = state->next_state(action);
        bool same = next->same_player_as_parent();
        
        // Pembalikan jendela pencarian root jika pemain berganti giliran
        int next_alpha = same ? alpha : -beta;
        int next_beta  = same ? beta  : -alpha;

        int raw_score = eval_ctx(next, depth - 1, next_alpha, next_beta, history, 1, ctx, p);
        int score = same ? raw_score : -raw_score;
        delete next;

        // Jika menemukan langkah yang lebih baik dari rekor terbaik di root saat ini
        if (score > best_score) {
            best_score = score;
            result.best_move = action;

            // Laporkan pembaruan parsial jika diminta oleh engine utama
            if (p.report_partial && ctx.on_root_update) {
                ctx.on_root_update({result.best_move, best_score, depth, move_index + 1, total_moves});
            }
        }  
        
        // Ikut memperbarui alpha di root untuk membantu mempercepat proses pemangkasan langkah sisa
        if (best_score > alpha) {
            alpha = best_score;
        }
        
        move_index++;
    }

    result.score = best_score;
    return result;
} 


/* ========================================================================
 * ALPHABETA — UTILITY PARAMETERS
 * ======================================================================== */
ParamMap AlphaBeta::default_params() {
    return {
        {"UseKPEval", "true"},
        {"UseEvalMobility", "true"},
        {"ReportPartial", "true"},
    };
}

std::vector<ParamDef> AlphaBeta::param_defs() {
    return {
        {"UseKPEval", ParamDef::CHECK, "true"},
        {"UseEvalMobility", ParamDef::CHECK, "true"},
        {"ReportPartial", ParamDef::CHECK, "true"},
    };
}