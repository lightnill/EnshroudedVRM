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
       width>budget.limits.image_dimension||height>budget.limits.image_dimension){
        *error+="Invalid or unsupported image dimensions/bit depth";return false;
    }
    const bool needed=budget.needed[static_cast<std::size_t>(index)];
    const auto decoded=needed?static_cast<std::size_t>(width)*static_cast<std::size_t>(height)*4:std::size_t(4);
    if(budget.used>budget.limits.decoded_image_bytes||decoded>budget.limits.decoded_image_bytes-budget.used){*error+="Decoded image memory exceeds limit";return false;}
    const bool high_depth=needed&&stbi_is_16_bit_from_memory(bytes,size);
    // Adam7 can grow the inflated rows while retaining a full 16-bit image
    // and a pass buffer. Reserve pixel headroom; retained/GPU data stays RGBA8.
    if(high_depth&&decoded>(budget.limits.decoded_image_bytes-budget.used)/8){*error+="High bit-depth image working memory exceeds limit";return false;}
    budget.used+=decoded;
    if(!needed){image->width=image->height=1;image->component=4;image->bits=8;image->pixel_type=TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE;image->image={255,255,255,255};return true;}
    if(high_depth){
        int decoded_width=0,decoded_height=0,decoded_channels=0;
        // Own the wide buffer directly: the pinned stb automatic 16-to-8
        // conversion does not free it if its narrow-buffer allocation fails.
        // Grayscale/RGB expansion and transparency are resolved before this.
        std::unique_ptr<stbi_us,decltype(&stbi_image_free)> pixels(
            stbi_load_16_from_memory(bytes,size,&decoded_width,&decoded_height,&decoded_channels,4),&stbi_image_free);
        if(!pixels){*error+="Cannot decode high bit-depth PNG image";return false;}
        if(decoded_width!=width||decoded_height!=height||
           (request_width>0&&request_width!=decoded_width)||(request_height>0&&request_height!=decoded_height)){
            *error+="Decoded image dimensions differ from header/request";return false;
        }
        image->width=decoded_width;image->height=decoded_height;image->component=4;
        image->bits=8;image->pixel_type=TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE;image->as_is=false;
        image->image.resize(decoded);
        for(std::size_t channel=0;channel<decoded;++channel)image->image[channel]=static_cast<std::uint8_t>(pixels.get()[channel]>>8);
        return true;
    }
    return tinygltf::LoadImageData(image,index,error,warning,request_width,request_height,bytes,size,nullptr);
}
