#include "weather/forecast_refresh.hpp"
#include "weather/time_zone.hpp"
#include <cstring>

namespace patio {
std::uint32_t retry_after_seconds(const char* value, std::int64_t now_utc) {
    if (!value) return 0;
    while (*value==' ' || *value=='\t') ++value;
    auto len = std::strlen(value);
    while (len && (value[len-1]==' ' || value[len-1]=='\t')) --len;
    if (!len) return 0;
    if (value[0]>='0' && value[0]<='9') {
        std::uint64_t seconds=0;
        for (std::size_t i=0;i<len;++i) {
            if (value[i]<'0' || value[i]>'9') return 0;
            seconds=std::min<std::uint64_t>(UINT32_MAX,seconds*10+value[i]-'0');
        }
        return static_cast<std::uint32_t>(seconds);
    }
    // IMF-fixdate, always GMT. Do not change the process time zone.
    if (len!=29 || now_utc<=0 || std::strncmp(value+3,", ",2) || value[7]!=' ' || value[11]!=' ' ||
        value[16]!=' ' || value[19]!=':' || value[22]!=':' || std::strncmp(value+25," GMT",4)) return 0;
    const char* months[]={"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
    int month=1;
    while (month<=12 && std::strncmp(value+8,months[month-1],3)) ++month;
    auto number=[&](int start,int n) {
        int out=0;
        while (n--) { char c=value[start++]; if (c<'0'||c>'9') return -1; out=10*out+c-'0'; }
        return out;
    };
    const int year=number(12,4),day=number(5,2),hour=number(17,2),minute=number(20,2),second=number(23,2);
    std::int64_t start{}, end{};
    if (month>12 || year<1970 || day<1 || day>31 || hour<0 || hour>23 || minute<0 || minute>59 ||
        second<0 || second>59 || !local_day_bounds_utc("Etc/UTC",year,month,day,start,end)) return 0;
    return static_cast<std::uint32_t>(std::clamp<std::int64_t>(start+3600*hour+60*minute+second-now_utc,0,UINT32_MAX));
}
} // namespace patio
