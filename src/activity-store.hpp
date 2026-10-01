#pragma once
#include "activity.hpp"
#include <windows.h>
#include <fstream>

namespace interval {
class ActivityStore {
    HANDLE file_=INVALID_HANDLE_VALUE;
    std::wstring path_;
    std::string pending_;
    bool damaged_=false, failed_=false;
public:
    ActivityHistory history;
    ~ActivityStore() { if(file_!=INVALID_HANDLE_VALUE) CloseHandle(file_); }
    bool failed() const { return failed_; }
    bool damaged() const { return damaged_; }
    const std::wstring& path() const { return path_; }
    void open(const std::wstring& path) {
        path_=path; if(path.empty()) return; // Tests without a path are memory-only.
        const auto attrs=GetFileAttributesW(path.c_str());
        if(attrs==INVALID_FILE_ATTRIBUTES&&GetLastError()!=ERROR_FILE_NOT_FOUND) failed_=true;
        std::ifstream input(path,std::ios::binary); std::string line;
        bool hole=false;
        while(std::getline(input,line)) {
            if(line.empty()||line=="\r") continue;
            ActivityEvent e;
            if(!decodeActivity(line,e)) { damaged_=true; hole=true; continue; }
            if(hole&&!history.events().empty()) {
                const auto& last=history.events().back();
                history.append(last.time,"damaged_record",ActivityState::Unknown,last.minutes);
            }
            hole=false;
            if(!history.restore(e)) { damaged_=true; hole=true; }
        }
        if(hole&&!history.events().empty()) {
            const auto last=history.events().back(); history.append(last.time,"damaged_record",ActivityState::Unknown,last.minutes);
        }
        // Start a fresh line even if the previous process stopped during a write.
        pending_="\n"; flush();
    }
    void flush() {
        if(path_.empty()) return;
        if(file_==INVALID_HANDLE_VALUE) file_=CreateFileW(path_.c_str(),FILE_APPEND_DATA,FILE_SHARE_READ,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(file_==INVALID_HANDLE_VALUE) { failed_=true; return; }
        while(!pending_.empty()) {
            DWORD written=0;
            if(!WriteFile(file_,pending_.data(),static_cast<DWORD>(std::min<size_t>(pending_.size(),1024*1024)),&written,nullptr)||!written) { failed_=true; return; }
            pending_.erase(0,written);
        }
        failed_=!FlushFileBuffers(file_);
    }
    void record(std::int64_t time,const std::string& kind,ActivityState state,int minutes) {
        const auto e=history.append(time,kind,state,minutes);
        if(!path_.empty()) pending_+=encodeActivity(e);
        flush();
    }
    void start(std::int64_t now,ActivityState state,int minutes) {
        if(!history.events().empty()) {
            const auto last=history.events().back();
            if(last.state!=ActivityState::Offline) record(last.time,"interrupted",ActivityState::Unknown,last.minutes);
        }
        record(now,"app_start",state,minutes);
    }
};
}
