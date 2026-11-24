#include "native_mtoon_composite.hlsli"
[[vk::binding(0,0)]] cbuffer Parameters {uint width,height,generation,reserved;}; // reserved: owned outline AA 0/1
[[vk::binding(1,0)]] cbuffer Permit {uint active,permitted_generation,reserved0,reserved1;};
[[vk::binding(2,0)]] ByteAddressBuffer packets;
[[vk::binding(3,0)]] ByteAddressBuffer identities;
[[vk::binding(4,0),vk::image_format("rgba16f")]] RWTexture2D<float4> native_hdr;
[[vk::binding(5,0),vk::image_format("r16f")]] RWTexture2D<float> native_brightness;
[[vk::binding(6,0),vk::image_format("r8")]] RWTexture2D<float> native_emission;
[[vk::binding(7,0)]] RWByteAddressBuffer observations;
[numthreads(8,8,1)] void cs_composite(uint3 id:SV_DispatchThreadID){
    // Submission permission precedes every borrowed/native resource access.
    if(active!=1u||!generation||permitted_generation!=generation||reserved>1u||reserved0||reserved1||
       !width||!height||width>16384u||height>16384u||width>16777216u/height||any(id.xy>=uint2(width,height)))return;
    uint bytes,idBytes;packets.GetDimensions(bytes);identities.GetDimensions(idBytes);const uint pixels=width*height;
    if(bytes<16u+pixels*16u||idBytes<pixels*4u||any(packets.Load4(0)!=uint4(0x4d544350u,generation,width,height)))return;
    const uint index=id.y*width+id.x,key=identities.Load(index*4u);const uint4 packet=packets.Load4(16u+index*16u);
    float4 hdr;float brightness,emission;
    if(!native_mtoon_unpack_composite(packet,key,hdr,brightness,emission))return;
    uint w,h;native_hdr.GetDimensions(w,h);if(any(uint2(w,h)!=uint2(width,height)))return;
    native_brightness.GetDimensions(w,h);if(any(uint2(w,h)!=uint2(width,height)))return;
    native_emission.GetDimensions(w,h);if(any(uint2(w,h)!=uint2(width,height)))return;
    const uint owner=native_mtoon_composite_owner(packet);uint accepted=0;
    if(reserved==1u&&owner!=0u){
        const float3 original=hdr.rgb;const float4 source=float4(hdr.rgb,brightness);const float sourceEmission=emission;
        float4 mainSum=0,outlineSum=0,weightedOutline=0;float mainEmission=0,outlineEmission=0,weightedEmission=0,outlineCoverage=0;
        uint mainCount=0,outlineCount=0;
        // Immutable OWNED records only. Same actor/material and exact current
        // keys are required. No native neighbour image is read or borrowed.
        [unroll] for(int y=-1;y<=1;++y)[unroll] for(int x=-1;x<=1;++x){
            if(x==0&&y==0)continue;const int2 pixel=int2(id.xy)+int2(x,y);
            if(any(pixel<0)||any(pixel>=int2(width,height)))continue;
            const uint at=uint(pixel.y)*width+uint(pixel.x);const uint4 other=packets.Load4(16u+at*16u);
            if(native_mtoon_composite_owner(other)!=owner||((packet.z>>24u)!=4u&&(other.z>>24u)!=4u))continue;
            float4 colour;float grey,emit;
            if(!native_mtoon_unpack_composite(other,identities.Load(at*4u),colour,grey,emit))continue;
            const float4 value=float4(colour.rgb,grey);
            if((other.z>>24u)==4u){const float amount=native_mtoon_composite_coverage(other);
                outlineSum+=value;outlineEmission+=emit;weightedOutline+=value*amount;weightedEmission+=emit*amount;outlineCoverage+=amount;++outlineCount;}
            else{mainSum+=value;mainEmission+=emit;++mainCount;}
            ++accepted;
        }
        const bool outline=(packet.z>>24u)==4u,baseAvailable=!outline||mainCount>0u;
        const float4 base=outline?mainSum/float(max(mainCount,1u)):source;
        const float baseEmission=outline?mainEmission/float(max(mainCount,1u)):sourceEmission;
        const float amount=native_mtoon_composite_coverage(packet);
        const float4 centre=outline&&baseAvailable?lerp(base,source,amount):source;
        const float centreEmission=outline&&baseAvailable?lerp(baseEmission,sourceEmission,amount):sourceEmission;
        // Scale only outline contrast by its estimated fractional pixel width.
        // Without a same-owner main colour, retain the original outline: world
        // pixels and other materials never stand in for a missing underlayer.
        const float4 neighbours=mainSum+(baseAvailable?weightedOutline+base*(float(outlineCount)-outlineCoverage):outlineSum);
        const float neighbourEmission=mainEmission+(baseAvailable?weightedEmission+baseEmission*(float(outlineCount)-outlineCoverage):outlineEmission);
        const float4 filtered=centre+(neighbours-float(accepted)*centre)*(1.f/9.f);
        if(baseAvailable){hdr.rgb=filtered.rgb;brightness=filtered.w;emission=centreEmission+(neighbourEmission-float(accepted)*centreEmission)*(1.f/9.f);}
        // Diagnostic sampling is independent of output permission. A corrupt
        // observation header never changes the colour or creates authority.
        const uint2 grid=min(uint2(width,height),129u),cell=id.xy*grid/uint2(width,height);
        if(all(id.xy==((cell*2u+1u)*uint2(width,height)/(grid*2u)))){
            uint count;observations.GetDimensions(count);
            if(count>=32u&&all(observations.Load4(0)==uint4(0x4d544141u,generation,width,height))){
                uint unused;observations.InterlockedAdd(16u,1u,unused);
                if(accepted){observations.InterlockedAdd(20u,1u,unused);
                    if(any(f32tof16(hdr.rgb)!=f32tof16(original))){observations.InterlockedAdd(24u,1u,unused);if((packet.z>>24u)==4u)observations.InterlockedAdd(28u,1u,unused);}}
            }
        }
    }
    // All destination checks precede the first of the three native writes.
    native_hdr[id.xy]=hdr;native_brightness[id.xy]=brightness;native_emission[id.xy]=emission;
}
