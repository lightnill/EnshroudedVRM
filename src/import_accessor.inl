// Visit effective rows without allocating the complete source accessor. Sparse
// overrides merge in source order; discarded rows still receive validation.
template<class Visit>
void visit_accessor(const tinygltf::Model&model,int index,int expected_type,bool require_finite,Visit visit){
    const auto&a=indexed(model.accessors,index,"accessor");
    require(a.type==expected_type,"Unexpected accessor shape");
    const int n=tinygltf::GetNumComponentsInType(static_cast<std::uint32_t>(a.type));
    require(n>0&&n<=16,"Unsupported accessor component count");
    const auto width=static_cast<std::size_t>(component_size(a.componentType));
    require(expected_type!=TINYGLTF_TYPE_MAT4||a.componentType==5126,"Inverse bind accessor must be float MAT4");
    const unsigned char*base=nullptr;std::size_t stride=width*n;
    if(a.bufferView>=0){
        const auto&view=indexed(model.bufferViews,a.bufferView,"accessor bufferView");
        if(view.byteStride)stride=view.byteStride;
        span(a.byteOffset,a.count,stride,width*n,view.byteLength,"accessor");
        base=view_data(model,a.bufferView,a.byteOffset);
    }
    const unsigned char*sparse_indices=nullptr,*sparse_values=nullptr;
    std::size_t sparse_count=0,cursor=0,next=a.count;int index_width=0;
    if(a.sparse.isSparse){
        require(a.sparse.count>0&&static_cast<std::size_t>(a.sparse.count)<=a.count,"Invalid sparse accessor count");
        sparse_count=static_cast<std::size_t>(a.sparse.count);
        sparse_indices=view_data(model,a.sparse.indices.bufferView,a.sparse.indices.byteOffset);
        sparse_values=view_data(model,a.sparse.values.bufferView,a.sparse.values.byteOffset);
        index_width=component_size(a.sparse.indices.componentType);
    }
    auto target=[&](std::size_t at){
        const auto value=static_cast<std::size_t>(value_at(sparse_indices+at*index_width,a.sparse.indices.componentType,false));
        require(value<a.count&&(at==0||value>next),"Invalid sparse accessor index order/range");return value;
    };
    if(sparse_count)next=target(0);
    for(std::size_t i=0;i<a.count;++i){
        const bool sparse=i==next;
        const auto*row=sparse?sparse_values+cursor*n*width:(base?base+i*stride:nullptr);
        std::array<double,16> values{};
        for(int c=0;c<n;++c){
            values[c]=row?value_at(row+c*width,a.componentType,a.normalized):0;
            if(require_finite&&(!std::isfinite(values[c])||std::abs(values[c])>std::numeric_limits<float>::max()))
                fail("Accessor contains non-finite value");
        }
        visit(i,values);
        if(sparse){++cursor;next=cursor<sparse_count?target(cursor):a.count;}
    }
}
std::vector<double> read_accessor(const tinygltf::Model&model,int index,int expected_type){
    const auto&a=indexed(model.accessors,index,"accessor");
    const int n=tinygltf::GetNumComponentsInType(static_cast<std::uint32_t>(expected_type));
    require(n>0&&n<=16&&a.count<=std::numeric_limits<std::size_t>::max()/n,"Accessor allocation exceeds limit");
    std::vector<double> values(a.count*n);
    visit_accessor(model,index,expected_type,true,[&](std::size_t i,const auto&row){
        for(int c=0;c<n;++c)values[i*n+c]=row[c];
    });
    return values;
}
