#ifndef VPT_BEDROCK_TRANSPORT_GLSL
#define VPT_BEDROCK_TRANSPORT_GLSL

// Bedrock RTX's observed behaviour.
// eta is incident IOR / transmitted IOR. The denser-to-rarer branch uses cos(theta_t),
// not a linear remapping of cos(theta_i) to the critical angle.
float bedrockInterfaceFresnel(float cosIncident, float eta) {
    float c = clamp(cosIncident, 0.0, 1.0);
    float sinTransmitted2 = eta * eta * (1.0 - c * c);
    if (sinTransmitted2 >= 1.0) { return 1.0; }
    float cosine = eta <= 1.0 ? c : sqrt(max(1.0 - sinTransmitted2, 0.0));
    float f0 = (1.0 - eta) / (1.0 + eta);
    f0 *= f0;
    return f0 + (1.0 - f0) * pow(1.0 - cosine, 5.0);
}

#endif
