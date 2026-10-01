#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <objidl.h>
#include <gdiplus.h>
#include <winhttp.h>
#include <wtsapi32.h>
#include <commctrl.h>
#include <shobjidl.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <ctime>
#include <fstream>
#include <memory>
#include <mutex>
#include <random>
#include <sstream>
#include <thread>
#include "core.hpp"
#include "activity-store.hpp"
#include "content.hpp"

#pragma comment(lib,"gdiplus.lib")
#pragma comment(lib,"winhttp.lib")
#pragma comment(lib,"wtsapi32.lib")
#pragma comment(lib,"comctl32.lib")
#pragma comment(lib,"shell32.lib")
#pragma comment(lib,"advapi32.lib")
#pragma comment(lib,"user32.lib")
#pragma comment(lib,"gdi32.lib")
#pragma comment(lib,"ole32.lib")
#pragma comment(lib,"uuid.lib")

using namespace Gdiplus;
using namespace interval;
namespace {
constexpr UINT TrayMessage=WM_APP+1, MarketMessage=WM_APP+2, OpenMessage=WM_APP+3;
constexpr UINT_PTR DueTimer=1, MinuteTimer=2, ScreenTimer=3, RefreshTimer=4, CountdownTimer=5;
enum Command { Restart=100, Snooze, Theme, Settings, NextThought, DollarChart, WonChart, Refresh,
    IntervalEdit=200, ApplyInterval, ThemeSystem, ThemeLight, ThemeDark, Startup, AllScreens, RestNow, HideSettings, TaskbarDisplay,
    TrayOpen=300, TrayRest, TrayRestart, TrayPause, TrayQuit,
    Dashboard=400, DayPrevious, DayNext, DayToday, EventsPrevious, EventsNext };
enum class ThemeMode { System=0, Light=1, Dark=2 };
struct Palette { Color background, text, muted, line, surface, accent, accentText; };
Palette lightPalette() { return {Color(250,250,247),Color(30,34,31),Color(105,112,105),Color(224,227,220),Color(239,241,234),Color(206,234,157),Color(35,55,25)}; }
Palette darkPalette() { return {Color(20,24,22),Color(236,241,231),Color(154,166,155),Color(52,61,53),Color(30,37,31),Color(192,225,144),Color(28,45,22)}; }
Tick clockNow() { ULONGLONG time=0; QueryUnbiasedInterruptTime(&time); return time/10000; }
std::int64_t unixNow() { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count(); }
std::wstring executable() { wchar_t buffer[32768]{}; GetModuleFileNameW(nullptr,buffer,32768); return buffer; }
std::wstring localTime(std::int64_t stamp) {
    if(!stamp) return L"";
    ULARGE_INTEGER time{}; time.QuadPart=static_cast<ULONGLONG>(stamp)*10000+116444736000000000ULL;
    FILETIME utc{time.LowPart,time.HighPart}, local{}; SYSTEMTIME s{};
    FileTimeToLocalFileTime(&utc,&local); FileTimeToSystemTime(&local,&s);
    wchar_t out[32]{}; swprintf_s(out,L"%02u:%02u",s.wHour,s.wMinute); return out;
}
std::wstring dateLabel(std::int64_t stamp) {
    ULARGE_INTEGER time{}; time.QuadPart=static_cast<ULONGLONG>(stamp)*10000+116444736000000000ULL;
    FILETIME utc{time.LowPart,time.HighPart}, local{}; SYSTEMTIME s{};
    FileTimeToLocalFileTime(&utc,&local); FileTimeToSystemTime(&local,&s);
    wchar_t out[32]{}; swprintf_s(out,L"%u.%02u",s.wMonth,s.wDay); return out;
}
std::wstring money(double number,bool won) {
    if(number<=0) return L"—";
    wchar_t raw[64]{}; swprintf_s(raw,won?L"%.0f":L"%.2f",number);
    std::wstring s=raw; size_t decimal=s.find(L'.'); if(decimal==std::wstring::npos) decimal=s.size();
    for(int i=static_cast<int>(decimal)-3;i>0;i-=3) s.insert(static_cast<size_t>(i),L",");
    return (won?L"₩ ":L"$ ")+s;
}
std::wstring percentage(double n) { wchar_t out[40]{}; swprintf_s(out,L"%+.2f%%",n); return out; }
std::wstring duration(Tick ms) { auto secs=(ms+999)/1000; wchar_t out[40]{}; swprintf_s(out,L"%02llu:%02llu",secs/60,secs%60); return out; }
class HttpHandle {
    HINTERNET handle=nullptr;
public:
    explicit HttpHandle(HINTERNET h):handle(h) { if(!h) throw std::runtime_error("HTTP handle failed"); }
    ~HttpHandle() { if(handle) WinHttpCloseHandle(handle); }
    operator HINTERNET() const { return handle; }
    HttpHandle(const HttpHandle&)=delete;
};
std::string httpGet(const std::wstring& hostName,const std::wstring& path) {
    HttpHandle session(WinHttpOpen(L"Interval/1.0",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0));
    WinHttpSetTimeouts(session,2500,2500,2500,3500);
    HttpHandle connection(WinHttpConnect(session,hostName.c_str(),INTERNET_DEFAULT_HTTPS_PORT,0));
    HttpHandle request(WinHttpOpenRequest(connection,L"GET",path.c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE));
    DWORD policy=WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    WinHttpSetOption(request,WINHTTP_OPTION_REDIRECT_POLICY,&policy,sizeof(policy));
    if(!WinHttpSendRequest(request,L"Accept: application/json\r\n",static_cast<DWORD>(-1),WINHTTP_NO_REQUEST_DATA,0,0,0) || !WinHttpReceiveResponse(request,nullptr)) throw std::runtime_error("HTTP unavailable");
    DWORD status=0, size=sizeof(status);
    if(!WinHttpQueryHeaders(request,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&status,&size,WINHTTP_NO_HEADER_INDEX) || status!=200) throw std::runtime_error("HTTP status error");
    std::string body; char buffer[8192]; DWORD bytes=0;
    const auto deadline=GetTickCount64()+12000;
    for(;;) {
        if(GetTickCount64()>deadline) throw std::runtime_error("HTTP deadline");
        if(!WinHttpReadData(request,buffer,sizeof(buffer),&bytes)) throw std::runtime_error("HTTP read error");
        if(!bytes) break;
        if(body.size()+bytes>2*1024*1024) throw std::runtime_error("HTTP body too large");
        body.append(buffer,bytes);
    }
    return body;
}
struct NetworkState {
    std::mutex mutex;
    Market result;
    std::atomic<bool> cancelled{false};
    std::atomic<bool> done{false};
};
struct Options {
    bool background=false, preview=false, offline=false, test=false;
    int testSeconds=0;
    std::wstring diagnostic;
    std::wstring history;
};
class App;
App* instance=nullptr;
LRESULT CALLBACK windowProc(HWND,UINT,WPARAM,LPARAM);
class App {
    Options options;
    HWND owner=nullptr, settings=nullptr, screen=nullptr, intervalEdit=nullptr, dashboard=nullptr;
    ActivityStore activity;
    std::vector<HWND> dashboardButtons;
    std::int64_t lastObserved=0, lastSaved=0;
    Tick lastObservedTick=0;
    bool activityStarted=false, activityEnded=false;
    int dayOffset=0;
    size_t eventOffset=0;
    std::vector<HWND> shades;
    Schedule schedule;
    int minutes=50;
    ThemeMode mode=ThemeMode::System;
    TaskbarMode taskbarMode=TaskbarMode::Always;
    bool allMonitors=true, paused=false, locked=false, sleeping=false, breaking=false, trayAdded=false, fontLoaded=false;
    bool wonSelected=false;
    Tick breakStarted=0;
    Tick cycleLength=0;
    UINT taskbarCreated=0, taskbarButtonCreated=0;
    HICON icon=nullptr;
    HICON countdownIcon=nullptr;
    int countdownIconKey=-1;
    ITaskbarList3* taskbar=nullptr;
    bool comInitialized=false, settingsTaskbarReady=false, screenTaskbarReady=false;
    bool taskbarProgressApplied=false;
    ULONG_PTR gdiplusToken=0;
    std::unique_ptr<PrivateFontCollection> fonts;
    std::unique_ptr<FontFamily> fontLight, fontMedium;
    std::vector<HANDLE> nativeFontResources;
    HFONT editFont=nullptr;
    Market market;
    std::thread worker;
    std::shared_ptr<NetworkState> network;
    std::mt19937 random{static_cast<unsigned>(GetTickCount64()^GetCurrentProcessId())};
    size_t restIndex=0, thoughtIndex=0;
    bool marketThought=false;
    std::wstring marketTitle, marketBody;
    std::wstring statusMessage;
    std::vector<HWND> mainButtons, settingButtons;
    ActivityState activityState() const {
        if(sleeping) return ActivityState::Sleeping;
        if(locked) return ActivityState::Locked;
        if(paused) return ActivityState::Paused;
        return breaking?ActivityState::Resting:ActivityState::Running;
    }
    void observeActivity() {
        if(!activityStarted||activityEnded) return;
        const auto now=unixNow(); const auto tick=clockNow();
        const auto wallElapsed=now-lastObserved;
        const auto awakeElapsed=static_cast<std::int64_t>(tick-lastObservedTick);
        if(lastObserved&&activityState()!=ActivityState::Sleeping&&(wallElapsed>45000||wallElapsed-awakeElapsed>2000)) {
            activity.record(lastObserved,"unobserved",ActivityState::Unknown,minutes);
            activity.record(now,"recovered",activityState(),minutes);
        } else if(wallElapsed<0) activity.record(now,"clock_changed",activityState(),minutes);
        lastObserved=now; lastObservedTick=tick;
    }
    void recordActivity(const char* kind) {
        if(!activityStarted||activityEnded) return;
        activity.record(unixNow(),kind,activityState(),minutes); lastSaved=unixNow();
        lastObserved=lastSaved; lastObservedTick=clockNow();
        if(dashboard) InvalidateRect(dashboard,nullptr,FALSE);
    }
    void endActivity(const char* kind) {
        if(!activityStarted||activityEnded) return;
        observeActivity(); activity.record(unixNow(),kind,ActivityState::Offline,minutes); activityEnded=true;
    }
    Palette palette() const {
        bool dark=mode==ThemeMode::Dark;
        if(mode==ThemeMode::System) { DWORD value=1,size=sizeof(value); RegGetValueW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",L"AppsUseLightTheme",RRF_RT_REG_DWORD,nullptr,&value,&size); dark=value==0; }
        return dark?darkPalette():lightPalette();
    }
    Tick intervalLength() const { return options.testSeconds>0 ? static_cast<Tick>(options.testSeconds)*1000 : static_cast<Tick>(minutes)*60000; }
    bool startupEnabled() const {
        wchar_t buffer[32768]{}; DWORD bytes=sizeof(buffer);
        if(RegGetValueW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",L"Interval",RRF_RT_REG_SZ,nullptr,buffer,&bytes)!=ERROR_SUCCESS) return false;
        return std::wstring(buffer)==L"\""+executable()+L"\" --background";
    }
    void savePreferences() {
        if(options.test) return;
        HKEY key=nullptr;
        if(RegCreateKeyExW(HKEY_CURRENT_USER,L"Software\\Interval",0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr)!=ERROR_SUCCESS) { statusMessage=L"설정을 저장하지 못했어요."; return; }
        DWORD interval=static_cast<DWORD>(minutes), theme=static_cast<DWORD>(mode), monitors=allMonitors?1:0, taskbarDisplay=static_cast<DWORD>(taskbarMode);
        const bool ok=RegSetValueExW(key,L"IntervalMinutes",0,REG_DWORD,reinterpret_cast<BYTE*>(&interval),sizeof(interval))==ERROR_SUCCESS
            && RegSetValueExW(key,L"Theme",0,REG_DWORD,reinterpret_cast<BYTE*>(&theme),sizeof(theme))==ERROR_SUCCESS
            && RegSetValueExW(key,L"AllMonitors",0,REG_DWORD,reinterpret_cast<BYTE*>(&monitors),sizeof(monitors))==ERROR_SUCCESS
            && RegSetValueExW(key,L"TaskbarDisplay",0,REG_DWORD,reinterpret_cast<BYTE*>(&taskbarDisplay),sizeof(taskbarDisplay))==ERROR_SUCCESS;
        RegCloseKey(key); if(!ok) statusMessage=L"설정을 저장하지 못했어요.";
    }
    void loadPreferences() {
        if(options.test) return;
        DWORD value=50,bytes=sizeof(value);
        if(RegGetValueW(HKEY_CURRENT_USER,L"Software\\Interval",L"IntervalMinutes",RRF_RT_REG_DWORD,nullptr,&value,&bytes)==ERROR_SUCCESS && value>=1 && value<=240) minutes=static_cast<int>(value);
        value=0; bytes=sizeof(value); if(RegGetValueW(HKEY_CURRENT_USER,L"Software\\Interval",L"Theme",RRF_RT_REG_DWORD,nullptr,&value,&bytes)==ERROR_SUCCESS && value<=2) mode=static_cast<ThemeMode>(value);
        value=1; bytes=sizeof(value); if(RegGetValueW(HKEY_CURRENT_USER,L"Software\\Interval",L"AllMonitors",RRF_RT_REG_DWORD,nullptr,&value,&bytes)==ERROR_SUCCESS) allMonitors=value!=0;
        value=0; bytes=sizeof(value); if(RegGetValueW(HKEY_CURRENT_USER,L"Software\\Interval",L"TaskbarDisplay",RRF_RT_REG_DWORD,nullptr,&value,&bytes)==ERROR_SUCCESS&&value<=2) taskbarMode=static_cast<TaskbarMode>(value);
    }
    void setStartup() {
        if(options.test) { statusMessage=L"테스트 모드에서는 자동 실행 설정을 변경하지 않아요."; repaint(); return; }
        const bool enabled=startupEnabled(); HKEY key=nullptr;
        if(RegCreateKeyExW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr)!=ERROR_SUCCESS) { statusMessage=L"자동 실행 설정을 변경하지 못했어요."; repaint(); return; }
        LONG result;
        if(enabled) result=RegDeleteValueW(key,L"Interval");
        else { auto value=L"\""+executable()+L"\" --background"; result=RegSetValueExW(key,L"Interval",0,REG_SZ,reinterpret_cast<const BYTE*>(value.c_str()),static_cast<DWORD>((value.size()+1)*sizeof(wchar_t))); }
        RegCloseKey(key);
        statusMessage=result==ERROR_SUCCESS ? (enabled?L"Windows 자동 실행을 껐어요.":L"다음 Windows 로그인부터 트레이에서 시작해요.") : L"자동 실행 설정을 변경하지 못했어요.";
        updateSettingsLabels(); repaint();
    }
    void holdState() {
        if(paused||locked||sleeping||breaking) schedule.hold(clockNow()); else schedule.resume(clockNow());
        armTimer(); updateTray(); updateTaskbar(); diagnostics();
    }
    void armTimer() {
        KillTimer(owner,DueTimer);
        if(!schedule.holding()) SetTimer(owner,DueTimer,static_cast<UINT>(std::clamp<Tick>(schedule.remaining(clockNow()),10,USER_TIMER_MAXIMUM)),nullptr);
    }
    void restart(Tick length=0,const char* reason="restart") {
        closeBreak(); paused=false;
        cycleLength=length?length:intervalLength(); schedule.restart(clockNow(),cycleLength);
        recordActivity(reason); holdState(); updateSettingsLabels(); repaint();
    }
    void updateTray(bool add=false) {
        NOTIFYICONDATAW data{}; data.cbSize=sizeof(data); data.hWnd=owner; data.uID=1;
        data.uFlags=NIF_ICON|NIF_MESSAGE|NIF_TIP|NIF_SHOWTIP; data.hIcon=icon; data.uCallbackMessage=TrayMessage;
        auto label=L"인터벌 · "+(breaking?std::wstring(L"휴식 중"):paused?std::wstring(L"일시 정지"):locked||sleeping?std::wstring(L"잠시 보류"):L"다음 휴식 "+duration(schedule.remaining(clockNow())));
        wcsncpy_s(data.szTip,label.c_str(),_TRUNCATE);
        if(add||!trayAdded) { trayAdded=Shell_NotifyIconW(NIM_ADD,&data)!=FALSE; data.uVersion=NOTIFYICON_VERSION_4; Shell_NotifyIconW(NIM_SETVERSION,&data); }
        else Shell_NotifyIconW(NIM_MODIFY,&data);
    }
    void trayMenu() {
        HMENU menu=CreatePopupMenu();
        auto time=breaking?L"휴식 중":paused?L"일시 정지":L"다음 휴식 · "+duration(schedule.remaining(clockNow()));
        AppendMenuW(menu,MF_STRING|MF_DISABLED,0,time.c_str()); AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
        AppendMenuW(menu,MF_STRING,TrayRest,L"지금 휴식하기"); AppendMenuW(menu,MF_STRING,TrayRestart,L"타이머 다시 시작");
        AppendMenuW(menu,MF_STRING,TrayPause,paused?L"알림 다시 켜기":L"알림 일시 정지");
        AppendMenuW(menu,MF_STRING,Dashboard,L"사용 현황판");
        AppendMenuW(menu,MF_STRING,TrayOpen,L"설정 열기"); AppendMenuW(menu,MF_SEPARATOR,0,nullptr); AppendMenuW(menu,MF_STRING,TrayQuit,L"인터벌 종료");
        POINT point{}; GetCursorPos(&point); SetForegroundWindow(owner);
        UINT command=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,point.x,point.y,0,owner,nullptr);
        DestroyMenu(menu); PostMessageW(owner,WM_NULL,0,0); if(command) execute(command);
    }
    HICON makeIcon() {
        return reinterpret_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(1),IMAGE_ICON,64,64,0));
    }
    void countdownGlyph(Graphics& g,const std::wstring& label) {
        ensureFonts();
        StringFormat format(StringFormat::GenericTypographic());
        format.SetFormatFlags(StringFormatFlagsNoWrap|StringFormatFlagsNoClip);
        Font measureFont(fontMedium.get(),44,FontStyleRegular,UnitPixel);
        RectF measured;
        g.MeasureString(label.c_str(),static_cast<INT>(label.size()),&measureFont,PointF(0,0),&format,&measured);
        const float fit=std::min({1.f,52.f/std::max(1.f,measured.Width),54.f/std::max(1.f,measured.Height)});
        Font font(fontMedium.get(),44*fit,FontStyleRegular,UnitPixel);
        format.SetAlignment(StringAlignmentCenter); format.SetLineAlignment(StringAlignmentCenter);
        SolidBrush ink(Color(35,55,25));
        g.DrawString(label.c_str(),static_cast<INT>(label.size()),&font,RectF(0,0,64,64),&format,&ink);
    }
    CountdownIndicator indicator() const { return countdownIndicator(schedule.remaining(clockNow()),schedule.holding(),breaking,taskbarMode); }
    void updateTaskbar(bool force=false) {
        if(!settings) return;
        const auto state=indicator();
        const int key=breaking?1000:paused?1001:locked||sleeping?1002:static_cast<int>(state.minutes)+(state.warning?500:0);
        if(force||key!=countdownIconKey) {
            Bitmap bitmap(64,64,PixelFormat32bppARGB); Graphics g(&bitmap);
            g.Clear(Color(0,0,0,0)); g.SetSmoothingMode(SmoothingModeAntiAlias); g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
            rounded(g,RectF(1,1,62,62),15,state.warning?Color(255,212,122):palette().accent);
            const auto label=breaking?L"쉼":paused?L"Ⅱ":locked||sleeping?L"—":std::to_wstring(state.minutes);
            countdownGlyph(g,label);
            HICON next=nullptr;
            if(bitmap.GetHICON(&next)==Ok&&next) {
                SendMessageW(settings,WM_SETICON,ICON_BIG,reinterpret_cast<LPARAM>(next)); SendMessageW(settings,WM_SETICON,ICON_SMALL,reinterpret_cast<LPARAM>(next));
                if(screen) { SendMessageW(screen,WM_SETICON,ICON_BIG,reinterpret_cast<LPARAM>(next)); SendMessageW(screen,WM_SETICON,ICON_SMALL,reinterpret_cast<LPARAM>(next)); }
                if(countdownIcon) DestroyIcon(countdownIcon); countdownIcon=next; countdownIconKey=key;
            }
        }
        const auto label=L"인터벌 · "+(breaking?std::wstring(L"휴식 중"):paused?L"일시 정지 · "+duration(schedule.remaining(clockNow())):locked||sleeping?std::wstring(L"잠시 보류"):duration(schedule.remaining(clockNow()))+L" 남음"+(state.warning?L" · 곧 휴식":L""));
        SetWindowTextW(settings,label.c_str());
        if(!breaking) {
            if(state.visible&&!IsWindowVisible(settings)) ShowWindow(settings,SW_SHOWMINNOACTIVE);
            else if(!state.visible&&IsIconic(settings)) ShowWindow(settings,SW_HIDE);
        }
        taskbarProgressApplied=false;
        if(taskbar) {
            const auto remaining=std::min(schedule.remaining(clockNow()),cycleLength);
            for(auto window:{settings,screen}) {
                if(!window||(window==settings?!settingsTaskbarReady:!screenTaskbarReady)) continue;
                if(taskbarMode==TaskbarMode::Hidden) { taskbar->SetProgressState(window,TBPF_NOPROGRESS); continue; }
                const Tick value=breaking?cycleLength:cycleLength-remaining;
                const auto progress=breaking||paused||locked||sleeping||state.warning?TBPF_PAUSED:TBPF_NORMAL;
                // Set value first: SetProgressValue may clear a previously selected state.
                const auto valueResult=taskbar->SetProgressValue(window,value,std::max<Tick>(1,cycleLength));
                const auto stateResult=taskbar->SetProgressState(window,progress);
                if(SUCCEEDED(valueResult)&&SUCCEEDED(stateResult)) taskbarProgressApplied=true;
            }
        }
    }
    void dismissSettings() {
        if(!settings) return;
        ShowWindow(settings,!breaking&&indicator().visible?SW_MINIMIZE:SW_HIDE);
        updateTaskbar(); diagnostics();
    }
    void ensureFonts() {
        if(fontLoaded) return;
        fonts=std::make_unique<PrivateFontCollection>();
        for(int id:{101,102}) {
            auto res=FindResourceW(nullptr,MAKEINTRESOURCEW(id),RT_RCDATA);
            if(res) {
                auto memory=LoadResource(nullptr,res); auto bytes=SizeofResource(nullptr,res); auto data=LockResource(memory);
                fonts->AddMemoryFont(data,static_cast<INT>(bytes));
                DWORD count=0; auto native=AddFontMemResourceEx(data,bytes,nullptr,&count);
                if(native) nativeFontResources.push_back(native);
            }
        }
        int count=fonts->GetFamilyCount(),found=0;
        if(count>0) {
            auto families=std::make_unique<FontFamily[]>(count); fonts->GetFamilies(count,families.get(),&found);
            for(int i=0;i<found;++i) {
                wchar_t name[LF_FACESIZE]{}; families[i].GetFamilyName(name);
                std::wstring n=name;
                if(n.find(L"Light")!=std::wstring::npos || n.find(L"L") == n.size()-1) fontLight.reset(families[i].Clone());
                else fontMedium.reset(families[i].Clone());
            }
            if(!fontLight) fontLight.reset(families[0].Clone());
            if(!fontMedium) fontMedium.reset(families[found-1].Clone());
        }
        if(!fontLight) fontLight=std::make_unique<FontFamily>(L"맑은 고딕");
        if(!fontMedium) fontMedium=std::make_unique<FontFamily>(L"맑은 고딕");
        fontLoaded=true;
    }
    void text(Graphics& g,const std::wstring& value,float size,Color color,RectF box,bool centered=false,bool medium=false) {
        ensureFonts(); Font font(medium?fontMedium.get():fontLight.get(),size,FontStyleRegular,UnitPixel);
        SolidBrush brush(color); StringFormat format;
        format.SetAlignment(centered?StringAlignmentCenter:StringAlignmentNear);
        format.SetLineAlignment(StringAlignmentCenter); format.SetTrimming(StringTrimmingEllipsisCharacter);
        format.SetFormatFlags(StringFormatFlagsNoClip);
        g.DrawString(value.c_str(),static_cast<INT>(value.size()),&font,box,&format,&brush);
    }
    static void rounded(Graphics& g,RectF r,float radius,Color color) {
        GraphicsPath path; float d=radius*2;
        path.AddArc(r.X,r.Y,d,d,180,90); path.AddArc(r.GetRight()-d,r.Y,d,d,270,90);
        path.AddArc(r.GetRight()-d,r.GetBottom()-d,d,d,0,90); path.AddArc(r.X,r.GetBottom()-d,d,d,90,90); path.CloseFigure();
        SolidBrush brush(color); g.FillPath(&brush,&path);
    }
    void logo(Graphics& g,float x,float y,float scale=1) {
        auto p=palette(); rounded(g,RectF(x,y,32*scale,32*scale),10*scale,p.accent);
        SolidBrush ink(p.accentText); g.FillRectangle(&ink,x+10*scale,y+8*scale,4*scale,16*scale); g.FillRectangle(&ink,x+18*scale,y+8*scale,4*scale,16*scale);
        text(g,L"interval",21*scale,p.text,RectF(x+43*scale,y,145*scale,34*scale),false,true);
    }
    HWND button(HWND parent,int id,const wchar_t* title) {
        return CreateWindowExW(0,L"BUTTON",title,WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,0,0,0,0,parent,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);
    }
    void place(HWND control,float x,float y,float w,float h,float scale) { MoveWindow(control,static_cast<int>(x*scale),static_cast<int>(y*scale),static_cast<int>(w*scale),static_cast<int>(h*scale),TRUE); }
    float screenScale(HWND hwnd,float& width,float& height) {
        RECT r{}; GetClientRect(hwnd,&r); float scale=GetDpiForWindow(hwnd)/96.f;
        width=r.right/scale; height=r.bottom/scale;
        // Small screens retain the whole layout; large screens keep a comfortable reading width.
        const float fit=std::min({1.f,width/900.f,height/850.f});
        scale*=fit; width=r.right/scale; height=r.bottom/scale;
        return scale;
    }
    float settingsScale(float& width,float& height) { RECT r{}; GetClientRect(settings,&r); float scale=std::min({GetDpiForWindow(settings)/96.f,r.right/562.f,r.bottom/750.f}); width=r.right/scale; height=r.bottom/scale; return scale; }
    void layoutScreen() {
        if(!screen) return;
        float w,h; float s=screenScale(screen,w,h); float x=(w-780)/2, y=(h-770)/2;
        place(mainButtons[0],x+168,y+681,272,54,s); place(mainButtons[1],x+456,y+681,156,54,s);
        place(mainButtons[2],w-201,28,88,34,s); place(mainButtons[3],w-101,28,68,34,s);
        place(mainButtons[4],x+656,y+498,108,30,s);
        place(mainButtons[5],x+607,y+389,70,30,s); place(mainButtons[6],x+689,y+389,70,30,s);
        place(mainButtons[7],x+662,y+237,100,30,s);
    }
    void layoutSettings() {
        if(!settings) return;
        float w,h; float s=settingsScale(w,h);
        place(intervalEdit,42,286,148,46,s);
        ensureFonts();
        wchar_t family[LF_FACESIZE]{}; fontLight->GetFamilyName(family);
        HFONT newFont=CreateFontW(-static_cast<int>(26*s),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,family);
        SendMessageW(intervalEdit,WM_SETFONT,reinterpret_cast<WPARAM>(newFont),TRUE);
        if(editFont) DeleteObject(editFont); editFont=newFont;
        RECT editRect{static_cast<LONG>(8*s),static_cast<LONG>(7*s),static_cast<LONG>(140*s),static_cast<LONG>(43*s)};
        SendMessageW(intervalEdit,EM_SETRECT,0,reinterpret_cast<LPARAM>(&editRect));
        place(settingButtons[0],204,286,114,46,s);
        place(settingButtons[1],42,385,154,42,s); place(settingButtons[2],204,385,154,42,s); place(settingButtons[3],366,385,154,42,s);
        place(settingButtons[4],42,467,478,42,s); place(settingButtons[5],42,517,478,42,s);
        place(settingButtons[8],42,567,478,42,s);
        place(settingButtons[9],366,95,154,42,s);
        place(settingButtons[6],42,657,234,48,s); place(settingButtons[7],288,657,232,48,s);
    }
    void updateSettingsLabels() {
        if(!settings) return;
        SetWindowTextW(settingButtons[4],startupEnabled()?L"Windows 시작 시 실행                          켜짐":L"Windows 시작 시 실행                          꺼짐");
        SetWindowTextW(settingButtons[5],allMonitors?L"모든 모니터에 휴식 알림                          켜짐":L"모든 모니터에 휴식 알림                          꺼짐");
        SetWindowTextW(settingButtons[8],taskbarMode==TaskbarMode::Always?L"작업표시줄 타이머                          항상 표시":taskbarMode==TaskbarMode::LastFiveMinutes?L"작업표시줄 타이머                     5분 전부터 표시":L"작업표시줄 타이머                          표시 안 함");
    }
    void repaint() {
        if(screen) { InvalidateRect(screen,nullptr,FALSE); for(auto b:mainButtons) InvalidateRect(b,nullptr,FALSE); }
        if(settings && IsWindowVisible(settings)) { InvalidateRect(settings,nullptr,FALSE); for(auto b:settingButtons) InvalidateRect(b,nullptr,FALSE); InvalidateRect(intervalEdit,nullptr,TRUE); }
        for(auto shade:shades) InvalidateRect(shade,nullptr,FALSE);
        if(dashboard&&IsWindowVisible(dashboard)) InvalidateRect(dashboard,nullptr,FALSE);
    }
    void cycleTheme() { mode=mode==ThemeMode::Dark?ThemeMode::Light:ThemeMode::Dark; savePreferences(); updateTaskbar(true); repaint(); diagnostics(); }
    void pickRest() { size_t next=random()%rests.size(); if(next==restIndex) next=(next+1)%rests.size(); restIndex=next; }
    void pickThought() {
        const Series& series=wonSelected?market.wonChart:market.dollarChart;
        marketThought=random()%5==0 && series.points.size()>1 && series.fresh;
        if(marketThought) {
            marketTitle=L"최근 7일, "+percentage(series.change())+L"의 움직임.";
            marketBody=series.source+L" · "+(wonSelected?L"BTC/KRW":L"BTC/USDT")+L"의 시간별 종가를 기준으로 한 변화예요. 가격의 짧은 호흡과 저축의 긴 호흡을 구분해보세요.";
        } else { size_t next=random()%thoughts.size(); if(next==thoughtIndex) next=(next+1)%thoughts.size(); thoughtIndex=next; }
        repaint();
    }
    void requestMarket(bool forceCharts=false) {
        if(options.offline || !screen) return;
        if(worker.joinable()) { if(!network->done) return; worker.join(); }
        network=std::make_shared<NetworkState>(); network->result=market;
        const bool charts=forceCharts || market.dollarChart.points.empty() || market.wonChart.points.empty()
            || unixNow()-std::min(market.dollarChart.fetchedAt,market.wonChart.fetchedAt)>=600000;
        auto state=network; HWND receiver=owner;
        worker=std::thread([state,receiver,charts]() {
            Market result=state->result;
            auto publish=[&]() { { std::lock_guard<std::mutex> guard(state->mutex); state->result=result; } PostMessageW(receiver,MarketMessage,0,0); };
            Fetch fetch=[state](const auto& hostName,const auto& path) { if(state->cancelled) throw std::runtime_error("Cancelled"); return httpGet(hostName,path); };
            try {
                refreshQuote(result.dollar,Provider::Binance,Provider::Okx,fetch,unixNow()); publish();
                if(!state->cancelled) { refreshQuote(result.won,Provider::Bithumb,Provider::Upbit,fetch,unixNow()); publish(); }
                if(charts && !state->cancelled) { refreshSeries(result.dollarChart,Provider::Binance,Provider::Okx,fetch,unixNow()); publish(); }
                if(charts && !state->cancelled) { refreshSeries(result.wonChart,Provider::Bithumb,Provider::Upbit,fetch,unixNow()); publish(); }
            } catch(const std::exception&) {} // Keep the break UI available even if a feed is malformed.
            state->done=true; publish();
        });
        repaint();
    }
    static BOOL CALLBACK enumerateShade(HMONITOR monitor,HDC,LPRECT,LPARAM param) {
        auto* self=reinterpret_cast<App*>(param);
        if(monitor==MonitorFromWindow(self->screen,MONITOR_DEFAULTTONEAREST)) return TRUE;
        MONITORINFO info{sizeof(info)}; if(!GetMonitorInfoW(monitor,&info)) return TRUE;
        const RECT r=info.rcMonitor;
        HWND shade=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW,L"Interval.Shade",L"인터벌 · 휴식",WS_POPUP,r.left,r.top,r.right-r.left,r.bottom-r.top,self->screen,nullptr,GetModuleHandleW(nullptr),nullptr);
        self->shades.push_back(shade); ShowWindow(shade,SW_SHOWNOACTIVATE); return TRUE;
    }
    void beginBreak(bool automatic=false) {
        if(locked||sleeping) return;
        if(screen) { SetForegroundWindow(screen); return; }
        if(settings) ShowWindow(settings,SW_HIDE);
        breaking=true; breakStarted=clockNow(); holdState(); pickRest(); pickThought();
        POINT cursor{}; GetCursorPos(&cursor);
        HMONITOR monitor=MonitorFromWindow(GetForegroundWindow(),MONITOR_DEFAULTTONEAREST);
        if(!monitor) monitor=MonitorFromPoint(cursor,MONITOR_DEFAULTTONEAREST);
        MONITORINFO info{sizeof(info)}; GetMonitorInfoW(monitor,&info); RECT r=info.rcMonitor;
        screen=CreateWindowExW(WS_EX_TOPMOST|WS_EX_APPWINDOW|WS_EX_CONTROLPARENT,L"Interval.Screen",L"인터벌 · 잠시 쉬어가세요",WS_POPUP|WS_CLIPCHILDREN,r.left,r.top,r.right-r.left,r.bottom-r.top,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        if(!screen) { breaking=false; schedule.restart(clockNow(),intervalLength()); holdState(); showSettings(); return; }
        mainButtons={button(screen,Restart,L"다시 시작  →"),button(screen,Snooze,L"5분 뒤 알림"),button(screen,Theme,L"테마 전환"),button(screen,Settings,L"설정"),button(screen,NextThought,L"다른 이야기  ↗"),button(screen,DollarChart,L"USDT"),button(screen,WonChart,L"KRW"),button(screen,Refresh,L"새로고침")};
        layoutScreen(); if(allMonitors) EnumDisplayMonitors(nullptr,nullptr,enumerateShade,reinterpret_cast<LPARAM>(this));
        ShowWindow(screen,SW_SHOW); SetForegroundWindow(screen); SetFocus(mainButtons[0]);
        recordActivity(automatic?"break_auto":"break_manual");
        SetTimer(owner,ScreenTimer,1000,nullptr); SetTimer(owner,RefreshTimer,60000,nullptr);
        updateTaskbar(true); requestMarket(); diagnostics();
    }
    void closeBreak() {
        const bool wasBreaking=breaking;
        KillTimer(owner,ScreenTimer); KillTimer(owner,RefreshTimer);
        if(network && !network->done) network->cancelled=true;
        for(auto shade:shades) DestroyWindow(shade); shades.clear();
        if(screen) { HWND old=screen; screen=nullptr; DestroyWindow(old); } screenTaskbarReady=false; mainButtons.clear(); breaking=false;
        if(wasBreaking) recordActivity("break_end");
    }
    void createSettings() {
        if(!settings) {
            const UINT dpi=GetDpiForSystem();
            POINT cursor{}; GetCursorPos(&cursor); MONITORINFO info{sizeof(info)}; GetMonitorInfoW(MonitorFromPoint(cursor,MONITOR_DEFAULTTONEAREST),&info);
            const float s=std::min(dpi/96.f,std::max(0.5f,(info.rcWork.bottom-info.rcWork.top-60)/750.f));
            RECT r{0,0,static_cast<LONG>(562*s),static_cast<LONG>(750*s)}; AdjustWindowRectExForDpi(&r,WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,FALSE,WS_EX_APPWINDOW|WS_EX_CONTROLPARENT,dpi);
            settings=CreateWindowExW(WS_EX_APPWINDOW|WS_EX_CONTROLPARENT,L"Interval.Settings",L"인터벌 · 설정",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX|WS_CLIPCHILDREN,
                info.rcWork.left+(info.rcWork.right-info.rcWork.left-(r.right-r.left))/2, info.rcWork.top+std::max(0L,(info.rcWork.bottom-info.rcWork.top-(r.bottom-r.top))/2),r.right-r.left,r.bottom-r.top,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
            if(!settings) return;
            intervalEdit=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",std::to_wstring(minutes).c_str(),WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_NUMBER|ES_CENTER|ES_AUTOHSCROLL|ES_MULTILINE,0,0,0,0,settings,reinterpret_cast<HMENU>(IntervalEdit),GetModuleHandleW(nullptr),nullptr);
            SendMessageW(intervalEdit,EM_SETLIMITTEXT,3,0);
            settingButtons={button(settings,ApplyInterval,L"간격 적용"),button(settings,ThemeSystem,L"시스템 설정"),button(settings,ThemeLight,L"라이트"),button(settings,ThemeDark,L"다크"),button(settings,Startup,L"Windows 시작 시 실행"),button(settings,AllScreens,L"모든 모니터에 휴식 알림"),button(settings,RestNow,L"지금 휴식하기  →"),button(settings,HideSettings,L"접고 계속 실행"),button(settings,TaskbarDisplay,L"작업표시줄 타이머"),button(settings,Dashboard,L"사용 현황판  ↗")};
            layoutSettings(); updateSettingsLabels();
        }
    }
    void showSettings() {
        createSettings(); if(!settings) return;
        SetWindowPos(settings,screen?HWND_TOPMOST:HWND_NOTOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
        ShowWindow(settings,SW_RESTORE); SetForegroundWindow(settings); updateTaskbar(); repaint(); diagnostics();
    }
    static std::int64_t dayBoundary(int offset) {
        const auto now=std::time(nullptr); std::tm day{}; localtime_s(&day,&now);
        day.tm_hour=0; day.tm_min=0; day.tm_sec=0; day.tm_mday+=offset; day.tm_isdst=-1;
        return static_cast<std::int64_t>(std::mktime(&day))*1000;
    }
    static std::wstring activityDuration(std::int64_t ms) {
        if(ms<60000) return std::to_wstring(ms/1000)+L"초";
        const auto m=ms/60000;
        return m>=60?std::to_wstring(m/60)+L"시간 "+std::to_wstring(m%60)+L"분":std::to_wstring(m)+L"분";
    }
    static std::wstring activityName(ActivityState state) {
        static const wchar_t* names[]={L"타이머 가동",L"휴식",L"일시 정지",L"화면 잠금",L"절전·대기",L"앱 미실행",L"확인 불가"};
        return names[static_cast<size_t>(state)];
    }
    Color activityColor(ActivityState state) const {
        const bool dark=palette().background.GetR()<100;
        static const Color colors[]={Color(104,154,66),Color(68,158,181),Color(219,168,72),Color(150,121,187),Color(88,115,167),Color(138,146,141),Color(200,111,103)};
        auto c=colors[static_cast<size_t>(state)];
        return dark?Color(static_cast<BYTE>(std::min(255,static_cast<int>(c.GetR())+20)),static_cast<BYTE>(std::min(255,static_cast<int>(c.GetG())+20)),static_cast<BYTE>(std::min(255,static_cast<int>(c.GetB())+20))):c;
    }
    static std::wstring eventName(const std::string& kind) {
        if(kind=="app_start") return L"앱 시작";
        if(kind=="app_exit") return L"앱 종료";
        if(kind=="shutdown") return L"Windows 종료·재시작";
        if(kind=="logoff") return L"Windows 로그아웃";
        if(kind=="session_end") return L"Windows 요청으로 앱 종료";
        if(kind=="break_auto") return L"정해진 간격에 휴식 시작";
        if(kind=="break_manual") return L"지금 휴식 시작";
        if(kind=="break_end") return L"휴식 종료";
        if(kind=="restart") return L"타이머 다시 시작";
        if(kind=="snooze") return L"5분 뒤 알림";
        if(kind=="pause") return L"알림 일시 정지";
        if(kind=="resume") return L"알림 다시 켜기";
        if(kind=="lock") return L"화면 잠금";
        if(kind=="unlock") return L"잠금 해제";
        if(kind=="suspend") return L"절전·대기 시작";
        if(kind=="wake") return L"절전·대기에서 복귀";
        if(kind=="interrupted") return L"종료 기록 없는 중단 · 마지막 저장 시점";
        if(kind=="unobserved") return L"동작 확인이 끊긴 시점";
        if(kind=="recovered") return L"동작 확인 재개";
        if(kind=="clock_changed") return L"시스템 시각 변경";
        if(kind=="damaged_record") return L"일부 기록을 읽을 수 없음";
        if(kind=="interval_changed") return L"알림 간격 변경";
        return L"상태 변경";
    }
    float dashboardScale(float& w,float& h) const {
        RECT r{}; GetClientRect(dashboard,&r);
        const auto scale=std::max(.1f,std::min({GetDpiForWindow(dashboard)/96.f,r.right/1000.f,r.bottom/900.f}));
        w=r.right/scale; h=r.bottom/scale; return scale;
    }
    void layoutDashboard() {
        if(!dashboard||dashboardButtons.empty()) return;
        float w,h; const auto s=dashboardScale(w,h);
        place(dashboardButtons[0],700,80,80,36,s); place(dashboardButtons[1],790,80,80,36,s); place(dashboardButtons[2],880,80,80,36,s);
        place(dashboardButtons[3],780,670,85,30,s); place(dashboardButtons[4],875,670,85,30,s);
    }
    void showDashboard() {
        if(!dashboard) {
            const auto dpi=GetDpiForSystem(); MONITORINFO info{sizeof(info)};
            GetMonitorInfoW(MonitorFromWindow(settings,MONITOR_DEFAULTTONEAREST),&info);
            const auto s=std::min({dpi/96.f,(info.rcWork.right-info.rcWork.left-60)/1000.f,(info.rcWork.bottom-info.rcWork.top-60)/900.f});
            RECT r{0,0,static_cast<LONG>(1000*s),static_cast<LONG>(900*s)};
            AdjustWindowRectExForDpi(&r,WS_OVERLAPPEDWINDOW,FALSE,WS_EX_APPWINDOW,dpi);
            dashboard=CreateWindowExW(WS_EX_APPWINDOW|WS_EX_CONTROLPARENT,L"Interval.Dashboard",L"인터벌 · 사용 현황판",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,
                info.rcWork.left+(info.rcWork.right-info.rcWork.left-(r.right-r.left))/2,info.rcWork.top+(info.rcWork.bottom-info.rcWork.top-(r.bottom-r.top))/2,r.right-r.left,r.bottom-r.top,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
            if(!dashboard) return;
            dashboardButtons={button(dashboard,DayPrevious,L"이전 날"),button(dashboard,DayNext,L"다음 날"),button(dashboard,DayToday,L"오늘"),button(dashboard,EventsPrevious,L"최근 기록"),button(dashboard,EventsNext,L"이전 기록")};
            layoutDashboard();
        }
        ShowWindow(dashboard,SW_RESTORE); SetForegroundWindow(dashboard); repaint(); diagnostics();
    }
    std::vector<const ActivityEvent*> dayEvents() const {
        std::vector<const ActivityEvent*> result;
        const auto begin=dayBoundary(dayOffset), end=dayBoundary(dayOffset+1);
        const auto& events=activity.history.events();
        auto it=std::lower_bound(events.begin(),events.end(),begin,[](const ActivityEvent& e,auto t){return e.time<t;});
        for(;it!=events.end()&&it->time<end;++it) if(it->kind!="heartbeat") result.push_back(&*it);
        std::reverse(result.begin(),result.end()); return result;
    }
    void drawDashboard(Graphics& g,float w,float) {
        const auto p=palette(); const auto now=unixNow(), begin=dayBoundary(dayOffset), end=dayBoundary(dayOffset+1);
        logo(g,40,28); text(g,L"사용 현황판",28,p.text,RectF(40,78,400,40),false,true);
        auto time=static_cast<std::time_t>(begin/1000); std::tm day{}; localtime_s(&day,&time);
        wchar_t label[64]{}; swprintf_s(label,L"%04d년 %02d월 %02d일",day.tm_year+1900,day.tm_mon+1,day.tm_mday);
        text(g,std::wstring(label)+(dayOffset==0?L" · 오늘":L""),13,p.muted,RectF(40,122,600,27));
        EnableWindow(dashboardButtons[1],dayOffset<0);
        for(size_t i=0;i<ActivityStateCount;++i) {
            const float x=40+static_cast<float>(i)*132;
            SolidBrush dot(activityColor(static_cast<ActivityState>(i))); g.FillEllipse(&dot,x,169.f,9.f,9.f);
            text(g,activityName(static_cast<ActivityState>(i)),11,p.muted,RectF(x+15,158,116,30));
        }
        const auto summary=activity.history.summarize(begin,end,now); const auto& ms=summary.milliseconds;
        const std::array<std::int64_t,4> totals{ms[0],ms[1],ms[2]+ms[3]+ms[4],ms[5]+ms[6]};
        const wchar_t* titles[]={L"타이머 가동",L"휴식",L"정지·잠금·절전",L"미실행·확인 불가"};
        for(size_t i=0;i<4;++i) {
            const float x=40+static_cast<float>(i)*234;
            rounded(g,RectF(x,204,220,76),12,p.surface);
            text(g,titles[i],11,p.muted,RectF(x+14,213,194,22));
            text(g,activityDuration(totals[i]),22,p.text,RectF(x+14,238,194,34),false,true);
        }
        const auto active=ms[0]+ms[1];
        const auto ratio=active?std::to_wstring(ms[1]*100/active)+L"%":L"—";
        text(g,L"앱 시작 "+std::to_wstring(summary.starts)+L"회     휴식 "+std::to_wstring(summary.rests)+L"회     다시 시작 "+std::to_wstring(summary.restarts)+L"회     미루기 "+std::to_wstring(summary.snoozes)+L"회     휴식 비중 "+ratio,13,p.text,RectF(40,290,w-80,28));
        text(g,L"하루의 흐름",15,p.text,RectF(40,325,500,28),false,true);
        SolidBrush background(p.surface); g.FillRectangle(&background,40.f,361.f,920.f,27.f);
        for(const auto& span:activity.history.spans(begin,end,now)) {
            const float x=40+920*static_cast<float>(static_cast<double>(span.begin-begin)/(end-begin));
            const float width=920*static_cast<float>(static_cast<double>(span.end-span.begin)/(end-begin));
            SolidBrush ink(activityColor(span.state)); g.FillRectangle(&ink,x,361.f,width,27.f);
        }
        for(int i=0;i<=4;++i) text(g,std::to_wstring(i*6)+L"시",10,p.muted,RectF(40+i*230.f-(i==4?30.f:0),390,60,20));
        text(g,L"최근 7일 · 상태별 누적 시간",15,p.text,RectF(40,425,600,28),false,true);
        text(g,L"0시간",10,p.muted,RectF(160,447,80,18));
        text(g,L"12시간",10,p.muted,RectF(455,447,80,18),true);
        text(g,L"24시간",10,p.muted,RectF(770,447,70,18),true);
        for(int i=0;i<7;++i) {
            const auto a=dayBoundary(dayOffset-6+i), b=dayBoundary(dayOffset-5+i);
            const auto sum=activity.history.summarize(a,b,now); const float y=466+i*26.f;
            text(g,dateLabel(a),11,p.muted,RectF(40,y,100,22));
            g.FillRectangle(&background,160.f,y+3,650.f,16.f); float x=160;
            std::int64_t total=0;
            for(size_t state=0;state<ActivityStateCount;++state) {
                const auto amount=sum.milliseconds[state]; total+=amount;
                const float width=650*static_cast<float>(static_cast<double>(amount)/(b-a));
                SolidBrush ink(activityColor(static_cast<ActivityState>(state))); g.FillRectangle(&ink,x,y+3,width,16.f); x+=width;
            }
            text(g,activityDuration(total),10,p.muted,RectF(825,y,145,12));
            text(g,L"휴식 "+std::to_wstring(sum.rests)+L" · 재시작 "+std::to_wstring(sum.restarts),9,p.muted,RectF(825,y+12,145,12));
        }
        auto events=dayEvents(); if(eventOffset>=events.size()) eventOffset=events.empty()?0:((events.size()-1)/5)*5;
        text(g,L"이벤트 기록 · "+std::to_wstring(events.size())+L"건",15,p.text,RectF(40,670,550,28),false,true);
        EnableWindow(dashboardButtons[3],eventOffset>0); EnableWindow(dashboardButtons[4],eventOffset+5<events.size());
        for(size_t i=0;i<5&&eventOffset+i<events.size();++i) {
            const auto& e=*events[eventOffset+i]; const float y=715+static_cast<float>(i)*24;
            text(g,localTime(e.time),11,p.muted,RectF(40,y,80,22));
            text(g,eventName(e.kind),12,p.text,RectF(125,y,610,22));
            text(g,activityName(e.state)+L" · "+std::to_wstring(e.minutes)+L"분 간격",10,p.muted,RectF(745,y,220,22));
        }
        if(events.empty()) text(g,L"이 날에 기록된 이벤트가 없어요.",12,p.muted,RectF(40,717,900,30));
        const auto note=activity.failed()?L"기록 저장 실패 · 폴더 접근 권한과 디스크 공간을 확인해주세요.":
            activity.damaged()?L"일부 기록 손상 · 읽을 수 없는 구간은 확인 불가로 표시합니다.":
            L"가동은 타이머 실행 시간입니다. 휴식 비중 = 휴식 / (가동 + 휴식). 빈 영역은 기록 전·미래입니다.";
        text(g,note,10,activity.failed()?activityColor(ActivityState::Unknown):p.muted,RectF(40,850,w-80,24));
        text(g,L"절전·잠금·미실행·확인 불가 시간은 가동과 휴식에서 제외합니다. 종료 기록 없는 중단은 마지막 저장부터 표시합니다.",10,p.muted,RectF(40,875,w-80,22));
    }
    std::wstring quoteLabel(const Quote& quote,bool won) {
        if(!quote.price) return options.offline?L"오프라인 · 가격을 가져오지 않았어요":network&&!network->done?L"거래소에 연결하고 있어요…":L"연결 실패 · 다음 갱신 때 다시 시도해요";
        return quote.source+L" · "+(won?L"BTC/KRW":L"BTC/USDT")+L" · "+localTime(quote.fetchedAt)+(quote.fresh?L" 갱신":L" 마지막 수신 · 갱신 실패");
    }
    void paintChart(Graphics& g,float x,float y,float width,float height) {
        const auto p=palette(); const auto& series=wonSelected?market.wonChart:market.dollarChart;
        Pen grid(p.line,1.f);
        for(int i=0;i<3;++i) g.DrawLine(&grid,x,y+i*height/2,x+width,y+i*height/2);
        if(series.points.size()<2) {
            text(g,options.offline?L"인터넷 연결 시 최근 7일 차트가 표시돼요.":network&&!network->done?L"7일의 흐름을 가져오는 중…":L"차트를 불러오지 못했어요. 새로고침으로 다시 시도하세요.",13,p.muted,RectF(x,y,width,height),true);
            return;
        }
        const auto range=std::minmax_element(series.points.begin(),series.points.end(),[](const auto&a,const auto&b){return a.price<b.price;});
        double lo=range.first->price,hi=range.second->price,delta=hi-lo;
        if(delta<hi*.00001) delta=std::max(1.0,hi*.001);
        lo-=delta*.13; hi+=delta*.13;
        const auto first=series.points.front().time, last=series.points.back().time;
        std::vector<PointF> points; points.reserve(series.points.size());
        for(const auto& point:series.points) points.emplace_back(x+static_cast<float>(static_cast<double>(point.time-first)/(last-first))*width,y+height-static_cast<float>((point.price-lo)/(hi-lo))*height);
        GraphicsPath area; area.AddLines(points.data(),static_cast<INT>(points.size())); area.AddLine(points.back(),PointF(x+width,y+height)); area.AddLine(PointF(x+width,y+height),PointF(x,y+height)); area.CloseFigure();
        Color color=p.accent; Color start(55,color.GetR(),color.GetG(),color.GetB()),end(0,color.GetR(),color.GetG(),color.GetB());
        LinearGradientBrush fill(PointF(x,y),PointF(x,y+height),start,end); g.FillPath(&fill,&area);
        Color line=p.background.GetR()>100?Color(103,145,57):Color(192,225,144); Pen pen(line,2.2f); pen.SetLineJoin(LineJoinRound);
        g.DrawLines(&pen,points.data(),static_cast<INT>(points.size())); SolidBrush dot(line); auto lastPoint=points.back(); g.FillEllipse(&dot,lastPoint.X-3.5f,lastPoint.Y-3.5f,7.f,7.f);
        text(g,dateLabel(first),11,p.muted,RectF(x,y+height+5,100,20));
        text(g,L"현재",11,p.muted,RectF(x+width-40,y+height+5,40,20));
    }
    void drawScreen(Graphics& g,float w,float h) {
        const auto p=palette(); logo(g,34,29);
        text(g,L"A PAUSE FOR YOUR DAY",10,p.muted,RectF(34,h-53,250,24));
        text(g,L"ESC · 5분 뒤 알림",11,p.muted,RectF(w-173,h-53,145,24));
        float x=(w-780)/2, y=(h-770)/2;
        rounded(g,RectF(x+310,y,160,29),14,p.surface);
        text(g,L"지금은, 쉬어갈 시간",11,p.muted,RectF(x+310,y,160,29),true);
        text(g,rests[restIndex].title,42,p.text,RectF(x-60,y+49,900,117),true,true);
        text(g,rests[restIndex].detail,14,p.muted,RectF(x-20,y+178,820,26),true);
        Pen divider(p.line,1.f); g.DrawLine(&divider,x+17,y+225,x+763,y+225);
        text(g,L"BITCOIN",11,p.muted,RectF(x+18,y+238,160,22),false,true);
        text(g,L"달러 기준 · USDT",12,p.muted,RectF(x+18,y+275,300,24));
        text(g,L"원화",12,p.muted,RectF(x+410,y+275,300,24));
        text(g,money(market.dollar.price,false),35,p.text,RectF(x+18,y+304,365,48),false,true);
        text(g,money(market.won.price,true),35,p.text,RectF(x+410,y+304,355,48),false,true);
        text(g,quoteLabel(market.dollar,false),10,p.muted,RectF(x+18,y+356,365,22));
        text(g,quoteLabel(market.won,true),10,p.muted,RectF(x+410,y+356,355,22));
        g.DrawLine(&divider,x+391,y+280,x+391,y+373);
        const auto& series=wonSelected?market.wonChart:market.dollarChart;
        auto chartLabel=L"최근 7일 · "+(wonSelected?std::wstring(L"KRW"):std::wstring(L"USDT"));
        if(series.points.size()>1) chartLabel+=L"   "+percentage(series.change())+L"  ·  "+series.source+(series.fresh?L"":L" · 마지막 수신 "+localTime(series.fetchedAt));
        text(g,chartLabel,11,p.muted,RectF(x+18,y+391,740,24));
        paintChart(g,x+18,y+424,744,58);
        rounded(g,RectF(x,y+526,780,130),16,p.surface);
        const auto& thought=thoughts[thoughtIndex];
        text(g,marketThought?L"MARKET · 관측 시점의 데이터":thought.category,10,p.muted,RectF(x+25,y+541,730,20),false,true);
        text(g,marketThought?marketTitle:thought.title,18,p.text,RectF(x+25,y+566,730,29),false,true);
        text(g,marketThought?marketBody:thought.body,12,p.muted,RectF(x+25,y+602,730,39));
        text(g,L"휴식한 만큼, 다시 가볍게.  다음 알림은 다시 시작한 뒤 "+std::to_wstring(minutes)+L"분 후예요.",11,p.muted,RectF(x,y+742,780,24),true);
        text(g,L"휴식 중  "+duration(clockNow()-breakStarted),11,p.muted,RectF(x+280,y+651,220,24),true);
    }
    void drawSettings(Graphics& g,float w,float) {
        auto p=palette(); logo(g,42,33);
        text(g,L"집중에 쉼표를.",30,p.text,RectF(42,94,315,49),false,true);
        text(g,L"정해진 시간이 되면 화면 전체로 쉬어갈 때를 알려드려요.",12,p.muted,RectF(42,152,w-84,28));
        rounded(g,RectF(42,195,w-84,53),12,p.surface);
        auto state=breaking?L"지금은 휴식 중이에요.":paused?L"알림이 일시 정지되어 있어요.":L"다음 휴식까지   "+duration(schedule.remaining(clockNow()));
        text(g,state,16,p.text,RectF(59,195,w-118,53),false,true);
        text(g,L"휴식 알림 간격",12,p.text,RectF(42,254,220,25),false,true);
        text(g,L"분  ·  1–240분",11,p.muted,RectF(334,286,186,46));
        text(g,L"화면 모드",12,p.text,RectF(42,348,478,26),false,true);
        text(g,L"상주 설정",12,p.text,RectF(42,434,478,26),false,true);
        text(g,statusMessage.empty()?L"창을 닫아도 타이머와 휴식 알림은 계속 실행됩니다.":statusMessage,11,p.muted,RectF(42,617,w-84,29));
        text(g,L"INTERVAL 1.0   ·   가벼운 쉼, 오래가는 집중",10,p.muted,RectF(42,717,w-84,21),true);
    }
    void renderClient(HWND hwnd,HDC dc,RECT dirty) {
        RECT r{}; GetClientRect(hwnd,&r);
        if(r.right<=0||r.bottom<=0) return;
        // Buffers live for one paint only, so no full-screen pixel allocation remains in the tray.
        if(dirty.right<=dirty.left||dirty.bottom<=dirty.top) return;
        Bitmap buffer(dirty.right-dirty.left,dirty.bottom-dirty.top,PixelFormat32bppPARGB); Graphics g(&buffer);
        g.Clear(palette().background); g.SetSmoothingMode(SmoothingModeAntiAlias); g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
        float w,h,s;
        auto transform=[&](float scale) { Matrix matrix(scale,0,0,scale,-static_cast<float>(dirty.left),-static_cast<float>(dirty.top)); g.SetTransform(&matrix); };
        if(hwnd==screen) { s=screenScale(hwnd,w,h); transform(s); drawScreen(g,w,h); }
        else if(hwnd==settings) { s=settingsScale(w,h); transform(s); drawSettings(g,w,h); }
        else if(hwnd==dashboard) { s=dashboardScale(w,h); transform(s); drawDashboard(g,w,h); }
        else { s=GetDpiForWindow(hwnd)/96.f; w=r.right/s; h=r.bottom/s; transform(s); text(g,L"잠깐 쉬어가세요.",32,palette().text,RectF(0,h/2-40,w,80),true,true); text(g,L"화면에서 눈을 떼고, 가볍게 몸을 움직여보세요.",14,palette().muted,RectF(0,h/2+47,w,35),true); text(g,L"ESC · 5분 뒤 알림",11,palette().muted,RectF(0,h-65,w,28),true); }
        Graphics target(dc); target.DrawImage(&buffer,dirty.left,dirty.top);
    }
    void paint(HWND hwnd) {
        PAINTSTRUCT ps{}; HDC dc=BeginPaint(hwnd,&ps); renderClient(hwnd,dc,ps.rcPaint); EndPaint(hwnd,&ps);
    }
    void drawButton(const DRAWITEMSTRUCT& item) {
        auto p=palette(); Graphics g(item.hDC); g.SetSmoothingMode(SmoothingModeAntiAlias); g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
        float s=GetDpiForWindow(item.hwndItem)/96.f,w=static_cast<float>(item.rcItem.right),h=static_cast<float>(item.rcItem.bottom);
        if(screen && GetParent(item.hwndItem)==screen) { float sw,sh; s=screenScale(screen,sw,sh); }
        if(dashboard&&GetParent(item.hwndItem)==dashboard) { float dw,dh; s=dashboardScale(dw,dh); }
        g.ScaleTransform(s,s); w/=s; h/=s; g.Clear(p.background);
        int id=static_cast<int>(item.CtlID); bool primary=id==Restart||id==RestNow||id==ApplyInterval;
        bool selected=(id==ThemeSystem&&mode==ThemeMode::System)||(id==ThemeLight&&mode==ThemeMode::Light)||(id==ThemeDark&&mode==ThemeMode::Dark)||(id==DollarChart&&!wonSelected)||(id==WonChart&&wonSelected);
        bool flat=id==NextThought||id==Refresh;
        Color background=primary||selected?p.accent:p.surface;
        if(item.itemState&ODS_SELECTED) background=p.line;
        if(!flat) rounded(g,RectF(0,0,w,h),std::min(10.f,h/2),background);
        wchar_t label[256]{}; GetWindowTextW(item.hwndItem,label,256);
        text(g,label,id==Restart?15.f:id==Startup||id==AllScreens||id==TaskbarDisplay?12.f:11.f,(item.itemState&ODS_DISABLED)?p.muted:primary||selected?p.accentText:p.text,RectF(8,0,w-16,h),true,primary||selected);
        if((item.itemState&ODS_FOCUS) && !(item.itemState&ODS_NOFOCUSRECT)) { Pen focus(p.muted,1.f); focus.SetDashStyle(DashStyleDot); g.DrawRectangle(&focus,3.f,3.f,w-6,h-6); }
    }
    void applyInterval() {
        wchar_t input[16]{}; GetWindowTextW(intervalEdit,input,16); wchar_t* end=nullptr; long n=wcstol(input,&end,10);
        if(!*input||*end||n<1||n>240) { statusMessage=L"알림 간격을 1–240분 사이로 입력해주세요."; repaint(); SetFocus(intervalEdit); return; }
        minutes=static_cast<int>(n); statusMessage=std::to_wstring(minutes)+L"분 간격을 적용했어요.";
        recordActivity("interval_changed");
        savePreferences(); cycleLength=intervalLength(); schedule.restart(clockNow(),cycleLength); armTimer(); updateTray(); updateTaskbar(); repaint(); diagnostics();
    }
    void execute(UINT command) {
        observeActivity();
        switch(command) {
            case Restart: case TrayRestart: restart(); break;
            case Snooze: restart(5*60000,"snooze"); break;
            case Dashboard: showDashboard(); break;
            case DayPrevious: --dayOffset; eventOffset=0; repaint(); break;
            case DayNext: dayOffset=std::min(0,dayOffset+1); eventOffset=0; repaint(); break;
            case DayToday: dayOffset=0; eventOffset=0; repaint(); break;
            case EventsPrevious: eventOffset=eventOffset>5?eventOffset-5:0; repaint(); break;
            case EventsNext: if(eventOffset+5<dayEvents().size()) eventOffset+=5; repaint(); break;
            case Theme: cycleTheme(); break;
            case Settings: case TrayOpen: showSettings(); break;
            case TrayRest: case RestNow: beginBreak(); break;
            case NextThought: pickThought(); break;
            case DollarChart: wonSelected=false; repaint(); break;
            case WonChart: wonSelected=true; repaint(); break;
            case Refresh: requestMarket(true); break;
            case ApplyInterval: applyInterval(); break;
            case ThemeSystem: case ThemeLight: case ThemeDark: mode=static_cast<ThemeMode>(command-ThemeSystem); savePreferences(); updateTaskbar(true); repaint(); diagnostics(); break;
            case Startup: setStartup(); break;
            case AllScreens: allMonitors=!allMonitors; savePreferences(); updateSettingsLabels(); repaint(); diagnostics(); break;
            case HideSettings: dismissSettings(); break;
            case TaskbarDisplay: taskbarMode=static_cast<TaskbarMode>((static_cast<int>(taskbarMode)+1)%3); savePreferences(); updateSettingsLabels(); updateTaskbar(); repaint(); diagnostics(); break;
            case TrayPause: if(breaking) restart(); paused=!paused; recordActivity(paused?"pause":"resume"); holdState(); repaint(); break;
            case TrayQuit: DestroyWindow(owner); break;
        }
    }
    void diagnostics() {
        if(!options.test||options.diagnostic.empty()) return;
        const auto temporary=options.diagnostic+L".tmp";
        std::ofstream f(temporary,std::ios::trunc);
        if(!f) return;
        const auto state=indicator();
        const auto summary=activity.history.summarize(dayBoundary(0),dayBoundary(1),unixNow());
        f<<"{\"pid\":"<<GetCurrentProcessId()<<",\"breaking\":"<<(breaking?"true":"false")<<",\"paused\":"<<(paused?"true":"false")<<",\"holding\":"<<(schedule.holding()?"true":"false")<<",\"remainingMs\":"<<schedule.remaining(clockNow())<<",\"intervalMinutes\":"<<minutes<<",\"theme\":"<<static_cast<int>(mode)<<",\"shades\":"<<shades.size()<<",\"dollar\":"<<market.dollar.price<<",\"won\":"<<market.won.price<<",\"dollarPoints\":"<<market.dollarChart.points.size()<<",\"wonPoints\":"<<market.wonChart.points.size()<<",\"fontFamilies\":"<<(fonts?fonts->GetFamilyCount():0)
            <<",\"taskbarMode\":"<<static_cast<int>(taskbarMode)<<",\"taskbarVisible\":"<<(settings&&IsWindowVisible(settings)?"true":"false")<<",\"taskbarWarning\":"<<(state.warning?"true":"false")<<",\"taskbarMinutes\":"<<state.minutes<<",\"taskbarProgressApplied\":"<<(taskbarProgressApplied?"true":"false")
            <<",\"activityEvents\":"<<activity.history.events().size()<<",\"activityState\":"<<static_cast<int>(activityState())<<",\"historyFailed\":"<<(activity.failed()?"true":"false")<<",\"runningMs\":"<<summary.milliseconds[0]<<",\"restingMs\":"<<summary.milliseconds[1]<<",\"sleepingMs\":"<<summary.milliseconds[4]<<",\"unknownMs\":"<<summary.milliseconds[6]<<",\"dashboardVisible\":"<<(dashboard&&IsWindowVisible(dashboard)?"true":"false")<<"}";
        f.close(); MoveFileExW(temporary.c_str(),options.diagnostic.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
    }
public:
    explicit App(Options opts):options(std::move(opts)) { instance=this; }
    ~App() {
        if(network) network->cancelled=true;
        // The worker owns only its shared state and OS handles; it never dereferences App.
        // Shutdown is immediate even if a public API is currently timing out.
        if(worker.joinable()) { if(network->done) worker.join(); else worker.detach(); }
        if(editFont) DeleteObject(editFont);
        fontLight.reset(); fontMedium.reset(); fonts.reset(); for(auto resource:nativeFontResources) RemoveFontMemResourceEx(resource);
        if(countdownIcon) DestroyIcon(countdownIcon);
        if(taskbar) taskbar->Release(); if(comInitialized) CoUninitialize();
        if(icon) DestroyIcon(icon); GdiplusShutdown(gdiplusToken); instance=nullptr;
    }
    bool start() {
        GdiplusStartupInput input; if(GdiplusStartup(&gdiplusToken,&input,nullptr)!=Ok) return false;
        loadPreferences(); icon=makeIcon(); taskbarCreated=RegisterWindowMessageW(L"TaskbarCreated"); taskbarButtonCreated=RegisterWindowMessageW(L"TaskbarButtonCreated");
        comInitialized=SUCCEEDED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED));
        if(comInitialized&&SUCCEEDED(CoCreateInstance(CLSID_TaskbarList,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&taskbar)))) {
            if(FAILED(taskbar->HrInit())) { taskbar->Release(); taskbar=nullptr; }
        }
        for(auto name:{L"Interval.Owner",L"Interval.Screen",L"Interval.Settings",L"Interval.Shade",L"Interval.Dashboard"}) {
            WNDCLASSEXW cls{sizeof(cls)}; cls.lpfnWndProc=windowProc; cls.hInstance=GetModuleHandleW(nullptr); cls.hCursor=LoadCursorW(nullptr,IDC_ARROW); cls.hIcon=icon; cls.hIconSm=icon; cls.lpszClassName=name;
            if(!RegisterClassExW(&cls)) return false;
        }
        owner=CreateWindowExW(WS_EX_TOOLWINDOW,L"Interval.Owner",options.test?L"Interval.Background.Test":L"Interval.Background",WS_POPUP,0,0,0,0,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        if(!owner) return false;
        std::wstring historyPath=options.test?options.history:L"";
        if(!options.test) {
            const auto path=executable();
            historyPath=path.substr(0,path.find_last_of(L"\\/"))+L"\\activity-v1.tsv";
        }
        activity.open(historyPath); activityStarted=true;
        lastObserved=unixNow(); lastSaved=lastObserved; lastObservedTick=clockNow();
        activity.start(lastObserved,activityState(),minutes);
        WTSRegisterSessionNotification(owner,NOTIFY_FOR_THIS_SESSION);
        cycleLength=intervalLength(); schedule.restart(clockNow(),cycleLength); armTimer(); SetTimer(owner,MinuteTimer,60000,nullptr); SetTimer(owner,CountdownTimer,1000,nullptr); updateTray(true);
        createSettings(); updateTaskbar();
        if(options.preview) beginBreak(); else if(!options.background) showSettings();
        diagnostics(); return true;
    }
    int run() {
        MSG msg{};
        while(GetMessageW(&msg,nullptr,0,0)>0) {
            const HWND active=GetAncestor(msg.hwnd,GA_ROOT);
            if(msg.message==WM_KEYDOWN && msg.wParam==VK_ESCAPE) { if(screen && (active==screen || std::find(shades.begin(),shades.end(),active)!=shades.end())) { observeActivity(); restart(5*60000,"snooze"); continue; } if(active==settings) { dismissSettings(); continue; } if(active==dashboard) { ShowWindow(dashboard,SW_HIDE); continue; } }
            if(msg.message==WM_KEYDOWN && msg.wParam==VK_RETURN && msg.hwnd==intervalEdit) { applyInterval(); continue; }
            if((active==settings||active==screen||active==dashboard) && IsDialogMessageW(active,&msg)) continue;
            TranslateMessage(&msg); DispatchMessageW(&msg);
        }
        return static_cast<int>(msg.wParam);
    }
    LRESULT procedure(HWND hwnd,UINT message,WPARAM wParam,LPARAM lParam) {
        if(message==taskbarCreated && hwnd==owner) { trayAdded=false; settingsTaskbarReady=false; screenTaskbarReady=false; updateTray(true); updateTaskbar(true); return 0; }
        if(message==taskbarButtonCreated) { if(hwnd==settings) settingsTaskbarReady=true; if(hwnd==screen) screenTaskbarReady=true; updateTaskbar(true); diagnostics(); return 0; }
        switch(message) {
            case WM_COMMAND: if(HIWORD(wParam)==BN_CLICKED) execute(LOWORD(wParam)); return 0;
            case WM_DRAWITEM: drawButton(*reinterpret_cast<DRAWITEMSTRUCT*>(lParam)); return TRUE;
            case WM_ERASEBKGND: return 1;
            case WM_PAINT: if(hwnd!=owner) { paint(hwnd); return 0; } break;
            case WM_PRINTCLIENT: if(hwnd!=owner) { RECT r{}; GetClientRect(hwnd,&r); renderClient(hwnd,reinterpret_cast<HDC>(wParam),r); return 0; } break;
            case WM_SIZE: if(hwnd==settings&&!settingButtons.empty()) layoutSettings(); if(hwnd==screen&&!mainButtons.empty()) layoutScreen(); if(hwnd==dashboard) layoutDashboard(); return 0;
            case WM_MOUSEWHEEL: if(hwnd==dashboard) { execute(GET_WHEEL_DELTA_WPARAM(wParam)>0?EventsPrevious:EventsNext); return 0; } break;
            case WM_DPICHANGED: { auto r=reinterpret_cast<RECT*>(lParam); SetWindowPos(hwnd,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE); repaint(); return 0; }
            case WM_SETTINGCHANGE: updateTaskbar(true); repaint(); return 0;
            case WM_DISPLAYCHANGE: if(breaking) { closeBreak(); breaking=false; beginBreak(); } return 0;
            case WM_CTLCOLORSTATIC: case WM_CTLCOLOREDIT: {
                static HBRUSH brush=nullptr; if(brush) DeleteObject(brush); auto p=palette(); brush=CreateSolidBrush(RGB(p.surface.GetR(),p.surface.GetG(),p.surface.GetB()));
                SetTextColor(reinterpret_cast<HDC>(wParam),RGB(p.text.GetR(),p.text.GetG(),p.text.GetB())); SetBkColor(reinterpret_cast<HDC>(wParam),RGB(p.surface.GetR(),p.surface.GetG(),p.surface.GetB())); return reinterpret_cast<LRESULT>(brush);
            }
            case WM_CLOSE: observeActivity(); if(hwnd==settings) dismissSettings(); else if(hwnd==dashboard) ShowWindow(dashboard,SW_HIDE); else if(hwnd==screen) restart(); else if(hwnd!=owner) restart(5*60000,"snooze"); return 0;
            case WM_TIMER:
                observeActivity();
                if(wParam==DueTimer) { KillTimer(owner,DueTimer); if(schedule.due(clockNow())) beginBreak(true); else armTimer(); }
                else if(wParam==MinuteTimer) { updateTray(); if(settings&&IsWindowVisible(settings)) InvalidateRect(settings,nullptr,FALSE); }
                else if(wParam==CountdownTimer) { if(unixNow()-lastSaved>=15000) recordActivity("heartbeat"); updateTaskbar(); if(settings&&IsWindowVisible(settings)&&!IsIconic(settings)) InvalidateRect(settings,nullptr,FALSE); if(dashboard&&IsWindowVisible(dashboard)&&!IsIconic(dashboard)) InvalidateRect(dashboard,nullptr,FALSE); diagnostics(); }
                else if(wParam==ScreenTimer && screen) { float w,h; auto s=screenScale(screen,w,h); auto x=(w-780)/2,y=(h-770)/2; RECT r{static_cast<LONG>((x+280)*s),static_cast<LONG>((y+650)*s),static_cast<LONG>((x+500)*s),static_cast<LONG>((y+675)*s)}; InvalidateRect(screen,&r,FALSE); }
                else if(wParam==RefreshTimer) requestMarket();
                return 0;
            case TrayMessage:
                if(LOWORD(lParam)==WM_CONTEXTMENU) trayMenu();
                else if(LOWORD(lParam)==NIN_SELECT||LOWORD(lParam)==NIN_KEYSELECT) showSettings(); return 0;
            case MarketMessage:
                if(network) { { std::lock_guard<std::mutex> guard(network->mutex); market=network->result; } if(network->done&&worker.joinable()) worker.join(); repaint(); diagnostics(); }
                return 0;
            case OpenMessage: if(wParam==1) beginBreak(); else showSettings(); return 0;
            case WM_WTSSESSION_CHANGE:
                if(hwnd!=owner) break;
                observeActivity();
                if(wParam==WTS_SESSION_LOCK&&!locked) { locked=true; if(breaking) { closeBreak(); cycleLength=intervalLength(); schedule.restart(clockNow(),cycleLength); } recordActivity("lock"); holdState(); }
                if(wParam==WTS_SESSION_UNLOCK&&locked) { locked=false; recordActivity("unlock"); holdState(); } return 0;
            case WM_POWERBROADCAST:
                if(hwnd!=owner) return TRUE;
                observeActivity();
                if(wParam==PBT_APMSUSPEND&&!sleeping) { sleeping=true; if(breaking) { closeBreak(); cycleLength=intervalLength(); schedule.restart(clockNow(),cycleLength); } recordActivity("suspend"); holdState(); }
                if((wParam==PBT_APMRESUMEAUTOMATIC||wParam==PBT_APMRESUMESUSPEND||wParam==PBT_APMRESUMECRITICAL)&&sleeping) { sleeping=false; recordActivity("wake"); holdState(); } return TRUE;
            case WM_QUERYENDSESSION: if(hwnd==owner) { observeActivity(); recordActivity("heartbeat"); } return TRUE;
            case WM_ENDSESSION:
                if(hwnd==owner&&wParam) { endActivity((lParam&ENDSESSION_LOGOFF)?"logoff":(lParam&ENDSESSION_CLOSEAPP)?"session_end":"shutdown"); DestroyWindow(owner); }
                return 0;
            case WM_DESTROY:
                if(hwnd==owner) { closeBreak(); endActivity("app_exit"); if(dashboard) { auto old=dashboard; dashboard=nullptr; DestroyWindow(old); } if(settings) { auto old=settings; settings=nullptr; DestroyWindow(old); } NOTIFYICONDATAW data{}; data.cbSize=sizeof(data); data.hWnd=owner; data.uID=1; Shell_NotifyIconW(NIM_DELETE,&data); WTSUnRegisterSessionNotification(owner); if(network) network->cancelled=true; PostQuitMessage(0); } return 0;
        }
        return DefWindowProcW(hwnd,message,wParam,lParam);
    }
};
LRESULT CALLBACK windowProc(HWND hwnd,UINT message,WPARAM wParam,LPARAM lParam) { return instance?instance->procedure(hwnd,message,wParam,lParam):DefWindowProcW(hwnd,message,wParam,lParam); }
Options parseOptions() {
    Options options; int argc=0; auto argv=CommandLineToArgvW(GetCommandLineW(),&argc);
    for(int i=1;i<argc;++i) {
        std::wstring arg=argv[i];
        if(arg==L"--background") options.background=true;
        else if(arg==L"--preview") options.preview=true;
        else if(arg==L"--offline") options.offline=true;
        else if(arg==L"--test-mode") options.test=true;
        else if(arg==L"--test-seconds"&&i+1<argc) options.testSeconds=std::clamp(_wtoi(argv[++i]),1,86400);
        else if(arg==L"--diagnostics"&&i+1<argc) options.diagnostic=argv[++i];
        else if(arg==L"--history"&&i+1<argc) options.history=argv[++i];
    }
    LocalFree(argv); return options;
}
}
int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int) {
    auto options=parseOptions();
    HANDLE mutex=CreateMutexW(nullptr,FALSE,options.test?L"Local\\Interval.Desktop.Test.1":L"Local\\Interval.Desktop.1");
    if(!mutex) return 1;
    if(GetLastError()==ERROR_ALREADY_EXISTS) { if(auto existing=FindWindowW(L"Interval.Owner",options.test?L"Interval.Background.Test":L"Interval.Background")) PostMessageW(existing,OpenMessage,options.preview?1:0,0); CloseHandle(mutex); return 0; }
    App app(std::move(options));
    if(!app.start()) { MessageBoxW(nullptr,L"인터벌을 시작하지 못했습니다.",L"인터벌",MB_OK|MB_ICONERROR); CloseHandle(mutex); return 1; }
    int result=app.run(); CloseHandle(mutex); return result;
}
