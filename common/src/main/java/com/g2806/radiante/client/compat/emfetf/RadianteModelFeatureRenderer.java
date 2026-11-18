package com.g2806.radiante.client.compat.emfetf;

import com.g2806.radiante.client.render.EntityCollector;
import com.mojang.blaze3d.vertex.VertexConsumer;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import net.minecraft.client.renderer.feature.ModelFeatureRenderer;
import net.minecraft.client.renderer.rendertype.RenderType;

/**
 * Runs deferred {@link ModelFeatureRenderer.Submit} nodes through Minecraft's real {@link ModelFeatureRenderer}
 * pipeline while routing every {@code getVertexBuilder(RenderType)} request into Radiante's {@link EntityCollector}.
 *
 * <p>Executing {@code buildGroup} on a genuine {@link ModelFeatureRenderer} triggers all EMF and ETF mixins on
 * {@code prepareModel} and {@code getVertexBuilder} — mounting the entity's {@code ETFEntityRenderState}, copying
 * {@code EMFBipedPose} onto player and mob armor, and redirecting custom per-part textures and emissive/enchanted
 * overlays back into Radiante's ray-traced entity layers.
 */
public final class RadianteModelFeatureRenderer extends ModelFeatureRenderer {

    public static final RadianteModelFeatureRenderer INSTANCE = new RadianteModelFeatureRenderer();

    private final List<EtfVertexConsumerBase> wrapperPool = new ArrayList<>(8);
    private int wrappersUsed;
    private EntityCollector activeCollector;
    private RenderType primaryRenderType;
    private boolean initialBuilderResolved;
    private int baseLayer;
    private int extraLayerCounter;
    private boolean primaryWaterMask;

    private RadianteModelFeatureRenderer() {
    }

    /** Replays one deferred model submission into {@code collector}. */
    public void replay(EntityCollector collector, Submit<?> submit, int layer, boolean waterMask) {
        EntityCollector prevCollector = this.activeCollector;
        RenderType prevPrimary = this.primaryRenderType;
        boolean prevInitial = this.initialBuilderResolved;
        int prevBaseLayer = this.baseLayer;
        int prevExtra = this.extraLayerCounter;
        boolean prevWaterMask = this.primaryWaterMask;
        int prevWrappersUsed = this.wrappersUsed;

        this.activeCollector = collector;
        this.primaryRenderType = submit.renderType();
        this.initialBuilderResolved = false;
        this.baseLayer = layer;
        this.extraLayerCounter = 0;
        this.primaryWaterMask = waterMask;
        try {
            this.buildGroup(null, Collections.singletonList(submit));
        } finally {
            for (int i = prevWrappersUsed; i < this.wrappersUsed; i++) {
                this.wrapperPool.get(i).clear();
            }
            this.wrappersUsed = prevWrappersUsed;
            this.activeCollector = prevCollector;
            this.primaryRenderType = prevPrimary;
            this.initialBuilderResolved = prevInitial;
            this.baseLayer = prevBaseLayer;
            this.extraLayerCounter = prevExtra;
            this.primaryWaterMask = prevWaterMask;
        }
    }

    /**
     * Called from {@code RenderTypeFeatureRendererMixin} when {@code getVertexBuilder(RenderType)} runs on this
     * instance. Secondary {@code RenderType} requests within the same model draw (EMF per-part texture overrides, ETF
     * emissive or glint overlays) automatically step forward one {@code layer} so they never Z-fight with the base
     * surface underneath.
     */
    public VertexConsumer vertexBuilderFor(RenderType renderType) {
        if (this.activeCollector == null) {
            throw new IllegalStateException("RadianteModelFeatureRenderer.vertexBuilderFor called outside replay");
        }
        int effectiveLayer;
        if (!this.initialBuilderResolved) {
            this.initialBuilderResolved = true;
            this.primaryRenderType = renderType;
            effectiveLayer = this.baseLayer;
        } else if (renderType == this.primaryRenderType) {
            effectiveLayer = this.baseLayer;
        } else {
            effectiveLayer = this.baseLayer + (++this.extraLayerCounter);
        }

        VertexConsumer raw = this.activeCollector.featureVertexConsumer(
            renderType, effectiveLayer, this.primaryWaterMask && renderType == this.primaryRenderType);
        if (!EmfEtfCompat.isEtfInstalled()) {
            return raw;
        }
        EtfVertexConsumerBase wrapper = acquireWrapper();
        if (wrapper == null) {
            return raw;
        }
        wrapper.bind(raw);
        return wrapper;
    }

    private EtfVertexConsumerBase acquireWrapper() {
        if (this.wrappersUsed < this.wrapperPool.size()) {
            return this.wrapperPool.get(this.wrappersUsed++);
        }
        EtfVertexConsumerBase created = EmfEtfCompat.createEtfWrapper();
        if (created == null) {
            return null;
        }
        this.wrapperPool.add(created);
        this.wrappersUsed++;
        return created;
    }
}
