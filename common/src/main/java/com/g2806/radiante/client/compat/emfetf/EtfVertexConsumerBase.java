package com.g2806.radiante.client.compat.emfetf;

import com.mojang.blaze3d.vertex.VertexConsumer;

/**
 * Direct-delegating {@link VertexConsumer} base class for ETF's {@code ETFVertexConsumer} bridge.
 *
 * <p>When Entity Texture Features (ETF) is installed, {@link EmfEtfCompat} generates a tiny runtime subclass that
 * implements {@code traben.entity_texture_features.utils.ETFVertexConsumer} by reading and writing the fields here, so
 * per-vertex calls run as direct virtual calls with zero reflection, zero boxing and zero allocations, while Radiante
 * needs no compile-time dependency on ETF.
 */
public class EtfVertexConsumerBase implements VertexConsumer {

    private VertexConsumer delegate;
    protected Object etfProvider;
    protected Object etfRenderLayer;
    protected Object etfTexture;

    public EtfVertexConsumerBase() {
    }

    /** Binds a fresh underlying consumer before handing this wrapper to {@code ModelFeatureRenderer}. */
    public final void bind(VertexConsumer delegate) {
        this.delegate = delegate;
        this.etfProvider = null;
        this.etfRenderLayer = null;
        this.etfTexture = null;
    }

    /** Releases per-submit references once the model draw finishes. */
    public final void clear() {
        this.delegate = null;
        this.etfProvider = null;
        this.etfRenderLayer = null;
        this.etfTexture = null;
    }

    /** Invoked by the generated {@code etf$initETFVertexConsumer} bridge method. */
    protected final void initEtf(Object provider, Object renderLayer) {
        this.etfProvider = provider;
        this.etfRenderLayer = renderLayer;
        this.etfTexture = EmfEtfCompat.lookupEtfTexture(renderLayer);
    }

    @Override
    public VertexConsumer addVertex(float x, float y, float z) {
        this.delegate.addVertex(x, y, z);
        return this;
    }

    @Override
    public VertexConsumer setColor(int red, int green, int blue, int alpha) {
        this.delegate.setColor(red, green, blue, alpha);
        return this;
    }

    @Override
    public VertexConsumer setColor(int color) {
        this.delegate.setColor(color);
        return this;
    }

    @Override
    public VertexConsumer setUv(float u, float v) {
        this.delegate.setUv(u, v);
        return this;
    }

    @Override
    public VertexConsumer setUv1(int u, int v) {
        this.delegate.setUv1(u, v);
        return this;
    }

    @Override
    public VertexConsumer setUv2(int u, int v) {
        this.delegate.setUv2(u, v);
        return this;
    }

    @Override
    public VertexConsumer setNormal(float x, float y, float z) {
        this.delegate.setNormal(x, y, z);
        return this;
    }

    @Override
    public VertexConsumer setLineWidth(float width) {
        this.delegate.setLineWidth(width);
        return this;
    }
}
