#include "weather/forecast_strip.hpp"
#include "weather/rainfall.hpp"
#include "weather/severity.hpp"
#include <algorithm>

namespace patio {

unsigned ForecastHazards::count() const {
    return static_cast<unsigned>(std::count_if(levels.begin(),levels.end(),[](Severity s){return s!=Severity::None;}));
}

ForecastHazard ForecastHazards::strongest() const {
    auto it=std::max_element(levels.begin(),levels.end());
    return *it==Severity::None ? ForecastHazard::Count : static_cast<ForecastHazard>(it-levels.begin());
}

ForecastHazards hourly_hazards(const HourlyWeather& h,const AdviceThresholds& t,bool uv_enabled) {
    using K=ForecastHazard;
    ForecastHazards out;
    const int code=h.weather_code.valid?h.weather_code.value:-1;
    if(code==96 || code==99) out[K::Hail]=Severity::Danger;
    else if(code==95 || code==97) out[K::Storm]=Severity::Danger;
    if(code==56 || code==66) out[K::Ice]=Severity::Warning;
    if(code==57 || code==67) out[K::Ice]=Severity::Danger;

    // Use the intensity for this hour, never an accumulation from other hours.
    const auto liquid=liquid_rain_for_hour(h);
    auto rain=rain_impact(liquid.millimetres,0,t);
    auto snow=snow_impact(h.snowfall_cm.valid?h.snowfall_cm.value:0,0);
    // Fall back to an explicit heavy-precipitation code when quantitative
    // coverage is missing. A complete hourly amount takes precedence.
    if((code==65 || code==82) && (!h.rain_mm.valid || !h.showers_mm.valid))
        rain=std::max(rain,Severity::Warning);
    if((code==75 || code==86) && !h.snowfall_cm.valid) snow=Severity::Warning;
    if(rain>=Severity::Warning) out[K::Rain]=rain;
    if(snow>=Severity::Warning) out[K::Snow]=snow;
    out[K::Wind]=wind_impact(h.wind_kmh.valid?h.wind_kmh.value:0,h.gust_kmh.valid?h.gust_kmh.value:0);
    if(h.apparent_temperature_c.valid) {
        out[K::Heat]=heat_impact(h.apparent_temperature_c.value,t);
        out[K::Cold]=cold_impact(h.apparent_temperature_c.value,t);
    }
    if(uv_enabled && h.uv_index.valid) {
        const float uv=h.uv_index.value;
        out[K::UV]=uv_impact(uv,t);
        // Moderate UV is a quiet panel-only caution at the default setting.
        // A deliberately raised strong-UV threshold suppresses these earlier notices.
        if(out[K::UV]==Severity::None && t.high_uv_index<=6 && uv>=3 && uv<6) out[K::UV]=Severity::Caution;
    }
    return out;
}

namespace {
bool same_kinds(const ForecastHazards& a,const ForecastHazards& b) {
    for(std::size_t i=0;i<a.levels.size();++i)
        if((a.levels[i]==Severity::None)!=(b.levels[i]==Severity::None)) return false;
    return true;
}
}

void build_forecast_strip(const WeatherSnapshot& w,const CivilTime& local,std::int64_t now,
                          const AdviceThresholds& t,bool uv_enabled,ForecastStrip& out) {
    out.count=0; out.stacked=false; out.wind_gap=false; out.have_wind=false; out.next_change_utc=0;
    if(!local.valid || (w.status.state!=SourceState::Fresh && w.status.state!=SourceState::Stale)) return;
    auto revisit=[&](std::int64_t at) {
        if(at>now && (!out.next_change_utc || at<out.next_change_utc)) out.next_change_utc=at;
    };
    const int today=local.year*10000+local.month*100+local.day;
    int cursor=std::clamp(local.hour*60+local.minute,kTimelineStartMinute,kTimelineEndMinute);
    std::array<std::size_t,2> prev{};
    constexpr auto none=2*kMaxWeatherHours;
    prev.fill(none);
    bool have_day=false;
    // Provider intervals are ordered by UTC. Require both local and UTC
    // adjacency before coalescing, including at fractional-offset boundaries.
    for(std::size_t i=0;i<std::min(w.hour_count,w.hours.size());++i) {
        const auto& h=w.hours[i];
        const int from=std::max(kTimelineStartMinute,h.local_start_minute);
        const int to=std::min(kTimelineEndMinute,h.local_end_minute);
        if(h.local_date_key!=today || from>=to || h.interval_start_utc>=h.interval_end_utc) continue;
        have_day=true;
        auto rest=hourly_hazards(h,t,uv_enabled);
        const bool upcoming=h.interval_end_utc>now;
        if(upcoming) {
            if(from>cursor) { out.wind_gap=true; revisit(h.interval_start_utc); }
            cursor=std::max(cursor,to);
            out.have_wind|=h.wind_kmh.valid || h.gust_kmh.valid;
            if(rest[ForecastHazard::Wind]==Severity::None && (!h.wind_kmh.valid || !h.gust_kmh.valid)) {
                out.wind_gap=true; revisit(h.interval_end_utc);
            }
        }
        std::array<ForecastHazards,2> group{};
        const auto first=rest.strongest();
        if(first!=ForecastHazard::Count) {
            group[0][first]=rest[first]; rest[first]=Severity::None; group[1]=rest;
        }
        const unsigned n=first==ForecastHazard::Count?0:(group[1].count()?2:1);
        out.stacked|=upcoming && n==2;
        for(auto& index:prev) {
            if(index!=none && (out.blocks[index].to!=from || out.blocks[index].end_utc!=h.interval_start_utc)) index=none;
        }
        std::array<int,2> lane{-1,-1};
        bool occupied[2]{};
        // Keep a continuing band's lane when a second hazard starts or ends.
        // With three or more, reserve the first lane for the highest severity
        // and the other for a tappable group containing every remaining alert.
        if(group[1].count()<=1) {
            for(unsigned g=0;g<n;++g) for(int l=0;l<2;++l)
                if(!occupied[l] && prev[l]!=none && same_kinds(out.blocks[prev[l]].hazards,group[g])) {
                    lane[g]=l; occupied[l]=true; break;
                }
        }
        std::array<std::size_t,2> next{none,none};
        for(unsigned g=0;g<n;++g) {
            if(lane[g]<0) { lane[g]=occupied[0]?1:0; occupied[lane[g]]=true; }
            const int l=lane[g];
            auto index=prev[l];
            if(index==none || out.blocks[index].hazards.levels!=group[g].levels) {
                index=out.count++;
                out.blocks[index]={group[g],static_cast<std::uint8_t>(l),static_cast<std::int16_t>(from),0,0};
            }
            out.blocks[index].to=static_cast<std::int16_t>(to);
            out.blocks[index].end_utc=h.interval_end_utc;
            next[l]=index;
        }
        prev=next;
    }
    out.wind_gap|=have_day && cursor<kTimelineEndMinute;
    // Drop finished runs only after merging. A refresh halfway through a
    // continuous hazard must not move its start to the most recent hour.
    std::size_t kept=0;
    for(std::size_t i=0;i<out.count;++i) {
        if(out.blocks[i].end_utc<=now) continue;
        if(kept!=i) out.blocks[kept]=out.blocks[i];
        revisit(out.blocks[kept++].end_utc);
    }
    out.count=kept;
}
} // namespace patio
