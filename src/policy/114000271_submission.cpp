#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

#include "114000271_submission.hpp"
#include "config.hpp"

namespace {

// ──── constants ───────────────────────────────────────────────────────────────

const int INF     = 100'000'000;   // search-window sentinel (> P_MAX)
const int Q_DEPTH = 5;              // extra ply in quiescence search

const int PVAL[7] = { 0, 100, 500, 320, 330, 900, 20000 }; // piece values
const int CVAL[7] = { 0,   1,   5,   3,   3,   9,   100 }; // MVV-LVA weights

// ──── transposition table ─────────────────────────────────────────────────────

struct TTEntry {
    uint64_t key = 0;
    int score = 0;
    int depth = -1;
    int8_t flag = 0;      // 0 exact, 1 lower, 2 upper
    Move best_move;
    bool has_move = false; // 0=exact  1=lower-bound  2=upper-bound
};

const int TT_BITS = 21;            // 2 M slots ≈ 42 MB, well within 4 GB
const int TT_SIZE = 1 << TT_BITS;
const int TT_MASK = TT_SIZE - 1;

TTEntry g_tt[TT_SIZE];             // zero-initialised in BSS

void tt_clear() {
    std::fill(g_tt, g_tt + TT_SIZE, TTEntry{});
}

// Returns true when the cached entry resolves the current node (exact or cutoff).
// May narrow alpha/beta as a side-effect.
bool tt_probe(uint64_t key, int depth, int& alpha, int& beta, int& out)
{
    const TTEntry& e = g_tt[key & TT_MASK];
    if (e.key != key || e.depth < depth) return false;
    out = e.score;
    if (e.flag == 0) return true;                          // exact score
    if (e.flag == 1 && e.score > alpha) alpha = e.score;  // lower bound → raise alpha
    if (e.flag == 2 && e.score < beta)  beta  = e.score;  // upper bound → lower beta
    return (alpha >= beta);                                // window collapsed
}

void tt_store(uint64_t key, int score, int depth, int8_t flag, const Move& best_move)
{
    TTEntry& e = g_tt[key & TT_MASK];

    if (e.key == 0 || e.key == key || depth >= e.depth) {
        e.key = key;
        e.score = score;
        e.depth = depth;
        e.flag = flag;
        e.best_move = best_move;
        e.has_move = true;
    }
}

bool tt_best_move(uint64_t key, Move& out)
{
    TTEntry& e = g_tt[key & TT_MASK];

    if (e.key == key && e.has_move) {
        out = e.best_move;
        return true;
    }

    return false;
}

// ──── helpers ─────────────────────────────────────────────────────────────────

void ensure_legal(State* s)
{
    if (s->game_state == UNKNOWN || s->legal_actions.empty())
        s->get_legal_actions();
}

bool is_capture(State* s, const Move& m)
{
    return s->piece_at(1 - s->player,
                       (int)m.second.first, (int)m.second.second) > 0;
}

bool is_promotion(State* s, const Move& m)
{
    int p  = s->piece_at(s->player, (int)m.first.first, (int)m.first.second);
    int tr = (int)m.second.first;
    return (p == 1) && (tr == 0 || tr == BOARD_H - 1);
}

int center_bonus(int r, int c)
{
    return 12 - 4 * std::abs(c - 2) - 2 * std::abs(r - 2);
}

// ──── evaluation ─────────────────────────────────────────────────────────────

int mat_score(State* s, int side)
{
    int sc = 0;
    for (int r = 0; r < BOARD_H; ++r) {
        for (int c = 0; c < BOARD_W; ++c) {
            int p = s->piece_at(side, r, c);
            if (p <= 0 || p > 6) continue;

            sc += PVAL[p];
            if (p != 6) sc += center_bonus(r, c);
            if (p == 1) {
                // Advance bonus: reward pawns that are closer to promotion.
                int adv  = (side == 0) ? (BOARD_H - 1 - r) : r;
                int dist = (side == 0) ? r : (BOARD_H - 1 - r);
                sc += adv * 15;
                if      (dist == 1) sc += 80;
                else if (dist == 2) sc += 35;
            }
        }
    }
    return sc;
}

int king_pressure(State* s, int side)
{
    // Reward having pieces close to the opponent's king.
    int opp = 1 - side, kr = -1, kc = -1;
    for (int r = 0; r < BOARD_H && kr < 0; ++r)
        for (int c = 0; c < BOARD_W && kr < 0; ++c)
            if (s->piece_at(opp, r, c) == 6) { kr = r; kc = c; }
    if (kr < 0) return 0;

    int bonus = 0;
    for (int r = 0; r < BOARD_H; ++r) {
        for (int c = 0; c < BOARD_W; ++c) {
            int p = s->piece_at(side, r, c);
            if (p <= 0 || p == 6) continue;
            int d = std::max(std::abs(r - kr), std::abs(c - kc));
            if (d <= 3) {
                int w = (p == 5) ? 18 : (p == 2 || p == 3) ? 12 : 7;
                bonus += (4 - d) * w;
            }
        }
    }
    return bonus;
}

int official_mat(State* s, int side)
{
    // Uses the official tournament tiebreak values.
    static const int v[7] = {0, 2, 6, 7, 8, 20, 100};
    int t = 0;
    for (int r = 0; r < BOARD_H; ++r)
        for (int c = 0; c < BOARD_W; ++c) {
            int p = s->piece_at(side, r, c);
            if (p > 0 && p <= 6) t += v[p];
        }
    return t;
}

int evaluate(State* s, const SubmissionParams& p)
{
    // Called only after WIN/DRAW terminal checks have already been done.
    int side = s->player, opp = 1 - side;

    int sc = mat_score(s, side)     - mat_score(s, opp)
           + king_pressure(s, side) - king_pressure(s, opp);

    // Endgame urgency: official material difference is weighted more heavily
    // as the 100-step clock winds down to prevent draw-by-count losses.
    int left = MAX_STEP - s->step;
    if (left <= 20) {
        int dm = official_mat(s, side) - official_mat(s, opp);
        sc += dm * (30 + (20 - left) * 3);
    }

    if (p.use_eval_mobility) {
        int self_mob = (int)s->legal_actions.size();
        State opp_s(s->board, opp);
        opp_s.get_legal_actions();
        sc += 6 * (self_mob - (int)opp_s.legal_actions.size());
        // Heavy penalty if opponent can capture our king right now.
        if (opp_s.game_state == WIN) sc -= 5000;
    }
    return sc;
}

// ──── move ordering ───────────────────────────────────────────────────────────

int move_score(State* s, const Move& m, const Move* tt_move)
{
    if (tt_move != nullptr && m == *tt_move) {
        return 20'000'000;
    }

    int fr = (int)m.first.first,  fc = (int)m.first.second;
    int tr = (int)m.second.first, tc = (int)m.second.second;
    int att = s->piece_at(s->player,     fr, fc);
    int vic = s->piece_at(1 - s->player, tr, tc);
    int sc  = 0;

    if (vic > 0) {
        if (vic == 6) return 10'000'000;
        sc += 100'000 + CVAL[vic] * 1000 - CVAL[att] * 10;
    }

    if (is_promotion(s, m)) {
        sc += 80'000;
    } else if (att == 1 && (tr == 1 || tr == BOARD_H - 2)) {
        sc += 10'000;
    }

    sc += center_bonus(tr, tc);
    return sc;
}

std::vector<Move> ordered_moves(State* s, bool tactical_only, const Move* tt_move = nullptr)
{
    std::vector<Move> mv;
    mv.reserve(s->legal_actions.size());

    for (const Move& m : s->legal_actions) {
        if (!tactical_only || is_capture(s, m) || is_promotion(s, m)) {
            mv.push_back(m);
        }
    }

    std::sort(mv.begin(), mv.end(),
              [s, tt_move](const Move& a, const Move& b) {
                  return move_score(s, a, tt_move) > move_score(s, b, tt_move);
              });

    return mv;
}

// ──── search ──────────────────────────────────────────────────────────────────

// Forward declarations.
int negamax(State*, int, int, int, GameHistory&, SearchContext&, const SubmissionParams&, int);
int qsearch(State*, int, int, GameHistory&, SearchContext&, const SubmissionParams&, int, int);

// Handles the perspective flip between parent and child in negamax.
// Increments ply so mate-distance scores are comparable across paths.
inline int child_call(State* c, int depth, int alpha, int beta,
                      GameHistory& h, SearchContext& ctx,
                      const SubmissionParams& p, int ply)
{
    if (c->same_player_as_parent())   // same player (e.g. Connect6 first stone)
        return  negamax(c, depth,  alpha,  beta, h, ctx, p, ply + 1);
    return      -negamax(c, depth, -beta, -alpha, h, ctx, p, ply + 1);
}

inline int qchild_call(State* c, int alpha, int beta,
                       GameHistory& h, SearchContext& ctx,
                       const SubmissionParams& p, int ply, int qdepth)
{
    if (c->same_player_as_parent())
        return  qsearch(c, alpha, beta, h, ctx, p, ply + 1, qdepth);
    return      -qsearch(c, -beta, -alpha, h, ctx, p, ply + 1, qdepth);
}

int qsearch(State* s, int alpha, int beta,
            GameHistory& h, SearchContext& ctx,
            const SubmissionParams& p, int ply, int qdepth)
{
    ctx.nodes++;
    if (ply > ctx.seldepth) ctx.seldepth = ply;

    ensure_legal(s);
    if (s->game_state == WIN)  return P_MAX - ply;
    if (s->game_state == DRAW) return 0;
    if (ctx.stop)              return evaluate(s, p);

    int rep = 0;
    if (s->check_repetition(h, rep)) return rep;

    // Stand-pat: the current player may choose not to capture.
    int stand = evaluate(s, p);

    if (stand >= beta) return stand;

    if (stand + PVAL[5] + 100 < alpha) {
        return alpha;
    }
    if (stand > alpha) alpha = stand;
    if (qdepth <= 0) return stand;

    uint64_t key = s->hash();
    h.push(key);

    auto mv   = ordered_moves(s, /*tactical_only=*/true);
    int  best = stand;

    for (const Move& m : mv) {
        if (ctx.stop) break;
        int tr = (int)m.second.first;
        int tc = (int)m.second.second;
        int cap = s->piece_at(1 - s->player, tr, tc);

        if (cap > 0 && cap != 6 && stand + PVAL[cap] + 100 < alpha) {
            continue;
        }

        State* c  = s->next_state(m);
        int    sc = qchild_call(c, alpha, beta, h, ctx, p, ply, qdepth - 1);
        delete c;
        if (sc > best)  best  = sc;
        if (sc > alpha) alpha = sc;
        if (alpha >= beta) break;
    }

    h.pop(key);
    return best;
}

int negamax(State* s, int depth, int alpha, int beta,
            GameHistory& h, SearchContext& ctx,
            const SubmissionParams& p, int ply)
{
    ctx.nodes++;
    if (ply > ctx.seldepth) ctx.seldepth = ply;

    ensure_legal(s);
    if (s->game_state == WIN)  return P_MAX - ply;
    if (s->game_state == DRAW) return 0;
    if (ctx.stop)              return evaluate(s, p);

    int rep = 0;
    if (s->check_repetition(h, rep)) return rep;

    if (depth <= 0) {
        return p.use_quiescence
               ? qsearch(s, alpha, beta, h, ctx, p, ply, Q_DEPTH)
               : evaluate(s, p);
    }

    uint64_t key = s->hash();
    Move tt_move;
    Move* tt_move_ptr = nullptr;

    if (p.use_tt && tt_best_move(key, tt_move)) {
        tt_move_ptr = &tt_move;
    }

    // TT probe before history.push so we can return early without a dangling push.
    if (p.use_tt) {
        int tt_sc;
        if (tt_probe(key, depth, alpha, beta, tt_sc)) return tt_sc;
    }

    h.push(key);

    auto mv = ordered_moves(s, false, tt_move_ptr);
    if (mv.empty()) {
        h.pop(key);
        return M_MAX + ply;   // no legal moves → current player loses
    }

    int orig_alpha = alpha;
    int best       = -INF;
    bool first     = true;
    Move best_move = mv[0];

    for (int i = 0; i < (int)mv.size(); ++i) {
        const Move& m = mv[i];
        if (ctx.stop) break;
        int tr = (int)m.second.first;
        int tc = (int)m.second.second;

        bool cap = s->piece_at(1 - s->player, tr, tc) > 0;
        bool pro = is_promotion(s, m);

        int reduction = 0;

        if (!cap && !pro && depth >= 3 && i >= 4) {
            reduction = 1;
            if (i >= 8 && depth >= 5) {
                reduction = 2;
            }
        }

        State* c = s->next_state(m);
        int sc;

        if (first || !p.use_pvs) {
            sc = child_call(c, depth - 1, alpha, beta, h, ctx, p, ply);
        } else {
            // PVS: search with a null window first to prove this move is worse.
            sc = child_call(c, depth - 1 - reduction, alpha, alpha + 1, h, ctx, p, ply);
            // If it might be better, re-search with the full window.
            if (!ctx.stop && sc > alpha && sc < beta)
                sc = child_call(c, depth - 1, alpha, beta, h, ctx, p, ply);
        }

        delete c;
        first = false;

        if (sc > best) {
            best = sc;
            best_move = m;
        }
        if (sc > alpha) alpha = sc;
        if (alpha >= beta) break;
    }

    h.pop(key);

    if (best == -INF) return evaluate(s, p); // only hits if every move aborted

    // Store in TT only when the search completed without being aborted.
    if (!ctx.stop && p.use_tt) {
        int8_t flag = (best <= orig_alpha) ? 2   // all-node  → upper bound
                    : (best >= beta)       ? 1   // cut-node  → lower bound
                    :                        0;  // PV-node   → exact
        tt_store(key, best, depth, flag, best_move);
    }

    return best;
}

} // namespace

// ──── public entry point ──────────────────────────────────────────────────────

SearchResult Submission::search(State*       state,
                                int          depth,
                                GameHistory& history,
                                SearchContext& ctx)
{
    ctx.reset();
    SubmissionParams p = SubmissionParams::from_map(ctx.params);

    // Clear the TT at the start of each new position (first iteration = depth 1).
    if (depth == 1 && p.use_tt) tt_clear();

    SearchResult result;
    result.depth = depth;

    ensure_legal(state);
    if (state->legal_actions.empty()) {
        result.nodes    = ctx.nodes;
        result.seldepth = ctx.seldepth;
        return result;
    }

    uint64_t root_key = state->hash();

    Move root_tt_move;
    Move* root_tt_ptr = nullptr;

    if (p.use_tt && tt_best_move(root_key, root_tt_move)) {
        root_tt_ptr = &root_tt_move;
    }

    auto mv = ordered_moves(state, false, root_tt_ptr);
    int  total = (int)mv.size();

    // ── Safe fallback: report the top-ordered move BEFORE search begins.
    // The game runner always has a valid move even if the search times out
    // on the very first iteration.
    result.best_move = mv[0];
    result.pv        = {mv[0]};
    result.score     = -INF;

    // Kirim fallback cuma di depth 1.
    // Jangan overwrite best move depth sebelumnya di depth lebih tinggi.
    if (depth <= 1 && p.report_partial && ctx.on_root_update) {
        ctx.on_root_update({result.best_move, result.score, depth, 1, total});
    }

    // If the root itself is a terminal win (shouldn't happen in normal play),
    // return immediately.
    if (state->game_state == WIN) {
        result.score    = P_MAX;
        result.nodes    = ctx.nodes;
        result.seldepth = ctx.seldepth;
        return result;
    }

    int best_score = -INF;
    int alpha      = -INF;
    int beta       =  INF;

    for (int i = 0; i < total; ++i) {
        if (ctx.stop) break;

        State* c = state->next_state(mv[i]);
        int sc;

        if (i == 0 || !p.use_pvs) {
            sc = child_call(c, depth - 1, alpha, beta, history, ctx, p, 0);
        } else {
            sc = child_call(c, depth - 1, alpha, alpha + 1, history, ctx, p, 0);
            if (!ctx.stop && sc > alpha)
                sc = child_call(c, depth - 1, alpha, beta, history, ctx, p, 0);
        }

        delete c;

        // ── KEY FIX ──────────────────────────────────────────────────────────
        // Do NOT update the best move if the search was aborted mid-tree.
        // An incomplete search may return a meaningless score that is
        // accidentally better than the previous best, corrupting the result.
        // The pre-search fallback above already guarantees a valid move exists.
        if (ctx.stop) break;
        // ─────────────────────────────────────────────────────────────────────

        if (sc > best_score) {
            best_score       = sc;
            result.best_move = mv[i];
            result.pv        = {mv[i]};
            result.score     = sc;

            if (p.report_partial && ctx.on_root_update)
                ctx.on_root_update({result.best_move, result.score, depth, i + 1, total});
        }

        if (sc > alpha) alpha = sc;
        // Note: no beta cutoff at root – we want the true best move, not just
        // a lower bound, so we score all moves.
    }

    if (!ctx.stop && p.use_tt && best_score != -INF) {
        tt_store(root_key, result.score, depth, 0, result.best_move);
    }
    result.nodes    = ctx.nodes;
    result.seldepth = ctx.seldepth;
    return result;
}

ParamMap Submission::default_params()
{
    return {
        {"UseKPEval",       "true"},
        {"UseEvalMobility", "true"},
        {"ReportPartial",   "true"},
        {"UsePVS",          "true"},
        {"UseQuiescence",   "true"},
        {"UseTT",           "true"},
    };
}

std::vector<ParamDef> Submission::param_defs()
{
    return {
        {"UseKPEval",       ParamDef::CHECK, "true"},
        {"UseEvalMobility", ParamDef::CHECK, "true"},
        {"ReportPartial",   ParamDef::CHECK, "true"},
        {"UsePVS",          ParamDef::CHECK, "true"},
        {"UseQuiescence",   ParamDef::CHECK, "true"},
        {"UseTT",           ParamDef::CHECK, "true"},
    };
}