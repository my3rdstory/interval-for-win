#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

namespace interval {
enum class ActivityState { Running, Resting, Paused, Locked, Sleeping, Offline, Unknown };
constexpr size_t ActivityStateCount=7;
struct ActivityEvent {
    std::int64_t time=0;
    std::string kind;
    ActivityState state=ActivityState::Unknown;
    int minutes=50;
};
struct ActivitySpan { std::int64_t begin, end; ActivityState state; };
struct ActivitySummary {
    std::array<std::int64_t,ActivityStateCount> milliseconds{};
    unsigned starts=0, rests=0, restarts=0, snoozes=0;
};
// Versioned, line-delimited records: a torn last write cannot invalidate earlier history.
inline std::string encodeActivity(const ActivityEvent& e) {
    return "1\t"+std::to_string(e.time)+"\t"+e.kind+"\t"+std::to_string(static_cast<int>(e.state))+"\t"+std::to_string(e.minutes)+"\n";
}
inline bool decodeActivity(const std::string& line,ActivityEvent& e) {
    std::istringstream input(line); int version=0,state=0; ActivityEvent parsed;
    if(!(input>>version>>parsed.time>>parsed.kind>>state>>parsed.minutes)||version!=1||parsed.time<=0||state<0||state>=static_cast<int>(ActivityStateCount)||parsed.minutes<1||parsed.minutes>240) return false;
    std::string extra; if(input>>extra) return false;
    if(parsed.kind.empty()||parsed.kind.find_first_not_of("abcdefghijklmnopqrstuvwxyz_")!=std::string::npos) return false;
    parsed.state=static_cast<ActivityState>(state); e=std::move(parsed); return true;
}
class ActivityHistory {
    std::vector<ActivityEvent> events_;
public:
    const std::vector<ActivityEvent>& events() const { return events_; }
    bool restore(const ActivityEvent& e) {
        if(!events_.empty()&&e.time<events_.back().time) return false;
        if(!events_.empty()&&e.kind=="heartbeat"&&events_.back().kind=="heartbeat"&&events_.back().state==e.state) { events_.back()=e; return true; }
        events_.push_back(e); return true;
    }
    ActivityEvent append(std::int64_t time,const std::string& kind,ActivityState state,int minutes) {
        // A backwards system-clock adjustment must never produce negative durations.
        if(!events_.empty()) time=std::max(time,events_.back().time);
        ActivityEvent e{time,kind,state,minutes}; restore(e); return e;
    }
    std::vector<ActivitySpan> spans(std::int64_t begin,std::int64_t end,std::int64_t now) const {
        std::vector<ActivitySpan> result; end=std::min(end,now);
        if(begin>=end||events_.empty()) return result;
        auto it=std::upper_bound(events_.begin(),events_.end(),begin,[](auto time,const ActivityEvent& e){return time<e.time;});
        size_t index=it==events_.begin()?0:static_cast<size_t>(it-events_.begin()-1);
        for(;index<events_.size();++index) {
            const auto& e=events_[index]; if(e.time>=end) break;
            const auto stop=index+1<events_.size()?events_[index+1].time:now;
            const auto a=std::max(begin,e.time), b=std::min(end,stop);
            if(b<=a) continue;
            if(!result.empty()&&result.back().state==e.state&&result.back().end==a) result.back().end=b;
            else result.push_back({a,b,e.state});
        }
        return result;
    }
    ActivitySummary summarize(std::int64_t begin,std::int64_t end,std::int64_t now) const {
        ActivitySummary result;
        for(const auto& s:spans(begin,end,now)) result.milliseconds[static_cast<size_t>(s.state)]+=s.end-s.begin;
        auto it=std::lower_bound(events_.begin(),events_.end(),begin,[](const ActivityEvent& e,auto time){return e.time<time;});
        for(;it!=events_.end()&&it->time<end&&it->time<=now;++it) {
            if(it->kind=="app_start") ++result.starts;
            if(it->kind=="break_auto"||it->kind=="break_manual") ++result.rests;
            if(it->kind=="restart") ++result.restarts;
            if(it->kind=="snooze") ++result.snoozes;
        }
        return result;
    }
};
}
