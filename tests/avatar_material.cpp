#include "../src/native_avatar_material.hpp"
#include "../src/native_publication.hpp"
#include <iostream>
#include <stdexcept>
void require(bool v,const char* why){if(!v)throw std::runtime_error(why);}
int main(){try{
    vrm::Material source;source.base_color={.2f,.4f,.6f,.8f};source.alpha=vrm::AlphaMode::Blend;source.double_sided=true;
    evrm::AvatarAppearance look;look.whole=evrm::AvatarShaderPreset::Matte;look.materials={evrm::AvatarShaderPreset::GameLighting,evrm::AvatarShaderPreset::Matte};
    const auto game=evrm::native_avatar_material(source,99,evrm::avatar_material_shader_mode(&look,0),false);
    const auto matte=evrm::native_avatar_material(source,99,evrm::avatar_material_shader_mode(&look,1),false);
    require(game.alpha[3]==0&&matte.alpha[3]==2,"per-material shader mode wrong");
    require(game.alpha[1]==2&&matte.alpha[1]==2&&matte.alpha[2]==1&&matte.base[3]==.8f&&matte.instance[0]==99,"preset changed alpha/tags/sidedness");
    const auto menu=evrm::native_avatar_material(source,99,2,true);require(menu.alpha[0]==.5f&&menu.alpha[1]==1,"menu cutout behavior changed");
    auto root=std::make_shared<evrm::NativeAvatarPublication>();root->asset=std::make_shared<vrm::Avatar>();root->source_instances=90;root->source_buffer_epoch=3;root->serial=1;root->presentation_serial=2;
    auto child=std::make_shared<evrm::NativeAvatarPublication>(*root);child->source_instances=91;root->additional_views.push_back(child);
    auto remote=std::make_shared<evrm::NativeAvatarPublication>(*child);remote->subject="remote";child->additional_subjects.push_back(remote);
    auto appearance=std::make_shared<const evrm::AvatarAppearance>(look);const auto changed=evrm::native_publication_appearance(root,appearance);
    require(changed&&changed!=root&&changed->appearance==appearance&&changed->additional_views[0]->appearance==appearance&&!root->appearance&&!child->appearance,"appearance snapshot not coherent/immutable");
    require(evrm::native_publication_appearance(changed,appearance)==changed,"unchanged appearance allocates publication");
    require(changed->additional_views[0]->additional_subjects[0]->appearance==appearance,"same-asset subject in another view missed appearance");
    auto partial=std::make_shared<evrm::NativeAvatarPublication>(*changed);partial->additional_views={child};
    require(evrm::native_publication_appearance(partial,appearance)!=partial,"root-only no-op missed another affected view");
    auto duplicate=std::make_shared<evrm::NativeAvatarPublication>(*root);auto duplicate_view=std::make_shared<evrm::NativeAvatarPublication>(*root);duplicate_view->additional_views.clear();duplicate->additional_views={duplicate_view};
    require(!evrm::native_publication_appearance(duplicate,appearance),"duplicate native views accepted");
    std::cout<<"PASS production native material constants, alpha/sidedness, inheritance and immutable style propagation\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
