#pragma once

#include <mbgl/shaders/mtl/common.hpp>
#include <mbgl/shaders/mtl/shader_program.hpp>
#include <mbgl/shaders/shader_source.hpp>
#include <mbgl/shaders/symbol_layer_ubo.hpp>

namespace mbgl {
namespace shaders {

template <>
struct ShaderSource<BuiltIn::SymbolSDFIconShader, gfx::Backend::Type::Metal> {
    static constexpr auto name = "SymbolSDFIconShader";
    static constexpr auto vertexMainFunction = "vertexMain";
    static constexpr auto fragmentMainFunction = "fragmentMain";

    static const std::array<UniformBlockInfo, 5> uniforms;
    static const std::array<AttributeInfo, 10> attributes;
    static constexpr std::array<AttributeInfo, 0> instanceAttributes{};
    static const std::array<TextureInfo, 1> textures;

    static constexpr auto source = R"(
struct VertexStage {
    float4 pos_offset [[attribute(5)]];
    float4 data [[attribute(6)]];
    float4 pixeloffset [[attribute(7)]];
    float3 projected_pos [[attribute(8)]];
    float fade_opacity [[attribute(9)]];

#if !defined(HAS_UNIFORM_u_fill_color)
    float4 fill_color [[attribute(10)]];
#endif
#if !defined(HAS_UNIFORM_u_halo_color)
    float4 halo_color [[attribute(11)]];
#endif
#if !defined(HAS_UNIFORM_u_opacity)
    float opacity [[attribute(12)]];
#endif
#if !defined(HAS_UNIFORM_u_halo_width)
    float halo_width [[attribute(13)]];
#endif
#if !defined(HAS_UNIFORM_u_halo_blur)
    float halo_blur [[attribute(14)]];
#endif
};

struct FragmentStage {
    float4 position [[position, invariant]];

#if !defined(HAS_UNIFORM_u_fill_color)
    half4 fill_color;
#endif
#if !defined(HAS_UNIFORM_u_halo_color)
    half4 halo_color;
#endif

    half2 tex;
    half gamma_scale;
    half fontScale;
    half fade_opacity;

#if !defined(HAS_UNIFORM_u_opacity)
    half opacity;
#endif
#if !defined(HAS_UNIFORM_u_halo_width)
    half halo_width;
#endif
#if !defined(HAS_UNIFORM_u_halo_blur)
    half halo_blur;
#endif
};

FragmentStage vertex vertexMain(thread const VertexStage vertx [[stage_in]],
                                device const SymbolDynamicUBO& dynamic [[buffer(0)]],
                                device const SymbolDrawableUBO& drawable [[buffer(1)]],
                                device const SymbolTilePropsUBO& tileprops [[buffer(2)]],
                                device const SymbolInterpolateUBO& interp [[buffer(3)]],
                                device const SymbolEvaluatedPropsUBO& props [[buffer(4)]]) {

    const float2 a_pos = vertx.pos_offset.xy;
    const float2 a_offset = vertx.pos_offset.zw;

    const float2 a_tex = vertx.data.xy;
    const float2 a_size = vertx.data.zw;

    const float a_size_min = floor(a_size[0] * 0.5);
    const float2 a_pxoffset = vertx.pixeloffset.xy;

    const float segment_angle = -vertx.projected_pos[2];

    float size;
    if (!tileprops.is_size_zoom_constant && !tileprops.is_size_feature_constant) {
        size = mix(a_size_min, a_size[1], tileprops.size_t) / 128.0;
    } else if (tileprops.is_size_zoom_constant && !tileprops.is_size_feature_constant) {
        size = a_size_min / 128.0;
    } else {
        size = tileprops.size;
    }

    const float4 projectedPoint = drawable.matrix * float4(a_pos, 0, 1);
    const float camera_to_anchor_distance = projectedPoint.w;
    // If the label is pitched with the map, layout is done in pitched space,
    // which makes labels in the distance smaller relative to viewport space.
    // We counteract part of that effect by multiplying by the perspective ratio.
    // If the label isn't pitched with the map, we do layout in viewport space,
    // which makes labels in the distance larger relative to the features around
    // them. We counteract part of that effect by dividing by the perspective ratio.
    const float distance_ratio = tileprops.pitch_with_map ?
        camera_to_anchor_distance / dynamic.camera_to_center_distance :
        dynamic.camera_to_center_distance / camera_to_anchor_distance;
    const float perspective_ratio = clamp(
        0.5 + 0.5 * distance_ratio,
        0.0, // Prevents oversized near-field symbols in pitched/overzoomed tiles
        4.0);

    size *= perspective_ratio;

    const float fontScale = tileprops.is_text ? size / 24.0 : size;

    float symbol_rotation = 0.0;
    if (drawable.rotate_symbol) {
        // Point labels with 'rotation-alignment: map' are horizontal with respect to tile units
        // To figure out that angle in projected space, we draw a short horizontal line in tile
        // space, project it, and measure its angle in projected space.
        const float4 offsetProjectedPoint = drawable.matrix * float4(a_pos + float2(1, 0), 0, 1);

        const float2 a = projectedPoint.xy / projectedPoint.w;
        const float2 b = offsetProjectedPoint.xy / offsetProjectedPoint.w;

        symbol_rotation = atan2((b.y - a.y) / dynamic.aspect_ratio, b.x - a.x);
    }

    const float angle_sin = sin(segment_angle + symbol_rotation);
    const float angle_cos = cos(segment_angle + symbol_rotation);
    const auto rotation_matrix = float2x2(angle_cos, -1.0 * angle_sin, angle_sin, angle_cos);
    const float4 projected_pos = drawable.label_plane_matrix * float4(vertx.projected_pos.xy, 0.0, 1.0);
    const float2 pos_rot = a_offset / 32.0 * fontScale + a_pxoffset;
    const float2 pos0 = projected_pos.xy / projected_pos.w + rotation_matrix * pos_rot;
    const float4 position = drawable.coord_matrix * float4(pos0, 0.0, 1.0);
    const float2 fade_opacity = unpack_opacity(vertx.fade_opacity);
    const float fade_change = (fade_opacity[1] > 0.5) ? dynamic.fade_change : -dynamic.fade_change;

    return {
        .position     = position,
#if !defined(HAS_UNIFORM_u_fill_color)
        .fill_color   = half4(unpack_mix_color(vertx.fill_color, interp.fill_color_t)),
#endif
#if !defined(HAS_UNIFORM_u_halo_color)
        .halo_color   = half4(unpack_mix_color(vertx.halo_color, interp.halo_color_t)),
#endif
#if !defined(HAS_UNIFORM_u_halo_width)
        .halo_width   = half(unpack_mix_float(vertx.halo_width, interp.halo_width_t)),
#endif
#if !defined(HAS_UNIFORM_u_halo_blur)
        .halo_blur    = half(unpack_mix_float(vertx.halo_blur, interp.halo_blur_t)),
#endif
#if !defined(HAS_UNIFORM_u_opacity)
        .opacity      = half(unpack_mix_float(vertx.opacity, interp.opacity_t)),
#endif
        .tex          = half2(a_tex / drawable.texsize),
        .gamma_scale  = half(position.w),
        .fontScale    = half(fontScale),
        .fade_opacity = half(max(0.0, min(1.0, fade_opacity[0] + fade_change))),
    };
}

half4 fragment fragmentMain(FragmentStage in [[stage_in]],
                            device const SymbolDynamicUBO& dynamic [[buffer(0)]],
                            device const SymbolDrawableUBO& drawable [[buffer(1)]],
                            device const SymbolTilePropsUBO& tileprops [[buffer(2)]],
                            device const SymbolEvaluatedPropsUBO& props [[buffer(4)]],
                            texture2d<float, access::sample> image [[texture(0)]],
                            sampler image_sampler [[sampler(0)]]) {
#if defined(OVERDRAW_INSPECTOR)
    return half4(1.0);
#endif

#if defined(HAS_UNIFORM_u_fill_color)
    const half4 fill_color = half4(tileprops.is_text ? props.text_fill_color : props.icon_fill_color);
#else
    const half4 fill_color = in.fill_color;
#endif
#if defined(HAS_UNIFORM_u_halo_color)
    const half4 halo_color = half4(tileprops.is_text ? props.text_halo_color : props.icon_halo_color);
#else
    const half4 halo_color = in.halo_color;
#endif
#if defined(HAS_UNIFORM_u_opacity)
    const float opacity = tileprops.is_text ? props.text_opacity : props.icon_opacity;
#else
    const float opacity = in.opacity;
#endif
#if defined(HAS_UNIFORM_u_halo_width)
    const float halo_width = tileprops.is_text ? props.text_halo_width : props.icon_halo_width;
#else
    const float halo_width = in.halo_width;
#endif
#if defined(HAS_UNIFORM_u_halo_blur)
    const float halo_blur = tileprops.is_text ? props.text_halo_blur : props.icon_halo_blur;
#else
    const float halo_blur = in.halo_blur;
#endif

    const float EDGE_GAMMA = 0.105 / DEVICE_PIXEL_RATIO;
    const float fontGamma = in.fontScale * drawable.gamma_scale;
    const half4 color = tileprops.is_halo ? halo_color : fill_color;
    const float gamma = ((tileprops.is_halo ? (halo_blur * 1.19 / SDF_PX) : 0) + EDGE_GAMMA) / fontGamma;
    const float buff = tileprops.is_halo ? (6.0 - halo_width / in.fontScale) / SDF_PX : (256.0 - 64.0) / 256.0;
    const float dist = image.sample(image_sampler, float2(in.tex)).a;
    const float gamma_scaled = gamma * in.gamma_scale;
    const float alpha = smoothstep(buff - gamma_scaled, buff + gamma_scaled, dist);

    return half4(color * (alpha * opacity * in.fade_opacity));
}
)";
};

} // namespace shaders
} // namespace mbgl
