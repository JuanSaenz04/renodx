#include "./shared.h"

SamplerState blit_sampler : register(s0);
Texture2D<float4> blit_texture : register(t0);

float4 main(float2 texcoord : TEXCOORD0, float4 position : SV_POSITION0) : SV_TARGET0 {
  float4 output_color = blit_texture.Sample(blit_sampler, texcoord);
  output_color.rgb = renodx::draw::SwapChainPass(output_color.rgb, texcoord);
  return output_color;
}
