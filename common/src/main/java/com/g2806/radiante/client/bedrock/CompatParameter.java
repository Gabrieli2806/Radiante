package com.g2806.radiante.client.bedrock;

/**
 * One setting of the Bedrock RTX look ({@link BedrockRtxValues}): the pipeline attribute, its default value, and how
 * well that value is known.
 *
 * @param group the part of the renderer it belongs to ("exposure", "toneMapping", "bloom", ...)
 * @param module the pipeline module that owns the attribute, as {@code Pipeline.*_MODULE_NAME}; null for an
 * attribute of the ray tracing shader pack
 * @param attribute the attribute's name in that module
 * @param value the default value, as configs.json or the module's yaml declares it
 * @param status how well {@code value} is known
 * @param source where it comes from, or why it is a stand-in
 */
public record CompatParameter(String group, String module, String attribute, String value, ParameterStatus status,
    String source) {
}
