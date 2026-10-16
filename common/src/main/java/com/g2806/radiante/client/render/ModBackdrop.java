package com.g2806.radiante.client.render;

import com.mojang.blaze3d.pipeline.RenderTarget;
import com.mojang.blaze3d.pipeline.TextureTarget;
import com.mojang.blaze3d.systems.RenderSystem;
import com.mojang.renderpearl.api.GpuFormat;
import com.mojang.renderpearl.api.commands.RenderPass;
import com.mojang.renderpearl.api.pipeline.ColorTargetState;
import com.mojang.renderpearl.api.pipeline.CompareOp;
import com.mojang.renderpearl.api.pipeline.DepthStencilState;
import com.mojang.renderpearl.api.pipeline.PrimitiveTopology;
import com.mojang.renderpearl.api.pipeline.RenderPipeline;
import com.mojang.renderpearl.api.textures.FilterMode;
import java.util.Optional;
import net.minecraft.client.renderer.BindGroupLayouts;
import net.minecraft.resources.Identifier;

/**
 * What other mods draw behind the world - a sky of their own - kept behind the traced world.
 *
 * <p>In Minecraft the sky comes first and the terrain is drawn over it, so a mod adds to the sky by drawing, without
 * any depth test, before the terrain: in the sky pass (Spyglass Astronomy's stars and planets) or at the start of the
 * main pass (BetterEnd's nebulae, through Fabric's START_MAIN). With the world traced there is no terrain to come
 * after: the traced picture is already in the target, and those draws landed on top of it, the nebula across the
 * ground as an overlay.
 *
 * <p>So the order Minecraft has is put back. The traced picture goes into the target before the sky pass and a copy
 * of it is kept ({@link #capture}); mods draw their sky over it as they always do; and where the terrain would be
 * drawn, the copy is drawn back wherever the traced depth says there is world ({@link #restore}). What is left of the
 * mods' draws is what lies over the traced sky. Nothing here knows any mod: whatever is drawn before the terrain is
 * behind the world, whatever comes after it is over it, as in Minecraft.
 */
public final class ModBackdrop {

    private static final Identifier VERTEX_SHADER = Identifier.fromNamespaceAndPath("radiante", "core/world_restore");
    private static final Identifier PIPELINE = Identifier.fromNamespaceAndPath("radiante", "pipeline/world_restore");

    private static RenderPipeline pipeline;
    private static GpuFormat pipelineFormat;
    private static TextureTarget copy;
    /** The copy holds this frame's traced picture and the world has not been drawn back yet. */
    private static boolean pending;
    private static boolean failed;

    /** A mod draws the stars itself (it keeps Minecraft's from being drawn): Radiante's own are left out. */
    private static boolean modStars;

    private ModBackdrop() {
    }

    public static boolean modStars() {
        return modStars;
    }

    public static void setModStars(boolean value) {
        modStars = value;
    }

    /** Right after the traced picture went into Minecraft's target: keeps a copy of it for {@link #restore}. */
    public static void capture(RenderTarget main) {
        pending = false;
        if (failed || main == null || main.getColorTexture() == null || !main.hasDepth()) {
            return;
        }
        try {
            GpuFormat format = main.getColorTexture().getFormat();
            if (copy == null || copy.width != main.width || copy.height != main.height
                || copy.getColorTexture() == null || copy.getColorTexture().getFormat() != format) {
                if (copy != null) {
                    copy.destroyBuffers();
                }
                copy = new TextureTarget("radiante traced world", main.width, main.height, format, null);
            }
            copy.copyColorFrom(main);
            pending = true;
        } catch (RuntimeException e) {
            fail(e);
        }
    }

    /**
     * In the main pass where the opaque terrain would be drawn: the traced world drawn back over whatever was drawn
     * before it. The quad sits just in front of the far plane, so it passes wherever the traced depth is nearer than
     * that - the world - and leaves the sky pixels, with what mods drew on them, alone.
     */
    public static void restore(RenderPass pass) {
        if (!pending || failed || copy == null) {
            return;
        }
        pending = false;
        try {
            GpuFormat format = copy.getColorTexture().getFormat();
            if (pipeline == null || pipelineFormat != format) {
                // Minecraft's depth runs from 1 at the camera to 0 far away: nearer than the quad is greater.
                pipeline = RenderPipeline.builder().withLocation(PIPELINE).withVertexShader(VERTEX_SHADER)
                    .withFragmentShader("core/blit_screen").withBindGroupLayout(BindGroupLayouts.IN_SAMPLER)
                    .withColorTargetState(new ColorTargetState(Optional.empty(), format, ColorTargetState.WRITE_ALL))
                    .withDepthStencilState(new DepthStencilState(CompareOp.LESS_THAN, false))
                    .withCull(false).withPrimitiveTopology(PrimitiveTopology.TRIANGLES).build();
                pipelineFormat = format;
            }
            pass.setPipeline(RenderSystem.getCompiledPipeline(pipeline));
            pass.setUniform("InSampler", copy.getColorTextureView(),
                RenderSystem.getSamplerCache().getClampToEdge(FilterMode.NEAREST));
            pass.draw(3, 1, 0, 0);
        } catch (RuntimeException e) {
            fail(e);
        }
    }

    private static void fail(RuntimeException e) {
        failed = true;
        RadianteRenderer.LOGGER.warn("Other mods' skies cannot be kept behind the traced world; they are drawn over "
            + "it as before", e);
    }
}
