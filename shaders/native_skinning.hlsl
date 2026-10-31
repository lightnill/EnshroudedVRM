// Independent compute skinning from this project's model-space CPU contract.
// The aggregate finalize pass can revoke, but never grant, the common commit.
[[vk::binding(0,0)]] ByteAddressBuffer sources;   // 64 bytes per immutable vertex
[[vk::binding(1,0)]] ByteAddressBuffer palettes;  // 64-byte column-major matrices
[[vk::binding(2,0)]] ByteAddressBuffer batches;   // world matrix, base/count, padding: 80 bytes
[[vk::binding(3,0)]] ByteAddressBuffer history;   // prior committed 44-byte native vertices
[[vk::binding(4,0)]] RWByteAddressBuffer outputVertices;
[[vk::binding(5,0)]] RWByteAddressBuffer result;
[[vk::binding(6,0)]] RWByteAddressBuffer preparedFrame;
struct Control {
    uint vertices,batchCount,matrixCount,useHistory;
    uint mouthLookup,mouthRecords,mouthRecordCount,mouthEnabled;
    float4 mouthWeights0,mouthWeights1;
};
// Private UBO avoids disturbing the game's push-constant state.
[[vk::binding(7,0)]] ConstantBuffer<Control> control;

struct Affine {float3 x,y,z,t;};
bool finite3(float3 v){return all(isfinite(v));}
float largest(float3 v){v=abs(v);return max(v.x,max(v.y,v.z));}
void reject(uint reason){uint old;result.InterlockedOr(0,reason,old);}
bool loadAffine(ByteAddressBuffer data,uint at,out Affine m){
    float4 x=asfloat(data.Load4(at)),y=asfloat(data.Load4(at+16));
    float4 z=asfloat(data.Load4(at+32)),t=asfloat(data.Load4(at+48));
    m.x=x.xyz;m.y=y.xyz;m.z=z.xyz;m.t=t.xyz;
    return all(isfinite(x))&&all(isfinite(y))&&all(isfinite(z))&&all(isfinite(t))&&
        abs(x.w)<=1e-5f&&abs(y.w)<=1e-5f&&abs(z.w)<=1e-5f&&abs(t.w-1.f)<=1e-5f;
}
float3 direction(Affine m,float3 v){return m.x*v.x+m.y*v.y+m.z*v.z;}
Affine compose(Affine a,Affine b){
    Affine r;r.x=direction(a,b.x);r.y=direction(a,b.y);r.z=direction(a,b.z);r.t=direction(a,b.t)+a.t;return r;
}
bool inverseTransposeDirection(Affine m,float3 n,out float3 normal){
    normal=0;
    // A common positive scale cancels during final normal normalization. This
    // avoids overflow/underflow from cubing ordinary uniform import scales.
    float scale=max(largest(m.x),max(largest(m.y),largest(m.z)));
    if(!isfinite(scale)||scale<=0)return false;
    float3 x=m.x/scale,y=m.y/scale,z=m.z/scale;
    float3 cx=cross(y,z),cy=cross(z,x),cz=cross(x,y);
    float determinant=dot(x,cx);
    if(!isfinite(determinant)||determinant==0)return false;
    float3 cofactor=cx*n.x+cy*n.y+cz*n.z;
    float magnitude=largest(cofactor);
    if(!isfinite(magnitude)||magnitude<=0)return false;
    cofactor/=magnitude;
    normal=normalize(cofactor)*(determinant<0?-1.f:1.f);
    return finite3(normal);
}
bool deformMouth(uint vertex,uint sourceBytes,inout float3 position,inout float3 normal){
    if(control.mouthEnabled==0u)return true;
    if(control.mouthEnabled!=1u||control.mouthLookup!=control.vertices*64u||control.mouthRecordCount==0u||control.mouthRecordCount>98304u)return false;
    uint expectedRecords=(control.mouthLookup+control.vertices*4u+15u)&~15u;
    if(control.mouthRecords!=expectedRecords||control.mouthRecords>sourceBytes||
       control.mouthRecordCount>(sourceBytes-control.mouthRecords)/128u||sourceBytes-control.mouthRecords!=control.mouthRecordCount*128u)return false;
    float weights[5]={control.mouthWeights0.x,control.mouthWeights0.y,control.mouthWeights0.z,control.mouthWeights0.w,control.mouthWeights1.x};
    [unroll]for(uint n=0;n<5u;++n)if(!isfinite(weights[n])||weights[n]<0.f||weights[n]>1.f)return false;
    uint record=sources.Load(control.mouthLookup+vertex*4u);
    if(record==0xffffffffu)return true;
    if(record>=control.mouthRecordCount)return false;
    uint at=control.mouthRecords+record*128u;float3 baseNormal=normal;
    [unroll]for(uint n=0;n<5u;++n){
        float3 delta=asfloat(sources.Load3(at+n*12u)),normalDelta=asfloat(sources.Load3(at+60u+n*12u));
        if(!finite3(delta)||!finite3(normalDelta))return false;
        position+=delta*weights[n];normal+=normalDelta*weights[n];
    }
    if(!finite3(position)||!finite3(normal))return false;
    float scale=largest(normal);
    normal=scale>0.f?normalize(normal/scale):baseNormal;
    return finite3(normal);
}
[numthreads(64,1,1)]
void cs_skin(uint3 thread:SV_DispatchThreadID){
    uint i=thread.x;if(i>=control.vertices)return;
    uint resultBytes,frameBytes;result.GetDimensions(resultBytes);preparedFrame.GetDimensions(frameBytes);
    if(resultBytes<4u||frameBytes<224u)return;
    if(preparedFrame.Load(192u)!=1u)return;
    uint sourceBytes,paletteBytes,batchBytes,historyBytes,outputBytes;
    sources.GetDimensions(sourceBytes);palettes.GetDimensions(paletteBytes);batches.GetDimensions(batchBytes);
    history.GetDimensions(historyBytes);outputVertices.GetDimensions(outputBytes);
    if(control.vertices>2000000u||i>=sourceBytes/64u||i>=outputBytes/44u||
        control.batchCount>batchBytes/80u||control.matrixCount>paletteBytes/64u){reject(1);return;}
    // Appended data never counts as additional immutable source vertices, even
    // when the mouth is disabled and no optional lookup/delta read is needed.
    if((control.mouthLookup!=0u&&control.mouthLookup!=control.vertices*64u)||
       (control.mouthLookup==0u&&sourceBytes!=control.vertices*64u)){reject(256);return;}
    uint at=i*64u,batch=sources.Load(at+56),flags=sources.Load(at+60);
    if(batch>=control.batchCount||flags>1u){reject(2);return;}
    float3 position=asfloat(sources.Load3(at)),normal=asfloat(sources.Load3(at+12));
    float2 uv=asfloat(sources.Load2(at+24));
    if(!finite3(position)||!finite3(normal)||!all(isfinite(uv))){reject(4);return;}
    if(!deformMouth(i,sourceBytes,position,normal)){reject(256);return;}
    Affine world;if(!loadAffine(batches,batch*80u,world)){reject(8);return;}
    Affine transform=world;
    if(flags!=0){
        uint base=batches.Load(batch*80u+64),count=batches.Load(batch*80u+68);
        uint2 packed=sources.Load2(at+32);
        uint4 joints=uint4(packed.x&65535u,packed.x>>16,packed.y&65535u,packed.y>>16);
        float4 weights=asfloat(sources.Load4(at+40));
        float sum=dot(weights,float4(1,1,1,1));
        // Static source weights already passed the CPU's double-precision
        // 1e-4 check. Account for four FP32 additions in this redundant guard.
        const float sumRounding=4.f*1.1920928955078125e-7f;
        if(count==0||count>512u||base>control.matrixCount||count>control.matrixCount-base||
            any(joints>=count)||!all(isfinite(weights))||any(weights<0)||any(weights>1.001f)||abs(sum-1.f)>1e-4f+sumRounding){reject(16);return;}
        Affine blend;blend.x=blend.y=blend.z=blend.t=0;
        [unroll]for(uint k=0;k<4;++k){
            Affine bone;if(!loadAffine(palettes,(base+joints[k])*64u,bone)){reject(32);return;}
            blend.x+=bone.x*weights[k];blend.y+=bone.y*weights[k];blend.z+=bone.z*weights[k];blend.t+=bone.t*weights[k];
        }
        transform=compose(world,blend);
    }
    float3 posed=direction(transform,position)+transform.t,posedNormal;
    if(!finite3(posed)||!inverseTransposeDirection(transform,normal,posedNormal)){reject(64);return;}
    float3 oldPosition=posed;
    if(control.useHistory==1u&&preparedFrame.Load(196u)==1u){
        if(i>=historyBytes/44u){reject(128);return;}
        oldPosition=asfloat(history.Load3(i*44u));
        if(!finite3(oldPosition)){reject(128);return;}
    }
    uint outAt=i*44u;
    outputVertices.Store3(outAt,asuint(posed));outputVertices.Store3(outAt+12u,asuint(posedNormal));
    outputVertices.Store2(outAt+24u,asuint(uv));outputVertices.Store3(outAt+32u,asuint(oldPosition));
}

// This invocation runs only after all deformation writes/errors are available.
// Clipped native body and all avatar passes use this same existing commit word.
[numthreads(1,1,1)]
void cs_finalize(){
    uint resultBytes,frameBytes;result.GetDimensions(resultBytes);preparedFrame.GetDimensions(frameBytes);
    if(frameBytes>=208u&&(resultBytes<4u||result.Load(0u)!=0u))preparedFrame.Store4(192u,uint4(0,0,0,0));
}
