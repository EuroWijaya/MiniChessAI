#pragma once

#include "../src/search_types.hpp"
#include "game_history.hpp"

// Forward declaration supaya tidak include melingkar
class State;

struct PVSParams {
    bool use_kp_eval = true;
    bool use_eval_mobility = true;
    bool report_partial = true;

    static PVSParams from_map(const ParamMap& m) {
        PVSParams p;
        p.use_kp_eval       = param_bool(m, "UseKPEval", true);
        p.use_eval_mobility = param_bool(m, "UseEvalMobility", true);
        p.report_partial    = param_bool(m, "ReportPartial", true);
        return p;
    }
};

class PVS {
public:
    // Principal Variation Search internal recursive function
    static int eval_ctx(
        State *state,
        int depth,
        int alpha,
        int beta,
        GameHistory& history,
        int ply,
        SearchContext& ctx,
        const PVSParams& p
    );

    // Root search interface
    static SearchResult search(
        State *state,
        int depth,
        GameHistory& history,
        SearchContext& ctx
    );

    static ParamMap default_params();
    static std::vector<ParamDef> param_defs();
};