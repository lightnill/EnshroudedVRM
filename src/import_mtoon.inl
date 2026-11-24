// Factual contracts from pinned VRM schemas / MIT UniVRM MToon properties.
// See docs/research/2026-11-05-mtoon-reference-provenance.md.
std::shared_ptr<const MToonMaterial> parse_mtoon(const Json& doc,std::size_t index){
    const auto& material=doc.at("materials").at(index);
    const Json* extension=nullptr;const Json* legacy=nullptr;
    if(material.contains("extensions")&&material.at("extensions").contains("VRMC_materials_mtoon"))
        extension=&material.at("extensions").at("VRMC_materials_mtoon");
    if(!extension&&doc.contains("extensions")&&doc.at("extensions").contains("VRM")){
        const auto& vrm=doc.at("extensions").at("VRM");
        if(vrm.contains("materialProperties")){
            const auto& props=vrm.at("materialProperties");require(props.is_array(),"Invalid MToon material array");
            if(index<props.size()){
                const auto& candidate=props.at(index);require(candidate.is_object(),"Invalid MToon material object");
                if(candidate.value("shader",std::string{})=="VRM/MToon")legacy=&candidate;
            }
        }
    }
    if(!extension&&!legacy)return {};
    auto out=std::make_shared<MToonMaterial>();
    const auto mark=[&](MToonField key){out->authored.set(static_cast<std::size_t>(key));};
    const auto numeric=[](const Json& value,double low=-1e12,double high=1e12){
        require(value.is_number(),"Invalid MToon numeric type");const double n=value.get<double>();
        require(std::isfinite(n)&&n>=low&&n<=high,"Invalid MToon numeric range");return static_cast<float>(n);
    };
    const auto integer=[&](const Json& value,int low,int high,bool strict){
        if(strict)require(value.is_number_integer()&&!value.is_boolean(),"Invalid MToon integer type");
        require(value.is_number(),"Invalid MToon mode type");const double n=value.get<double>();
        require(std::isfinite(n)&&n>=low&&n<=high&&std::floor(n)==n,"MToon mode must be integral and in range");return static_cast<int>(n);
    };
    const auto scalar=[&](const Json& object,const char* name,float& destination,MToonField key,double low=-1e12,double high=1e12){
        if(object.contains(name)){destination=numeric(object.at(name),low,high);mark(key);}
    };
    const auto color=[&](const Json& object,const char* name,auto& destination,MToonField key,std::size_t count,double upper){
        if(!object.contains(name))return;const auto& values=object.at(name);
        require(values.is_array()&&values.size()==count,"Invalid MToon colour shape");
        for(std::size_t c=0;c<count;++c){const float n=numeric(values[c],0,upper);if(c<destination.size())destination[c]=n;}mark(key);
    };
    const auto tex=[&](const Json& object,const char* name,MToonTextureSlot slot,bool old){
        if(!object.contains(name))return;const auto i=static_cast<std::size_t>(slot);out->texture_authored.set(i);
        const auto& value=object.at(name);
        if(old){require(value.is_number_integer()&&!value.is_boolean(),"Invalid MToon texture index type");
            if(value==Json(-1)){out->textures[i]=SurfaceTexture{};return;}
            out->textures[i]=surface_texture(Json{{"index",value}},doc);
        }else out->textures[i]=surface_texture(value,doc);
    };
    if(material.contains("pbrMetallicRoughness")){
        const auto& pbr=material.at("pbrMetallicRoughness");require(pbr.is_object(),"Invalid MToon base material");
        color(pbr,"baseColorFactor",out->base_color,MToonField::BaseColor,4,1);
        tex(pbr,"baseColorTexture",MToonTextureSlot::Base,false);
    }
    color(material,"emissiveFactor",out->emission_color,MToonField::EmissionColor,3,1e12);
    tex(material,"emissiveTexture",MToonTextureSlot::Emission,false);
    tex(material,"normalTexture",MToonTextureSlot::Normal,false);
    if(material.contains("normalTexture"))scalar(material.at("normalTexture"),"scale",out->normal_scale,MToonField::NormalScale);
    scalar(material,"alphaCutoff",out->alpha_cutoff,MToonField::AlphaCutoff,0,1);
    if(material.contains("alphaMode")){
        require(material.at("alphaMode").is_string(),"Invalid MToon alpha mode");const auto mode=material.at("alphaMode").get<std::string>();
        require(mode=="OPAQUE"||mode=="MASK"||mode=="BLEND","Unsupported MToon alpha mode");
        out->render_mode=mode=="BLEND"?MToonRenderMode::Transparent:mode=="MASK"?MToonRenderMode::Cutout:MToonRenderMode::Opaque;mark(MToonField::RenderMode);
    }
    if(material.contains("doubleSided")){
        require(material.at("doubleSided").is_boolean(),"Invalid MToon double-sided flag");
        out->cull_mode=material.at("doubleSided").get<bool>()?MToonCullMode::None:MToonCullMode::Back;mark(MToonField::CullMode);
    }
    if(extension){
        const auto& e=*extension;require(e.is_object()&&e.at("specVersion")=="1.0","Unsupported MToon source version");
        color(e,"shadeColorFactor",out->shade_color,MToonField::ShadeColor,3,1);
        color(e,"matcapFactor",out->matcap_color,MToonField::MatcapColor,3,1);
        color(e,"parametricRimColorFactor",out->rim_color,MToonField::RimColor,3,1);
        color(e,"outlineColorFactor",out->outline_color,MToonField::OutlineColor,3,1);
        scalar(e,"shadingShiftFactor",out->shade_shift,MToonField::ShadeShift);
        scalar(e,"shadingToonyFactor",out->shade_toony,MToonField::ShadeToony,0,1);
        scalar(e,"giEqualizationFactor",out->gi_equalization,MToonField::GiEqualization,0,1);
        scalar(e,"rimLightingMixFactor",out->rim_lighting_mix,MToonField::RimLightingMix,0,1);
        scalar(e,"parametricRimFresnelPowerFactor",out->rim_power,MToonField::RimPower,0);
        scalar(e,"parametricRimLiftFactor",out->rim_lift,MToonField::RimLift);
        scalar(e,"outlineWidthFactor",out->outline_width,MToonField::OutlineWidth,0);
        scalar(e,"outlineLightingMixFactor",out->outline_lighting_mix,MToonField::OutlineLightingMix,0,1);
        scalar(e,"uvAnimationScrollXSpeedFactor",out->uv_scroll[0],MToonField::UvScrollX);
        scalar(e,"uvAnimationScrollYSpeedFactor",out->uv_scroll[1],MToonField::UvScrollY);
        scalar(e,"uvAnimationRotationSpeedFactor",out->uv_rotation,MToonField::UvRotation);
        for(const auto& entry:std::array<std::pair<const char*,MToonTextureSlot>,6>{{
            {"shadeMultiplyTexture",MToonTextureSlot::Shade},{"matcapTexture",MToonTextureSlot::Matcap},
            {"rimMultiplyTexture",MToonTextureSlot::Rim},{"outlineWidthMultiplyTexture",MToonTextureSlot::OutlineWidth},
            {"uvAnimationMaskTexture",MToonTextureSlot::UvAnimationMask},{"shadingShiftTexture",MToonTextureSlot::ShadingShift}}})tex(e,entry.first,entry.second,false);
        if(e.contains("shadingShiftTexture"))scalar(e.at("shadingShiftTexture"),"scale",out->shade_shift_texture_scale,MToonField::ShiftTextureScale);
        if(e.contains("outlineWidthMode")){
            require(e.at("outlineWidthMode").is_string(),"Invalid MToon outline mode type");const auto mode=e.at("outlineWidthMode").get<std::string>();
            require(mode=="none"||mode=="worldCoordinates"||mode=="screenCoordinates","Invalid MToon outline mode");
            out->outline_mode=mode=="worldCoordinates"?MToonOutlineMode::World:mode=="screenCoordinates"?MToonOutlineMode::Screen:MToonOutlineMode::None;mark(MToonField::OutlineMode);
        }
        if(e.contains("transparentWithZWrite")){
            require(e.at("transparentWithZWrite").is_boolean(),"Invalid MToon depth-write flag");
            out->transparent_with_zwrite=e.at("transparentWithZWrite").get<bool>();mark(MToonField::DepthWrite);
        }
        if(e.contains("renderQueueOffsetNumber")){out->render_queue_offset=integer(e.at("renderQueueOffsetNumber"),-9,9,true);mark(MToonField::Queue);}
        if(out->render_mode==MToonRenderMode::Transparent&&out->transparent_with_zwrite)out->render_mode=MToonRenderMode::TransparentZWrite;
        out->depth_write=out->render_mode!=MToonRenderMode::Transparent;
    }else{
        out->version=MToonVersion::Vrm0;out->shade_color={.97f,.81f,.86f};out->rim_lighting_mix=0;out->rim_power=1;out->outline_width=.5f;
        const auto& e=*legacy;const auto floats=e.value("floatProperties",Json::object()),vectors=e.value("vectorProperties",Json::object()),textures=e.value("textureProperties",Json::object());
        require(floats.is_object()&&vectors.is_object()&&textures.is_object(),"Invalid legacy MToon property maps");
        color(vectors,"_Color",out->base_color,MToonField::BaseColor,4,1e12);
        color(vectors,"_ShadeColor",out->shade_color,MToonField::ShadeColor,4,1e12);
        color(vectors,"_EmissionColor",out->emission_color,MToonField::EmissionColor,4,1e12);
        color(vectors,"_RimColor",out->rim_color,MToonField::RimColor,4,1e12);
        color(vectors,"_OutlineColor",out->outline_color,MToonField::OutlineColor,4,1e12);
        scalar(floats,"_ShadeShift",out->shade_shift,MToonField::ShadeShift,-1,1);
        scalar(floats,"_ShadeToony",out->shade_toony,MToonField::ShadeToony,0,1);
        scalar(floats,"_ReceiveShadowRate",out->receive_shadow,MToonField::ReceiveShadow,0,1);
        scalar(floats,"_ShadingGradeRate",out->shading_grade,MToonField::ShadingGrade,0,1);
        scalar(floats,"_LightColorAttenuation",out->light_color_attenuation,MToonField::LightColorAttenuation,0,1);
        scalar(floats,"_IndirectLightIntensity",out->indirect_intensity,MToonField::IndirectIntensity,0,1);
        scalar(floats,"_BumpScale",out->normal_scale,MToonField::NormalScale);
        scalar(floats,"_RimLightingMix",out->rim_lighting_mix,MToonField::RimLightingMix,0,1);
        scalar(floats,"_RimFresnelPower",out->rim_power,MToonField::RimPower,0,100);
        scalar(floats,"_RimLift",out->rim_lift,MToonField::RimLift,0,1);
        scalar(floats,"_OutlineWidth",out->outline_width,MToonField::OutlineWidth,0);
        scalar(floats,"_OutlineScaledMaxDistance",out->outline_max_distance,MToonField::OutlineMaxDistance,0);
        scalar(floats,"_OutlineLightingMix",out->outline_lighting_mix,MToonField::OutlineLightingMix,0,1);
        scalar(floats,"_UvAnimScrollX",out->uv_scroll[0],MToonField::UvScrollX);
        scalar(floats,"_UvAnimScrollY",out->uv_scroll[1],MToonField::UvScrollY);
        scalar(floats,"_UvAnimRotation",out->uv_rotation,MToonField::UvRotation);
        scalar(floats,"_Cutoff",out->alpha_cutoff,MToonField::AlphaCutoff,0,1);
        const auto mode=[&](const char* key,int fallback,int low,int high,MToonField field){
            if(!floats.contains(key))return fallback;mark(field);return integer(floats.at(key),low,high,false);
        };
        out->render_mode=static_cast<MToonRenderMode>(mode("_BlendMode",int(out->render_mode),0,3,MToonField::RenderMode));
        out->cull_mode=static_cast<MToonCullMode>(mode("_CullMode",int(out->cull_mode),0,2,MToonField::CullMode));
        out->outline_mode=static_cast<MToonOutlineMode>(mode("_OutlineWidthMode",0,0,2,MToonField::OutlineMode));
        out->outline_cull_mode=static_cast<MToonCullMode>(mode("_OutlineCullMode",1,0,2,MToonField::OutlineCullMode));
        out->outline_color_mode=static_cast<std::uint8_t>(mode("_OutlineColorMode",0,0,1,MToonField::OutlineColorMode));
        out->depth_write=mode("_ZWrite",out->render_mode==MToonRenderMode::Transparent?0:1,0,1,MToonField::DepthWrite)!=0;
        out->transparent_with_zwrite=out->render_mode==MToonRenderMode::TransparentZWrite;
        if(e.contains("renderQueue")){out->legacy_render_queue=integer(e.at("renderQueue"),-1,5000,true);mark(MToonField::Queue);}
        const bool blended=out->render_mode==MToonRenderMode::Transparent||out->render_mode==MToonRenderMode::TransparentZWrite;
        out->source_blend=blended?5:1;out->destination_blend=blended?10:0;
        if(floats.contains("_SrcBlend"))out->source_blend=integer(floats.at("_SrcBlend"),0,10,false);
        if(floats.contains("_DstBlend"))out->destination_blend=integer(floats.at("_DstBlend"),0,10,false);
        if(floats.contains("_AlphaToMask"))out->alpha_to_coverage=integer(floats.at("_AlphaToMask"),0,1,false)!=0;
        const std::array<const char*,10> keys{{"_MainTex","_ShadeTexture","_BumpMap","_ReceiveShadowTexture","_ShadingGradeTexture","_EmissionMap","_RimTexture","_SphereAdd","_OutlineWidthTexture","_UvAnimMaskTexture"}};
        for(std::size_t i=0;i<keys.size();++i)tex(textures,keys[i],static_cast<MToonTextureSlot>(i),true);
        SurfaceTexture main;
        if(vectors.contains("_MainTex")){
            const auto& v=vectors.at("_MainTex");require(v.is_array()&&v.size()==4,"Invalid legacy MToon main UV transform");
            main.offset={numeric(v[0]),numeric(v[1])};main.scale={numeric(v[2]),numeric(v[3])};
            main.offset[1]=numeric(Json(1.-double(main.offset[1])-double(main.scale[1])));mark(MToonField::MainTransform);
        }
        for(std::size_t i=0;i<out->textures.size();++i)if(i!=static_cast<std::size_t>(MToonTextureSlot::Matcap)){
            out->textures[i].offset=main.offset;out->textures[i].scale=main.scale;out->textures[i].rotation=0;out->textures[i].texcoord=0;
        }
    }
    return out;
}
