// Optional standard-mouth import. Keep failures separate from base glTF import;
// accessor storage is still preflighted by the existing importer before here.
class MouthImporter {
    struct Bind {unsigned channel=0;int target=0;float weight=0;};
    using Rows=std::map<std::uint32_t,MouthDeltaRecord>;
    const tinygltf::Model& model_;
    const Avatar& avatar_;
    const ImportLimits& limits_;
    std::map<int,std::map<std::uint32_t,std::vector<Bind>>> requests_;
    std::map<std::pair<std::uint32_t,std::uint32_t>,Rows> pending_;
    std::array<bool,5> seen_{};
    std::array<std::size_t,5> channel_bindings_{};
    std::size_t bindings_=0,expanded_=0,visits_=0,records_=0;
    std::uint8_t binary_=0;
    bool material_effects_=false;
    const char* failure_=nullptr;

    void discard(const char* reason)noexcept {failure_=reason;requests_.clear();pending_.clear();}
    void expand(int mesh,std::uint32_t node,const Bind& bind){
        require(expanded_<max_mouth_expanded_bindings,"Expanded mouth bindings exceed limit");++expanded_;
        requests_[mesh][node].push_back(bind);
    }
    void expression(const Json& value,unsigned channel,bool legacy){
        require(value.is_object()&&!seen_[channel],"Ambiguous mouth expression");seen_[channel]=true;
        if(value.contains("isBinary")){require(value.at("isBinary").is_boolean(),"Invalid mouth binary flag");
            if(value.at("isBinary").get<bool>())binary_|=std::uint8_t(1u<<channel);}
        const auto material=[&](const char* key){if(value.contains(key)){
            require(value.at(key).is_array(),"Invalid mouth material metadata");material_effects_|=!value.at(key).empty();}};
        if(legacy)material("materialValues");else {material("materialColorBinds");material("textureTransformBinds");}
        const char* key=legacy?"binds":"morphTargetBinds";if(!value.contains(key))return;
        const auto& list=value.at(key);require(list.is_array()&&list.size()<=max_mouth_bindings,"Invalid mouth bindings");
        for(const auto& item:list){
            require(item.is_object()&&bindings_<std::min(limits_.mouth_bindings,max_mouth_bindings)&&
                channel_bindings_[channel]<max_mouth_channel_bindings,"Mouth binding limit");++bindings_;++channel_bindings_[channel];
            const auto target=number(item.at("index")),owner=number(item.at(legacy?"mesh":"node"));
            require(target<=static_cast<std::size_t>(std::numeric_limits<int>::max()),"Invalid mouth target index");
            require(item.at("weight").is_number(),"Invalid mouth binding weight");const auto weight=item.at("weight").get<double>();
            require(std::isfinite(weight)&&weight>=0&&weight<=(legacy?100:1),"Invalid mouth binding weight");
            int mesh=-1;
            if(legacy){require(owner<model_.meshes.size(),"Invalid mouth mesh");mesh=int(owner);}
            else {require(owner<avatar_.nodes.size()&&avatar_.nodes[owner].mesh>=0,"Invalid mouth node");mesh=avatar_.nodes[owner].mesh;}
            for(const auto& primitive:model_.meshes.at(mesh).primitives)require(target<primitive.targets.size(),"Invalid primitive mouth target");
            Bind bind{channel,int(target),float(weight/(legacy?100:1))};if(!bind.weight)continue;
            if(legacy){for(std::size_t n=0;n<avatar_.nodes.size();++n)if(avatar_.nodes[n].active&&avatar_.nodes[n].mesh==mesh)expand(mesh,std::uint32_t(n),bind);}
            else if(avatar_.nodes[owner].active)expand(mesh,std::uint32_t(owner),bind);
        }
    }
    void parse(const Json& doc){
        const auto& extensions=doc.at("extensions");
        const char* old_names[]={"a","i","u","e","o"};const char* new_names[]={"aa","ih","ou","ee","oh"};
        if(avatar_.version==VrmVersion::V0){
            const auto& vrm=extensions.at("VRM");if(!vrm.contains("blendShapeMaster"))return;
            const auto& master=vrm.at("blendShapeMaster");require(master.is_object(),"Invalid blend shape metadata");
            if(!master.contains("blendShapeGroups"))return;
            const auto& groups=master.at("blendShapeGroups");require(groups.is_array()&&groups.size()<=512,"Mouth expression metadata limit");
            for(const auto& group:groups){require(group.is_object(),"Invalid blend shape group");if(!group.contains("presetName"))continue;
                require(group.at("presetName").is_string(),"Invalid blend shape preset");const auto name=group.at("presetName").get<std::string>();
                for(unsigned channel=0;channel<5;++channel)if(name==old_names[channel])expression(group,channel,true);}
        }else{
            const auto& vrm=extensions.at("VRMC_vrm");if(!vrm.contains("expressions"))return;
            const auto& expressions=vrm.at("expressions");require(expressions.is_object(),"Invalid expression metadata");
            if(!expressions.contains("preset"))return;
            const auto& preset=expressions.at("preset");require(preset.is_object()&&preset.size()<=512,"Mouth expression metadata limit");
            for(unsigned channel=0;channel<5;++channel)if(preset.contains(new_names[channel]))expression(preset.at(new_names[channel]),channel,false);
        }
    }
    void accessor_rows(int accessor,std::size_t count,const Bind& bind,std::uint32_t node,std::uint32_t primitive,
                       const std::vector<std::uint32_t>& remap,bool normal){
        const auto& source=indexed(model_.accessors,accessor,"mouth accessor");
        require(source.componentType==5126&&!source.normalized&&source.type==TINYGLTF_TYPE_VEC3&&source.count==count,"Invalid mouth accessor type/count");
        const auto limit=std::min(limits_.mouth_source_vertex_visits,max_mouth_source_visits);
        require(visits_<=limit&&count<=limit-visits_,"Mouth source work limit");visits_+=count;
        visit_accessor(model_,accessor,TINYGLTF_TYPE_VEC3,true,[&](std::size_t i,const auto& row){
            if(remap[i]==std::numeric_limits<std::uint32_t>::max()||(row[0]==0&&row[1]==0&&row[2]==0))return;
            const auto key=std::make_pair(node,primitive);auto range=pending_.find(key);
            if(range==pending_.end()){require(pending_.size()<std::min(limits_.mouth_ranges,max_mouth_ranges),"Mouth range limit");range=pending_.emplace(key,Rows{}).first;}
            auto record=range->second.find(remap[i]);
            if(record==range->second.end()){require(records_<std::min(limits_.mouth_records,max_mouth_records),"Mouth record limit");
                MouthDeltaRecord fresh;fresh.vertex=remap[i];record=range->second.emplace(remap[i],fresh).first;++records_;}
            auto& delta=(normal?record->second.normal:record->second.position)[bind.channel];
            for(unsigned c=0;c<3;++c){const auto value=double(delta[c])+row[c]*bind.weight;
                if(!std::isfinite(value)||std::abs(value)>std::numeric_limits<float>::max())fail("Mouth delta accumulation overflow");
                delta[c]=float(value);}
        });
    }
    static std::uint8_t channels(const MouthDeltaRecord& record)noexcept {
        std::uint8_t result=0;for(unsigned channel=0;channel<5;++channel)for(unsigned c=0;c<3;++c)
            if(record.position[channel][c]!=0||record.normal[channel][c]!=0)result|=std::uint8_t(1u<<channel);
        return result;
    }
public:
    MouthImporter(const tinygltf::Model& model,const Json& doc,const Avatar& avatar,const ImportLimits& limits)noexcept:
        model_(model),avatar_(avatar),limits_(limits){try{parse(doc);}catch(...){discard("Unsupported mouth metadata");}}
    void primitive(std::size_t mesh,std::size_t index,const tinygltf::Primitive& source,const std::vector<std::uint32_t>& remap)noexcept {
        if(failure_)return;
        try{
            const auto matching=requests_.find(int(mesh));if(matching==requests_.end())return;
            for(const auto& node:matching->second)for(const auto& bind:node.second){
                const auto& target=source.targets.at(std::size_t(bind.target));require(target.count("POSITION"),"Mouth target requires position deltas");
                accessor_rows(target.at("POSITION"),remap.size(),bind,node.first,std::uint32_t(index),remap,false);
                if(target.count("NORMAL"))accessor_rows(target.at("NORMAL"),remap.size(),bind,node.first,std::uint32_t(index),remap,true);
            }
        }catch(...){discard("Unsupported mouth deltas or limits");}
    }
    void finish(Avatar& avatar)noexcept {
        try{
            if(failure_){avatar.mouth_limitation=failure_;return;}
            auto result=std::make_shared<MouthData>();result->records.reserve(records_);result->ranges.reserve(pending_.size());
            for(const auto& item:pending_){MouthPrimitiveRange range{item.first.first,item.first.second,std::uint32_t(result->records.size()),0};
                const auto& base=avatar.meshes.at(avatar.nodes.at(range.node).mesh).primitives.at(range.primitive);
                for(const auto& row:item.second){const auto used=channels(row.second);if(!used)continue;
                    const auto& vertex=base.vertices.at(row.first);
                    for(unsigned c=0;c<3;++c){double position=std::abs(double(vertex.position[c])),normal=std::abs(double(vertex.normal[c]));
                        for(unsigned channel=0;channel<5;++channel){position+=std::abs(double(row.second.position[channel][c]));normal+=std::abs(double(row.second.normal[channel][c]));}
                        if(position>std::numeric_limits<float>::max()/16.||normal>std::numeric_limits<float>::max()/16.)fail("Unsafe mouth arithmetic range");}
                    result->records.push_back(row.second);result->available|=used;++range.count;
                }
                if(range.count)result->ranges.push_back(range);
            }
            if(!result->available){if(material_effects_)avatar.mouth_limitation="Mouth shapes need unsupported material effects";return;}
            result->binary=binary_&result->available;avatar.mouth=std::move(result);
            avatar.mouth_limitation=material_effects_?"Some mouth material effects are unsupported":"";
        }catch(...){avatar.mouth.reset();try{avatar.mouth_limitation="Unsupported mouth deltas or limits";}catch(...){} }
    }
};
