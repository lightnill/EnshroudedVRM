#include "enshrouded_bridge.hpp"
#include "../vendor/tinygltf/json.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <set>
#include <stdexcept>

namespace evrm {
namespace {
using Json = nlohmann::json;
using Matrix = vrm::Matrix;
constexpr double position_quantization = 2097151.0;
constexpr double packed_position_scale = 131072.0;
constexpr std::size_t instance_stride = 128, mesh_stride = 80, skin_stride = 48;
constexpr const char* required_semantics[] = {
    "hips", "spine", "chest", "upperChest", "neck", "head",
    "leftUpperArm", "leftLowerArm", "leftHand",
    "rightUpperArm", "rightLowerArm", "rightHand",
    "leftUpperLeg", "leftLowerLeg", "leftFoot",
    "rightUpperLeg", "rightLowerLeg", "rightFoot",
};
[[noreturn]] void fail(const std::string& message) { throw std::runtime_error(message); }
void require(bool value, const std::string& message) { if (!value) fail(message); }
Matrix identity() { return {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1}; }
Matrix multiply(const Matrix& a, const Matrix& b) {
    Matrix out{};
    for (unsigned c=0;c<4;++c) for (unsigned r=0;r<4;++r)
        for (unsigned k=0;k<4;++k) out[c*4+r]+=a[k*4+r]*b[c*4+k];
    return out;
}
void finite(const Matrix& m, const char* label) {
    require(std::all_of(m.begin(),m.end(),[](float x) { return std::isfinite(x); }),std::string("Nonfinite ")+label);
}
void affine(const Matrix& m, const char* label) {
    finite(m,label);
    require(std::abs(m[3])<1e-6f && std::abs(m[7])<1e-6f && std::abs(m[11])<1e-6f && std::abs(m[15]-1)<1e-6f,
            std::string("Non-affine ")+label);
    const double a=m[0],b=m[4],c=m[8],d=m[1],e=m[5],f=m[9],g=m[2],h=m[6],i=m[10];
    const double det=a*(e*i-f*h)-b*(d*i-f*g)+c*(d*h-e*g);
    const double scale=std::max({std::abs(a),std::abs(b),std::abs(c),std::abs(d),std::abs(e),std::abs(f),std::abs(g),std::abs(h),std::abs(i)});
    require(scale>0 && std::abs(det)>1e-8*scale*scale*scale,std::string("Singular ")+label);
}
void invertible_camera(const Matrix& m) {
    finite(m,"camera matrix"); double a[4][4]{}; double magnitude=0;
    for (unsigned r=0;r<4;++r) for (unsigned c=0;c<4;++c) {
        a[r][c]=m[c*4+r]; magnitude=std::max(magnitude,std::abs(a[r][c]));
    }
    require(magnitude>0,"Singular camera matrix");
    for (unsigned c=0;c<4;++c) {
        unsigned pivot=c;
        for (unsigned r=c+1;r<4;++r) if (std::abs(a[r][c])>std::abs(a[pivot][c])) pivot=r;
        require(std::abs(a[pivot][c])>magnitude*1e-12,"Singular camera matrix");
        for (unsigned k=0;k<4;++k) std::swap(a[c][k],a[pivot][k]);
        for (unsigned r=c+1;r<4;++r) {
            const double ratio=a[r][c]/a[c][c];
            for (unsigned k=c;k<4;++k) a[r][k]-=ratio*a[c][k];
        }
    }
}
bool consistent(const Matrix& a, const Matrix& b) {
    for (unsigned i=0;i<16;++i)
        if (std::abs(a[i]-b[i])>1e-4f*std::max({1.0f,std::abs(a[i]),std::abs(b[i])})) return false;
    return true;
}
Json load_json(const std::filesystem::path& path, std::size_t limit) {
    std::ifstream input(path,std::ios::binary|std::ios::ate);
    require(static_cast<bool>(input),"Cannot open local pose metadata: "+path.string());
    const auto length=input.tellg();
    require(length>0 && static_cast<std::uint64_t>(length)<=limit,"Pose metadata file size exceeds limit");
    std::string text(static_cast<std::size_t>(length),'\0'); input.seekg(0);
    input.read(text.data(),static_cast<std::streamsize>(text.size()));
    require(static_cast<bool>(input),"Truncated local pose metadata"); return Json::parse(text);
}
std::uint32_t number(const Json& value) {
    require(value.is_number_integer() && !value.is_boolean(),"Expected integer in pose metadata");
    if (!value.is_number_unsigned()) require(value.get<std::int64_t>()>=0,"Negative pose metadata index");
    const auto n=value.get<std::uint64_t>();
    require(n<=std::numeric_limits<std::uint32_t>::max(),"Pose metadata index exceeds uint32"); return static_cast<std::uint32_t>(n);
}
std::array<double,3> vector3(const Json& value) {
    require(value.is_array() && value.size()==3,"Expected pose metadata vector3"); std::array<double,3> out{};
    for (unsigned i=0;i<3;++i) { out[i]=value[i].get<double>(); require(std::isfinite(out[i]),"Nonfinite catalog descriptor"); }
    return out;
}
Matrix matrix(const Json& value) {
    require(value.is_array() && value.size()==16,"Expected column-major pose matrix"); Matrix out{};
    for (unsigned i=0;i<16;++i) out[i]=value[i].get<float>();
    affine(out,"source rest matrix"); return out;
}
std::string hash_string(const std::string& value) {
    require(value.size()==64,"Executable SHA-256 is missing or malformed"); std::string out=value;
    for (char& c : out) {
        if (c>='A' && c<='F') c=static_cast<char>(c-'A'+'a');
        require((c>='0' && c<='9') || (c>='a' && c<='f'),"Executable SHA-256 is malformed");
    }
    return out;
}
std::uint64_t count_key(std::uint32_t vertices, std::uint32_t indices) { return (static_cast<std::uint64_t>(vertices)<<32)|indices; }
class Reader {
public:
    Reader(const GpuSnapshot& snapshot, const char* name) : name_(name) {
        const auto it=snapshot.buffers.find(name); require(it!=snapshot.buffers.end(),"Missing capture buffer: "+name_); buffer_=&it->second;
        require(!buffer_->bytes.empty() && buffer_->binding.range!=0,"Empty capture buffer/range: "+name_);
        require(buffer_->binding.range==VK_WHOLE_SIZE || buffer_->bytes.size()<=buffer_->binding.range,"Captured bytes exceed descriptor range: "+name_);
    }
    std::size_t size() const { return buffer_->bytes.size(); }
    void span(std::size_t offset, std::size_t count) const {
        if(offset>size() || count>size()-offset)fail("Truncated capture buffer: "+name_);
        if(buffer_->binding.range!=VK_WHOLE_SIZE && (offset>buffer_->binding.range || count>buffer_->binding.range-offset))fail("Read exceeds descriptor range: "+name_);
    }
    std::uint32_t u32(std::size_t offset) const { span(offset,4); std::uint32_t out; std::memcpy(&out,buffer_->bytes.data()+offset,4); return out; }
    float f32(std::size_t offset) const { span(offset,4); float out; std::memcpy(&out,buffer_->bytes.data()+offset,4); return out; }
private:
    const GpuBufferSnapshot* buffer_=nullptr; std::string name_;
};
Matrix actor_matrix(const Reader& instances, std::size_t offset, const std::array<std::uint32_t,3>& origin) {
    instances.span(offset,instance_stride); const double scale=instances.f32(offset+12);
    double x=instances.f32(offset+16),y=instances.f32(offset+20),z=instances.f32(offset+24),w=instances.f32(offset+28);
    require(std::isfinite(scale) && scale>0,"Invalid actor uniform scale");
    require(std::isfinite(x)&&std::isfinite(y)&&std::isfinite(z)&&std::isfinite(w),"Nonfinite actor quaternion");
    const double length=x*x+y*y+z*z+w*w; require(length>0 && std::abs(length-1)<.01,"Invalid actor quaternion length");
    const double norm=std::sqrt(length); x/=norm; y/=norm; z/=norm; w/=norm; auto out=identity();
    const double rows[3][3]={{1-2*(y*y+z*z),2*(x*y-z*w),2*(x*z+y*w)},
                             {2*(x*y+z*w),1-2*(x*x+z*z),2*(y*z-x*w)},
                             {2*(x*z-y*w),2*(y*z+x*w),1-2*(x*x+y*y)}};
    for (unsigned r=0;r<3;++r) for (unsigned c=0;c<3;++c) out[c*4+r]=static_cast<float>(rows[r][c]*scale);
    for (unsigned i=0;i<3;++i) {
        const std::uint32_t wrapped=instances.u32(offset+i*4)-origin[i]; std::int64_t delta=wrapped;
        if (delta>2147483647LL) delta-=4294967296LL;
        out[12+i]=static_cast<float>(static_cast<double>(delta)/packed_position_scale);
    }
    affine(out,"actor world matrix"); return out;
}
} // namespace

struct EnshroudedBridge::Impl {
    struct Descriptor { std::array<double,3> native_scale{},offset{}; std::vector<std::uint32_t> palette,auxiliary; std::vector<std::string> guids; bool reject=false; };
    struct Match { std::vector<std::uint32_t> palette,auxiliary; std::vector<std::string> guids; bool found=false; };
    std::vector<Matrix> rest;
    std::map<std::string,std::uint32_t> semantics;
    std::map<std::uint64_t,std::vector<Descriptor>> descriptors;
    double tolerance=1e-6;
    Impl(const Json& profile, const Json& catalog, const std::string& verified_hash) {
        require(profile.at("schema")=="enshrouded-native-rig-profile-v1","Unsupported pose profile schema");
        require(catalog.at("schema")=="enshrouded-player-catalog-v2","Unsupported player catalog schema");
        const auto expected_hash=hash_string(profile.at("game").at("executable_sha256").get<std::string>());
        require(hash_string(verified_hash)==expected_hash,"Executable build hash does not match pose profile");
        if (catalog.contains("game")) require(hash_string(catalog.at("game").at("executable_sha256").get<std::string>())==expected_hash,"Catalog executable hash differs from pose profile");
        require(profile.at("hierarchy_guid")==catalog.at("hierarchy_guid"),"Catalog hierarchy differs from pose profile");
        const auto count=number(profile.at("joint_count"));
        require(count>0 && count<=1024 && number(catalog.at("joint_count"))==count,"Invalid pose hierarchy joint count");
        require(profile.at("source_rest_global").is_array() && profile.at("source_rest_global").size()==count,"Incomplete source rest globals");
        for (const auto& value : profile.at("source_rest_global")) rest.push_back(matrix(value));
        for (const auto& entry : profile.at("semantic_to_joint").items()) {
            const auto joint=number(entry.value()); require(joint<count,"Semantic joint exceeds hierarchy"); semantics.emplace(entry.key(),joint);
        }
        for (const auto* name : required_semantics) require(semantics.count(name)!=0,"Pose profile lacks core semantic: "+std::string(name));
        tolerance=catalog.at("match_tolerance_native_units").get<double>();
        require(std::isfinite(tolerance) && tolerance>0 && tolerance<=1e-4,"Unsafe catalog match tolerance");
        for (const auto& entry : catalog.at("descriptor_signatures")) {
            Descriptor descriptor; descriptor.native_scale=vector3(entry.at("nativePositionScale")); descriptor.offset=vector3(entry.at("positionOffset"));
            descriptor.reject=entry.at("must_reject_pose_binding").get<bool>();
            require(entry.at("palette_options").is_array() && !entry.at("palette_options").empty(),"Empty catalog palette options");
            std::set<std::vector<std::uint32_t>> palettes;
            for (const auto& option : entry.at("palette_options")) {
                require(option.is_array() && !option.empty() && option.size()<=count,"Invalid catalog palette"); std::vector<std::uint32_t> palette;
                for (const auto& value : option) { auto joint=number(value); require(joint<count,"Catalog palette joint exceeds hierarchy"); palette.push_back(joint); }
                palettes.insert(std::move(palette));
            }
            descriptor.reject=descriptor.reject || palettes.size()!=1; descriptor.palette=*palettes.begin(); std::set<std::string> guids;
            require(entry.at("auxiliary_palette_options").is_array() && !entry.at("auxiliary_palette_options").empty(),"Missing auxiliary palette classifications");
            std::set<std::vector<std::uint32_t>> auxiliary_options;
            for(const auto& option:entry.at("auxiliary_palette_options")) {
                require(option.is_array() && option.size()<=count,"Invalid auxiliary palette classification");
                std::vector<std::uint32_t> auxiliary;
                for(const auto& value:option) {
                    const auto index=number(value);
                    require(auxiliary.empty() || auxiliary.back()<index,"Auxiliary palette indices must be unique and sorted");
                    auxiliary.push_back(index);
                }
                const auto compatible=[&](const auto& palette) {
                    return std::all_of(auxiliary.begin(),auxiliary.end(),[&](auto index){return index<palette.size() && palette[index]==0;});
                };
                require(std::any_of(palettes.begin(),palettes.end(),compatible),"Auxiliary index lacks a matching zero-sentinel palette slot");
                auxiliary_options.insert(std::move(auxiliary));
            }
            descriptor.reject=descriptor.reject || auxiliary_options.size()!=1;descriptor.auxiliary=*auxiliary_options.begin();
            for (const auto& match : entry.at("matches")) guids.insert(match.at("guid").get<std::string>());
            require(!guids.empty(),"Catalog descriptor lacks resource provenance"); descriptor.guids.assign(guids.begin(),guids.end());
            const auto vertices=number(entry.at("vertexCount")),indices=number(entry.at("indexCount"));
            require(vertices>0 && indices>0,"Empty catalog mesh descriptor"); descriptors[count_key(vertices,indices)].push_back(std::move(descriptor));
        }
        require(!descriptors.empty(),"Empty player descriptor catalog");
    }
    Match match(const Reader& mesh, std::size_t offset) const {
        mesh.span(offset,mesh_stride); const auto it=descriptors.find(count_key(mesh.u32(offset+44),mesh.u32(offset+36)));
        if (it==descriptors.end()) return {};
        std::array<double,3> scale{},position{};
        for (unsigned i=0;i<3;++i) {
            scale[i]=static_cast<double>(mesh.f32(offset+i*4))*position_quantization; position[i]=mesh.f32(offset+16+i*4);
            require(std::isfinite(scale[i]) && std::isfinite(position[i]),"Nonfinite candidate mesh descriptor");
        }
        Match result; std::set<std::string> guids;
        for (const auto& descriptor : it->second) {
            bool close=true;
            for (unsigned i=0;i<3;++i) close=close && std::abs(scale[i]-descriptor.native_scale[i])<=tolerance && std::abs(position[i]-descriptor.offset[i])<=tolerance;
            if (!close) continue;
            require(!descriptor.reject,"Catalog rejects ambiguous palette signature");
            require(!result.found || (result.palette==descriptor.palette && result.auxiliary==descriptor.auxiliary),"Differing palette classifications match captured descriptor");
            result.found=true; result.palette=descriptor.palette;result.auxiliary=descriptor.auxiliary;guids.insert(descriptor.guids.begin(),descriptor.guids.end());
        }
        result.guids.assign(guids.begin(),guids.end()); return result;
    }
    PlayerFrame read(const GpuSnapshot& snapshot) const {
        const Reader camera(snapshot,"camera"),instances(snapshot,"instances"),skin(snapshot,"skin"),mesh(snapshot,"mesh-draw"),culled(snapshot,"culled-instances");
        camera.span(0,912); require(instances.size()>=instance_stride && instances.size()%instance_stride==0,"Truncated instance layout");
        require(culled.size()>=8 && culled.size()%8==0,"Truncated culled-instance layout"); PlayerFrame frame; frame.queue_family=snapshot.queue_family;
        require(!snapshot.culled_spans.empty(),"Missing submitted culled-instance spans");
        bool has_drawn_records=false;
        for (const auto& drawn : snapshot.culled_spans) {
            const std::uint64_t first=static_cast<std::uint64_t>(drawn.first_instance)*8;
            const std::uint64_t bytes=static_cast<std::uint64_t>(drawn.instance_count)*8;
            if (first>std::numeric_limits<std::size_t>::max() || bytes>std::numeric_limits<std::size_t>::max())
                fail("Culled draw span byte range overflow");
            culled.span(static_cast<std::size_t>(first),static_cast<std::size_t>(bytes));
            has_drawn_records=has_drawn_records || drawn.instance_count!=0;
        }
        require(has_drawn_records,"No submitted culled-instance records");
        for (unsigned i=0;i<3;++i) frame.camera_packed_origin[i]=camera.u32(816+i*4);
        for (unsigned r=0;r<4;++r) for (unsigned c=0;c<4;++c) frame.camera_world_to_clip[c*4+r]=camera.f32(848+r*16+c*4);
        invertible_camera(frame.camera_world_to_clip);
        frame.camera_stable_world_to_clip=frame.camera_world_to_clip;
        for(unsigned i=0;i<2;++i) {
            const auto jitter=camera.f32(792+i*4);
            require(std::isfinite(jitter) && std::abs(jitter)<.01f,"Invalid camera jitter UV offset");
            frame.camera_jitter_uv[i]=jitter;
        }
        // Native geometry is rasterized with temporal jitter, then resolved by
        // the game. The avatar is drawn after that resolve, so it needs the
        // matching stable projection rather than repeating the pixel dither.
        for(unsigned c=0;c<4;++c) {
            frame.camera_stable_world_to_clip[c*4]+=2*frame.camera_jitter_uv[0]*frame.camera_world_to_clip[c*4+3];
            frame.camera_stable_world_to_clip[c*4+1]-=2*frame.camera_jitter_uv[1]*frame.camera_world_to_clip[c*4+3];
        }
        invertible_camera(frame.camera_stable_world_to_clip);
        struct Part { SelectedPart selected; Matrix actor; }; std::map<std::uint32_t,Part> parts;
        std::set<std::pair<std::uint32_t,std::uint32_t>> visited;
        for (const auto& drawn : snapshot.culled_spans) {
          const auto first=static_cast<std::size_t>(drawn.first_instance)*8;
          const auto end=first+static_cast<std::size_t>(drawn.instance_count)*8;
          for (std::size_t offset=first;offset<end;offset+=8) {
            const auto mesh_id=culled.u32(offset)&1048575u,instance_id=culled.u32(offset+4);
            if (!visited.emplace(mesh_id,instance_id).second) continue;
            const auto matched=match(mesh,static_cast<std::size_t>(mesh_id)*mesh_stride); if (!matched.found) continue;
            const auto instance_offset=static_cast<std::size_t>(instance_id)*instance_stride; instances.span(instance_offset,instance_stride);
            auto found=parts.find(instance_id);
            if (found!=parts.end()) {
                auto& selected=found->second.selected; require(selected.palette_joint_ids==matched.palette && selected.auxiliary_palette_indices==matched.auxiliary,"One instance matches incompatible palettes");
                std::vector<std::string> common;
                std::set_intersection(selected.native_model_guids.begin(),selected.native_model_guids.end(),matched.guids.begin(),matched.guids.end(),std::back_inserter(common));
                require(!common.empty(),"Instance submeshes identify incompatible native models"); selected.native_model_guids=std::move(common); continue;
            }
            Part part; part.selected.instance_id=instance_id; part.selected.model_index=instances.u32(instance_offset+120);
            part.selected.skin_matrix_offset=instances.u32(instance_offset+108); part.selected.palette_joint_ids=matched.palette;part.selected.auxiliary_palette_indices=matched.auxiliary;part.selected.native_model_guids=matched.guids;
            part.actor=actor_matrix(instances,instance_offset,frame.camera_packed_origin); parts.emplace(instance_id,std::move(part));
          }
        }
        require(!parts.empty(),"No catalog-compatible player parts in capture");
        struct Group { std::vector<SelectedPart> parts; std::map<std::uint32_t,Matrix> globals; }; std::map<Matrix,Group> groups;
        for (const auto& item : parts) {
            const auto& part=item.second; auto& group=groups[part.actor];
            for (std::size_t palette_index=0;palette_index<part.selected.palette_joint_ids.size();++palette_index) {
                const std::size_t gpu_index=static_cast<std::size_t>(part.selected.skin_matrix_offset)+palette_index;
                require(gpu_index<=std::numeric_limits<std::size_t>::max()/skin_stride,"Skin palette offset overflow");
                const auto offset=gpu_index*skin_stride; skin.span(offset,skin_stride); auto gpu=identity();
                for (unsigned r=0;r<3;++r) for (unsigned c=0;c<4;++c) gpu[c*4+r]=skin.f32(offset+r*16+c*4);
                const auto label="GPU skin matrix instance="+std::to_string(part.selected.instance_id)+" palette="+std::to_string(palette_index)+" offset="+std::to_string(part.selected.skin_matrix_offset);
                affine(gpu,label.c_str());
                if(std::binary_search(part.selected.auxiliary_palette_indices.begin(),part.selected.auxiliary_palette_indices.end(),palette_index))continue;
                const auto joint=part.selected.palette_joint_ids[palette_index];
                auto live=multiply(gpu,rest[joint]); affine(live,"live joint global"); const auto old=group.globals.find(joint);
                require(old==group.globals.end() || consistent(old->second,live),"Inconsistent duplicated skeleton joint"); group.globals[joint]=live;
            }
            group.parts.push_back(part.selected);
        }
        // Even an incomplete second actor is unsafe: ownership cannot be
        // inferred from palette order, model ID, camera distance or coverage.
        require(groups.size()==1,"Ambiguous compatible actor groups"); const auto& selected=*groups.begin(); const auto& group=selected.second;
        for (const auto* name : required_semantics) require(group.globals.count(semantics.at(name))!=0,"Selected actor lacks core joint: "+std::string(name));
        for (const auto& semantic : semantics) {
            const auto found=group.globals.find(semantic.second); if (found==group.globals.end()) continue;
            frame.source_pose.rest.emplace(semantic.first,rest[semantic.second]); frame.source_pose.current.emplace(semantic.first,found->second);
        }
        frame.actor_world=selected.first; frame.parts=group.parts; frame.observed_joint_count=group.globals.size(); return frame;
    }
};
EnshroudedBridge::EnshroudedBridge(const std::filesystem::path& profile_path,const std::filesystem::path& catalog_path,const std::string& verified_exe_sha256) {
    try {
        const auto profile=load_json(profile_path,2*1024*1024),catalog=load_json(catalog_path,64*1024*1024);
        auto complete=std::make_shared<Impl>(profile,catalog,verified_exe_sha256); impl_=std::move(complete);
    } catch (const std::exception& e) { error_=e.what(); } catch (...) { error_="Unknown pose metadata load failure"; }
}
bool EnshroudedBridge::ready() const noexcept { return static_cast<bool>(impl_); }
const std::string& EnshroudedBridge::error() const noexcept { return error_; }
PlayerFrame EnshroudedBridge::read_player_frame(const GpuSnapshot& snapshot) const noexcept {
    try { require(static_cast<bool>(impl_),error_.empty()?"Pose bridge unavailable":error_); return impl_->read(snapshot); }
    catch (const std::exception& e) { PlayerFrame failed; failed.error=e.what(); return failed; }
    catch (...) { PlayerFrame failed; failed.error="Unknown pose capture failure"; return failed; }
}
std::map<std::string,std::size_t> EnshroudedBridge::readback_limits() {
    return {{"camera",1024},{"instances",1024*1024},{"skin",512*1024},{"mesh-draw",4*1024*1024},{"culled-instances",512*1024}};
}
} // namespace evrm
