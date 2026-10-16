package com.g2806.radiante.client.bedrock;

/**
 * How the material channels of a texture were authored, i.e. what the numbers in them mean.
 *
 * <p>Radiante renders from LabPBR maps (the {@code _s} and {@code _n} atlases). A Bedrock RTX pack authors its
 * materials differently (metalness, emissive, roughness and subsurface in the channels of one MER(S) image), and
 * converting that to LabPBR is lossy: emission, for one, was stored as {@code min(254, 4 x value)}. The converter
 * therefore also keeps the original MER(S) data, unchanged, next to the LabPBR maps (see
 * {@link BedrockPackConverter#RAW_MER_DIR}), tagged {@link #BEDROCK_MER}, so a Bedrock-specific renderer profile can
 * apply Bedrock's own transforms to the original values instead of to a pre-cooked approximation.
 *
 * <p>With the Bedrock Pack Materials option on (the default), the shaders read metalness and roughness from the raw
 * MER through a separate block atlas, and light emitting blocks take the texels that glow from its emission. Its
 * alpha marks the texels it covers (0 where it covers none) and carries the set's height map where Bedrock shades the
 * set by one (see {@link BedrockPackConverter}); subsurface still comes from the converted LabPBR map.
 */
public enum MaterialEncoding {
    /** LabPBR 1.3 specular/normal maps, the format every shader pack and Radiante's renderer use. */
    LAB_PBR,
    /** Bedrock texture-set MER(S): R metalness, G emissive, B roughness, A subsurface (when the set has it). */
    BEDROCK_MER
}
