// The current material path consumes base color only. Keep all texture/image
// indices stable while replacing unused maps with budgeted white placeholders.
struct ImageBudget {
    const ImportLimits&limits;std::size_t used=0;std::vector<bool> needed;
    ImageBudget(const ImportLimits&l,const Json&doc):limits(l),needed(doc.value("images",Json::array()).size()){
        for(const auto&material:doc.value("materials",Json::array())){
            if(!material.contains("pbrMetallicRoughness"))continue;
            const auto&pbr=material.at("pbrMetallicRoughness");if(!pbr.contains("baseColorTexture"))continue;
            const auto texture=number(pbr.at("baseColorTexture").at("index"));
            const auto image=number(doc.at("textures").at(texture).at("source"));
            require(image<needed.size(),"Invalid needed image reference");needed[image]=true;
        }
    }
};
bool load_image(tinygltf::Image*image,int index,std::string*error,std::string*warning,
                int request_width,int request_height,const unsigned char*bytes,int size,void*data){
    auto&budget=*static_cast<ImageBudget*>(data);int width=0,height=0,channels=0;
    if(index<0||static_cast<std::size_t>(index)>=budget.needed.size()||
       !stbi_info_from_memory(bytes,size,&width,&height,&channels)||width<=0||height<=0||
       width>budget.limits.image_dimension||height>budget.limits.image_dimension||stbi_is_16_bit_from_memory(bytes,size)){
        *error+="Invalid or unsupported image dimensions/bit depth";return false;
    }
    const bool needed=budget.needed[static_cast<std::size_t>(index)];
    const auto decoded=needed?static_cast<std::size_t>(width)*static_cast<std::size_t>(height)*4:std::size_t(4);
    if(budget.used>budget.limits.decoded_image_bytes||decoded>budget.limits.decoded_image_bytes-budget.used){*error+="Decoded image memory exceeds limit";return false;}
    budget.used+=decoded;
    if(!needed){image->width=image->height=1;image->component=4;image->bits=8;image->pixel_type=TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE;image->image={255,255,255,255};return true;}
    return tinygltf::LoadImageData(image,index,error,warning,request_width,request_height,bytes,size,nullptr);
}
