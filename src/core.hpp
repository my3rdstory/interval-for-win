#pragma once
#include "json.hpp"
#include <algorithm>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace interval {
using Tick = std::uint64_t;
// The platform clock excludes sleep; explicit holds additionally exclude locked sessions.
class Schedule {
    Tick due_ = 0, held_ = 0;
    bool holding_ = false;
public:
    void restart(Tick now, Tick duration) { held_=duration; due_=now+duration; }
    Tick remaining(Tick now) const { return holding_ ? held_ : (due_>now ? due_-now : 0); }
    bool due(Tick now) const { return !holding_ && remaining(now)==0; }
    void hold(Tick now) { if(!holding_) { held_=remaining(now); holding_=true; } }
    void resume(Tick now) { if(holding_) { due_=now+held_; holding_=false; } }
    bool holding() const { return holding_; }
};
struct Quote {
    double price=0;
    std::wstring source;
    std::int64_t fetchedAt=0;
    bool fresh=false;
};
struct Point { std::int64_t time=0; double price=0; };
struct Series {
    std::vector<Point> points;
    std::wstring source;
    std::int64_t fetchedAt=0;
    bool fresh=false;
    double change() const { return points.size()>1 ? (points.back().price/points.front().price-1)*100 : 0; }
};
struct Market { Quote dollar, won; Series dollarChart, wonChart; };
enum class Provider { Binance, Okx, Bithumb, Upbit };
inline std::wstring providerName(Provider p) {
    switch(p) { case Provider::Binance:return L"Binance"; case Provider::Okx:return L"OKX"; case Provider::Bithumb:return L"빗썸"; default:return L"업비트"; }
}
inline Quote parseQuote(Provider provider,const std::string& body,std::int64_t now) {
    auto root=parseJson(body);
    double price=0;
    if(provider==Provider::Binance) {
        if(root.at("symbol").string!="BTCUSDT") throw std::runtime_error("Wrong market");
        price=root.at("price").numeric();
    } else if(provider==Provider::Okx) {
        if(root.at("code").string!="0" || root.at("data").at(0).at("instId").string!="BTC-USDT") throw std::runtime_error("OKX error");
        price=root.at("data").at(0).at("last").numeric();
    } else {
        if(root.at(0).at("market").string!="KRW-BTC") throw std::runtime_error("Wrong market");
        price=root.at(0).at("trade_price").numeric();
    }
    if(price<=0) throw std::runtime_error("Invalid price");
    return {price,providerName(provider),now,true};
}
inline Series parseSeries(Provider provider,const std::string& body,std::int64_t now) {
    auto root=parseJson(body);
    Series result; result.source=providerName(provider); result.fetchedAt=now; result.fresh=true;
    const Json* rows=&root;
    if(provider==Provider::Okx) {
        if(root.at("code").string!="0") throw std::runtime_error("OKX error");
        rows=&root.at("data");
    }
    if(rows->kind!=Json::Kind::Array) throw std::runtime_error("Invalid candles");
    for(const auto& row:rows->array) {
        Point p;
        if(provider==Provider::Binance||provider==Provider::Okx) { p.time=static_cast<std::int64_t>(row.at(0).numeric()); p.price=row.at(4).numeric(); }
        else {
            if(row.at("market").string!="KRW-BTC") throw std::runtime_error("Wrong market");
            // Candle timestamp is last trade time, not its start; fine for a close-price line.
            p.time=static_cast<std::int64_t>(row.at("timestamp").numeric()); p.price=row.at("trade_price").numeric();
        }
        if(p.price<=0 || p.time<=0 || p.time>now+3600000) throw std::runtime_error("Invalid candle");
        if(p.time>=now-7LL*24*3600000) result.points.push_back(p);
    }
    std::sort(result.points.begin(),result.points.end(),[](const Point&a,const Point&b){return a.time<b.time;});
    result.points.erase(std::unique(result.points.begin(),result.points.end(),[](const Point&a,const Point&b){return a.time==b.time;}),result.points.end());
    if(result.points.size()<2) throw std::runtime_error("Insufficient history");
    return result;
}
using Fetch = std::function<std::string(const std::wstring&,const std::wstring&)>;
inline std::wstring host(Provider p) {
    switch(p) { case Provider::Binance:return L"data-api.binance.vision"; case Provider::Okx:return L"www.okx.com"; case Provider::Bithumb:return L"api.bithumb.com"; default:return L"api.upbit.com"; }
}
inline std::wstring quotePath(Provider p) {
    if(p==Provider::Binance) return L"/api/v3/ticker/price?symbol=BTCUSDT";
    if(p==Provider::Okx) return L"/api/v5/market/ticker?instId=BTC-USDT";
    return L"/v1/ticker?markets=KRW-BTC";
}
inline std::wstring chartPath(Provider p) {
    if(p==Provider::Binance) return L"/api/v3/klines?symbol=BTCUSDT&interval=1h&limit=169";
    if(p==Provider::Okx) return L"/api/v5/market/candles?instId=BTC-USDT&bar=1H&limit=169";
    return L"/v1/candles/minutes/60?market=KRW-BTC&count=169";
}
// Each feed fails over independently. Failed refreshes retain the last value marked stale.
inline void refreshQuote(Quote& q,Provider first,Provider second,const Fetch& fetch,std::int64_t now) {
    q.fresh=false;
    for(auto p:{first,second}) { try { q=parseQuote(p,fetch(host(p),quotePath(p)),now); return; } catch(const std::exception&) {} }
}
inline void refreshSeries(Series& s,Provider first,Provider second,const Fetch& fetch,std::int64_t now) {
    s.fresh=false;
    for(auto p:{first,second}) { try { s=parseSeries(p,fetch(host(p),chartPath(p)),now); return; } catch(const std::exception&) {} }
}
}
