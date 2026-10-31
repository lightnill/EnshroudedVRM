// Retain only referenced vertices. Stream every original attribute row so bad
// discarded positions/weights/joints still reject, without full source arrays.
void build_meshes(const tinygltf::Model&model,const Json&doc,Avatar&avatar,const ImportLimits&limits){
    MouthImporter mouth(model,doc,avatar,limits);std::size_t mesh_index=0;
    struct BindingSummary {std::size_t count=0;std::uint16_t max_joint=0;};
    std::vector<std::vector<BindingSummary>> bindings;
    std::size_t source_visits=0;bool morphs=false,repaired_uvs=false,reconstructed_normals=false;
    constexpr auto absent=std::numeric_limits<std::uint32_t>::max();
    for(const auto&source:model.meshes){
        for(double w:source.weights)require(w==0,"Nonzero default morph weights are not implemented");
        Mesh mesh;std::vector<BindingSummary> mesh_bindings;std::size_t primitive_index=0;
        for(const auto&p:source.primitives){
            require(p.mode==-1||p.mode==4,"Only triangle primitives are supported");
            require(p.extensions.empty(),"Compressed/extended mesh primitives are not implemented");
            require(p.attributes.count("POSITION"),"Mesh needs POSITION");
            const int position=p.attributes.at("POSITION");
            const auto&position_accessor=indexed(model.accessors,position,"position accessor");
            require(position_accessor.componentType==5126&&!position_accessor.normalized,"Positions must be float VEC3");
            const auto count=position_accessor.count;
            require(source_visits<=limits.source_vertex_visits&&count<=limits.source_vertex_visits-source_visits,"Source vertex work exceeds limit");
            source_visits+=count;
            auto attribute=[&](const std::string&name,const char*label){
                const auto at=p.attributes.find(name);if(at==p.attributes.end())return -1;
                const auto&a=indexed(model.accessors,at->second,label);
                require(a.count==count,std::string(label)+"/position count mismatch");return at->second;
            };
            BaseTextureUv texture_uv;
            if(p.material>=0){
                const auto&material=doc.at("materials").at(static_cast<std::size_t>(p.material));
                if(material.contains("pbrMetallicRoughness")&&material.at("pbrMetallicRoughness").contains("baseColorTexture"))
                    texture_uv=validate_base_texture_uv(material.at("pbrMetallicRoughness").at("baseColorTexture"));
            }
            const auto uv_name=std::string("TEXCOORD_")+std::to_string(texture_uv.coordinate);
            const int normal=attribute("NORMAL","Normal"),uv=attribute(uv_name,"UV");
            Primitive primitive;primitive.material=p.material;
            if(p.material!=-1)indexed(avatar.materials,p.material,"primitive material");
            if(p.material!=-1&&avatar.materials[p.material].base_texture!=-1)
                require(uv>=0,"Textured primitive requires "+uv_name+" UVs");
            require(p.attributes.count("JOINTS_0")==p.attributes.count("WEIGHTS_0"),"Incomplete mesh skin attributes");
            primitive.skinned=p.attributes.count("JOINTS_0")!=0;
            require(!p.attributes.count("JOINTS_1")&&!p.attributes.count("WEIGHTS_1"),"More than four skin weights are unsupported");
            const int joints=attribute("JOINTS_0","Skin"),weights=attribute("WEIGHTS_0","Skin");
            if(joints>=0){
                const auto&a=indexed(model.accessors,joints,"joint accessor");
                require((a.componentType==5121||a.componentType==5123)&&!a.normalized,"Invalid joint accessor type");
            }
            std::size_t index_count=count;
            if(p.indices!=-1){
                const auto&a=indexed(model.accessors,p.indices,"index accessor");index_count=a.count;
                require((a.componentType==5121||a.componentType==5123||a.componentType==5125)&&!a.normalized,"Invalid triangle index type");
            }
            require(index_count%3==0&&avatar.triangle_count<=limits.indices/3&&index_count<=limits.indices-avatar.triangle_count*3,"Triangle indices exceed limit or are incomplete");
            primitive.indices.resize(index_count);
            if(p.indices!=-1)visit_accessor(model,p.indices,TINYGLTF_TYPE_SCALAR,true,[&](std::size_t i,const auto&row){
                if(!(row[0]>=0&&row[0]<count))fail("Triangle index exceeds vertex count");
                primitive.indices[i]=static_cast<std::uint32_t>(row[0]);
            });
            else for(std::size_t i=0;i<count;++i)primitive.indices[i]=static_cast<std::uint32_t>(i);
            avatar.triangle_count+=index_count/3;
            std::vector<std::uint32_t> remap(count,absent);std::size_t retained=0;
            for(auto index:primitive.indices)if(remap[index]==absent){remap[index]=0;++retained;}
            require(avatar.vertex_count<=limits.vertices&&retained<=limits.vertices-avatar.vertex_count,"Retained vertex count exceeds limit");
            primitive.vertices.resize(retained);std::uint32_t next=0;
            for(auto&index:remap)if(index!=absent)index=next++;
            for(auto&index:primitive.indices)index=remap[index];
            avatar.vertex_count+=retained;
            visit_accessor(model,position,TINYGLTF_TYPE_VEC3,true,[&](std::size_t i,const auto&row){
                if(remap[i]!=absent)for(int c=0;c<3;++c)primitive.vertices[remap[i]].position[c]=static_cast<float>(row[c]);
            });
            if(normal>=0)visit_accessor(model,normal,TINYGLTF_TYPE_VEC3,false,[&](std::size_t i,const auto&row){
                if(remap[i]==absent)return;
                auto&out=primitive.vertices[remap[i]].normal;
                const auto length=std::hypot(row[0],row[1],row[2]);
                if(std::isfinite(length)&&length>0)for(int c=0;c<3;++c)out[c]=static_cast<float>(row[c]/length);
            });
            const auto cosine=std::cos(texture_uv.rotation),sine=std::sin(texture_uv.rotation);
            if(uv>=0)visit_accessor(model,uv,TINYGLTF_TYPE_VEC2,false,[&](std::size_t i,const auto&row){
                if(remap[i]==absent)return;
                auto u=row[0],w=row[1];if(!std::isfinite(u)||!std::isfinite(w)){u=w=0;repaired_uvs=true;}
                const double transformed[2]={texture_uv.offset[0]+cosine*texture_uv.scale[0]*u-sine*texture_uv.scale[1]*w,
                    texture_uv.offset[1]+sine*texture_uv.scale[0]*u+cosine*texture_uv.scale[1]*w};
                for(int c=0;c<2;++c){
                    if(!std::isfinite(transformed[c])||std::abs(transformed[c])>std::numeric_limits<float>::max())fail("Transformed UV contains non-finite value");
                    primitive.vertices[remap[i]].uv[c]=static_cast<float>(transformed[c]);
                }
            });
            BindingSummary binding;binding.count=count;
            if(joints>=0){
                visit_accessor(model,joints,TINYGLTF_TYPE_VEC4,true,[&](std::size_t i,const auto&row){
                    for(int c=0;c<4;++c){
                        const auto joint=static_cast<std::uint16_t>(row[c]);binding.max_joint=std::max(binding.max_joint,joint);
                        if(remap[i]!=absent)primitive.vertices[remap[i]].joints[c]=joint;
                    }
                });
                visit_accessor(model,weights,TINYGLTF_TYPE_VEC4,true,[&](std::size_t i,const auto&row){
                    double sum=0;for(int c=0;c<4;++c){if(!(row[c]>=0&&row[c]<=1.001))fail("Invalid vertex weight");sum+=row[c];}
                    if(!(sum>1e-8))fail("Zero vertex weight sum");
                    if(remap[i]!=absent)for(int c=0;c<4;++c)primitive.vertices[remap[i]].weights[c]=static_cast<float>(row[c]/sum);
                });
            }
            reconstructed_normals=repair_primitive_normals(primitive)||reconstructed_normals;
            mouth.primitive(mesh_index,primitive_index++,p,remap);
            morphs=morphs||!p.targets.empty();mesh.primitives.push_back(std::move(primitive));mesh_bindings.push_back(binding);
        }
        avatar.meshes.push_back(std::move(mesh));bindings.push_back(std::move(mesh_bindings));++mesh_index;
    }
    bool drawable=false;std::size_t binding_visits=0;std::set<std::pair<int,int>> validated_bindings;
    for(std::size_t i=0;i<avatar.nodes.size();++i){
        const auto&node=avatar.nodes[i];
        for(double w:model.nodes[i].weights)require(w==0,"Nonzero node morph weights are not implemented");
        if(node.mesh==-1)continue;
        if(validated_bindings.emplace(node.mesh,node.skin).second){
            const auto&mesh=avatar.meshes[node.mesh];
            for(std::size_t p=0;p<mesh.primitives.size();++p){
                if(mesh.primitives[p].skinned){
                    const auto&skin=indexed(avatar.skins,node.skin,"mesh skin");const auto&binding=bindings[node.mesh][p];
                    require(binding_visits<=limits.source_vertex_visits&&binding.count<=limits.source_vertex_visits-binding_visits,"Mesh skin validation exceeds source work limit");
                    binding_visits+=binding.count;require(binding.max_joint<skin.joints.size(),"Vertex joint exceeds skin palette");
                }else require(node.skin==-1,"Skinned node has no joint attributes");
            }
        }
        drawable=drawable||node.active;
    }
    require(drawable&&avatar.vertex_count>0&&avatar.triangle_count>0,"No drawable VRM geometry");
    if(reconstructed_normals)avatar.warnings.push_back("Reconstructed missing or invalid mesh normals from triangle geometry.");
    if(repaired_uvs)avatar.warnings.push_back("Nonfinite UV coordinates replaced with a finite fallback; texture placement may differ.");
    mouth.finish(avatar);
    if(morphs)avatar.warnings.push_back(avatar.mouth?"Only standard mouth morphs are supported; other expressions are deferred.":"Expressions and morph targets are deferred; base geometry imported.");
}
