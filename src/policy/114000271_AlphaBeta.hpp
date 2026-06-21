#pragma once
#include "../src/search_types.hpp"
#include "game_history.hpp"

// Forward declaration untuk State agar tidak perlu include melingkar
class State;

struct ABParams {
    bool use_kp_eval = true;
    bool use_eval_mobility = true;
    bool report_partial = true;

    static ABParams from_map(const ParamMap& m) {
        ABParams p;
        p.use_kp_eval       = param_bool(m, "UseKPEval", true);
        p.use_eval_mobility = param_bool(m, "UseEvalMobility", true);
        p.report_partial    = param_bool(m, "ReportPartial", true);
        return p;
    }
};

class AlphaBeta {
public:
    // Fungsi evaluasi rekursif internal dengan parameter tambahan alpha dan beta
    static int eval_ctx(
        State *state,
        int depth,
        int alpha,
        int beta,
        GameHistory& history,
        int ply,
        SearchContext& ctx,
        const ABParams& p
    );

    // Fungsi utama pencarian langkah terbaik yang dipanggil dari root
    static SearchResult search(
        State *state,
        int depth,
        GameHistory& history,
        SearchContext& ctx
    );

    static ParamMap default_params();
    static std::vector<ParamDef> param_defs();
};