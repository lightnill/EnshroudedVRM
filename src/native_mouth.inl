// Optional skin-asset compilation. Original influence boxes stay untouched so
// an unsafe optional pose can be packed again with neutral mouth weights.
void compile_native_mouth(const vrm::Avatar& avatar,NativeSkinAsset& out)noexcept {
    try{
        out.mouth_limitation=avatar.mouth_limitation;if(!avatar.mouth)return;
        const auto& data=*avatar.mouth;
        require(data.available&&!(data.available&~31u)&&!(data.binary&~data.available)&&
            !data.records.empty()&&data.records.size()<=vrm::max_mouth_records&&data.records.capacity()<=vrm::max_mouth_records&&
            !data.ranges.empty()&&data.ranges.size()<=vrm::max_mouth_ranges&&data.ranges.capacity()<=vrm::max_mouth_ranges,"Invalid optional mouth layout");
        struct Flat {std::uint32_t node,primitive,native,first,count;bool blend;};
        std::vector<Flat> flat;flat.reserve(out.layout->draws.size());std::uint32_t cursor=0;
        for(std::size_t n=0;n<out.nodes.size();++n){const auto& node=out.nodes[n];const auto& mesh=avatar.meshes.at(avatar.nodes.at(node.node).mesh);
            for(std::size_t p=0;p<mesh.primitives.size();++p){const auto& primitive=mesh.primitives[p];
                const bool blend=primitive.material>=0&&avatar.materials.at(primitive.material).alpha==vrm::AlphaMode::Blend;
                flat.push_back({node.node,std::uint32_t(p),std::uint32_t(n),cursor,std::uint32_t(primitive.vertices.size()),blend});cursor+=std::uint32_t(primitive.vertices.size());}}
        require(cursor==out.vertices.size()&&flat.size()<=4096,"Optional mouth flatten mismatch");
        const auto find=[&](const vrm::MouthPrimitiveRange& range)->const Flat& {
            const auto key=std::make_pair(range.node,range.primitive);
            const auto at=std::lower_bound(flat.begin(),flat.end(),key,[](const Flat& value,const auto& match){return std::make_pair(value.node,value.primitive)<match;});
            require(at!=flat.end()&&at->node==range.node&&at->primitive==range.primitive,"Optional mouth references inactive primitive");return *at;
        };
        std::vector<std::pair<std::uint32_t,std::uint32_t>> joints;joints.reserve(data.records.size()*4);
        std::vector<std::uint32_t> nodes;nodes.reserve(data.ranges.size());
        cursor=0;std::pair<std::uint32_t,std::uint32_t> previous{};bool first=true;std::uint8_t available=0;
        for(const auto& range:data.ranges){const auto key=std::make_pair(range.node,range.primitive);
            require((first||key>previous)&&range.first==cursor&&range.count&&range.count<=data.records.size()-cursor,"Optional mouth range order");first=false;previous=key;
            const auto& section=find(range);nodes.push_back(section.native);std::uint32_t prior=0;bool first_vertex=true;
            for(std::uint32_t r=0;r<range.count;++r){const auto& record=data.records[cursor++];
                require(record.vertex<section.count&&(first_vertex||record.vertex>prior),"Optional mouth retained index order");first_vertex=false;prior=record.vertex;
                const auto& vertex=out.vertices.at(section.first+record.vertex).vertex;
                require(vrm::mouth_position_interval(vertex,&record).valid,"Optional mouth interval invalid");std::uint8_t used=0;
                for(unsigned axis=0;axis<3;++axis){double normal=std::abs(double(vertex.normal[axis]));
                    for(unsigned channel=0;channel<5;++channel){const auto p=record.position[channel][axis],v=record.normal[channel][axis];
                        require(std::isfinite(p)&&std::isfinite(v),"Optional mouth delta nonfinite");normal+=std::abs(double(v));if(p!=0||v!=0)used|=std::uint8_t(1u<<channel);}
                    require(normal<=std::numeric_limits<float>::max()/8.,"Optional mouth normal range");}
                require(used&&!(used&~data.available),"Optional mouth availability mismatch");available|=used;
                if(section.blend&&out.vertices[section.first+record.vertex].skinned)
                    for(unsigned j=0;j<4;++j)if(vertex.weights[j]>0)joints.emplace_back(section.native,vertex.joints[j]);
            }
        }
        require(cursor==data.records.size()&&available==data.available,"Optional mouth records unused");
        std::sort(nodes.begin(),nodes.end());nodes.erase(std::unique(nodes.begin(),nodes.end()),nodes.end());
        std::sort(joints.begin(),joints.end());joints.erase(std::unique(joints.begin(),joints.end()),joints.end());
        const auto influence=nodes.size()*sizeof(NativeSkinMouthNode)+joints.size()*sizeof(NativeSkinMouthJoint);
        const auto storage=vrm::mouth_storage_layout(out.vertices.size(),data.records.size(),data.records.capacity(),data.ranges.capacity(),influence);
        require(storage.valid,"Optional mouth storage exceeds budget");
        std::vector<std::uint32_t> lookup(out.vertices.size(),UINT32_MAX);std::vector<NativeSkinMouthNode> bounds;bounds.reserve(nodes.size());
        for(auto n:nodes){NativeSkinMouthNode node;node.node=n;
            const auto begin=std::lower_bound(joints.begin(),joints.end(),std::make_pair(n,0u));
            const auto end=std::upper_bound(joints.begin(),joints.end(),std::make_pair(n,UINT32_MAX));node.joints.reserve(std::size_t(end-begin));
            for(auto at=begin;at!=end;++at){require(at->second<out.nodes.at(n).joint_blend.size(),"Optional mouth joint out of range");node.joints.push_back({at->second,{}});}
            bounds.push_back(std::move(node));}
        for(const auto& range:data.ranges){const auto& section=find(range);
            auto& node=*std::lower_bound(bounds.begin(),bounds.end(),section.native,[](const auto& value,std::uint32_t n){return value.node<n;});
            for(std::uint32_t r=0;r<range.count;++r){const auto index=range.first+r;const auto& record=data.records[index];const auto global=section.first+record.vertex;
                lookup[global]=index;const auto& source=out.vertices[global];const auto interval=vrm::mouth_position_interval(source.vertex,&record);
                point(node.safety,interval.low);point(node.safety,interval.high);
                if(!section.blend)continue;
                if(!source.skinned){point(node.static_blend,interval.low);point(node.static_blend,interval.high);continue;}
                for(unsigned j=0;j<4;++j)if(source.vertex.weights[j]>0){const auto id=source.vertex.joints[j];
                    auto& joint=*std::lower_bound(node.joints.begin(),node.joints.end(),id,[](const auto& value,std::uint32_t match){return value.joint<match;});
                    point(joint.bounds,interval.low);point(joint.bounds,interval.high);}
            }
        }
        out.mouth=data.available?avatar.mouth:nullptr;out.mouth_lookup=std::move(lookup);out.mouth_nodes=std::move(bounds);out.mouth_peak_bytes=storage.optional_peak_bytes;
    }catch(...){out.mouth.reset();decltype(out.mouth_lookup){}.swap(out.mouth_lookup);decltype(out.mouth_nodes){}.swap(out.mouth_nodes);out.mouth_peak_bytes=0;
        try{out.mouth_limitation="Unsupported mouth layout or storage";}catch(...){} }
}
// A small node-level magnitude proof protects opaque mouth geometry too, without
// revisiting vertices. Rejection only disables optional mouth work for this pose.
bool mouth_pose_safe(const NativeSkinMouthNode& mouth,const NativeSkinNode& node,const NativeSkinBatch& batch,const std::vector<vrm::Matrix>& matrices){
    std::array<double,3> position{};for(unsigned c=0;c<3;++c)position[c]=std::max(std::abs(mouth.safety.low[c]),std::abs(mouth.safety.high[c]));
    std::array<std::array<double,4>,3> bone{},transform{};
    if(node.skinned){
        for(std::uint32_t n=0;n<batch.count;++n)for(unsigned r=0;r<3;++r)for(unsigned c=0;c<4;++c)
            bone[r][c]=std::max(bone[r][c],std::abs(double(matrices.at(batch.base+n)[4*c+r]))*1.001);
    }else for(unsigned c=0;c<3;++c)bone[c][c]=1;
    constexpr double limit=std::numeric_limits<float>::max()/64.;
    for(unsigned r=0;r<3;++r)for(unsigned c=0;c<4;++c){
        for(unsigned k=0;k<3;++k)transform[r][c]+=std::abs(double(batch.world[4*k+r]))*bone[k][c];
        if(c==3)transform[r][c]+=std::abs(double(batch.world[12+r]));
        if(!std::isfinite(transform[r][c])||transform[r][c]>limit||bone[r][c]>limit)return false;
    }
    for(unsigned r=0;r<3;++r){double work=transform[r][3];for(unsigned c=0;c<3;++c)work+=transform[r][c]*position[c];if(!std::isfinite(work)||work>limit)return false;}
    return true;
}
