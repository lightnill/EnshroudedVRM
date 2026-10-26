#include "native_body_coverage.hpp"
namespace evrm { namespace {
using Digest=std::array<std::uint8_t,32>;
constexpr std::uint8_t nibble(char c){return static_cast<std::uint8_t>(c<='9'?c-'0':c-'a'+10);}
template<std::size_t N>constexpr Digest sha(const char (&text)[N]){
    static_assert(N==65,"full SHA256 literal required");Digest result{};
    for(unsigned i=0;i<32;++i)result[i]=static_cast<std::uint8_t>((nibble(text[i*2])<<4)|nibble(text[i*2+1]));
    return result;
}
constexpr NativeBodyCoverage clip(NativeClipKind kind,std::uint32_t instance,std::uint32_t culled){return {NativeBodyCoverageKind::clip,kind,instance,culled,0};}
constexpr NativeBodyCoverage transform(std::uint32_t binding){return {NativeBodyCoverageKind::separate_transform,NativeClipKind::none,0,0,binding};}
constexpr NativeBodyCoverage chunklet(std::uint32_t binding){return {NativeBodyCoverageKind::chunklet,NativeClipKind::none,0,0,binding};}
struct SceneContract {
    std::uint32_t stages;bool mesh;
    std::uint32_t front_bytes;Digest front;
    std::uint32_t control_bytes;Digest control;
    std::uint32_t evaluation_bytes;Digest evaluation;
    std::uint32_t fragment_bytes;Digest fragment;
    NativeBodyCoverage result;
};
#include "native_scene_contracts.generated.inl"
struct Contract {std::uint32_t bytes;Digest digest;NativeBodyCoverage result;};
// Exact same-build metadata only; source bytes remain local research artifacts.
// native-body-coverage-contracts.json SHA256:
// e88d1c08558325a850725b4fdac9f7f2cf381101f290cf5fefbf7361ebfe490c
constexpr Contract contracts[]={
    {26928,sha("b03b19480b45cdde956cf36d1fe591d68465bda7dbe97b4a454477b5c2b2c86d"),clip(NativeClipKind::visibility,4,5)},
    {27376,sha("a2491bcb6d0172e72a49c64ef2bae2b75e1f2ecf42cf28a2be64af408e9a5f89"),clip(NativeClipKind::visibility_alpha,4,5)},
    {48504,sha("e68043b76b025782c798105809be0f3e30635a1fb73bcabdbccb6828dfed9f64"),clip(NativeClipKind::gbuffer,4,5)},
    {7380,sha("0cde43e26b70fe3b664831c0d61361113d9d961b6fc84b3f39a00e3849d65178"),clip(NativeClipKind::billboard_visibility,3,4)},
    {9556,sha("edeb4bd938c4520b312dc08290b187f1de340974f69717ffcd5e3d3513a9b2e8"),clip(NativeClipKind::billboard_gbuffer,3,4)},
    {14956,sha("30939c254be124b334b0d251446288d9e91343bfc65d6a5fa91947ae4037beeb"),transform(4)},
    {15324,sha("7ce3da133f51e1529ad79c214debd7676e47ee85dd1418620ecb2459f225e7d7"),transform(4)},
    {15388,sha("34f06588a41daa39d002afaa2862a7bcf8f58883385cbd226796e3d30b164f37"),transform(4)},
    {25856,sha("ed87a2879e2430710624f00b46627b82c327d633bcec64876bff0c50c0ad1848"),transform(4)},
    {4540,sha("7397fc93d03b97bf41e6b804c59eb575485d6ab5f86641d1192535ed3d52a6f3"),transform(2)},
    {8924,sha("42ac9f86cebde5509526091ba39f02f239e84370fe17b1abee9461fb20a19218"),transform(2)},
    {5920,sha("7a588822443e39cb80f636962212ef363eb5f239d1362992b23e18b40fb3a827"),transform(2)},
    {8096,sha("f35051280f8d081fd24531db1702f0beff884aaec36ac956a9d808acd34e3120"),transform(2)}
};
// Brick is a full pair from native-body-pass-coverage-research.md, not a VS-only exception.
constexpr auto brick_vertex=sha("3f7bd63a07948c1627c487a059b1011ca1c1b9d40cfc951c3a45f2d0d39ef2dd");
constexpr auto brick_fragment=sha("489d5ad453c1e0a61c3060d8622465f0e33b4f902f7b30af9b33827e0fbeae51");
// Additional same-build native geometry tuples audited in
// work/rig/chunklet-family-20261024/audit.json. Native calls remain unchanged;
// this does not establish coverage of every character representation.
struct ChunkletContract {
    std::uint32_t stages,binding,vertex_bytes;Digest vertex;
    std::uint32_t control_bytes;Digest control;
    std::uint32_t evaluation_bytes;Digest evaluation;
    std::uint32_t fragment_bytes;Digest fragment;
};
constexpr ChunkletContract additional_chunklets[]={
    // Terrain_Visibility_Preview_Chunklets default
    {4,3,8660,sha("81eced2635046e2e99949c19280c73598f27698c02605b00f2ba3e312f9a5c65"),6228,sha("ab6221416bfd37be05b35a753a68ab3b1e09b0ab78fb9709339567d0d02d01fc"),32920,sha("9fb33be16e33521a20720c771e9f772c30afab4e61130ab8f3920ba61fc0948d"),3808,sha("c8d8516f025cdcef9eb5ecd2cb051d9cf4ceec6c26d32d98a4f0a87c985f6fdc")},
    // Building_Visibility_Tessellated_Chunklets default
    {4,2,8008,sha("60f0698b70aeb07c5e8fc5ffa3f56b5e6f8c88ecdf7c5e7e380d377fb26c8dcc"),50704,sha("3aafc4e7967a29edffafde068e9033483ef843befb055e50c498cc6269739d86"),33264,sha("237e456026ffdeb7f873ffe3abaff3373dff8343f2b54f70fb14e48c45056cf6"),5284,sha("be01af7336f341ddd63f0ca234b55100e05131b2cebcc8edc4532b67f0cb312c")},
    // Building_Visibility_Preview_Chunklets default
    {4,2,8008,sha("60f0698b70aeb07c5e8fc5ffa3f56b5e6f8c88ecdf7c5e7e380d377fb26c8dcc"),52332,sha("114b6348c24d5fef3ecd32383dcfe6432f8ce33974a81566f5a5fce8e097718e"),33264,sha("237e456026ffdeb7f873ffe3abaff3373dff8343f2b54f70fb14e48c45056cf6"),5396,sha("166ac251c6f224e9c9fc7a12fa9b71853301e32f0432e0eba5d82057c244aab8")},
    // Building_Visibility_Preview_Chunklets Float16
    {4,2,8008,sha("60f0698b70aeb07c5e8fc5ffa3f56b5e6f8c88ecdf7c5e7e380d377fb26c8dcc"),52332,sha("114b6348c24d5fef3ecd32383dcfe6432f8ce33974a81566f5a5fce8e097718e"),34284,sha("5dc38f55df5e6478ec6aa6a88272a3428dae206e07faf199fbfe8514e3645998"),5396,sha("166ac251c6f224e9c9fc7a12fa9b71853301e32f0432e0eba5d82057c244aab8")},
    // Roof_Visibility_Chunklets default
    {2,2,8256,sha("7cafd357bd3818b765cce7e734c035dfa79a3c8c85dec799db59a8c38889ac4b"),0,{},0,{},5048,sha("93241607e7897b0988a0ee0be1c6aa0561a19c9104a5f6a7d94d771ba182be4c")},
    // Roof_Visibility_Tessellated_Chunklets default
    {4,2,7676,sha("1cdd837cbcb897673a33ee3a742c82f2212cdcaee0466484834a097026f60b98"),8440,sha("f4b1ba0077f7425a2dc9443efe523eb0a2c3b16cb58f11e4c65e7e1b607c4aa2"),13752,sha("ab71273d2651fe3e90571e888890c3944ba5dea61f374bf40a9840174d5a6ebb"),5048,sha("b40b2a6d5621571ed74c64dc64ad3ce156c47ac2722e1e10d53fcf95e7e5bd19")},
    // Roof_Visibility_Preview_Chunklets default
    {4,2,7676,sha("1cdd837cbcb897673a33ee3a742c82f2212cdcaee0466484834a097026f60b98"),8880,sha("c6d2bb6274ac224b1b254a21c9f1861c312643d0df2e4a642fe0593dce08e373"),13752,sha("ab71273d2651fe3e90571e888890c3944ba5dea61f374bf40a9840174d5a6ebb"),5160,sha("d8891c7cb61e87cd42877ffa7d7ad6fe4128d6e35466f3cce96d4d89e5e70b12")},
};
bool matches(const NativeBodyShaderIdentity& id,const ChunkletContract& row)noexcept{
    return id.graphics_stage_count==row.stages&&id.vertex_bytes==row.vertex_bytes&&id.vertex_digest==row.vertex&&
        id.tess_control_bytes==row.control_bytes&&id.tess_control_digest==row.control&&
        id.tess_evaluation_bytes==row.evaluation_bytes&&id.tess_evaluation_digest==row.evaluation&&
        id.fragment_bytes==row.fragment_bytes&&id.fragment_digest==row.fragment;
}
bool complete(std::uint32_t bytes,const Digest& digest)noexcept{
    if(bytes<20||bytes>16u*1024*1024||bytes%4)return false;
    for(auto byte:digest)if(byte)return true;
    return false;
}
} // namespace
NativeBodyCoverage classify_native_body_coverage(const NativeBodyShaderIdentity& identity)noexcept{
    const Digest empty{};
    for(const auto& row:scene_contracts){
        if(identity.graphics_stage_count!=row.stages||identity.fragment_bytes!=row.fragment_bytes||identity.fragment_digest!=row.fragment||
           identity.tess_control_bytes!=row.control_bytes||identity.tess_control_digest!=row.control||
           identity.tess_evaluation_bytes!=row.evaluation_bytes||identity.tess_evaluation_digest!=row.evaluation)continue;
        if(row.mesh?(!identity.vertex_bytes&&identity.vertex_digest==empty&&identity.mesh_bytes==row.front_bytes&&identity.mesh_digest==row.front):
                    (!identity.mesh_bytes&&identity.mesh_digest==empty&&identity.vertex_bytes==row.front_bytes&&identity.vertex_digest==row.front))return row.result;
    }
    if(identity.mesh_bytes||identity.mesh_digest!=empty)return {};
    for(const auto& row:additional_chunklets)if(matches(identity,row))return chunklet(row.binding);
    // Exact native chunklet paths remain unchanged in the incomplete preview.
    // Tessellation is never inferred from just a vertex/fragment pair.
    if(identity.graphics_stage_count==4){
        if(identity.vertex_bytes==8008&&identity.vertex_digest==sha("60f0698b70aeb07c5e8fc5ffa3f56b5e6f8c88ecdf7c5e7e380d377fb26c8dcc")&&
           identity.tess_control_bytes==50704&&identity.tess_control_digest==sha("3aafc4e7967a29edffafde068e9033483ef843befb055e50c498cc6269739d86")&&
           identity.tess_evaluation_bytes==34284&&identity.tess_evaluation_digest==sha("5dc38f55df5e6478ec6aa6a88272a3428dae206e07faf199fbfe8514e3645998")&&
           identity.fragment_bytes==5284&&identity.fragment_digest==sha("be01af7336f341ddd63f0ca234b55100e05131b2cebcc8edc4532b67f0cb312c"))return chunklet(2);
        if(identity.vertex_bytes==8660&&identity.vertex_digest==sha("81eced2635046e2e99949c19280c73598f27698c02605b00f2ba3e312f9a5c65")&&
           identity.tess_control_bytes==6228&&identity.tess_control_digest==sha("ab6221416bfd37be05b35a753a68ab3b1e09b0ab78fb9709339567d0d02d01fc")&&
           identity.tess_evaluation_bytes==36108&&identity.tess_evaluation_digest==sha("e0051d56d33c41e06f39e302548b819d850a38a58cc34f781bfe3756711e6d1f")&&
           identity.fragment_bytes==3696&&identity.fragment_digest==sha("c392b233dcc1ef452e2026684d3de5547f3d4e232ee852715082b1877ad4e1bf"))return chunklet(3);
        return {};
    }
    if(identity.graphics_stage_count!=2||identity.tess_control_bytes||identity.tess_evaluation_bytes||
       identity.tess_control_digest!=empty||identity.tess_evaluation_digest!=empty)return {};
    if(!complete(identity.vertex_bytes,identity.vertex_digest)||!complete(identity.fragment_bytes,identity.fragment_digest))return {};
    if(identity.vertex_bytes==8036&&identity.vertex_digest==sha("13e2ccd24b50e99c55a8001a6f77144f57931fa0f6c742dd9fe3f9dce59d06f4")&&
       identity.fragment_bytes==5284&&identity.fragment_digest==sha("db9368b153c6ed5864ec205c3f1fd9b9c9a5dc1950481b468f8064916266d51f"))return chunklet(2);
    if(identity.vertex_bytes==35200&&identity.vertex_digest==sha("dc1b3a77be743a3bae9422d76d1f4c5f195405b851d3211eab6cdd70b7ed4dac")&&
       identity.fragment_bytes==3696&&identity.fragment_digest==sha("8f31a706d837f9c14acd2ba51d1325631e1e1a96961cab3acd4b27db0de293b0"))return chunklet(3);
    if(identity.vertex_bytes==7240&&identity.vertex_digest==brick_vertex){
        if(identity.fragment_bytes==9984&&identity.fragment_digest==brick_fragment)return {NativeBodyCoverageKind::brick,NativeClipKind::none,0,0,0};
        return {};
    }
    for(const auto& contract:contracts)if(identity.vertex_bytes==contract.bytes&&identity.vertex_digest==contract.digest)return contract.result;
    return {};
}
}
