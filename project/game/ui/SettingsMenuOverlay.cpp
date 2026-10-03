#define NOMINMAX
#include "SettingsMenuOverlay.h"
#include "GameSettingsManager.h"
#include "InputManager.h"
#include "Sprite.h"
#include "SpriteCommon.h"
#include "TextureManager.h"
#include "DirectXCommon.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <filesystem>

namespace {
constexpr const char* kActions[] = {"Forward","Backward","Left","Right","Jump","PrimaryAttack",
    "SecondaryAction","Ability","MorphRelease","LockOn","LockOnNext","Attack"};
constexpr int kDefaultKeys[] = {DIK_W,DIK_S,DIK_A,DIK_D,DIK_SPACE,0,0,DIK_E,DIK_R,DIK_Q,DIK_G,DIK_Z};
constexpr WORD kDefaultPads[] = {XINPUT_GAMEPAD_DPAD_UP,XINPUT_GAMEPAD_DPAD_DOWN,XINPUT_GAMEPAD_DPAD_LEFT,
    XINPUT_GAMEPAD_DPAD_RIGHT,XINPUT_GAMEPAD_A,XINPUT_GAMEPAD_X,XINPUT_GAMEPAD_LEFT_SHOULDER,
    XINPUT_GAMEPAD_Y,XINPUT_GAMEPAD_B,XINPUT_GAMEPAD_RIGHT_THUMB,XINPUT_GAMEPAD_LEFT_THUMB,0};
constexpr int kVisibleRows = 6;
constexpr Vector4 kInk{.07f,.17f,.21f,1};
constexpr Vector4 kAccent{.04f,.58f,.63f,1};
bool Same(const BindData& a,const BindData& b) { return a.keyCode==b.keyCode && a.mouseButton==b.mouseButton && a.padCode==b.padCode; }
bool Inside(Vector2 p,float x,float y,float w,float h) { return p.x>=x && p.x<x+w && p.y>=y && p.y<y+h; }
WinApp* Window() { return DirectXCommon::GetInstance()->GetWinApp(); }
RECT previousCursorClip{};
bool previousCursorVisible = true;
}

SettingsMenuOverlay::SettingsMenuOverlay() = default;
SettingsMenuOverlay::~SettingsMenuOverlay() { Finalize(); }
void SettingsMenuOverlay::Initialize(SpriteCommon* common,const std::string&) {
    common_=common; input_=InputManager::GetInstance();
    // 色と行間はJSONで調整可能。従来のレイアウトは移行用に残します。
    try {
        std::ifstream file("Resources/json/ui/settingsMenu.json"); nlohmann::json j;
        if (file >> j; j.is_object()) {
            rowHeight_=std::clamp(j.value("rowHeight",56.0f),48.0f,58.0f);
            auto read=[](const nlohmann::json& v,Vector4 fallback) {
                if (!v.is_array() || v.size()!=4) return fallback;
                return Vector4{v[0].get<float>(),v[1].get<float>(),v[2].get<float>(),v[3].get<float>()};
            };
            panelColor_=read(j.value("panelColor",nlohmann::json()),panelColor_);
            selectedColor_=read(j.value("selectedColor",nlohmann::json()),selectedColor_);
        }
    } catch (...) {}
}
void SettingsMenuOverlay::Finalize() {
    if (active_) SetActive(false);
    sprites_.clear(); common_=nullptr; input_=nullptr;
}
void SettingsMenuOverlay::ReadValues() {
    auto* s=GameSettingsManager::GetInstance();
    original_.bgm=s->GetBGMVolume(); original_.se=s->GetSEVolume(); original_.sensitivity=s->GetCameraSensitivity();
    original_.invertX=s->GetCameraInvertX();original_.invertY=s->GetCameraInvertY();
    original_.bindings=KeyConfig::GetInstance()->GetBindings();
    RECT rc{}; GetClientRect(Window()->GetHwnd(),&rc);
    original_.width=rc.right; original_.height=rc.bottom;
    original_.borderless=(GetWindowLongPtr(Window()->GetHwnd(),GWL_STYLE)&WS_OVERLAPPEDWINDOW)==0;
    draft_=original_;
}
void SettingsMenuOverlay::SetActive(bool active) {
    if (active_==active) return;
    if (active) {
        if(activeCount_==0) {
            GetClipCursor(&previousCursorClip);
            CURSORINFO cursor{sizeof(CURSORINFO)};
            previousCursorVisible=!GetCursorInfo(&cursor) || (cursor.flags&CURSOR_SHOWING)!=0;
            ClipCursor(nullptr);WinApp::SetCursorVisibility(true);
        }
        ++activeCount_; ReadValues(); category_=row_=scroll_=0; footer_=-1;
        modal_=Modal::None; status_.clear(); suppressInput_=true; replayOnly_=false;
    } else {
        --activeCount_;
        if(activeCount_==0 && !replayOnly_) {
            if(GetForegroundWindow()==Window()->GetHwnd()) ClipCursor(&previousCursorClip);
            WinApp::SetCursorVisibility(previousCursorVisible);
        }
        if (!replayOnly_) { RollbackDisplay(); draft_=original_; PreviewAudio(); }
        modal_=Modal::None;
    }
    active_=active;
    for(auto& s:sprites_) s->SetVisible(false);
    if(active_) BuildFrame();
}
bool SettingsMenuOverlay::IsDirty() const {
    if(original_.bgm!=draft_.bgm || original_.se!=draft_.se || original_.sensitivity!=draft_.sensitivity ||
       original_.width!=draft_.width || original_.height!=draft_.height || original_.borderless!=draft_.borderless || original_.invertX!=draft_.invertX || original_.invertY!=draft_.invertY) return true;
    for(const auto& [name,b]:draft_.bindings) {
        auto old=original_.bindings.find(name);
        if(old==original_.bindings.end() || !Same(b,old->second)) return true;
    }
    return false;
}
void SettingsMenuOverlay::PreviewAudio() {
    auto* s=GameSettingsManager::GetInstance(); s->SetBGMVolume(draft_.bgm); s->SetSEVolume(draft_.se);
}
void SettingsMenuOverlay::Apply() {
    if(!IsDirty()) return;
    if(original_.width!=draft_.width || original_.height!=draft_.height || original_.borderless!=draft_.borderless) {
        previousDisplay_=Window()->CaptureDisplayState();
        if(!Window()->SetDisplayMode(draft_.width,draft_.height,draft_.borderless)) {
            Window()->RestoreDisplayState(previousDisplay_); status_="display_error"; return;
        }
        displayTrial_=true; modal_=Modal::Display; modalSelection_=1;
        displayDeadline_=std::chrono::steady_clock::now()+std::chrono::seconds(15);
    } else Commit();
}
void SettingsMenuOverlay::Commit() {
    const auto fail=[this]() {RollbackDisplay();status_="save_error";};
    // 保存完了まで入力設定を公開しない。既存ファイルは一時ファイルから置換します。
    try {
        namespace fs=std::filesystem;
        const fs::path userPath="Resources/json/user_config.json", keyPath="Resources/json/key/keyconfig.json";
        auto read=[](const fs::path& path) {std::ifstream f(path);return std::string(std::istreambuf_iterator<char>(f),{});};
        const bool hadUser=fs::exists(userPath);
        const std::string oldUser=read(userPath);
        auto j=oldUser.empty()?nlohmann::json::object():nlohmann::json::parse(oldUser);
        if(!j.is_object()) j=nlohmann::json::object();
        j["bgmVolume"]=draft_.bgm;j["seVolume"]=draft_.se;j["cameraSensitivity"]=draft_.sensitivity;
        j["cameraInvertX"]=draft_.invertX;j["cameraInvertY"]=draft_.invertY;
        if(displayTrial_) j["display"]={{"width",draft_.width},{"height",draft_.height},{"borderless",draft_.borderless}};
        nlohmann::json keys=nlohmann::json::object();
        for(const auto& [name,b]:draft_.bindings) keys[name]={{"key",b.keyCode},{"mouse",b.mouseButton},{"pad",b.padCode}};
        auto replace=[](const fs::path& path,const std::string& text) {
            fs::create_directories(path.parent_path());
            // 前回失敗で残ったファイルや他の保存処理を上書きしないよう固有名を使います。
            const fs::path temporary=path.wstring()+L".options-"+std::to_wstring(GetCurrentProcessId())+L".tmp";
            if(fs::exists(temporary)) return false;
            {std::ofstream f(temporary,std::ios::binary);f<<text;f.flush();if(!f) {f.close();fs::remove(temporary);return false;}}
            if(MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) return true;
            fs::remove(temporary);return false;
        };
        if(!replace(userPath,j.dump(4))) {fail();return;}
        if(!replace(keyPath,keys.dump(4))) {
            if(hadUser) replace(userPath,oldUser);else fs::remove(userPath);
            fail();return;
        }
    } catch(...) {fail();return;}
    auto* keys=KeyConfig::GetInstance();for(const auto& [name,b]:draft_.bindings) keys->SetBinding(name,b);
    auto* s=GameSettingsManager::GetInstance();PreviewAudio();s->SetCameraSensitivity(draft_.sensitivity);
    s->SetCameraInversion(draft_.invertX,draft_.invertY);
    original_=draft_; displayTrial_=false; modal_=Modal::None; status_="saved";
}
void SettingsMenuOverlay::RollbackDisplay() {
    if(!displayTrial_) return;
    Window()->RestoreDisplayState(previousDisplay_); displayTrial_=false;
    draft_.width=original_.width; draft_.height=original_.height; draft_.borderless=original_.borderless;
    modal_=Modal::None; status_="display_restored";
}
void SettingsMenuOverlay::ResetCategory() {
    if(category_==0) { draft_.bgm=.8f; draft_.se=.8f; PreviewAudio(); }
    if(category_==1) {
        for(int i=0;i<12;++i) {
            auto& b=draft_.bindings[kActions[i]];
            if(device_==0) { b.keyCode=kDefaultKeys[i]; b.mouseButton=i==5?0:i==6?1:-1; }
            else b.padCode=kDefaultPads[i];
        }
    }
    if(category_==2) {draft_.sensitivity=1;draft_.invertX=draft_.invertY=false;}
    if(category_==3) { draft_.width=1280;draft_.height=720;draft_.borderless=false; }
}
int SettingsMenuOverlay::RowCount() const { return category_==0?2:category_==1?13:category_==2?3:2; }
void SettingsMenuOverlay::ChangeCategory(int category) {
    category_=(category+4)%4; row_=scroll_=0;footer_=-1; dragging_=false;status_.clear();
}
void SettingsMenuOverlay::Adjust(int direction) {
    status_.clear();
    if(category_==0) { float& value=row_==0?draft_.bgm:draft_.se; value=std::clamp(value+direction*.05f,0.f,1.f); PreviewAudio(); }
    if(category_==2) {
        if(row_==0) draft_.sensitivity=std::clamp(draft_.sensitivity+direction*.05f,.2f,1.f);
        else if(row_==1) draft_.invertX=!draft_.invertX;
        else draft_.invertY=!draft_.invertY;
    }
    if(category_==1 && row_==0) device_=1-device_;
    if(category_==3 && row_==0) draft_.borderless=!draft_.borderless;
    if(category_==3 && row_==1 && !draft_.borderless) {
        constexpr int widths[]={960,1280,1600,1920,2560}; constexpr int heights[]={540,720,900,1080,1440};
        MONITORINFO monitor{sizeof(MONITORINFO)};GetMonitorInfo(MonitorFromWindow(Window()->GetHwnd(),MONITOR_DEFAULTTONEAREST),&monitor);
        std::vector<int> choices;
        for(int i=0;i<5;++i) {
            RECT r{0,0,widths[i],heights[i]};AdjustWindowRect(&r,WS_OVERLAPPEDWINDOW,FALSE);
            if(r.right-r.left<=monitor.rcWork.right-monitor.rcWork.left && r.bottom-r.top<=monitor.rcWork.bottom-monitor.rcWork.top) choices.push_back(i);
        }
        if(choices.empty()) {status_="display_error";return;}
        int index=direction>0?-1:0;
        for(int i=0;i<static_cast<int>(choices.size());++i) if(widths[choices[i]]==draft_.width) index=i;
        index=(index+direction+static_cast<int>(choices.size()))%static_cast<int>(choices.size());
        draft_.width=widths[choices[index]];draft_.height=heights[choices[index]];
    }
}
void SettingsMenuOverlay::ActivateRow() {
    if(category_==1 && row_>0) {modal_=Modal::Binding;bindingArmed_=false;return;}
    Adjust(1);
}
void SettingsMenuOverlay::ActivateFooter(int index) {
    dragging_=false;modalSelection_=1;
    if(index==0) {if(IsDirty()) modal_=Modal::Discard;else SetActive(false);}
    if(index==1) modal_=Modal::Reset;
    if(index==2) Apply();
}
void SettingsMenuOverlay::CaptureBinding() {
    // 決定に使ったキーやクリックをそのまま割り当てないよう、一度離すまで待ちます。
    if(!bindingArmed_) {
        if(input_->GetPressedKeys().empty() && input_->GetPressedMouseButton()<0 && input_->GetPressedGamepadButton()==0) bindingArmed_=true;
        return;
    }
    auto& b=draft_.bindings[kActions[row_-1]];candidate_=b;bool found=false;
    if(device_==0) {
        for(int key=1;key<256;++key) if(input_->IsKeyTriggered(static_cast<BYTE>(key))) {
            if(key==DIK_ESCAPE || key==DIK_RETURN || key==DIK_TAB || key==DIK_F1 || key==DIK_LWIN || key==DIK_RWIN || key==DIK_LALT || key==DIK_RALT) {status_="reserved";return;}
            candidate_.keyCode=key;candidate_.mouseButton=-1;found=true;break;
        }
        for(int mouse=0;mouse<4;++mouse) if(input_->IsMouseButtonTriggered(mouse)) {
            candidate_.keyCode=0;candidate_.mouseButton=mouse;found=true;break;
        }
    } else {
        WORD pressed=input_->GetPressedGamepadButton();
        if(pressed && input_->IsGamepadButtonTriggered(pressed)) {
            if(pressed&(XINPUT_GAMEPAD_START|XINPUT_GAMEPAD_BACK)) {status_="reserved";return;}
            if((pressed&(pressed-1))!=0) return;
            candidate_.padCode=pressed;found=true;
        }
    }
    if(!found) return;
    conflictAction_.clear();
    for(const auto& [name,other]:draft_.bindings) {
        if(name==kActions[row_-1]) continue;
        bool conflict=device_==1 ? candidate_.padCode!=0 && candidate_.padCode==other.padCode :
            (candidate_.keyCode!=0 && candidate_.keyCode==other.keyCode) || (candidate_.mouseButton>=0 && candidate_.mouseButton==other.mouseButton);
        if(conflict) {conflictAction_=name;break;}
    }
    if(conflictAction_.empty()) AcceptBinding(false);
    else {modal_=Modal::Conflict;modalSelection_=1;}
}
void SettingsMenuOverlay::AcceptBinding(bool swap) {
    auto& current=draft_.bindings[kActions[row_-1]];
    if(swap && !conflictAction_.empty()) {
        auto& other=draft_.bindings[conflictAction_];
        if(device_==1) other.padCode=current.padCode;
        else {other.keyCode=current.keyCode;other.mouseButton=current.mouseButton;}
    }
    current=candidate_;modal_=Modal::None;status_.clear();suppressInput_=true;
}
void SettingsMenuOverlay::HandleModal(bool accept,bool back) {
    if(modal_==Modal::Display && (std::chrono::steady_clock::now()>=displayDeadline_ || GetForegroundWindow()!=Window()->GetHwnd())) {RollbackDisplay();return;}
    if(modal_==Modal::Binding) {
        if(input_->IsKeyTriggered(DIK_ESCAPE) || input_->IsGamepadButtonTriggered(XINPUT_GAMEPAD_BACK)) {modal_=Modal::None;status_.clear();} else CaptureBinding();
        return;
    }
    if(back || (accept && modalSelection_==1)) {
        if(modal_==Modal::Display) RollbackDisplay(); else modal_=Modal::None;
        return;
    }
    if(!accept) return;
    const Modal mode=modal_;modal_=Modal::None;
    if(mode==Modal::Discard) SetActive(false);
    if(mode==Modal::Reset) ResetCategory();
    if(mode==Modal::Conflict) AcceptBinding(true);
    if(mode==Modal::Display) Commit();
}
void SettingsMenuOverlay::SetEditorViewport(int left,int top,int right,int bottom) {
    editorViewport_={left,top,right,bottom};hasEditorViewport_=right>left && bottom>top;
}
Vector2 SettingsMenuOverlay::Pointer() const {
    Vector2 p=input_->GetMousePosition();float w=static_cast<float>(WinApp::kClientWidth),h=static_cast<float>(WinApp::kClientHeight);
#ifdef USE_IMGUI
    if(hasEditorViewport_) {
        POINT screen{};GetCursorPos(&screen);p={static_cast<float>(screen.x-editorViewport_.left),static_cast<float>(screen.y-editorViewport_.top)};
        w=static_cast<float>(editorViewport_.right-editorViewport_.left);h=static_cast<float>(editorViewport_.bottom-editorViewport_.top);
    }
#endif
    // 描画と同じ等倍拡縮・余白を逆変換し、文字や当たり判定の比率を保ちます。
    const float scale=std::min(w/1280.f,h/720.f);
    return {(p.x-(w-1280*scale)*.5f)/std::max(.001f,scale),(p.y-(h-720*scale)*.5f)/std::max(.001f,scale)};
}
SettingsMenuOverlay::Result SettingsMenuOverlay::Update(float) {
    if(!active_) return Result::None;
    if(replayOnly_) {
        // 通常更新が再開したら履歴の編集値は破棄し、現在の設定から開き直します。
        replayOnly_=false;active_=false;--activeCount_;SetActive(true);
        return Result::None;
    }
    if(suppressInput_) {
        if(input_->GetPressedKeys().empty() && input_->GetPressedMouseButton()<0 && input_->GetPressedGamepadButton()==0) suppressInput_=false;
        BuildFrame();return Result::None;
    }
    const bool accept=input_->IsKeyTriggered(DIK_RETURN)||input_->IsKeyTriggered(DIK_SPACE)||input_->IsGamepadButtonTriggered(XINPUT_GAMEPAD_A);
    const bool back=input_->IsKeyTriggered(DIK_ESCAPE)||input_->IsGamepadButtonTriggered(XINPUT_GAMEPAD_B);
    const int horizontal=(input_->IsKeyTriggered(DIK_RIGHT)||input_->IsGamepadButtonTriggered(XINPUT_GAMEPAD_DPAD_RIGHT)?1:0)-
        (input_->IsKeyTriggered(DIK_LEFT)||input_->IsGamepadButtonTriggered(XINPUT_GAMEPAD_DPAD_LEFT)?1:0);
    const int vertical=(input_->IsKeyTriggered(DIK_DOWN)||input_->IsGamepadButtonTriggered(XINPUT_GAMEPAD_DPAD_DOWN)?1:0)-
        (input_->IsKeyTriggered(DIK_UP)||input_->IsGamepadButtonTriggered(XINPUT_GAMEPAD_DPAD_UP)?1:0);
    Vector2 p=Pointer();const bool click=input_->IsMouseButtonTriggered(0);
    if(modal_!=Modal::None) {
        if(horizontal) modalSelection_=1-modalSelection_;
        bool choose=accept;
        if(modal_!=Modal::Binding && click) {
            if(Inside(p,365,405,255,52)) {modalSelection_=0;choose=true;}
            if(Inside(p,660,405,255,52)) {modalSelection_=1;choose=true;}
        }
        HandleModal(choose,back);
    } else {
        if(input_->IsKeyTriggered(DIK_TAB)) ChangeCategory(category_+(input_->IsKeyPressed(DIK_LSHIFT)?-1:1));
        if(input_->IsGamepadButtonTriggered(XINPUT_GAMEPAD_LEFT_SHOULDER)) ChangeCategory(category_-1);
        if(input_->IsGamepadButtonTriggered(XINPUT_GAMEPAD_RIGHT_SHOULDER)) ChangeCategory(category_+1);
        if(vertical) {
            if(footer_>=0) {footer_=-1;row_=vertical<0?RowCount()-1:0;}
            else if(row_+vertical>=RowCount() || row_+vertical<0) footer_=0;
            else row_+=vertical;
        }
        if(horizontal) {if(footer_>=0) footer_=(footer_+horizontal+3)%3;else Adjust(horizontal);}
        if(back) ActivateFooter(0);
        else if(accept) {if(footer_>=0) ActivateFooter(footer_);else ActivateRow();}
        if(click && modal_==Modal::None) {
            for(int c=0;c<4;++c) if(Inside(p,88,170+c*65.f,210,54)) ChangeCategory(c);
            for(int f=0;f<3;++f) if(Inside(p,350+f*280.f,605,258,48)) {footer_=f;ActivateFooter(f);}
            for(int n=0;n<kVisibleRows && n+scroll_<RowCount();++n) if(Inside(p,350,170+n*rowHeight_,825,rowHeight_-6)) {
                row_=scroll_+n;footer_=-1;
                if(category_==0 || (category_==2 && row_==0)) dragging_=p.x>=755 && p.x<=1055;
                else if(p.x>=720) {if(category_==1 && row_>0) ActivateRow();else Adjust(p.x<900?-1:1);}
            }
        }
        if(!input_->IsMouseButtonPressed(0)) dragging_=false;
        if(dragging_ && modal_==Modal::None && (category_==0||(category_==2 && row_==0))) {
            float t=std::clamp((p.x-755)/300.f,0.f,1.f);
            if(category_==0) {if(row_==0) draft_.bgm=t;else draft_.se=t;PreviewAudio();}
            else draft_.sensitivity=.2f+t*.8f;
        }
        if(Inside(p,350,170,825,350) && input_->GetMouseWheelDelta()!=0) {
            row_=std::clamp(row_+(input_->GetMouseWheelDelta()>0?-1:1),0,RowCount()-1);footer_=-1;
        }
        scroll_=std::clamp(scroll_,std::max(0,row_-kVisibleRows+1),row_);
    }
    if(active_) BuildFrame();
    return active_?Result::None:Result::Closed;
}

void SettingsMenuOverlay::Picture(const std::string& texture,float x,float y,float w,float h,const Vector4& color) {
    if(usedSprites_==sprites_.size()) {auto s=std::make_unique<Sprite>();s->Initialize(common_,Sprite::LoadTexture(texture));sprites_.push_back(std::move(s));}
    auto& s=sprites_[usedSprites_++];const auto handle=Sprite::LoadTexture(texture);const auto& meta=TextureManager::GetInstance()->GetMetadata(handle);
    s->SetTextureHandle(handle);s->SetTextureName(texture);s->SetTextureRect({0,0},{static_cast<float>(meta.width),static_cast<float>(meta.height)});
    const float scale=std::min(WinApp::kClientWidth/1280.f,WinApp::kClientHeight/720.f);
    s->SetAnchorPoint({0,0});s->SetPosition({(WinApp::kClientWidth-1280*scale)*.5f+x*scale,(WinApp::kClientHeight-720*scale)*.5f+y*scale});
    s->SetSize({w*scale,h*scale});s->SetColor(color);s->SetEmissive(1);s->SetVisible(true);s->Update();
}
void SettingsMenuOverlay::Rect(float x,float y,float w,float h,const Vector4& color) {Picture("common/white.png",x,y,w,h,color);}
void SettingsMenuOverlay::Label(const std::string& id,float x,float y,float height) {
    const std::string path="ui/settings/text/"+id+".png";const auto& m=TextureManager::GetInstance()->GetMetadata(Sprite::LoadTexture(path));
    Picture(path,x,y,height*static_cast<float>(m.width)/std::max(1.f,static_cast<float>(m.height)),height,kInk);
}
void SettingsMenuOverlay::Number(int value,float x,float y,float height) {
    for(char c:std::to_string(value)) {Label(std::string("digit_")+c,x,y,height);x+=height*.65f;}
}
void SettingsMenuOverlay::BuildFrame() {
    if(!common_) return;
    usedSprites_=0;
    // 外側は暗くし、設定欄は不透明にして背景の動きから情報を分離します。
    Rect(-1280,-720,3840,2160,{.015f,.035f,.055f,.90f});
    Rect(62,52,1156,622,kAccent);Rect(66,56,1148,614,panelColor_);
    Label("title",94,78,34);Label(IsDirty()?"dirty":"clean",940,90,18);
    Picture("title/slime_save_icon.png",1135,76,49,40,{1,1,1,1});
    Rect(86,132,1106,2,{.69f,.79f,.78f,1});
    constexpr const char* tabs[]={"sound","controls","camera","display"};
    for(int c=0;c<4;++c) {
        Rect(88,170+c*65.f,210,54,c==category_?selectedColor_:Vector4{.85f,.89f,.87f,1});
        if(c==category_) Rect(88,170+c*65.f,5,54,kAccent);
        Label(tabs[c],110,183+c*65.f,25);
    }
    for(int n=0;n<kVisibleRows && scroll_+n<RowCount();++n) {
        int r=scroll_+n;float y=170+n*rowHeight_;
        Rect(350,y,825,rowHeight_-6,r==row_ && footer_<0?selectedColor_:Vector4{1,1,1,1});
        if(r==row_ && footer_<0) Rect(350,y,4,rowHeight_-6,kAccent);
        std::string label=category_==0?(r==0?"bgm":"se"):category_==1?(r==0?"device":"action_"+std::to_string(r-1)):category_==2?(r==0?"sensitivity":r==1?"invert_x":"invert_y"):r==0?"window_mode":"resolution";
        Label(label,370,y+13,22);
        if(category_==0 || (category_==2 && r==0)) {
            float value=category_==0?(r==0?draft_.bgm:draft_.se):draft_.sensitivity;
            float t=category_==2?(value-.2f)/.8f:value;
            Rect(755,y+22,300,7,{.70f,.79f,.78f,1});Rect(755,y+22,std::max(1.f,t*300),7,kAccent);
            Rect(750+t*300,y+15,10,21,kAccent);Number(static_cast<int>(std::round(value*100)),1080,y+13);
        } else {
            if(category_==1 && r>0) {
                const auto& b=draft_.bindings[kActions[r-1]];
                const std::string value=device_==1?"pad_"+std::to_string(b.padCode):b.mouseButton>=0?"mouse_"+std::to_string(b.mouseButton):"key_"+std::to_string(b.keyCode);
                Label(value,815,y+13,22);Label("change",1080,y+15,18);
            } else if(category_==3 && r==1) {
                if(draft_.borderless) Label("desktop",785,y+13);
                else {Number(draft_.width,798,y+13);Label("times",862,y+13);Number(draft_.height,885,y+13);}
            } else if(category_==2) Label((r==1?draft_.invertX:draft_.invertY)?"on":"off",800,y+13,22);
            else Label(category_==1?(device_==0?"keyboard":"pad"):draft_.borderless?"borderless":"windowed",800,y+13,22);
            if(!(category_==1 && r>0)) {Label("left",730,y+12);Label("right",1128,y+12);}
        }
    }
    if(category_==1) {Label("scroll",1000,519,16);Number(row_+1,1140,519,16);}
    const char* description=category_==0?"help_sound":category_==1?"help_controls":category_==2?"help_camera":"help_display";
    Label(status_.empty()?description:status_,350,548,18);
    for(int f=0;f<3;++f) {
        Rect(350+f*280.f,605,258,48,footer_==f?selectedColor_:Vector4{.82f,.89f,.87f,1});
        Label(f==0?"back":f==1?"reset":"apply",374+f*280.f,618,22);
    }
    Label("hints",350,577,17);
    if(modal_!=Modal::None) {
        Rect(62,52,1156,622,{.01f,.04f,.06f,.70f});Rect(320,238,640,255,panelColor_);
        const char* message=modal_==Modal::Discard?"confirm_discard":modal_==Modal::Reset?"confirm_reset":modal_==Modal::Binding?"capture":modal_==Modal::Conflict?"conflict":"confirm_display";
        Label(message,350,270,26);
        if(modal_==Modal::Binding) {Label(status_.empty()?"capture_help":status_,350,330,20);}
        else {
            if(modal_==Modal::Display) {Label("timeout",350,329,21);Number(std::max(0,static_cast<int>(std::ceil(std::chrono::duration<float>(displayDeadline_-std::chrono::steady_clock::now()).count()))),820,329,24);}
            if(modal_==Modal::Conflict) {
                for(int i=0;i<12;++i) if(conflictAction_==kActions[i]) Label("action_"+std::to_string(i),350,330,22);
            }
            for(int b=0;b<2;++b) {Rect(365+b*295.f,405,255,52,b==modalSelection_?selectedColor_:Vector4{.82f,.88f,.87f,1});Label(b==0?"yes":"cancel",395+b*295.f,420,23);}
        }
    }
    for(size_t i=usedSprites_;i<sprites_.size();++i) sprites_[i]->SetVisible(false);
}
void SettingsMenuOverlay::Draw() {if(active_) for(auto& s:sprites_) s->Draw();}
void SettingsMenuOverlay::CollectReplaySprites(std::vector<Sprite*>& out) const {for(const auto& s:sprites_) out.push_back(s.get());}
void SettingsMenuOverlay::CaptureReplayState(nlohmann::json& state) const {
    state={{"active",active_},{"category",category_},{"row",row_},{"scroll",scroll_},{"footer",footer_},{"device",device_}};
    state["values"]={{"bgm",draft_.bgm},{"se",draft_.se},{"sensitivity",draft_.sensitivity},{"width",draft_.width},{"height",draft_.height},
        {"borderless",draft_.borderless},{"invertX",draft_.invertX},{"invertY",draft_.invertY}};
    for(const auto& [name,b]:draft_.bindings) state["bindings"][name]={{"key",b.keyCode},{"mouse",b.mouseButton},{"pad",b.padCode}};
}
void SettingsMenuOverlay::RestoreReplayState(const nlohmann::json& state) {
    if(!state.is_object()) return;
    const bool active=state.value("active",false);
    if(active_!=active) {activeCount_+=active?1:-1;active_=active;}
    replayOnly_=true;modal_=Modal::None;
    if(state.contains("values") && state["values"].is_object()) {
        const auto& v=state["values"];
        draft_.bgm=std::clamp(v.value("bgm",.8f),0.f,1.f);draft_.se=std::clamp(v.value("se",.8f),0.f,1.f);
        draft_.sensitivity=std::clamp(v.value("sensitivity",1.f),.2f,1.f);
        draft_.width=std::clamp(v.value("width",1280),960,7680);draft_.height=std::clamp(v.value("height",720),540,4320);
        draft_.borderless=v.value("borderless",false);draft_.invertX=v.value("invertX",false);draft_.invertY=v.value("invertY",false);
    }
    draft_.bindings=KeyConfig::GetInstance()->GetBindings();
    if(state.contains("bindings") && state["bindings"].is_object()) for(auto& [name,b]:draft_.bindings) {
        if(!state["bindings"].contains(name)) continue;
        const auto& v=state["bindings"][name];if(!v.is_object()) continue;
        b.keyCode=std::clamp(v.value("key",0),0,255);b.mouseButton=std::clamp(v.value("mouse",-1),-1,3);
        b.padCode=static_cast<WORD>(v.value("pad",0));
    }
    category_=std::clamp(state.value("category",0),0,3);row_=std::clamp(state.value("row",0),0,RowCount()-1);
    scroll_=std::clamp(state.value("scroll",0),0,row_);footer_=std::clamp(state.value("footer",-1),-1,2);device_=std::clamp(state.value("device",0),0,1);
    // リプレイ復元では実際の設定保存・ウィンドウ変更を行いません。
    if(active_) BuildFrame();else for(auto& s:sprites_) s->SetVisible(false);
}
