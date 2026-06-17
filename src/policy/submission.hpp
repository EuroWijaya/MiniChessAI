#pragma once

#include <vector>

#include "state.hpp"
#include "search_types.hpp"
#include "game_history.hpp"

struct SubmissionParams {
    bool use_kp_eval = true;
    bool use_eval_mobility = true;
    bool report_partial = true;
    bool use_pvs = true;
    bool use_quiescence = true;
    bool use_tt = true;

    static SubmissionParams from_map(const ParamMap& m){
        SubmissionParams p;
        p.use_kp_eval       = param_bool(m, "UseKPEval", true);
        p.use_eval_mobility = param_bool(m, "UseEvalMobility", true);
        p.report_partial    = param_bool(m, "ReportPartial", true);
        p.use_pvs           = param_bool(m, "UsePVS", true);
        p.use_quiescence    = param_bool(m, "UseQuiescence", true);
        p.use_tt            = param_bool(m, "UseTT", true);
        return p;
    }
};

class Submission {
public:
    static SearchResult search(
        State *state,
        int depth,
        GameHistory& history,
        SearchContext& ctx
    );

    static ParamMap default_params();
    static std::vector<ParamDef> param_defs();
};
