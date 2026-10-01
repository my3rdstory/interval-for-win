#pragma once
#include <cmath>
#include <cstdlib>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

// A bounded JSON reader for the public REST responses. No third-party runtime.
namespace interval {
struct Json {
    enum class Kind { Null, Bool, Number, String, Array, Object } kind = Kind::Null;
    double number = 0;
    std::string string;
    std::vector<Json> array;
    std::map<std::string, Json> object;
    const Json& at(const std::string& key) const {
        if (kind != Kind::Object) throw std::runtime_error("Expected object");
        return object.at(key);
    }
    const Json& at(size_t i) const {
        if (kind != Kind::Array) throw std::runtime_error("Expected array");
        return array.at(i);
    }
    double numeric() const {
        if (kind == Kind::Number && std::isfinite(number)) return number;
        if (kind == Kind::String && !string.empty()) {
            char* end = nullptr;
            const double n = std::strtod(string.c_str(), &end);
            if (end == string.c_str() + string.size() && std::isfinite(n)) return n;
        }
        throw std::runtime_error("Invalid numeric value");
    }
};

class JsonReader {
    const std::string& input;
    size_t pos = 0;
    [[noreturn]] void fail() const { throw std::runtime_error("Invalid JSON"); }
    void space() { while (pos < input.size() && (input[pos]==' ' || input[pos]=='\r' || input[pos]=='\n' || input[pos]=='\t')) ++pos; }
    bool take(char c) { space(); if (pos<input.size() && input[pos]==c) { ++pos; return true; } return false; }
    unsigned hex4() {
        unsigned n = 0;
        for (int i=0;i<4;++i) {
            if (pos>=input.size()) fail();
            const char c=input[pos++];
            n <<= 4;
            if (c>='0' && c<='9') n+=c-'0';
            else if (c>='a' && c<='f') n+=c-'a'+10;
            else if (c>='A' && c<='F') n+=c-'A'+10;
            else fail();
        }
        return n;
    }
    static void utf8(std::string& s, unsigned n) {
        if (n<0x80) s+=static_cast<char>(n);
        else if(n<0x800) { s+=static_cast<char>(0xc0|(n>>6)); s+=static_cast<char>(0x80|(n&63)); }
        else if(n<0x10000) { s+=static_cast<char>(0xe0|(n>>12)); s+=static_cast<char>(0x80|((n>>6)&63)); s+=static_cast<char>(0x80|(n&63)); }
        else { s+=static_cast<char>(0xf0|(n>>18)); s+=static_cast<char>(0x80|((n>>12)&63)); s+=static_cast<char>(0x80|((n>>6)&63)); s+=static_cast<char>(0x80|(n&63)); }
    }
    std::string text() {
        if (!take('"')) fail();
        std::string out;
        while (pos<input.size()) {
            const unsigned char c=static_cast<unsigned char>(input[pos++]);
            if (c=='"') return out;
            if (c<32) fail();
            if (c!='\\') { out+=static_cast<char>(c); continue; }
            if (pos>=input.size()) fail();
            switch(input[pos++]) {
                case '"': out+='"'; break; case '\\': out+='\\'; break; case '/': out+='/'; break;
                case 'b': out+='\b'; break; case 'f': out+='\f'; break; case 'n': out+='\n'; break;
                case 'r': out+='\r'; break; case 't': out+='\t'; break;
                case 'u': {
                    unsigned n=hex4();
                    if (n>=0xd800 && n<=0xdbff) {
                        if(pos+2>input.size() || input[pos++]!='\\' || input[pos++]!='u') fail();
                        unsigned low=hex4(); if(low<0xdc00 || low>0xdfff) fail();
                        n=0x10000+((n-0xd800)<<10)+(low-0xdc00);
                    } else if (n>=0xdc00 && n<=0xdfff) fail();
                    utf8(out,n); break;
                }
                default: fail();
            }
        }
        fail();
    }
    Json value(int depth) {
        if (depth>32) fail();
        space(); if(pos>=input.size()) fail();
        Json j;
        if(input[pos]=='"') { j.kind=Json::Kind::String; j.string=text(); }
        else if(take('[')) {
            j.kind=Json::Kind::Array;
            if(take(']')) return j;
            do { j.array.push_back(value(depth+1)); } while(take(','));
            if(!take(']')) fail();
        } else if(take('{')) {
            j.kind=Json::Kind::Object;
            if(take('}')) return j;
            do { auto key=text(); if(!take(':')) fail(); auto v=value(depth+1); if(!j.object.emplace(key,std::move(v)).second) fail(); } while(take(','));
            if(!take('}')) fail();
        } else if(input.compare(pos,4,"null")==0) { pos+=4; }
        else if(input.compare(pos,4,"true")==0) { j.kind=Json::Kind::Bool; j.number=1; pos+=4; }
        else if(input.compare(pos,5,"false")==0) { j.kind=Json::Kind::Bool; pos+=5; }
        else {
            size_t start=pos;
            if(input[pos]=='-') ++pos;
            if(pos>=input.size()) fail();
            if(input[pos]=='0') ++pos;
            else { if(input[pos]<'1'||input[pos]>'9') fail(); while(pos<input.size() && input[pos]>='0' && input[pos]<='9') ++pos; }
            if(pos<input.size() && input[pos]=='.') { ++pos; size_t p=pos; while(pos<input.size()&&input[pos]>='0'&&input[pos]<='9') ++pos; if(p==pos) fail(); }
            if(pos<input.size() && (input[pos]=='e'||input[pos]=='E')) { ++pos; if(pos<input.size()&&(input[pos]=='+'||input[pos]=='-')) ++pos; size_t p=pos; while(pos<input.size()&&input[pos]>='0'&&input[pos]<='9') ++pos; if(p==pos) fail(); }
            j.kind=Json::Kind::Number;
            j.number=std::strtod(input.substr(start,pos-start).c_str(),nullptr);
            if(!std::isfinite(j.number)) fail();
        }
        return j;
    }
public:
    explicit JsonReader(const std::string& s):input(s) {}
    Json parse() { if(input.size()>2*1024*1024) fail(); auto j=value(0); space(); if(pos!=input.size()) fail(); return j; }
};
inline Json parseJson(const std::string& s) { return JsonReader(s).parse(); }
}
