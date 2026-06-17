#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <vector>

#include "submission.hpp"
#include "config.hpp"

namespace {

const int INF = 100000000;
const int Q_DEPTH = 5;

const int piece_value[7] = {
    0,      // empty
    100,    // pawn
    500,    // rook
    320,    // knight
    330,    // bishop
    900,    // queen
    20000   // king
};

const int capture_value[7] = {
    0, 1, 5, 3, 3, 9, 100
};

void ensure_legal(State* s) {
    if (s->game_state == UNKNOWN || s->legal_actions.empty()) {
        s->get_legal_actions();
    }
}

bool has_king(State* s, int side) {
    for (int r = 0; r < BOARD_H; ++r) {
        for (int c = 0; c < BOARD_W; ++c) {
            if (s->piece_at(side, r, c) == 6) {
                return true;
            }
        }
    }
    return false;
}

bool is_capture(State* s, const Move& m) {
    int r = static_cast<int>(m.second.first);
    int c = static_cast<int>(m.second.second);
    return s->piece_at(1 - s->player, r, c) > 0;
}

bool is_promotion(State* s, const Move& m) {
    int fr = static_cast<int>(m.first.first);
    int fc = static_cast<int>(m.first.second);
    int tr = static_cast<int>(m.second.first);

    int p = s->piece_at(s->player, fr, fc);
    return p == 1 && (tr == 0 || tr == BOARD_H - 1);
}

int center_bonus(int r, int c) {
    return 12 - 4 * std::abs(c - 2) - 2 * std::abs(r - 2);
}

int material_score(State* s, int side) {
    int score = 0;

    for (int r = 0; r < BOARD_H; ++r) {
        for (int c = 0; c < BOARD_W; ++c) {
            int p = s->piece_at(side, r, c);

            if (p <= 0 || p > 6) {
                continue;
            }

            score += piece_value[p];

            if (p != 6) {
                score += center_bonus(r, c);
            }

            if (p == 1) {
                int advance = (side == 0) ? (BOARD_H - 1 - r) : r;
                score += advance * 15;

                int dist_to_promo = (side == 0) ? r : (BOARD_H - 1 - r);

                if (dist_to_promo == 1) {
                    score += 80;
                } else if (dist_to_promo == 2) {
                    score += 35;
                }
            }
        }
    }

    return score;
}

int official_material(State* s, int side) {
    const int v[7] = {0, 2, 6, 7, 8, 20, 100};
    int total = 0;

    for (int r = 0; r < BOARD_H; ++r) {
        for (int c = 0; c < BOARD_W; ++c) {
            int p = s->piece_at(side, r, c);
            if (p > 0 && p <= 6) {
                total += v[p];
            }
        }
    }

    return total;
}

int king_distance_bonus(State* s, int side) {
    int opp = 1 - side;
    int kr = -1, kc = -1;

    for (int r = 0; r < BOARD_H; ++r) {
        for (int c = 0; c < BOARD_W; ++c) {
            if (s->piece_at(opp, r, c) == 6) {
                kr = r;
                kc = c;
            }
        }
    }

    if (kr == -1) {
        return 0;
    }

    int bonus = 0;

    for (int r = 0; r < BOARD_H; ++r) {
        for (int c = 0; c < BOARD_W; ++c) {
            int p = s->piece_at(side, r, c);

            if (p <= 0 || p == 6) {
                continue;
            }

            int dist = std::max(std::abs(r - kr), std::abs(c - kc));

            if (dist <= 3) {
                if (p == 5) {
                    bonus += (4 - dist) * 18;
                } else if (p == 2 || p == 3) {
                    bonus += (4 - dist) * 12;
                } else {
                    bonus += (4 - dist) * 7;
                }
            }
        }
    }

    return bonus;
}

int evaluate(State* s, const SubmissionParams& p) {
    ensure_legal(s);

    if (s->game_state == WIN) {
        return P_MAX;
    }

    if (s->game_state == DRAW) {
        return 0;
    }

    int side = s->player;
    int opp = 1 - side;

    if (!has_king(s, side)) {
        return M_MAX;
    }

    if (!has_king(s, opp)) {
        return P_MAX;
    }

    int score = material_score(s, side) - material_score(s, opp);
    score += king_distance_bonus(s, side);
    score -= king_distance_bonus(s, opp);

    int moves_left = MAX_STEP - s->step;

    if (moves_left <= 20) {
        int self_official = official_material(s, side);
        int opp_official = official_material(s, opp);

        score += (self_official - opp_official) * (30 + (20 - moves_left) * 3);
    }

    if (p.use_eval_mobility) {
        int self_mobility = static_cast<int>(s->legal_actions.size());

        State opp_state(s->board, opp);
        opp_state.get_legal_actions();

        int opp_mobility = static_cast<int>(opp_state.legal_actions.size());

        score += 6 * (self_mobility - opp_mobility);

        // Dangerous: opponent can capture our king immediately.
        if (opp_state.game_state == WIN) {
            score -= 5000;
        }
    }

    return score;
}

int move_score(State* s, const Move& m) {
    int side = s->player;
    int opp = 1 - side;

    int fr = static_cast<int>(m.first.first);
    int fc = static_cast<int>(m.first.second);
    int tr = static_cast<int>(m.second.first);
    int tc = static_cast<int>(m.second.second);

    int attacker = s->piece_at(side, fr, fc);
    int victim = s->piece_at(opp, tr, tc);

    int score = 0;

    if (victim > 0) {
        if (victim == 6) {
            return 10000000;
        }

        // MVV-LVA: valuable victim first, cheap attacker first.
        score += 100000 + capture_value[victim] * 1000 - capture_value[attacker] * 10;
    }

    if (is_promotion(s, m)) {
        score += 80000;
    } else if (attacker == 1 && (tr == 1 || tr == BOARD_H - 2)) {
        score += 10000;
    }

    score += center_bonus(tr, tc);

    return score;
}

std::vector<Move> get_ordered_moves(State* s, bool tactical_only = false) {
    std::vector<Move> moves;
    moves.reserve(s->legal_actions.size());

    for (const Move& m : s->legal_actions) {
        bool tactical = is_capture(s, m) || is_promotion(s, m);

        if (!tactical_only || tactical) {
            moves.push_back(m);
        }
    }

    std::sort(moves.begin(), moves.end(), [s](const Move& a, const Move& b) {
        return move_score(s, a) > move_score(s, b);
    });

    return moves;
}

int search(
    State* s,
    int depth,
    int alpha,
    int beta,
    GameHistory& history,
    SearchContext& ctx,
    const SubmissionParams& p,
    int ply
);

int qsearch(
    State* s,
    int alpha,
    int beta,
    GameHistory& history,
    SearchContext& ctx,
    const SubmissionParams& p,
    int ply,
    int qdepth
);

int child_search(
    State* child,
    int depth,
    int alpha,
    int beta,
    GameHistory& history,
    SearchContext& ctx,
    const SubmissionParams& p,
    int ply
) {
    if (child->same_player_as_parent()) {
        return search(child, depth, alpha, beta, history, ctx, p, ply + 1);
    }

    return -search(child, depth, -beta, -alpha, history, ctx, p, ply + 1);
}

int child_qsearch(
    State* child,
    int alpha,
    int beta,
    GameHistory& history,
    SearchContext& ctx,
    const SubmissionParams& p,
    int ply,
    int qdepth
) {
    if (child->same_player_as_parent()) {
        return qsearch(child, alpha, beta, history, ctx, p, ply + 1, qdepth);
    }

    return -qsearch(child, -beta, -alpha, history, ctx, p, ply + 1, qdepth);
}

int qsearch(
    State* s,
    int alpha,
    int beta,
    GameHistory& history,
    SearchContext& ctx,
    const SubmissionParams& p,
    int ply,
    int qdepth
) {
    ctx.nodes++;

    if (ply > ctx.seldepth) {
        ctx.seldepth = ply;
    }

    ensure_legal(s);

    if (s->game_state == WIN) {
        return P_MAX - ply;
    }

    if (s->game_state == DRAW) {
        return 0;
    }

    if (ctx.stop) {
        return evaluate(s, p);
    }

    int rep_score = 0;
    if (s->check_repetition(history, rep_score)) {
        return rep_score;
    }

    int stand_pat = evaluate(s, p);

    if (stand_pat >= beta) {
        return stand_pat;
    }

    if (stand_pat > alpha) {
        alpha = stand_pat;
    }

    if (qdepth <= 0) {
        return stand_pat;
    }

    uint64_t key = s->hash();
    history.push(key);

    std::vector<Move> moves = get_ordered_moves(s, true);
    int best = stand_pat;

    for (const Move& m : moves) {
        if (ctx.stop) {
            break;
        }

        State* child = s->next_state(m);
        int score = child_qsearch(child, alpha, beta, history, ctx, p, ply, qdepth - 1);
        delete child;

        if (score > best) {
            best = score;
        }

        if (score > alpha) {
            alpha = score;
        }

        if (alpha >= beta) {
            break;
        }
    }

    history.pop(key);
    return best;
}

int search(
    State* s,
    int depth,
    int alpha,
    int beta,
    GameHistory& history,
    SearchContext& ctx,
    const SubmissionParams& p,
    int ply
) {
    ctx.nodes++;

    if (ply > ctx.seldepth) {
        ctx.seldepth = ply;
    }

    ensure_legal(s);

    if (s->game_state == WIN) {
        return P_MAX - ply;
    }

    if (s->game_state == DRAW) {
        return 0;
    }

    if (ctx.stop) {
        return evaluate(s, p);
    }

    int rep_score = 0;
    if (s->check_repetition(history, rep_score)) {
        return rep_score;
    }

    if (depth <= 0) {
        if (p.use_quiescence) {
            return qsearch(s, alpha, beta, history, ctx, p, ply, Q_DEPTH);
        }

        return evaluate(s, p);
    }

    uint64_t key = s->hash();
    history.push(key);

    std::vector<Move> moves = get_ordered_moves(s, false);

    if (moves.empty()) {
        history.pop(key);
        return evaluate(s, p);
    }

    int best = -INF;
    bool first_child = true;

    for (const Move& m : moves) {
        if (ctx.stop) {
            break;
        }

        State* child = s->next_state(m);
        int score;

        if (first_child || !p.use_pvs) {
            // Normal alpha-beta for first child.
            score = child_search(child, depth - 1, alpha, beta, history, ctx, p, ply);
        } else {
            // PVS: narrow window first.
            score = child_search(child, depth - 1, alpha, alpha + 1, history, ctx, p, ply);

            // If promising, search again using full window.
            if (score > alpha && score < beta && !ctx.stop) {
                score = child_search(child, depth - 1, alpha, beta, history, ctx, p, ply);
            }
        }

        delete child;
        first_child = false;

        if (score > best) {
            best = score;
        }

        if (score > alpha) {
            alpha = score;
        }

        if (alpha >= beta) {
            break;
        }
    }

    history.pop(key);

    if (best == -INF) {
        return evaluate(s, p);
    }

    return best;
}

} // namespace

SearchResult Submission::search(
    State* state,
    int depth,
    GameHistory& history,
    SearchContext& ctx
) {
    ctx.reset();

    SubmissionParams p = SubmissionParams::from_map(ctx.params);

    SearchResult result;
    result.depth = depth;

    ensure_legal(state);

    if (state->legal_actions.empty()) {
        result.score = 0;
        result.nodes = ctx.nodes;
        result.seldepth = ctx.seldepth;
        return result;
    }

    std::vector<Move> moves = get_ordered_moves(state, false);

    result.best_move = moves.front();
    result.pv = {result.best_move};
    result.score = evaluate(state, p);

    int total_moves = static_cast<int>(moves.size());

    // Send valid fallback immediately.
    // This prevents "no bestmove" when time limit cuts search early.
    if (p.report_partial && ctx.on_root_update) {
        ctx.on_root_update({
            result.best_move,
            result.score,
            depth,
            1,
            total_moves
        });
    }

    if (state->game_state == WIN) {
        result.score = P_MAX;

        if (p.report_partial && ctx.on_root_update) {
            ctx.on_root_update({
                result.best_move,
                result.score,
                depth,
                1,
                total_moves
            });
        }

        result.nodes = ctx.nodes;
        result.seldepth = ctx.seldepth;
        return result;
    }

    int best_score = -INF;
    int alpha = -INF;
    int beta = INF;

    for (int i = 0; i < total_moves; ++i) {
        if (ctx.stop) {
            break;
        }

        const Move& m = moves[i];

        State* child = state->next_state(m);

        child->get_legal_actions();
        bool immediate_bad = (child->game_state == WIN);

        int score;

        if (i == 0 || !p.use_pvs) {
            score = child_search(child, depth - 1, alpha, beta, history, ctx, p, 0);
        } else {
            // PVS at root.
            score = child_search(child, depth - 1, alpha, alpha + 1, history, ctx, p, 0);

            if (score > alpha && score < beta && !ctx.stop) {
                score = child_search(child, depth - 1, alpha, beta, history, ctx, p, 0);
            }
        }

        if (immediate_bad) {
            score -= 700000;
        }

        delete child;

        if (score > best_score || i == 0) {
            best_score = score;
            result.best_move = m;
            result.pv = {m};
            result.score = score;

            if (p.report_partial && ctx.on_root_update) {
                ctx.on_root_update({
                    result.best_move,
                    result.score,
                    depth,
                    i + 1,
                    total_moves
                });
            }
        }

        if (score > alpha) {
            alpha = score;
        }
    }

    if (best_score == -INF) {
        result.score = evaluate(state, p);
    }

    result.nodes = ctx.nodes;
    result.seldepth = ctx.seldepth;
    return result;
}

ParamMap Submission::default_params() {
    return {
        {"UseKPEval", "true"},
        {"UseEvalMobility", "true"},
        {"ReportPartial", "true"},
        {"UsePVS", "true"},
        {"UseQuiescence", "true"},
        {"UseTT", "false"},
    };
}

std::vector<ParamDef> Submission::param_defs() {
    return {
        {"UseKPEval", ParamDef::CHECK, "true"},
        {"UseEvalMobility", ParamDef::CHECK, "true"},
        {"ReportPartial", ParamDef::CHECK, "true"},
        {"UsePVS", ParamDef::CHECK, "true"},
        {"UseQuiescence", ParamDef::CHECK, "true"},
        {"UseTT", ParamDef::CHECK, "false"},
    };
}