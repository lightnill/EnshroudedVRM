struct SurfaceDuplicate {std::uint32_t source=0,destination=0;};
struct SurfaceMikk {
    const Primitive* primitive=nullptr;
    std::vector<std::array<float,4>> corners;
    double position_scale=1,uv_scale=1;
    static SurfaceMikk& data(const SMikkTSpaceContext* c){return *static_cast<SurfaceMikk*>(c->m_pUserData);}
    static std::uint32_t index(const SMikkTSpaceContext* c,int f,int v){return data(c).primitive->indices[std::size_t(f)*3+v];}
    static int faces(const SMikkTSpaceContext* c){return int(data(c).primitive->indices.size()/3);}
    static int vertices(const SMikkTSpaceContext*,int){return 3;}
    static void position(const SMikkTSpaceContext* c,float out[],int f,int v){const auto& d=data(c);const auto& p=d.primitive->vertices[index(c,f,v)].position;for(int a=0;a<3;++a)out[a]=float(double(p[a])*d.position_scale);}
    static void normal(const SMikkTSpaceContext* c,float out[],int f,int v){const auto& p=data(c).primitive->vertices[index(c,f,v)].normal;std::copy(p.begin(),p.end(),out);}
    static void uv(const SMikkTSpaceContext* c,float out[],int f,int v){const auto& d=data(c);const auto& p=d.primitive->surface_vertices[index(c,f,v)];for(int a=0;a<2;++a)out[a]=float(double(p.normal_uv[a])*d.uv_scale);}
    static void tangent(const SMikkTSpaceContext* c,const float t[],float sign,int f,int v){auto& o=data(c).corners[std::size_t(f)*3+v];o={t[0],t[1],t[2],sign};}
};
bool surface_normal_effective(const MaterialSurface& s){return s.normal.texture>=0&&s.normal_scale!=0;}
void build_surface_geometry(const tinygltf::Model& model,const tinygltf::Primitive& source,
        Primitive& p,const MaterialSurface* surface,const std::vector<std::uint32_t>& remap,
        std::vector<SurfaceDuplicate>& duplicates,Avatar& avatar,const ImportLimits& limits){
    if(!surface)return;
    const auto absent=std::numeric_limits<std::uint32_t>::max();
    auto selected=[&](const SurfaceTexture& binding){
        if(binding.texture<0)return -1;
        const auto key="TEXCOORD_"+std::to_string(binding.texcoord);auto at=source.attributes.find(key);
        require(at!=source.attributes.end(),"Surface textured primitive requires "+key+" UVs");
        const auto& a=indexed(model.accessors,at->second,"surface UV accessor");
        require(a.count==remap.size()&&a.type==TINYGLTF_TYPE_VEC2&&
            ((a.componentType==5126&&!a.normalized)||((a.componentType==5121||a.componentType==5123)&&a.normalized)),"Invalid surface UV accessor type/count");
        return at->second;
    };
    const int normal_uv=selected(surface->normal),rim_uv=selected(surface->rim);
    const bool normals=surface_normal_effective(*surface),rim=surface_rim_uses_uv(*surface);
    if(normals||rim)p.surface_vertices.resize(p.vertices.size());
    auto uv=[&](int accessor,const SurfaceTexture& t,bool normal){if(accessor<0)return;
        const double co=std::cos(double(t.rotation)),si=std::sin(double(t.rotation));
        visit_accessor(model,accessor,TINYGLTF_TYPE_VEC2,true,[&](std::size_t i,const auto& row){
            const double x=t.offset[0]+co*t.scale[0]*row[0]-si*t.scale[1]*row[1];
            const double y=t.offset[1]+si*t.scale[0]*row[0]+co*t.scale[1]*row[1];
            require(std::isfinite(x)&&std::isfinite(y)&&std::abs(x)<=std::numeric_limits<float>::max()&&std::abs(y)<=std::numeric_limits<float>::max(),"Invalid transformed surface UV");
            if(remap[i]!=absent&&!p.surface_vertices.empty()){auto& v=p.surface_vertices[remap[i]];auto* dest=normal?v.normal_uv:v.rim_uv;dest[0]=float(x);dest[1]=float(y);}
        });
    };
    // Validate every authored selected UV even when a zero factor disables
    // sampling. Inactive effects still allocate no persistent side stream.
    uv(normal_uv,surface->normal,true);
    uv(rim_uv,surface->rim,false);
    if(!normals)return;
    const auto authored=source.attributes.find("TANGENT");
    const auto& t=surface->normal;
    const bool use_authored=authored!=source.attributes.end()&&t.texcoord==0&&t.rotation==0&&t.scale==std::array<float,2>{1,1};
    if(authored!=source.attributes.end()){
        const auto& a=indexed(model.accessors,authored->second,"surface tangent accessor");
        require(a.count==remap.size()&&a.type==TINYGLTF_TYPE_VEC4&&a.componentType==5126&&!a.normalized,"Invalid surface tangent accessor type/count");
        visit_accessor(model,authored->second,TINYGLTF_TYPE_VEC4,true,[&](std::size_t i,const auto& row){
            require(row[3]==1||row[3]==-1,"Invalid surface tangent handedness");
            require(std::hypot(row[0],row[1],row[2])>1e-12,"Invalid surface tangent direction");
            if(!use_authored||remap[i]==absent)return;
            const auto& n=p.vertices[remap[i]].normal;double dot=0;for(int c=0;c<3;++c)dot+=row[c]*n[c];
            std::array<double,3> tangent;for(int c=0;c<3;++c)tangent[c]=row[c]-dot*n[c];
            const auto length=std::hypot(tangent[0],tangent[1],tangent[2]);auto& out=p.surface_vertices[remap[i]].tangent;
            if(length>1e-12){for(int c=0;c<3;++c)out[c]=float(tangent[c]/length);out[3]=float(row[3]);}
        });
    }
    SurfaceMikk mikk{&p,std::vector<std::array<float,4>>(p.indices.size())};
    if(use_authored){
        for(std::size_t c=0;c<p.indices.size();++c){const auto& t=p.surface_vertices[p.indices[c]].tangent;std::copy(t,t+4,mikk.corners[c].begin());}
    }else {
        // Uniform positive changes of units preserve the tangent basis, while
        // avoiding overflow/underflow in canonical Mikk's float intermediates.
        // Original coordinates and the published UV stream stay untouched.
        double position_max=0,uv_max=0;
        for(const auto& v:p.vertices)for(auto x:v.position)position_max=std::max(position_max,std::abs(double(x)));
        for(const auto& v:p.surface_vertices)for(auto x:v.normal_uv)uv_max=std::max(uv_max,std::abs(double(x)));
        if(position_max>0&&(position_max>1e8||position_max<1e-8))mikk.position_scale=1/position_max;
        if(uv_max>0&&(uv_max>1e8||uv_max<1e-8))mikk.uv_scale=1/uv_max;
        SMikkTSpaceInterface api{};api.m_getNumFaces=SurfaceMikk::faces;api.m_getNumVerticesOfFace=SurfaceMikk::vertices;
        api.m_getPosition=SurfaceMikk::position;api.m_getNormal=SurfaceMikk::normal;api.m_getTexCoord=SurfaceMikk::uv;api.m_setTSpaceBasic=SurfaceMikk::tangent;
        SMikkTSpaceContext context{&api,&mikk};require(genTangSpaceDefault(&context)!=0,"Cannot generate surface tangent basis");
    }
    // Mikk propagates nearby bases into degenerate UV faces; explicitly disable
    // normal mapping there. Each incompatible face-corner basis gets a copy.
    for(std::size_t f=0;f<p.indices.size();f+=3){const auto& a=p.surface_vertices[p.indices[f]],&b=p.surface_vertices[p.indices[f+1]],&c=p.surface_vertices[p.indices[f+2]];
        const double det=(double(b.normal_uv[0])-a.normal_uv[0])*(double(c.normal_uv[1])-a.normal_uv[1])-(double(b.normal_uv[1])-a.normal_uv[1])*(double(c.normal_uv[0])-a.normal_uv[0]);
        if(!std::isfinite(det)||det==0)for(int v=0;v<3;++v)mikk.corners[f+v]={};
    }
    std::vector<std::uint32_t> original(p.vertices.size());for(std::size_t i=0;i<remap.size();++i)if(remap[i]!=absent)original[remap[i]]=std::uint32_t(i);
    std::map<std::pair<std::uint32_t,std::array<float,4>>,std::uint32_t> distinct;
    std::vector<bool> assigned(p.vertices.size());
    for(std::size_t corner=0;corner<p.indices.size();++corner){const auto index=p.indices[corner];auto value=mikk.corners[corner];
        bool finite=true;for(auto x:value)finite=finite&&std::isfinite(x);if(!finite)value={};
        const auto key=std::make_pair(index,value);const auto found=distinct.find(key);
        if(found!=distinct.end()){p.indices[corner]=found->second;continue;}
        std::uint32_t destination=index;
        if(assigned[index]){
            require(avatar.vertex_count<limits.vertices,"Retained surface seam vertex count exceeds limit");++avatar.vertex_count;
            destination=std::uint32_t(p.vertices.size());p.vertices.push_back(p.vertices[index]);p.surface_vertices.push_back(p.surface_vertices[index]);
            duplicates.push_back({original[index],destination});
        }else assigned[index]=true;
        std::copy(value.begin(),value.end(),p.surface_vertices[destination].tangent);distinct.emplace(key,destination);p.indices[corner]=destination;
    }
    std::sort(duplicates.begin(),duplicates.end(),[](const auto& a,const auto& b){return a.source<b.source;});
}
