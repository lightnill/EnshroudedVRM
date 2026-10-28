// Included inside vrm_asset.cpp's private importer namespace. Metadata parsing
// shares its raw-number validation; it never uses TinyGLTF's narrowed integers.
void build_springs(const Json& doc, Avatar& avatar) {
    const auto& extensions=doc.at("extensions");
    const bool v1=extensions.contains("VRMC_springBone");
    const bool v0=avatar.version==VrmVersion::V0 && extensions.at("VRM").contains("secondaryAnimation");
    const auto required=doc.value("extensionsRequired",Json::array());
    const bool mandatory=std::find(required.begin(),required.end(),"VRMC_springBone")!=required.end();
    if(!v1&&!v0){require(!mandatory,"Required spring metadata missing");return;}
    try {
        const auto& source=v1?extensions.at("VRMC_springBone"):extensions.at("VRM").at("secondaryAnimation");
        require(source.is_object(),"Invalid spring object");
        if(v1)require(source.at("specVersion")=="1.0","Unsupported spring version");
        std::size_t palette_work=0;
        for(const auto& n:avatar.nodes)if(n.active&&n.mesh>=0&&n.skin>=0) {
            palette_work+=avatar.skins[n.skin].joints.size();require(palette_work<=8192,"Spring palette work limit");
        }
        auto array=[&](const Json& o,const char* key,std::size_t limit)->Json {
            const auto a=o.value(key,Json::array());require(a.is_array()&&a.size()<=limit,std::string("Spring array limit/type: ")+key);return a;
        };
        auto node=[&](const Json& o,const char* key) {
            const auto i=field(o,key,std::numeric_limits<std::size_t>::max());
            require(i<avatar.nodes.size()&&avatar.nodes[i].active,"Spring node index out of range/inactive");return static_cast<int>(i);
        };
        auto scalar=[](const Json& o,const char* key,float fallback,float upper=10000.f) {
            if(!o.contains(key))return fallback;
            const auto& v=o.at(key);require(v.is_number(),"Invalid spring numeric type");const auto n=v.get<double>();
            require(std::isfinite(n)&&n>=0&&n<=upper,"Invalid spring numeric range");return static_cast<float>(n);
        };
        auto vector=[](const Json& o,const char* key,std::array<float,3> fallback,bool legacy) {
            if(!o.contains(key))return fallback;
            const auto& value=o.at(key);require(legacy?value.is_object():value.is_array()&&value.size()==3,"Invalid spring vector shape");
            const char* names[]={"x","y","z"};
            for(int c=0;c<3;++c){const auto v=legacy?value.value(names[c],Json(0)):value.at(c);require(v.is_number(),"Invalid spring vector type");const double n=v.get<double>();require(std::isfinite(n)&&std::abs(n)<=10000,"Invalid spring vector range");fallback[c]=static_cast<float>(n);}return fallback;
        };
        auto center=[&](const Json& o) {if(!o.contains("center")||o.at("center")==-1)return -1;return node(o,"center");};
        std::vector<std::vector<std::size_t>> groups;
        auto add_collider=[&](int n,const Json& shape,bool legacy,bool capsule) {
            require(avatar.spring_colliders.size()<256,"Spring collider limit");SpringCollider c;c.node=n;c.capsule=capsule;
            c.offset=vector(shape,"offset",{},legacy);c.tail=vector(shape,"tail",{},false);c.radius=scalar(shape,"radius",0,100.f);
            // VRM0 offsets were serialized in Unity's left-handed coordinates.
            if(legacy)c.offset[2]=-c.offset[2];
            avatar.spring_colliders.push_back(c);return avatar.spring_colliders.size()-1;
        };
        if(v1) {
            for(const auto& c:array(source,"colliders",256)) {
                const int n=node(c,"node");const auto& s=c.at("shape");require(s.is_object()&&s.contains("sphere")!=s.contains("capsule"),"Invalid spring collider shape");
                const bool capsule=s.contains("capsule");const auto& shape=s.at(capsule?"capsule":"sphere");require(shape.is_object(),"Invalid spring collider properties");add_collider(n,shape,false,capsule);
            }
            for(const auto& g:array(source,"colliderGroups",256)) {
                std::vector<std::size_t> indices;for(const auto& i:array(g,"colliders",64)){const auto n=number(i);require(n<avatar.spring_colliders.size(),"Spring collider reference out of range");indices.push_back(n);}groups.push_back(std::move(indices));
            }
        } else {
            for(const auto& g:array(source,"colliderGroups",256)) {
                const int n=node(g,"node");std::vector<std::size_t> indices;
                for(const auto& c:array(g,"colliders",64))indices.push_back(add_collider(n,c,true,false));
                groups.push_back(std::move(indices));
            }
        }
        auto group_indices=[&](const Json& s) {
            std::set<std::size_t> unique;
            for(const auto& g:array(s,"colliderGroups",256)){const auto i=number(g);require(i<groups.size(),"Spring collider group out of range");unique.insert(groups[i].begin(),groups[i].end());require(unique.size()<=64,"Spring colliders per joint limit");}
            return std::vector<std::size_t>(unique.begin(),unique.end());
        };
        auto settings=[&](const Json& j) {
            require(j.is_object(),"Invalid spring joint object");SpringJoint out;
            out.hit_radius=scalar(j,"hitRadius",0,100.f);out.stiffness=scalar(j,v1?"stiffness":"stiffiness",1);
            out.gravity_power=scalar(j,"gravityPower",0);out.drag=scalar(j,"dragForce",v1?.5f:.4f,1);
            out.gravity=vector(j,"gravityDir",{0,-1,0},!v1);return out;
        };
        auto add_joint=[&](SpringJoint j){require(avatar.spring_joints.size()<256,"Spring joint limit");avatar.spring_joints.push_back(std::move(j));};
        if(v1) {
            for(const auto& s:array(source,"springs",256)) {
                const auto joints=array(s,"joints",257);const int ctr=center(s);const auto cols=group_indices(s);
                for(std::size_t i=0;i<joints.size();++i) {
                    auto j=settings(joints[i]);j.node=node(joints[i],"node");j.center=ctr;j.colliders=cols;
                    if(i+1<joints.size()){j.child=node(joints[i+1],"node");add_joint(std::move(j));}
                }
            }
        } else {
            for(const auto& s:array(source,"boneGroups",256)) {
                auto prototype=settings(s);prototype.center=center(s);prototype.colliders=group_indices(s);
                std::set<int> group_nodes;
                for(const auto& root:array(s,"bones",256)) {
                    Json ref={{"node",root}};std::vector<int> queue{node(ref,"node")};
                    for(std::size_t i=0;i<queue.size();++i) {
                        require(queue.size()<=256,"Spring recursive node limit");const int n=queue[i];auto j=prototype;j.node=n;
                        if(!group_nodes.insert(n).second)continue;
                        const auto& children=avatar.nodes[n].children;j.child=children.empty()?-1:children.front();add_joint(std::move(j));queue.insert(queue.end(),children.begin(),children.end());
                    }
                }
            }
        }
        std::set<int> simulated;
        auto ancestor=[&](int a,int b){for(int n=b;n>=0;n=avatar.nodes[n].parent)if(n==a)return true;return false;};
        for(const auto& j:avatar.spring_joints) {
            require(simulated.insert(j.node).second,"Duplicated spring joint");
            require(j.child<0||(j.child!=j.node&&ancestor(j.node,j.child)),"Invalid spring joint hierarchy");
            require(j.center<0||ancestor(j.center,j.node),"Invalid spring center hierarchy");
            for(const auto& h:avatar.humanoid)require(!ancestor(j.node,h.second),"Spring joint owns humanoid bone");
        }
        // Skipped intermediate nodes cannot be reused by another chain.
        for(const auto& j:avatar.spring_joints)if(j.child>=0)for(int n=avatar.nodes[j.child].parent;n!=j.node;n=avatar.nodes[n].parent)
            require(!simulated.count(n),"Overlapping spring joint paths");
        // Check runtime support here too, so a required extension cannot pass
        // import and then silently lose simulation during solver construction.
        std::vector<bool> secondary(avatar.nodes.size());std::size_t visits=0,collisions=0;
        for(const auto& j:avatar.spring_joints) {
            std::vector<int> subtree{j.node};
            for(std::size_t cursor=0;cursor<subtree.size();++cursor) {
                require(visits+subtree.size()<=16384,"Spring descendant work limit");
                const int n=subtree[cursor];secondary[n]=true;const auto& children=avatar.nodes[n].children;subtree.insert(subtree.end(),children.begin(),children.end());
            }
            visits+=subtree.size();collisions+=j.colliders.size()*2;require(collisions<=16384,"Spring collision work limit");
        }
        for(const auto& c:avatar.spring_colliders)require(!secondary[c.node],"Spring collider depends on simulated subtree");
        for(const auto& j:avatar.spring_joints)if(j.center>=0&&secondary[j.center])
            for(const auto& owner:avatar.spring_joints)if(ancestor(owner.node,j.center))
                require(owner.node==j.center&&owner.center==j.center,"Spring center depends on another simulation chain");
    } catch(const std::exception& error) {
        avatar.spring_colliders.clear();avatar.spring_joints.clear();
        if(mandatory)fail(std::string("Invalid required spring bones: ")+error.what());
        avatar.warnings.push_back(std::string("Spring bones disabled: ")+error.what());
    }
}
