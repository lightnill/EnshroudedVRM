// Retain only distinct UV sets referenced by authored MToon maps. Old vertex
// and surface streams keep their ABI/static transforms; MToon animates raw UVs.
void build_mtoon_geometry(const tinygltf::Model& model,const tinygltf::Primitive& source,
    Primitive& p,const MToonMaterial* material,const std::vector<std::uint32_t>& remap,
    const std::vector<SurfaceDuplicate>& duplicates){
    if(!material)return;
    std::set<int> selected;
    for(std::size_t i=0;i<material->textures.size();++i){const auto& t=material->textures[i];
        if(t.texture>=0&&i!=static_cast<std::size_t>(MToonTextureSlot::Matcap))selected.insert(t.texcoord);
    }
    for(int coordinate:selected){
        const auto name="TEXCOORD_"+std::to_string(coordinate);const auto found=source.attributes.find(name);
        require(found!=source.attributes.end(),"MToon textured primitive requires "+name+" UVs");
        const auto& a=indexed(model.accessors,found->second,"MToon UV accessor");
        require(a.count==remap.size()&&a.type==TINYGLTF_TYPE_VEC2&&
            ((a.componentType==5126&&!a.normalized)||((a.componentType==5121||a.componentType==5123)&&a.normalized)),"Invalid MToon UV accessor type/count");
        MToonUvChannel channel;channel.texcoord=coordinate;channel.values.resize(p.vertices.size());
        visit_accessor(model,found->second,TINYGLTF_TYPE_VEC2,true,[&](std::size_t i,const auto& row){
            require(std::abs(row[0])<=std::numeric_limits<float>::max()&&std::abs(row[1])<=std::numeric_limits<float>::max(),"MToon UV exceeds float range");
            const std::array<float,2> value{float(row[0]),float(row[1])};
            if(remap[i]!=std::numeric_limits<std::uint32_t>::max())channel.values[remap[i]]=value;
            const auto begin=std::lower_bound(duplicates.begin(),duplicates.end(),i,[](const auto& d,std::size_t index){return d.source<index;});
            for(auto d=begin;d!=duplicates.end()&&d->source==i;++d)channel.values[d->destination]=value;
        });
        p.mtoon_uvs.push_back(std::move(channel));
    }
}
