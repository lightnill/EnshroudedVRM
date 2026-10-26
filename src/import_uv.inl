// Included in the private importer namespace. Keep raw JSON validation ahead
// of TinyGLTF's integer/float narrowing; bake the supported base UV path once.
struct BaseTextureUv {
    std::size_t coordinate=0;
    std::array<double,2> offset{0,0},scale{1,1};
    double rotation=0;
};
BaseTextureUv validate_base_texture_uv(const Json& texture) {
    require(texture.is_object(),"Invalid base texture UV info");BaseTextureUv out;
    auto coordinate=[](const Json& value){
        require(value.is_number_integer()&&!value.is_boolean()&&(value.is_number_unsigned()||value.get<std::int64_t>()>=0),"Invalid base texture UV set integer");
        const auto index=number(value);require(index<=static_cast<std::size_t>(std::numeric_limits<int>::max()),"Base texture UV set exceeds decoder integer range");return index;
    };
    auto scalar=[](const Json& value){require(value.is_number(),"Invalid base texture UV transform number");const auto n=value.get<double>();require(std::isfinite(n)&&std::abs(n)<=std::numeric_limits<float>::max(),"Invalid base texture UV transform range");return n;};
    auto pair=[&](const Json& value){require(value.is_array()&&value.size()==2,"Invalid base texture UV transform pair");return std::array<double,2>{scalar(value[0]),scalar(value[1])};};
    if(texture.contains("texCoord"))out.coordinate=coordinate(texture.at("texCoord"));
    if(texture.contains("extensions")){
        const auto&extensions=texture.at("extensions");require(extensions.is_object(),"Invalid base texture UV extensions");
        for(const auto&extension:extensions.items()){
            require(extension.key()=="KHR_texture_transform","Base texture requires unsupported UV extension");
            const auto&transform=extension.value();require(transform.is_object(),"Invalid base texture UV transform object");
            for(const auto&item:transform.items()){
                if(item.key()=="offset")out.offset=pair(item.value());
                else if(item.key()=="scale")out.scale=pair(item.value());
                else if(item.key()=="rotation")out.rotation=scalar(item.value());
                else if(item.key()=="texCoord")out.coordinate=coordinate(item.value());
                else if(item.key()=="extensions")require(item.value().is_object()&&item.value().empty(),"Base texture requires unsupported nested UV extension");
                else require(item.key()=="extras","Base texture requires unsupported UV transform field");
            }
        }
    }
    return out;
}
