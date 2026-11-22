#pragma once

#include "race_racing_line.h"

namespace horse_races::bookmaking {
// One bookmaker competitor's line is solved before the screen fades in. The
// mapped corridor is the SAME recorded route the race judge uses; only the
// share of its safe offset and the rider's cornering model vary. Unknown
// survey rows allow no offset, never a blind cut into a storefront.
inline bool BuildPersonalLine(const course::Path& centre,const obstacle_map::Map& survey,
    float legal_half_width,float utilization,const race_pace::Dynamics& dynamics,
    racing_line::Result& line,race_pace::Plan& pace,const std::vector<float>* recorded_starts=nullptr,
    float tracking_reserve=racing_line::kTrackingMarginMetres) {
    line={};pace={};
    bool sections=survey.row_metres.size()==survey.rows.size() && survey.sections.size()==survey.rows.size() &&
        !survey.rows.empty();
    if(sections) {
        sections=std::abs(survey.row_metres.front())<.001F &&
            survey.row_metres.back()<=centre.Length()+.001F &&
            centre.Length()-survey.row_metres.back()<=obstacle_map::kStepMetres+.001F;
        for(std::size_t row=0;sections && row<survey.rows.size();++row)
            sections=std::isfinite(survey.row_metres[row]) && destination::Finite(survey.sections[row].centre) &&
                std::isfinite(survey.sections[row].nx) && std::isfinite(survey.sections[row].ny) &&
                (!row || survey.row_metres[row]>survey.row_metres[row-1]);
    }
    const bool uniform=survey.row_metres.empty() && survey.sections.empty() &&
        survey.rows.size()==obstacle_map::Map::Rows(centre.Length());
    if((!sections && !uniform) || survey.geometry_hash!=obstacle_map::Map::GeometryHash(centre) ||
        survey.width<legal_half_width || !std::isfinite(utilization) ||
        utilization<.8F || utilization>1.0F || !dynamics.Valid() ||
        !(legal_half_width>=5 && legal_half_width<=20) || !tracking_margin::Valid(tracking_reserve))return false;
    racing_line::Params params;
    params.half_width=legal_half_width;
    params.margin=tracking_reserve;
    params.aggression=utilization;
    params.dynamics=dynamics;
    params.obstacles=&survey;
    params.obstacle_path_starts=recorded_starts;
    course::Path dense;
    const course::Path* source=&centre;
    if(racing_line::Densify(centre,8.0F,dense) && dense.points.size()>centre.points.size())source=&dense;
    return racing_line::Build(*source,params,line) && line.verified &&
        pace.Build(line.path,dynamics,line.reference);
}
} // namespace horse_races::bookmaking
