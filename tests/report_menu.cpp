#include "../src/mod_menu.hpp"
#include "../vendor/imgui/imgui.h"
#include <iostream>
#include <stdexcept>
void need(bool value,const char* why){if(!value)throw std::runtime_error(why);}
int main(){try{
    ImGui::CreateContext();evrm::configure_mod_menu(1,false);auto& io=ImGui::GetIO();io.DisplaySize={1200,1600};io.DeltaTime=1.f/60;
    unsigned char* pixels;int w,h;io.Fonts->GetTexDataAsRGBA32(&pixels,&w,&h);
    evrm::ModSettings settings;evrm::ModMenuStatus status;evrm::ModMenuPlacement placement;evrm::ModMenuLayout layout;bool expanded=false,close=false;
    auto report=std::make_shared<evrm::DebugReportStatus>();status.report=report;evrm::DebugReportAction action,last;
    const auto frame=[&]{ImGui::NewFrame();evrm::draw_mod_menu(settings,status,expanded,close,placement,&layout,nullptr,&action);ImGui::Render();if(action.kind!=evrm::DebugReportActionKind::None)last=action;};
    const auto click=[&](evrm::ModMenuRect rect){need(rect.x1>rect.x0&&rect.y1>rect.y0,"report control missing");last={};io.AddMousePosEvent((rect.x0+rect.x1)/2,(rect.y0+rect.y1)/2);frame();io.AddMouseButtonEvent(0,true);frame();io.AddMouseButtonEvent(0,false);frame();frame();};
    frame();frame();click(layout.report_create);need(last.kind==evrm::DebugReportActionKind::Begin&&!last.include_image,"default report included screenshot");
    click(layout.report_image);click(layout.report_create);need(last.include_image&&!placement.report_include_image,"screenshot choice persisted to next report");
    report->job=7;report->phase=evrm::DebugReportPhase::Preview;report->image_id="exact-image";auto preview=std::make_shared<evrm::DebugReportImage>();preview->width=2;preview->height=1;report->preview=preview;
    frame();frame();click(layout.report_include);need(last.kind==evrm::DebugReportActionKind::None,"include enabled before preview was presented");
    status.report_texture=123;report->preview_presented=true;frame();frame();need(layout.report_preview_visible,"preview not drawn");click(layout.report_include);
    need(last.kind==evrm::DebugReportActionKind::IncludePreview&&last.job==7&&last.image_id=="exact-image","preview approval lacks image identity");
    click(layout.report_text);need(last.kind==evrm::DebugReportActionKind::SaveTextOnly,"omit image action missing");click(layout.report_cancel);need(last.kind==evrm::DebugReportActionKind::Cancel,"cancel missing");
    report->phase=evrm::DebugReportPhase::Partial;report->filename="EnshroudedVRM-report-fixture.zip";frame();frame();click(layout.report_folder);need(last.kind==evrm::DebugReportActionKind::OpenFolder,"explicit folder action missing");
    need(!settings.voice_enabled&&!close,"report changed voice or closed menu");ImGui::DestroyContext();std::cout<<"PASS F8 report default, one-shot screenshot choice, preview consent, omit/cancel and explicit folder action\n";return 0;
}catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}}
