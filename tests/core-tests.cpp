#include "../src/core.hpp"
#include <iostream>
#include <stdexcept>
using namespace interval;
int checks=0;
void check(bool condition,const char* name) { ++checks; if(!condition) throw std::runtime_error(name); }
template<class F> void rejects(F fn,const char* name) { bool failed=false; try { fn(); } catch(const std::exception&) { failed=true; } check(failed,name); }
int main() {
    try {
        Schedule timer;
        timer.restart(100,50*60000);
        check(timer.remaining(100)==3000000,"default 50 minutes");
        check(!timer.due(3000099)&&timer.due(3000100),"exact deadline");
        timer.hold(1000100); check(timer.remaining(9900000)==2000000,"locked time is excluded");
        timer.hold(2000000); timer.resume(9900000); check(timer.remaining(9900000)==2000000,"nested hold does not overwrite remaining");
        timer.hold(9900010); timer.restart(9900010,3000000); timer.resume(15000000);
        check(timer.remaining(15000000)==3000000,"restart from a break counts a fresh full interval");
        timer.restart(15000000,300000); check(timer.due(15300000),"five minute snooze");
        auto countdown=countdownIndicator(3000000,false,false,TaskbarMode::Always);
        check(countdown.minutes==50&&countdown.visible&&!countdown.warning,"taskbar shows default 50 minutes");
        countdown=countdownIndicator(300001,false,false,TaskbarMode::LastFiveMinutes);
        check(countdown.minutes==6&&!countdown.visible&&!countdown.warning,"no early five-minute warning");
        countdown=countdownIndicator(300000,false,false,TaskbarMode::LastFiveMinutes);
        check(countdown.minutes==5&&countdown.visible&&countdown.warning,"five-minute boundary enables taskbar");
        check(countdownIndicator(1,false,false,TaskbarMode::Always).minutes==1,"remaining minutes round up");
        check(!countdownIndicator(0,false,false,TaskbarMode::LastFiveMinutes).visible,"deadline is not a pre-break warning");
        check(!countdownIndicator(240000,true,false,TaskbarMode::LastFiveMinutes).warning,"held schedule does not warn");
        check(!countdownIndicator(240000,false,true,TaskbarMode::LastFiveMinutes).visible,"break is not counted as upcoming");
        check(!countdownIndicator(240000,false,false,TaskbarMode::Hidden).visible,"disabled taskbar remains hidden");
        check(countdownIndicator(240*60000,false,false,TaskbarMode::Always).minutes==240,"three-digit maximum interval");
        check(parseJson("{\"x\":[1,true,null,\"\\uD83D\\uDE00\"]}").at("x").at(3).string.size()==4,"Unicode surrogate pair");
        for(const auto* bad:{"[1,]","{\"a\":1,}","01","1e","\"\\uD800\"","{\"x\":1,\"x\":2}","true false","1e999"}) rejects([&](){parseJson(bad);},"malformed JSON rejected");
        rejects([](){parseJson(std::string(40,'[')+"0"+std::string(40,']'));},"depth bounded");
        constexpr std::int64_t now=1790870400000;
        auto dollar=parseQuote(Provider::Binance,R"({"symbol":"BTCUSDT","price":"60001.75"})",now);
        check(dollar.price==60001.75&&dollar.fresh,"Binance quote");
        auto okx=parseQuote(Provider::Okx,R"({"code":"0","data":[{"instId":"BTC-USDT","last":"60000"}]})",now);
        check(okx.source==L"OKX"&&okx.price==60000,"OKX quote");
        auto won=parseQuote(Provider::Bithumb,R"([{"market":"KRW-BTC","trade_price":90000000}])",now);
        check(won.price==90000000,"KRW numeric quote");
        rejects([&](){parseQuote(Provider::Binance,R"({"symbol":"ETHUSDT","price":"1"})",now);},"wrong symbol rejected");
        rejects([&](){parseQuote(Provider::Okx,R"({"code":"51000","data":[]})",now);},"API error rejected");
        rejects([&](){parseQuote(Provider::Upbit,R"([{"market":"KRW-BTC","trade_price":0}])",now);},"zero price rejected");
        std::vector<std::wstring> calls;
        Fetch fallback=[&](const auto& host,const auto&) -> std::string { calls.push_back(host); if(host==L"data-api.binance.vision") throw std::runtime_error("blocked"); return R"({"code":"0","data":[{"instId":"BTC-USDT","last":"61000"}]})"; };
        refreshQuote(dollar,Provider::Binance,Provider::Okx,fallback,now);
        check(calls.size()==2&&calls[0]==L"data-api.binance.vision"&&calls[1]==L"www.okx.com"&&dollar.source==L"OKX","dollar provider priority and fallback");
        calls.clear();
        Fetch krwFallback=[&](const auto& host,const auto&) -> std::string { calls.push_back(host); return host==L"api.bithumb.com"?"{\"error\":1}":R"([{"market":"KRW-BTC","trade_price":91000000}])"; };
        refreshQuote(won,Provider::Bithumb,Provider::Upbit,krwFallback,now);
        check(calls.size()==2&&calls[0]==L"api.bithumb.com"&&calls[1]==L"api.upbit.com"&&won.source==L"업비트","malformed primary fails over to Upbit");
        Fetch fail=[](const auto&,const auto&) -> std::string { throw std::runtime_error("offline"); };
        auto timestamp=dollar.fetchedAt;
        refreshQuote(dollar,Provider::Binance,Provider::Okx,fail,now+60000);
        check(dollar.price==61000&&!dollar.fresh&&dollar.fetchedAt==timestamp,"offline preserves last quote with stale state");
        std::string candles=R"({"code":"0","data":[["1790870400000","1","1","1","120"],["1790866800000","1","1","1","100"]]})";
        auto series=parseSeries(Provider::Okx,candles,now);
        check(series.points.front().price==100&&series.points.back().price==120,"reverse-order candles sorted");
        check(std::abs(series.change()-20)<.0001,"chart-derived weekly change");
        auto krwSeries=parseSeries(Provider::Upbit,R"([{"market":"KRW-BTC","timestamp":1790870400000,"trade_price":200},{"market":"KRW-BTC","timestamp":1790866800000,"trade_price":100}])",now);
        check(krwSeries.points.size()==2&&krwSeries.points.front().price==100,"KRW chart");
        rejects([&](){parseSeries(Provider::Binance,R"([[1790870400000,"1","1","1","120"]])",now);},"one point cannot masquerade as a chart");
        refreshSeries(series,Provider::Binance,Provider::Okx,fail,now+60000);
        check(!series.fresh&&series.points.size()==2,"offline preserves chart with stale state");
        std::cout<<"PASS: "<<checks<<" meaningful checks (schedule, JSON, feed parsing, priority, fallback, stale data).\n";
        return 0;
    } catch(const std::exception& e) { std::cerr<<"FAIL: "<<e.what()<<"\n"; return 1; }
}
