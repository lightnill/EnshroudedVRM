// Reconstruct only invalid/missing normals from unchanged finite triangle
// geometry. Authored valid directions and all positions/indices are retained.
bool repair_primitive_normals(Primitive&p){
    bool needed=false;for(const auto&v:p.vertices)if(v.normal==std::array<float,3>{}){needed=true;break;}
    if(!needed)return false;
    using V=std::array<double,3>;std::vector<V> sums(p.vertices.size()),first(p.vertices.size());
    for(std::size_t at=0;at<p.indices.size();at+=3){
        const auto&a=p.vertices[p.indices[at]].position;const auto&b=p.vertices[p.indices[at+1]].position;const auto&c=p.vertices[p.indices[at+2]].position;
        const V x{double(b[0])-a[0],double(b[1])-a[1],double(b[2])-a[2]},y{double(c[0])-a[0],double(c[1])-a[1],double(c[2])-a[2]};
        const V n{x[1]*y[2]-x[2]*y[1],x[2]*y[0]-x[0]*y[2],x[0]*y[1]-x[1]*y[0]};
        if(!(std::hypot(n[0],n[1],n[2])>0))continue;
        for(int corner=0;corner<3;++corner){const auto i=p.indices[at+corner];if(p.vertices[i].normal!=std::array<float,3>{})continue;
            for(int k=0;k<3;++k)sums[i][k]+=n[k];
            if(first[i]==V{})first[i]=n;
        }
    }
    for(std::size_t i=0;i<p.vertices.size();++i)if(p.vertices[i].normal==std::array<float,3>{}){
        auto n=sums[i];auto length=std::hypot(n[0],n[1],n[2]);if(!(length>0)){n=first[i];length=std::hypot(n[0],n[1],n[2]);}
        if(!(length>0)){n={0,1,0};length=1;}
        require(std::isfinite(length),"Invalid reconstructed vertex normal");for(int k=0;k<3;++k)p.vertices[i].normal[k]=static_cast<float>(n[k]/length);
    }
    return true;
}
