#include <utility>
#include "state.hpp"
#include "PVS.hpp"

/* ========================================================================
 * PVS — EVAL_CTX
 *
 * Principal Variation Search.
 *
 * Ide utama:
 * - Anak pertama dicari dengan full alpha-beta window.
 * - Anak berikutnya dicari dulu dengan narrow/null window.
 * - Kalau narrow search menunjukkan move itu bisa lebih bagus,
 *   baru search ulang dengan full window.
 * ======================================================================== */
int PVS::eval_ctx(
    State *state,
    int depth,
    int alpha,
    int beta,
    GameHistory& history,
    int ply,
    SearchContext& ctx,
    const PVSParams& p
) {
    ctx.nodes++;

    if (ply > ctx.seldepth) {
        ctx.seldepth = ply;
    }

    if (ctx.stop) {
        return 0;
    }

    /* === Lazy move generation === */
    if (state->legal_actions.empty() && state->game_state == UNKNOWN) {
        state->get_legal_actions();
    }

    /* === Terminal checks === */
    if (state->game_state == WIN) {
        return P_MAX - ply;
    }

    if (state->game_state == DRAW) {
        return 0;
    }

    if (state->legal_actions.empty()) {
        return M_MAX + ply;
    }

    /* === Repetition check === */
    int rep_score;
    if (state->check_repetition(history, rep_score)) {
        return rep_score;
    }

    history.push(state->hash());

    /* === Leaf evaluation === */
    if (depth <= 0) {
        int score = state->evaluate(p.use_kp_eval, p.use_eval_mobility, &history);
        history.pop(state->hash());
        return score;
    }

    int best_score = M_MAX;
    bool first_child = true;

    for (auto& action : state->legal_actions) {
        State* next = state->next_state(action);
        bool same = next->same_player_as_parent();

        int score;

        if (first_child) {
            /*
             * First child = principal variation candidate.
             * Search normally using full alpha-beta window.
             */
            int next_alpha = same ? alpha : -beta;
            int next_beta  = same ? beta  : -alpha;

            int raw_score = eval_ctx(
                next,
                depth - 1,
                next_alpha,
                next_beta,
                history,
                ply + 1,
                ctx,
                p
            );

            score = same ? raw_score : -raw_score;
            first_child = false;
        } else {
            /*
             * PVS null-window search.
             *
             * Current side:
             *     search with [alpha, alpha + 1]
             *
             * If perspective flips:
             *     window becomes [-(alpha + 1), -alpha]
             */
            int narrow_alpha = alpha;
            int narrow_beta  = alpha + 1;

            int next_alpha = same ? narrow_alpha : -narrow_beta;
            int next_beta  = same ? narrow_beta  : -narrow_alpha;

            int raw_score = eval_ctx(
                next,
                depth - 1,
                next_alpha,
                next_beta,
                history,
                ply + 1,
                ctx,
                p
            );

            score = same ? raw_score : -raw_score;

            /*
             * Kalau null-window search menunjukkan move ini mungkin lebih baik,
             * search ulang dengan full alpha-beta window.
             */
            if (score > alpha && score < beta && !ctx.stop) {
                int full_alpha = same ? alpha : -beta;
                int full_beta  = same ? beta  : -alpha;

                raw_score = eval_ctx(
                    next,
                    depth - 1,
                    full_alpha,
                    full_beta,
                    history,
                    ply + 1,
                    ctx,
                    p
                );

                score = same ? raw_score : -raw_score;
            }
        }

        delete next;

        if (ctx.stop) {
            break;
        }

        if (score > best_score) {
            best_score = score;
        }

        if (best_score > alpha) {
            alpha = best_score;
        }

        if (alpha >= beta) {
            break;
        }
    }

    history.pop(state->hash());
    return best_score;
}


/* ========================================================================
 * PVS — SEARCH ROOT
 * ======================================================================== */
SearchResult PVS::search(
    State *state,
    int depth,
    GameHistory& history,
    SearchContext& ctx
) {
    ctx.reset();

    PVSParams p = PVSParams::from_map(ctx.params);

    SearchResult result;
    result.depth = depth;

    if (state->legal_actions.empty() && state->game_state == UNKNOWN) {
        state->get_legal_actions();
    }

    if (state->legal_actions.empty()) {
        result.score = M_MAX;
        result.nodes = ctx.nodes;
        result.seldepth = ctx.seldepth;
        return result;
    }

    int alpha = M_MAX;
    int beta  = P_MAX;

    int best_score = M_MAX - 10;
    int move_index = 0;
    int total_moves = (int)state->legal_actions.size();

    // Fallback move supaya kalau time cut terlalu cepat, tetap ada move valid.
    result.best_move = state->legal_actions[0];
    result.pv = {result.best_move};
    result.score = best_score;

    if (p.report_partial && ctx.on_root_update) {
        ctx.on_root_update({
            result.best_move,
            result.score,
            depth,
            1,
            total_moves
        });
    }

    bool first_child = true;

    for (auto& action : state->legal_actions) {
        if (ctx.stop) {
            break;
        }

        State* next = state->next_state(action);
        bool same = next->same_player_as_parent();

        int score;

        if (first_child) {
            /*
             * Root first move searched normally.
             */
            int next_alpha = same ? alpha : -beta;
            int next_beta  = same ? beta  : -alpha;

            int raw_score = eval_ctx(
                next,
                depth - 1,
                next_alpha,
                next_beta,
                history,
                1,
                ctx,
                p
            );

            score = same ? raw_score : -raw_score;
            first_child = false;
        } else {
            /*
             * Root PVS narrow-window search.
             */
            int narrow_alpha = alpha;
            int narrow_beta  = alpha + 1;

            int next_alpha = same ? narrow_alpha : -narrow_beta;
            int next_beta  = same ? narrow_beta  : -narrow_alpha;

            int raw_score = eval_ctx(
                next,
                depth - 1,
                next_alpha,
                next_beta,
                history,
                1,
                ctx,
                p
            );

            score = same ? raw_score : -raw_score;

            /*
             * Re-search kalau move ini ternyata kandidat bagus.
             */
            if (score > alpha && score < beta && !ctx.stop) {
                int full_alpha = same ? alpha : -beta;
                int full_beta  = same ? beta  : -alpha;

                raw_score = eval_ctx(
                    next,
                    depth - 1,
                    full_alpha,
                    full_beta,
                    history,
                    1,
                    ctx,
                    p
                );

                score = same ? raw_score : -raw_score;
            }
        }

        delete next;

        if (ctx.stop) {
            break;
        }

        if (score > best_score) {
            best_score = score;
            result.best_move = action;
            result.pv = {action};
            result.score = best_score;

            if (p.report_partial && ctx.on_root_update) {
                ctx.on_root_update({
                    result.best_move,
                    best_score,
                    depth,
                    move_index + 1,
                    total_moves
                });
            }
        }

        if (best_score > alpha) {
            alpha = best_score;
        }

        move_index++;
    }

    result.score = best_score;
    result.nodes = ctx.nodes;
    result.seldepth = ctx.seldepth;

    return result;
}


/* ========================================================================
 * PVS — PARAMETERS
 * ======================================================================== */
ParamMap PVS::default_params() {
    return {
        {"UseKPEval", "true"},
        {"UseEvalMobility", "true"},
        {"ReportPartial", "true"},
    };
}

std::vector<ParamDef> PVS::param_defs() {
    return {
        {"UseKPEval", ParamDef::CHECK, "true"},
        {"UseEvalMobility", ParamDef::CHECK, "true"},
        {"ReportPartial", ParamDef::CHECK, "true"},
    };
}