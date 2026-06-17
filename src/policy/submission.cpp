#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <vector>

#include "submission.hpp"
#include "config.hpp"

namespace {

constexpr int INF = 100000000;
constexpr int TT_SIZE = 1 << 20;
constexpr int TT_EXACT = 0;
constexpr int TT_UPPER = 1;
constexpr int TT_LOWER = 2;
constexpr int MAX_Q_DEPTH = 6;

// MiniChess material. King is intentionally large, but terminal king-capture
// scores still use P_MAX/M_MAX from BaseState.
static const int piece_value[7] = {0, 100, 320, 330, 350, 900, 20000};
static const int victim_value[7] = {0, 1, 3, 3, 3, 9, 1000};

// Small piece-square tables from White's perspective. Black mirrors the row.
static const int pst[7][BOARD_H][BOARD_W] = {
    // Empty
    {{0,0,0,0,0},{0,0,0,0,0},{0,0,0,0,0},{0,0,0,0,0},{0,0,0,0,0},{0,0,0,0,0}},
    // Pawn: reward advanced pawns and central files.
    {{0, 0, 0, 0, 0}, {55, 65, 75, 65, 55}, {25, 35, 45, 35, 25},
     {10, 18, 25, 18, 10}, {0, 8, 12, 8, 0}, {0, 0, 0, 0, 0}},
    // Rook
    {{8, 10, 12, 10, 8}, {12, 16, 18, 16, 12}, {0, 4, 8, 4, 0},
     {0, 4, 8, 4, 0}, {4, 6, 8, 6, 4}, {0, 0, 2, 0, 0}},
    // Knight
    {{-30, -12, -4, -12, -30}, {-12, 16, 28, 16, -12}, {-4, 28, 42, 28, -4},
     {-4, 28, 42, 28, -4}, {-12, 16, 28, 16, -12}, {-30, -12, -4, -12, -30}},
    // Bishop
    {{-14, -4, 0, -4, -14}, {-4, 18, 24, 18, -4}, {0, 22, 30, 22, 0},
     {0, 22, 30, 22, 0}, {-4, 18, 24, 18, -4}, {-14, -4, 0, -4, -14}},
    // Queen
    {{-10, 0, 8, 0, -10}, {0, 12, 20, 12, 0}, {8, 22, 32, 22, 8},
     {8, 22, 32, 22, 8}, {0, 12, 20, 12, 0}, {-10, 0, 8, 0, -10}},
    // King: early safety near home, but still not trapped in corners.
    {{-28, -24, -20, -24, -28}, {-22, -16, -14, -16, -22}, {-18, -12, -10, -12, -18},
     {-10, -6, -4, -6, -10}, {18, 22, 14, 22, 18}, {28, 34, 22, 34, 28}}
};

struct TTEntry {
    uint64_t key = 0;
    int depth = -1;
    int score = 0;
    int flag = TT_EXACT;
    Move best_move{};
    bool has_best = false;
};

std::vector<TTEntry>& tt_table(){
    static std::vector<TTEntry> table(TT_SIZE);
    return table;
}

bool same_move(const Move& a, const Move& b){
    return a.first.first == b.first.first &&
           a.first.second == b.first.second &&
           a.second.first == b.second.first &&
           a.second.second == b.second.second;
}

void ensure_legal(State* state){
    if(state->game_state == UNKNOWN || state->legal_actions.empty()){
        state->get_legal_actions();
    }
}

int mirror_row_for_side(int side, int row){
    return (side == 0) ? row : (BOARD_H - 1 - row);
}

int chebyshev_dist(int r1, int c1, int r2, int c2){
    int dr = std::abs(r1 - r2);
    int dc = std::abs(c1 - c2);
    return (dr > dc) ? dr : dc;
}

int manhattan_dist(int r1, int c1, int r2, int c2){
    return std::abs(r1 - r2) + std::abs(c1 - c2);
}

// Static evaluation from the side-to-move's perspective. It intentionally does
// not depend only on State::evaluate(), because the provided mobility term only
// counts the current side's moves. This one compares both sides and also adds
// promotion pressure, king tropism, and immediate-threat awareness.
int static_eval(State* state, const SubmissionParams& p){
    ensure_legal(state);

    if(state->game_state == WIN){
        return P_MAX;
    }
    if(state->game_state == DRAW){
        return 0;
    }

    const int side = state->player;
    const int opp = 1 - side;
    int king_r[2] = {-1, -1};
    int king_c[2] = {-1, -1};

    for(int pl = 0; pl < 2; ++pl){
        for(int r = 0; r < BOARD_H; ++r){
            for(int c = 0; c < BOARD_W; ++c){
                if(state->piece_at(pl, r, c) == 6){
                    king_r[pl] = r;
                    king_c[pl] = c;
                }
            }
        }
    }

    if(king_r[side] < 0){
        return M_MAX;
    }
    if(king_r[opp] < 0){
        return P_MAX;
    }

    int score_side[2] = {0, 0};

    for(int pl = 0; pl < 2; ++pl){
        const int enemy = 1 - pl;
        for(int r = 0; r < BOARD_H; ++r){
            for(int c = 0; c < BOARD_W; ++c){
                int pc = state->piece_at(pl, r, c);
                if(pc <= 0 || pc > 6){
                    continue;
                }

                int row = mirror_row_for_side(pl, r);
                int val = piece_value[pc] + pst[pc][row][c];

                // Pawns are very important on 6x5 because promotion is close.
                if(pc == 1){
                    int advance = (pl == 0) ? (BOARD_H - 1 - r) : r;
                    val += advance * 18;
                    int promotion_distance = (pl == 0) ? r : (BOARD_H - 1 - r);
                    if(promotion_distance <= 1){
                        val += 85;
                    }else if(promotion_distance == 2){
                        val += 35;
                    }
                }

                // Prefer central activity for non-king pieces.
                if(pc != 6){
                    int center_bonus = 12 - 4 * std::abs(c - 2) - 2 * std::abs(r - 2);
                    val += center_bonus;
                }

                // Attack pressure near the enemy king. Queen/rook/knight pressure
                // matters a lot in king-capture MiniChess.
                int kd = chebyshev_dist(r, c, king_r[enemy], king_c[enemy]);
                if(pc != 6 && kd <= 3){
                    int tropism = (4 - kd) * (pc == 5 ? 18 : (pc == 2 || pc == 3 ? 12 : 8));
                    val += tropism;
                }

                // Keep own king away from direct enemy king contact when possible.
                if(pc == 6){
                    int enemy_king_dist = manhattan_dist(r, c, king_r[enemy], king_c[enemy]);
                    if(enemy_king_dist <= 2){
                        val -= (3 - enemy_king_dist) * 35;
                    }
                }

                score_side[pl] += val;
            }
        }
    }

    int score = score_side[side] - score_side[opp];

    if(p.use_eval_mobility){
        int self_mobility = static_cast<int>(state->legal_actions.size());

        State opp_state(state->board, opp);
        opp_state.get_legal_actions();
        int opp_mobility = static_cast<int>(opp_state.legal_actions.size());

        score += 8 * (self_mobility - opp_mobility);

        // If the opponent would be able to capture our king immediately after a
        // pass, the current position is tactically dangerous.
        if(opp_state.game_state == WIN){
            score -= 4500;
        }
    }

    return score;
}

int move_order_score(State* state, const Move& move, const Move* tt_best){
    if(tt_best && same_move(move, *tt_best)){
        return 10000000;
    }

    const int side = state->player;
    const int opp = 1 - side;
    int from_r = static_cast<int>(move.first.first);
    int from_c = static_cast<int>(move.first.second);
    int to_r = static_cast<int>(move.second.first);
    int to_c = static_cast<int>(move.second.second);

    int attacker = state->piece_at(side, from_r, from_c);
    int victim = state->piece_at(opp, to_r, to_c);

    int score = 0;
    if(victim > 0){
        if(victim == 6){
            return 9000000;
        }
        // MVV-LVA: high-value victim, low-value attacker first.
        score += 100000 + victim_value[victim] * 1000 - victim_value[attacker] * 10;
    }

    // Pawn promotion or near-promotion.
    if(attacker == 1){
        if(to_r == 0 || to_r == BOARD_H - 1){
            score += 80000;
        }else if(to_r == 1 || to_r == BOARD_H - 2){
            score += 12000;
        }
    }

    // Prefer central destinations as quiet-move tie breakers.
    score += 20 - 4 * std::abs(to_c - 2) - 2 * std::abs(to_r - 2);
    return score;
}

std::vector<Move> ordered_moves(State* state, const Move* tt_best = nullptr, bool captures_only = false){
    std::vector<Move> moves;
    moves.reserve(state->legal_actions.size());

    const int opp = 1 - state->player;
    for(const Move& mv : state->legal_actions){
        int to_r = static_cast<int>(mv.second.first);
        int to_c = static_cast<int>(mv.second.second);
        bool is_capture = state->piece_at(opp, to_r, to_c) > 0;
        if(!captures_only || is_capture){
            moves.push_back(mv);
        }
    }

    std::sort(moves.begin(), moves.end(), [state, tt_best](const Move& a, const Move& b){
        return move_order_score(state, a, tt_best) > move_order_score(state, b, tt_best);
    });
    return moves;
}

int negamax(State* state, int depth, int alpha, int beta,
            GameHistory& history, int ply, SearchContext& ctx,
            const SubmissionParams& p);

int quiescence(State* state, int alpha, int beta,
               GameHistory& history, int ply, SearchContext& ctx,
               const SubmissionParams& p, int qdepth);

int child_score(State* child, int depth, int alpha, int beta,
                GameHistory& history, int ply, SearchContext& ctx,
                const SubmissionParams& p){
    if(child->same_player_as_parent()){
        return negamax(child, depth, alpha, beta, history, ply + 1, ctx, p);
    }
    return -negamax(child, depth, -beta, -alpha, history, ply + 1, ctx, p);
}

int q_child_score(State* child, int alpha, int beta,
                  GameHistory& history, int ply, SearchContext& ctx,
                  const SubmissionParams& p, int qdepth){
    if(child->same_player_as_parent()){
        return quiescence(child, alpha, beta, history, ply + 1, ctx, p, qdepth);
    }
    return -quiescence(child, -beta, -alpha, history, ply + 1, ctx, p, qdepth);
}

int quiescence(State* state, int alpha, int beta,
               GameHistory& history, int ply, SearchContext& ctx,
               const SubmissionParams& p, int qdepth){
    ctx.nodes++;
    if(ply > ctx.seldepth){
        ctx.seldepth = ply;
    }

    ensure_legal(state);

    if(state->game_state == WIN){
        return P_MAX - ply;
    }
    if(state->game_state == DRAW){
        return 0;
    }

    if(ctx.stop){
        return static_eval(state, p);
    }

    int rep_score = 0;
    if(state->check_repetition(history, rep_score)){
        return rep_score;
    }

    uint64_t key = state->hash();
    history.push(key);

    int stand_pat = static_eval(state, p);
    if(stand_pat >= beta){
        history.pop(key);
        return stand_pat;
    }
    if(stand_pat > alpha){
        alpha = stand_pat;
    }

    if(qdepth <= 0){
        history.pop(key);
        return stand_pat;
    }

    std::vector<Move> caps = ordered_moves(state, nullptr, true);
    int best = stand_pat;

    for(const Move& mv : caps){
        if(ctx.stop){
            break;
        }

        State* child = state->next_state(mv);
        int score = q_child_score(child, alpha, beta, history, ply, ctx, p, qdepth - 1);
        delete child;

        if(score > best){
            best = score;
        }
        if(score > alpha){
            alpha = score;
        }
        if(alpha >= beta){
            break;
        }
    }

    history.pop(key);
    return best;
}

int negamax(State* state, int depth, int alpha, int beta,
            GameHistory& history, int ply, SearchContext& ctx,
            const SubmissionParams& p){
    ctx.nodes++;
    if(ply > ctx.seldepth){
        ctx.seldepth = ply;
    }

    ensure_legal(state);

    if(state->game_state == WIN){
        return P_MAX - ply;
    }
    if(state->game_state == DRAW){
        return 0;
    }

    if(ctx.stop){
        return static_eval(state, p);
    }

    int rep_score = 0;
    if(state->check_repetition(history, rep_score)){
        return rep_score;
    }

    if(depth <= 0){
        if(p.use_quiescence){
            return quiescence(state, alpha, beta, history, ply, ctx, p, MAX_Q_DEPTH);
        }
        return static_eval(state, p);
    }

    uint64_t key = state->hash();
    bool tt_allowed = p.use_tt && history.count(key) == 0;
    int alpha_orig = alpha;
    int beta_orig = beta;
    Move tt_best{};
    bool has_tt_best = false;

    if(tt_allowed){
        TTEntry& entry = tt_table()[key & (TT_SIZE - 1)];
        if(entry.key == key){
            if(entry.has_best){
                tt_best = entry.best_move;
                has_tt_best = true;
            }
            if(entry.depth >= depth){
                if(entry.flag == TT_EXACT){
                    return entry.score;
                }
                if(entry.flag == TT_LOWER && entry.score > alpha){
                    alpha = entry.score;
                }else if(entry.flag == TT_UPPER && entry.score < beta){
                    beta = entry.score;
                }
                if(alpha >= beta){
                    return entry.score;
                }
            }
        }
    }

    history.push(key);

    std::vector<Move> moves = ordered_moves(state, has_tt_best ? &tt_best : nullptr, false);
    if(moves.empty()){
        history.pop(key);
        return static_eval(state, p);
    }

    int best = -INF;
    Move best_move = moves.front();
    bool first_child = true;

    for(const Move& mv : moves){
        if(ctx.stop){
            break;
        }

        State* child = state->next_state(mv);
        int score;

        if(first_child || !p.use_pvs){
            score = child_score(child, depth - 1, alpha, beta, history, ply, ctx, p);
        }else{
            // Principal Variation Search: first try a zero-width window. If the
            // move looks better than alpha, re-search with the full window.
            score = child_score(child, depth - 1, alpha, alpha + 1, history, ply, ctx, p);
            if(score > alpha && score < beta && !ctx.stop){
                score = child_score(child, depth - 1, alpha, beta, history, ply, ctx, p);
            }
        }

        delete child;
        first_child = false;

        if(score > best){
            best = score;
            best_move = mv;
        }
        if(score > alpha){
            alpha = score;
        }
        if(alpha >= beta){
            break;
        }
    }

    history.pop(key);

    if(best == -INF){
        best = static_eval(state, p);
    }

    if(tt_allowed && !ctx.stop){
        TTEntry& entry = tt_table()[key & (TT_SIZE - 1)];
        entry.key = key;
        entry.depth = depth;
        entry.score = best;
        entry.best_move = best_move;
        entry.has_best = true;
        if(best <= alpha_orig){
            entry.flag = TT_UPPER;
        }else if(best >= beta_orig){
            entry.flag = TT_LOWER;
        }else{
            entry.flag = TT_EXACT;
        }
    }

    return best;
}

} // namespace

SearchResult Submission::search(
    State *state,
    int depth,
    GameHistory& history,
    SearchContext& ctx
){
    ctx.reset();
    SubmissionParams p = SubmissionParams::from_map(ctx.params);

    SearchResult result;
    result.depth = depth;

    ensure_legal(state);

    // Always set a fallback move before doing any expensive search. This avoids
    // returning an empty/default move if the UBGI time manager stops us early.
    if(state->legal_actions.empty()){
        result.score = 0;
        return result;
    }

    std::vector<Move> root_moves = ordered_moves(state, nullptr, false);
    result.best_move = root_moves.front();
    result.score = static_eval(state, p);

    int best_score = -INF;
    int alpha = -INF;
    int beta = INF;
    int move_index = 0;
    int total_moves = static_cast<int>(root_moves.size());

    // If the root itself has an immediate king capture, get_legal_actions() has
    // already reduced legal_actions to that winning move in this project.
    if(state->game_state == WIN){
        result.best_move = root_moves.front();
        result.score = P_MAX;
        if(p.report_partial && ctx.on_root_update){
            ctx.on_root_update({result.best_move, result.score, depth, 1, total_moves});
        }
        return result;
    }

    for(const Move& mv : root_moves){
        if(ctx.stop){
            break;
        }

        State* child = state->next_state(mv);
        int score;

        if(move_index == 0 || !p.use_pvs){
            score = child_score(child, depth - 1, alpha, beta, history, 0, ctx, p);
        }else{
            score = child_score(child, depth - 1, alpha, alpha + 1, history, 0, ctx, p);
            if(score > alpha && score < beta && !ctx.stop){
                score = child_score(child, depth - 1, alpha, beta, history, 0, ctx, p);
            }
        }

        delete child;

        if(score > best_score || move_index == 0){
            best_score = score;
            result.best_move = mv;
            result.score = best_score;

            if(score > alpha){
                alpha = score;
            }

            if(p.report_partial && ctx.on_root_update){
                ctx.on_root_update({result.best_move, best_score, depth, move_index + 1, total_moves});
            }
        }else if(score > alpha){
            alpha = score;
        }

        ++move_index;
    }

    if(best_score == -INF){
        // Stopped before examining the first move: keep the fallback valid move.
        result.score = static_eval(state, p);
    }

    return result;
}

ParamMap Submission::default_params(){
    return {
        {"UseKPEval", "true"},
        {"UseEvalMobility", "true"},
        {"ReportPartial", "true"},
        {"UsePVS", "true"},
        {"UseQuiescence", "true"},
        {"UseTT", "true"},
    };
}

std::vector<ParamDef> Submission::param_defs(){
    return {
        {"UseKPEval", ParamDef::CHECK, "true"},
        {"UseEvalMobility", ParamDef::CHECK, "true"},
        {"ReportPartial", ParamDef::CHECK, "true"},
        {"UsePVS", ParamDef::CHECK, "true"},
        {"UseQuiescence", ParamDef::CHECK, "true"},
        {"UseTT", ParamDef::CHECK, "true"},
    };
}
