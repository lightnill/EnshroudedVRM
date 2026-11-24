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
            out.gravity=vector(j,"gravityDir",{0,-1,0},!v1);
            // VRM0 serialized world gravity in Unity coordinates, like collider offsets.
            // Keep all imported SpringJoint directions in canonical glTF coordinates.
            if(!v1)out.gravity[2]=-out.gravity[2];
            return out;
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

// VRM0 exporters sometimes put a spring on an unweighted duplicate of the
// visible chain. Use imported matrices and actual vertex influences; skin joint
// membership alone is insufficient. This runs once, after mesh validation.
void recover_vrm0_springs(const Json& doc, Avatar& avatar) {
    if (avatar.version != VrmVersion::V0 || avatar.spring_joints.empty() ||
        doc.at("extensions").contains("VRMC_springBone")) return;
    const auto& nodes = avatar.nodes;
    const auto& groups = doc.at("extensions").at("VRM").at("secondaryAnimation").at("boneGroups");
    std::set<int> roots;
    for (const auto& group : groups)
        if (group.contains("bones")) for (int root : group.at("bones"))
            if (nodes[root].parent >= 0 && nodes[nodes[root].parent].children.size() > 1) roots.insert(root);
    if (roots.empty()) return;

    std::vector<bool> weighted(nodes.size()), reserved(nodes.size());
    std::vector<std::vector<bool>> slots(avatar.meshes.size());
    std::vector<bool> scanned(avatar.meshes.size());
    for (const auto& n : nodes) {
        if (!n.active || n.mesh < 0 || n.skin < 0) continue;
        auto& used = slots[n.mesh];
        if (!scanned[n.mesh]) {
            // build_meshes retains only vertices referenced by the draw.
            for (const auto& p : avatar.meshes[n.mesh].primitives) if (p.skinned)
                for (const auto& v : p.vertices) for (unsigned k=0; k<v.weights.size(); ++k) {
                    if (v.weights[k] <= 0) continue;
                    const std::size_t index = v.joints[k];
                    if (index >= used.size()) used.resize(index+1);
                    used[index] = true;
                }
            scanned[n.mesh] = true;
        }
        const auto& skin = avatar.skins[n.skin];
        for (std::size_t i=0; i<used.size(); ++i) if (used[i]) weighted[skin.joints[i]] = true;
    }

    auto subtree = [&](int root) {
        std::vector<int> out{root};
        for (std::size_t i=0; i<out.size(); ++i) {
            const auto& children = nodes[out[i]].children;
            out.insert(out.end(), children.begin(), children.end());
        }
        return out;
    };
    std::vector<int> settings(nodes.size(), -1);
    std::size_t visits=0, collisions=0;
    for (std::size_t i=0; i<avatar.spring_joints.size(); ++i) {
        const auto& j=avatar.spring_joints[i];
        settings[j.node]=static_cast<int>(i);
        reserved[j.node]=true;
        if (j.center>=0) reserved[j.center]=true;
        visits += subtree(j.node).size();
        collisions += j.colliders.size()*2;
    }
    for (const auto& h : avatar.humanoid) reserved[h.second]=true;
    for (const auto& c : avatar.spring_colliders) reserved[c.node]=true;

    struct Proposal { int source; std::vector<int> target; };
    std::vector<Proposal> proposals;
    std::vector<unsigned> claims(nodes.size());
    for (int root : roots) {
        const auto from=subtree(root);
        if (std::any_of(from.begin(),from.end(),[&](int n){return weighted[n] || nodes[n].mesh>=0;})) continue;
        const auto& j=avatar.spring_joints[settings[root]];
        // A centre internal to the helper is not an ancestor of its sibling.
        if (j.center>=0 && std::find(from.begin(),from.end(),j.center)!=from.end()) continue;
        std::vector<int> match;
        for (int sibling : nodes[nodes[root].parent].children) {
            if (sibling==root || nodes[root].local!=nodes[sibling].local) continue;
            auto to=subtree(sibling);
            if (from.size()!=to.size() || std::none_of(to.begin(),to.end(),[&](int n){return weighted[n];})) continue;
            const bool congruent=std::equal(from.begin(),from.end(),to.begin(),[&](int a,int b){
                return nodes[a].local==nodes[b].local && nodes[a].children.size()==nodes[b].children.size();
            });
            if (!congruent) continue;
            if (!match.empty()) {match.clear();break;} // count ambiguity before testing eligibility
            match=std::move(to);
        }
        if (match.empty() || std::any_of(match.begin(),match.end(),[&](int n){return reserved[n] || nodes[n].mesh>=0;})) continue;
        for (int n : match) ++claims[n];
        proposals.push_back({root,std::move(match)});
    }

    std::vector<SpringJoint> additions;
    for (const auto& p : proposals) {
        const auto& to=p.target;
        if (std::any_of(to.begin(),to.end(),[&](int n){return claims[n]!=1;})) continue;
        const auto& source=avatar.spring_joints[settings[p.source]];
        const auto collision_cost=to.size()*source.colliders.size()*2;
        if (avatar.spring_joints.size()+additions.size()+to.size()>256 || collisions+collision_cost>16384) continue;
        std::size_t visit_cost=0;
        for (int n : to) visit_cost+=subtree(n).size();
        if (visits+visit_cost>16384) continue;
        visits+=visit_cost;
        collisions+=collision_cost;
        for (int n : to) {
            auto joint=source;
            joint.node=n;
            joint.child=nodes[n].children.empty()?-1:nodes[n].children.front();
            additions.push_back(std::move(joint));
        }
    }
    if (additions.empty()) return;
    avatar.spring_joints.reserve(avatar.spring_joints.size()+additions.size());
    avatar.warnings.push_back("Recovered VRM0 spring joints on weighted sibling chains: "+std::to_string(additions.size()));
    for (auto& joint : additions) avatar.spring_joints.push_back(std::move(joint));
}
