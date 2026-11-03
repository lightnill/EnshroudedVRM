// Raw JSON is checked before TinyGLTF can narrow or discard texture metadata.
float surface_number(const Json& v,double low=-1e12,double high=1e12){
    require(v.is_number(),"Invalid surface number type");const auto n=v.get<double>();
    require(std::isfinite(n)&&n>=low&&n<=high,"Invalid surface number range");return static_cast<float>(n);
}
SurfaceTexture surface_texture(const Json& texture,const Json& doc){
    require(texture.is_object(),"Invalid surface texture object");
    const auto index=number(texture.at("index"));
    require(doc.contains("textures")&&index<doc.at("textures").size(),"Invalid surface texture reference");
    const auto uv=validate_base_texture_uv(texture);SurfaceTexture out;out.texture=static_cast<int>(index);
    out.texcoord=static_cast<int>(uv.coordinate);out.rotation=static_cast<float>(uv.rotation);
    for(int c=0;c<2;++c){out.offset[c]=static_cast<float>(uv.offset[c]);out.scale[c]=static_cast<float>(uv.scale[c]);}
    return out;
}
bool surface_color_nonzero(const std::array<float,3>& c){return c[0]!=0||c[1]!=0||c[2]!=0;}
bool surface_rim_uses_uv(const MaterialSurface& s){
    return s.rim.texture>=0&&(surface_color_nonzero(s.rim_color)||
        (s.kind==SurfaceKind::MToon1&&s.matcap.texture>=0&&surface_color_nonzero(s.matcap_factor)));
}
std::shared_ptr<const MaterialSurface> parse_surface(const Json& doc,std::size_t index){
    const auto& m=doc.at("materials").at(index);require(m.is_object(),"Invalid surface material object");
    auto s=std::make_shared<MaterialSurface>();bool used=false;
    if(m.contains("normalTexture")){s->normal=surface_texture(m.at("normalTexture"),doc);used=true;
        const auto& n=m.at("normalTexture");if(n.contains("scale"))s->normal_scale=surface_number(n.at("scale"));}
    auto color=[&](const Json& value,std::array<float,3>& dest,bool legacy){
        require(value.is_array()&&value.size()==(legacy?4u:3u),"Invalid surface color shape");
        for(std::size_t c=0;c<value.size();++c){const auto v=surface_number(value[c],0,legacy?1e12:1);if(c<3)dest[c]=v;}
    };
    if(m.contains("extensions"))require(m.at("extensions").is_object(),"Invalid surface extensions");
    if(m.contains("extensions")&&m.at("extensions").contains("VRMC_materials_mtoon")){
        const auto& e=m.at("extensions").at("VRMC_materials_mtoon");
        require(e.is_object()&&e.at("specVersion")=="1.0","Unsupported surface MToon version");s->kind=SurfaceKind::MToon1;used=true;
        if(e.contains("matcapTexture"))s->matcap=surface_texture(e.at("matcapTexture"),doc);
        if(e.contains("rimMultiplyTexture"))s->rim=surface_texture(e.at("rimMultiplyTexture"),doc);
        if(e.contains("matcapFactor"))color(e.at("matcapFactor"),s->matcap_factor,false);
        if(e.contains("parametricRimColorFactor"))color(e.at("parametricRimColorFactor"),s->rim_color,false);
        if(e.contains("rimLightingMixFactor"))s->rim_lighting_mix=surface_number(e.at("rimLightingMixFactor"),0,1);
        if(e.contains("parametricRimFresnelPowerFactor"))s->rim_power=surface_number(e.at("parametricRimFresnelPowerFactor"),0);
        if(e.contains("parametricRimLiftFactor"))s->rim_lift=surface_number(e.at("parametricRimLiftFactor"));
    }else if(doc.contains("extensions")&&doc.at("extensions").contains("VRM")){
        const auto& vrm=doc.at("extensions").at("VRM");
        if(vrm.contains("materialProperties")){
            const auto& props=vrm.at("materialProperties");require(props.is_array(),"Invalid legacy surface properties");
            if(index<props.size()){
                const auto& e=props.at(index);require(e.is_object(),"Invalid legacy surface material");
                if(e.value("shader",std::string{})=="VRM/MToon"){
                    used=true;s->kind=SurfaceKind::MToon0;s->rim_lighting_mix=0;s->rim_power=1;
                    const auto floats=e.value("floatProperties",Json::object()),vectors=e.value("vectorProperties",Json::object()),textures=e.value("textureProperties",Json::object());
                    require(floats.is_object()&&vectors.is_object()&&textures.is_object(),"Invalid legacy surface property map");
                    auto scalar=[&](const char* key,float& v,double lo=-1e12,double hi=1e12){if(floats.contains(key))v=surface_number(floats.at(key),lo,hi);};
                    scalar("_BumpScale",s->normal_scale);scalar("_RimLightingMix",s->rim_lighting_mix,0,1);
                    scalar("_RimFresnelPower",s->rim_power,0,100);scalar("_RimLift",s->rim_lift,0,1);
                    if(vectors.contains("_RimColor"))color(vectors.at("_RimColor"),s->rim_color,true);
                    auto binding=[&](const char* key,SurfaceTexture& t){if(!textures.contains(key))return;
                        const auto& v=textures.at(key);require(v.is_number_integer()&&!v.is_boolean(),"Invalid legacy surface texture type");
                        if(v==Json(-1)){t=SurfaceTexture{};return;}t=surface_texture(Json{{"index",v}},doc);};
                    binding("_BumpMap",s->normal);binding("_SphereAdd",s->matcap);binding("_RimTexture",s->rim);
                    // UniVRM MToonCore samples normal/rim with _MainTex's UV,
                    // ignoring per-map ST. Stored legacy offsets are Unity UVs;
                    // conjugate the affine ST by v -> 1-v for our glTF UVs.
                    SurfaceTexture main;
                    if(vectors.contains("_MainTex")){
                        const auto& v=vectors.at("_MainTex");require(v.is_array()&&v.size()==4,"Invalid legacy surface UV transform");
                        main.offset={surface_number(v[0]),surface_number(v[1])};main.scale={surface_number(v[2]),surface_number(v[3])};
                        main.offset[1]=surface_number(Json(1.-double(main.offset[1])-double(main.scale[1])));
                    }
                    for(auto* t:{&s->normal,&s->rim}){t->offset=main.offset;t->scale=main.scale;t->rotation=0;t->texcoord=0;}
                }
            }
        }
    }
    return used?s:std::shared_ptr<const MaterialSurface>{};
}
