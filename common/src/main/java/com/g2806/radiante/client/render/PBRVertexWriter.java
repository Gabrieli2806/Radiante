package com.g2806.radiante.client.render;

import com.mojang.blaze3d.vertex.VertexConsumer;
import org.lwjgl.system.MemoryUtil;

/**
 * Writes vertices in the renderer's 128-byte {@code PBRVertex} layout (see native common/shared.hpp) into
 * a growable off-heap buffer. Quads are expected (4 vertices per face).
 */
public final class PBRVertexWriter implements VertexConsumer, AutoCloseable {

    public static final int STRIDE = 128;
    /** Set in the alpha mode word for water surfaces; see {@link #water}. */
    private static final int WATER_FLAG = 0x10;
    /** Set in the alpha mode word for rain sheets; see {@link #rain}. */
    private static final int RAIN_FLAG = 0x20;
    /** Set in the alpha mode word for what the player holds in first person; see {@link #held}. */
    private static final int HELD_FLAG = 0x40;
    /** Glass blocks and panes; see glass(). */
    private static final int GLASS_FLAG = 0x80;
    /** Small plants (grass, flowers, bushes); see plant(). */
    private static final int PLANT_FLAG = 0x100;
    /** A glass pane (not a glass block); see glassPane(). */
    private static final int PANE_FLAG = 0x200;
    /** The pane's top or bottom is open (no glass above or below), so its cap stays; see glassPane(). */
    private static final int CAP_UP_FLAG = 0x400;
    private static final int CAP_DOWN_FLAG = 0x800;
    /** Ice, slime and honey blocks; see blend(). */
    private static final int BLEND_FLAG = 0x1000;
    /** A light block's state that gives no light while drawing its glowing sprites; see unlit(). */
    private static final int UNLIT_FLAG = 0x2000;
    /** Not a surface: the marker of a light Bedrock RTX adds at a small light block; see pointLight(). */
    private static final int POINT_LIGHT_FLAG = 0x4000;
    /** The surface of lava, a self-lit sheet the shaders show by its colour, not by the light on it; see lava(). */
    private static final int LAVA_FLAG = 0x8000;
    /**
     * A layer drawn over a face of the same model and lifted off it (see layerOffset), such as the grass block's side
     * overlay: not carved by height maps, which along its edges let the view through to the face under it.
     */
    private static final int LAYER_FLAG = 0x10000;
    /** Leaves; see leaves(). */
    private static final int LEAVES_FLAG = 0x20000;
    /** A glow that is also cast into the section's light list (powered redstone dust); see glowLight(). */
    private static final int GLOW_LIGHT_FLAG = 0x40000;
    /** Ice and snow; see frost(). */
    private static final int FROST_FLAG = 0x80000;

    public static final int ALPHA_MODE_OPAQUE = 0;
    public static final int ALPHA_MODE_CUTOUT = 1;
    public static final int ALPHA_MODE_TRANSPARENT = 2;
    /** Kept by the any-hit shader with probability alpha, then shaded opaque; see util/alpha_mode.glsl. */
    public static final int ALPHA_MODE_STOCHASTIC = 9;
    /** A multiplicative decal such as block cracks; see util/alpha_mode.glsl. */
    public static final int ALPHA_MODE_DECAL = 10;
    /** An additive layer: only its coloured texels are surface; see util/alpha_mode.glsl. */
    public static final int ALPHA_MODE_ADDITIVE = 11;

    private static final int OFF_POS = 0;
    private static final int OFF_USE_NORM = 12;
    private static final int OFF_NORM = 16;
    private static final int OFF_USE_COLOR = 28;
    private static final int OFF_COLOR = 32;
    private static final int OFF_USE_TEXTURE = 48;
    private static final int OFF_USE_OVERLAY = 52;
    private static final int OFF_TEXTURE_UV = 56;
    private static final int OFF_OVERLAY_UV = 64;
    private static final int OFF_USE_GLINT = 72;
    private static final int OFF_TEXTURE_ID = 76;
    private static final int OFF_GLINT_UV = 80;
    private static final int OFF_GLINT_TEXTURE = 88;
    private static final int OFF_USE_LIGHT = 92;
    private static final int OFF_LIGHT_UV = 96;
    private static final int OFF_COORDINATE = 104;
    private static final int OFF_ALBEDO_EMISSION = 108;
    /** postBase.x, which carries the wind code of section geometry; see wind(). */
    private static final int OFF_WIND = 112;
    /** postBase.y, unused by section geometry otherwise: the light a glowing vertex casts; see castLight(). */
    private static final int OFF_CAST_LIGHT = 116;
    /** postBase.z, unused by section geometry otherwise: the colour of that light, 0xRRGGBB; see castColor(). */
    private static final int OFF_CAST_COLOR = 120;
    private static final int OFF_ALPHA_MODE = 124;

    /** The kinds of foliage the wind moves, as util/wind.glsl numbers them; see wind(). */
    public static final int WIND_GROUND = 0;
    public static final int WIND_LEAVES = 1;
    public static final int WIND_WATER = 2;
    /** No wind: the vertices stay put. */
    public static final int WIND_NONE = -1;
    private static final float WIND_KIND_STRIDE = 8.0f;
    private static final float WIND_MAX_WEIGHT = 4.0f;

    /**
     * Furthest a vertex may sit from the origin of its coordinate space. Minecraft occasionally poses something at
     * a position it never meant to draw - a block-attached entity whose support block is gone, a model scaled by a
     * degenerate matrix - and the result is an infinite or astronomically large corner. The rasteriser shrugs those
     * off; an acceleration structure does not, and the driver answers with a lost device.
     */
    private static final float POSITION_LIMIT = 1.0E6f;

    private long address;
    private long capacity;
    private int vertexCount;
    private long current = -1L;
    private boolean quadHasUnplaceableVertex;

    private int textureId;
    private int glintTextureId;
    private int alphaMode;
    private int coordinate;
    private boolean water;
    private boolean rain;
    private boolean held;
    private float albedoEmission;
    /**
     * How far, in blocks, each vertex is pushed out along its normal. Rasterised, a layer drawn over the same
     * model - armor trims, elytra trims, dyed overlays - sits exactly on it and wins the depth test by being drawn
     * later; traced, two coplanar surfaces tie and the one underneath can come out on top, hiding the layer.
     */
    private float layerOffset;
    private boolean glass;
    private boolean blend;
    private boolean unlit;
    private boolean lava;
    private boolean plant;
    private int windKind = WIND_NONE;
    private float windBaseY;
    private float windRise;
    private float windScale;
    private boolean leaves;
    private boolean frost;
    private boolean glowLight;
    private float castLight;
    private int castColor = 0xFFFFFF;
    private int paneFlags;
    private boolean computeQuadNormals;
    /** Divides vanilla's face shading back out of the colour of quads whose normal is computed (fluids). */
    private net.minecraft.world.level.CardinalLighting unshadeLighting;
    /** The vertex count the last finished quad ended at, so a quad is finished once. */
    private int finishedUpTo;
    private boolean overlayEnabled;
    private boolean glintEnabled;
    private int colorOverride;
    private int colorTint = 0xFFFFFF;
    private float uOffset;
    private float vOffset;

    public PBRVertexWriter(int initialVertices) {
        this.capacity = Math.max(4, initialVertices) * (long) STRIDE;
        this.address = MemoryUtil.nmemAllocChecked(this.capacity);
    }

    public PBRVertexWriter textureId(int textureId) {
        this.textureId = textureId;
        return this;
    }

    public PBRVertexWriter glintTextureId(int glintTextureId) {
        this.glintTextureId = glintTextureId;
        return this;
    }

    /**
     * Whether every vertex from here on carries the enchantment glint, sampled at its own texture coordinate -
     * the standard glint, the same way vanilla's own glint.vsh reuses UV0. Vanilla's screen-locked "special" foil
     * decal (compasses, a few other models) is approximated the same way rather than reprojected; it still glints,
     * just not locked to the screen.
     */
    public PBRVertexWriter glintEnabled(boolean glintEnabled) {
        this.glintEnabled = glintEnabled;
        return this;
    }

    /** An opaque RGB every vertex takes in place of the colour it is given, keeping its alpha; 0 turns it off. */
    public PBRVertexWriter colorOverride(int colorOverride) {
        this.colorOverride = colorOverride;
        return this;
    }

    /** An RGB every vertex colour is multiplied by; 0xFFFFFF leaves it alone. Emission follows it. */
    public PBRVertexWriter colorTint(int colorTint) {
        this.colorTint = colorTint;
        return this;
    }

    /** Added to every texture coordinate: vanilla's scrolling texture transform (charged creeper, wither armor). */
    public PBRVertexWriter uvOffset(float uOffset, float vOffset) {
        this.uOffset = uOffset;
        this.vOffset = vOffset;
        return this;
    }

    public PBRVertexWriter alphaMode(int alphaMode) {
        this.alphaMode = alphaMode;
        return this;
    }

    /** Whether the vertices from here on are what the player holds, whose glow has a brightness of its own. */
    public PBRVertexWriter held(boolean held) {
        this.held = held;
        return this;
    }

    /** Whether the vertices from here on are rain sheets, whose texture falls: their motion vectors follow it. */
    public PBRVertexWriter rain(boolean rain) {
        this.rain = rain;
        return this;
    }

    /**
     * Whether the vertices from here on are the surface of water. Carried to the shaders in a bit above the alpha
     * mode, which only uses the low four, so they can treat it as water: refraction, waves, caustics and the pack's
     * water medium.
     */
    public PBRVertexWriter water(boolean water) {
        this.water = water;
        return this;
    }

    public PBRVertexWriter coordinate(int coordinate) {
        this.coordinate = coordinate;
        return this;
    }

    /** Marks what follows as a glass block or pane, whose see-through texels are clear glass, not holes. */
    public PBRVertexWriter glass(boolean glass) {
        this.glass = glass;
        return this;
    }

    /**
     * Marks what follows as one of the other see-through blocks (ice, slime, honey), which Bedrock RTX draws the way
     * it draws glass; radiante-pt's Bedrock RTX look reads it.
     */
    public PBRVertexWriter blend(boolean blend) {
        this.blend = blend;
        return this;
    }

    /** Whether the vertices from here on are lava's surface (LAVA_SURFACE_BIT in the shaders). */
    public PBRVertexWriter lava(boolean lava) {
        this.lava = lava;
        return this;
    }

    /**
     * Marks what follows as a light block in a state that gives no light although it draws the sprites its lit
     * states glow with (unlit redstone ore): it neither glows nor casts light (see EmissionTiles.isUnlit).
     */
    public PBRVertexWriter unlit(boolean unlit) {
        this.unlit = unlit;
        return this;
    }

    /**
     * Marks what follows as a glass pane, and whether its top and bottom caps are open to air: seamless glass hides
     * the caps between stacked panes, but the top of the highest pane (and the bottom of the lowest) must stay, or
     * a window seen from above had a hole along its edge.
     */
    public PBRVertexWriter glassPane(boolean pane, boolean capUp, boolean capDown) {
        this.paneFlags = pane ? PANE_FLAG | (capUp ? CAP_UP_FLAG : 0) | (capDown ? CAP_DOWN_FLAG : 0) : 0;
        return this;
    }

    /** Marks what follows as a small plant, which the tracer may leave out far from the camera. */
    public PBRVertexWriter plant(boolean plant) {
        this.plant = plant;
        return this;
    }

    /**
     * How the vertices from here on sway in the wind (util/wind.glsl), carried to the renderer as a wind code in
     * postBase.x. Each gets a weight, how far it is from where the plant is held: {@code (y - baseY) * scale + rise}
     * for grass and water plants, so a stalk's foot stays where it grows and its tip moves most; {@code scale} alone
     * for leaves, which move the same at every corner so that neighbouring blocks never part. WIND_NONE (the default)
     * keeps them still.
     */
    public PBRVertexWriter wind(int kind, float baseY, float rise, float scale) {
        this.windKind = kind;
        this.windBaseY = baseY;
        this.windRise = rise;
        this.windScale = scale;
        return this;
    }

    private float windCode(float y) {
        if (this.windKind == WIND_NONE) {
            return 0.0f;
        }
        float weight = this.windKind == WIND_LEAVES ? this.windScale
            : (y - this.windBaseY) * this.windScale + this.windRise;
        weight = Math.max(0.0f, Math.min(WIND_MAX_WEIGHT, weight));
        // A leaf or water plant vertex keeps its kind even without weight: a code interpolated across a triangle must
        // never read as another kind (util/wind.glsl). Grass, the first kind, is 0 at its foot, the same as still.
        return this.windKind * WIND_KIND_STRIDE + weight;
    }

    /** Marks what follows as ice or snow (FROST_SURFACE_BIT in the shaders). */
    public PBRVertexWriter frost(boolean frost) {
        this.frost = frost;
        return this;
    }

    /** Marks what follows as leaves (LEAVES_SURFACE_BIT in the shaders). */
    public PBRVertexWriter leaves(boolean leaves) {
        this.leaves = leaves;
        return this;
    }

    /**
     * Marks what follows as a glow that also lights its surroundings (powered redstone dust, see RedstoneGlow): the
     * native side turns each of its quads with a cast light into lights of the section's list, and the shaders leave
     * its glow out where a bounce reaches a light the surface before it already sampled, as they do for glowing
     * texels. Kept on a geometry of its own, so every geometry carries one set of flags.
     */
    public PBRVertexWriter glowLight(boolean glowLight) {
        this.glowLight = glowLight;
        return this;
    }

    /** The light the vertices from here on cast, before their colour; 0 casts none. Read only with glowLight. */
    public PBRVertexWriter castLight(float castLight) {
        this.castLight = castLight;
        return this;
    }

    /** The colour of the light the vertices from here on cast, 0xRRGGBB. Read only with glowLight. */
    public PBRVertexWriter castColor(int castColor) {
        this.castColor = castColor;
        return this;
    }

    public PBRVertexWriter layerOffset(float layerOffset) {
        this.layerOffset = layerOffset;
        return this;
    }

    public PBRVertexWriter albedoEmission(float albedoEmission) {
        this.albedoEmission = albedoEmission;
        return this;
    }

    /**
     * Terrain has no overlay colour, and flagging it would make the shaders blend against the (empty) overlay
     * texture. Entities that flash red or white when hurt turn this on.
     */
    public PBRVertexWriter overlayEnabled(boolean overlayEnabled) {
        this.overlayEnabled = overlayEnabled;
        return this;
    }

    /** Derives face normals from quad positions instead of the values the caller supplies. */
    public PBRVertexWriter computeQuadNormals(boolean computeQuadNormals) {
        // The quad written last is finished under the settings it was written with.
        finishQuadIfComplete();
        this.computeQuadNormals = computeQuadNormals;
        return this;
    }

    /**
     * The marker of a light Bedrock RTX adds at a small light block (BedrockPointLights): four corners at its centre,
     * so it is never hit, flagged so the native side turns it into a light of the section's list. The colour is the
     * light's, its strength rides in the albedo emission and its radius in the normal's x.
     */
    public void pointLight(float x, float y, float z, int r, int g, int b, float strength, float radius) {
        finishQuadIfComplete();
        ensureCapacity((long) (this.vertexCount + 4) * STRIDE);
        for (int i = 0; i < 4; i++) {
            long v = this.address + (long) this.vertexCount * STRIDE;
            this.vertexCount++;
            this.current = v;
            MemoryUtil.memSet(v, 0, STRIDE);
            MemoryUtil.memPutFloat(v + OFF_POS, x);
            MemoryUtil.memPutFloat(v + OFF_POS + 4, y);
            MemoryUtil.memPutFloat(v + OFF_POS + 8, z);
            MemoryUtil.memPutInt(v + OFF_USE_NORM, 1);
            MemoryUtil.memPutFloat(v + OFF_NORM, radius);
            MemoryUtil.memPutInt(v + OFF_USE_COLOR, 1);
            MemoryUtil.memPutFloat(v + OFF_COLOR, r / 255.0f);
            MemoryUtil.memPutFloat(v + OFF_COLOR + 4, g / 255.0f);
            MemoryUtil.memPutFloat(v + OFF_COLOR + 8, b / 255.0f);
            MemoryUtil.memPutFloat(v + OFF_COLOR + 12, 1.0f);
            MemoryUtil.memPutInt(v + OFF_TEXTURE_ID, this.textureId);
            MemoryUtil.memPutFloat(v + OFF_ALBEDO_EMISSION, strength);
            MemoryUtil.memPutInt(v + OFF_ALPHA_MODE, ALPHA_MODE_TRANSPARENT | POINT_LIGHT_FLAG);
        }
        this.finishedUpTo = this.vertexCount;
    }

    /**
     * With computed normals, divides vanilla's face shading out of each quad's colour: the fluid renderer bakes it in
     * (tops by the up value, bottoms by the down one, sides by up times north or west), and the traced light shades
     * a fluid's faces already. Null keeps the colour as given.
     */
    public PBRVertexWriter unshadeQuads(net.minecraft.world.level.CardinalLighting lighting) {
        finishQuadIfComplete();
        this.unshadeLighting = lighting;
        return this;
    }

    public int vertexCount() {
        return this.vertexCount;
    }

    public long address() {
        return this.address;
    }

    public void reset() {
        this.layerOffset = 0.0f;
        this.vertexCount = 0;
        this.finishedUpTo = 0;
        this.current = -1L;
        this.quadHasUnplaceableVertex = false;
    }

    private void ensureCapacity(long required) {
        if (required <= this.capacity) {
            return;
        }
        long newCapacity = this.capacity;
        while (newCapacity < required) {
            newCapacity *= 2;
        }
        this.address = MemoryUtil.nmemReallocChecked(this.address, newCapacity);
        this.capacity = newCapacity;
    }

    private void finishQuadIfComplete() {
        if (this.vertexCount % 4 != 0 || this.vertexCount == 0 || this.vertexCount == this.finishedUpTo) {
            return;
        }
        if (this.quadHasUnplaceableVertex) {
            // Rewinding is the only honest answer. Clamping the corner to the origin would stretch the quad from
            // wherever the model really is all the way to the camera, which is worse than not drawing it at all.
            this.vertexCount -= 4;
            this.finishedUpTo = this.vertexCount;
            this.quadHasUnplaceableVertex = false;
            return;
        }
        this.finishedUpTo = this.vertexCount;
        if (!this.computeQuadNormals) {
            return;
        }
        long v0 = this.address + (long) (this.vertexCount - 4) * STRIDE;
        long v1 = v0 + STRIDE;
        long v2 = v1 + STRIDE;
        float ax = MemoryUtil.memGetFloat(v1) - MemoryUtil.memGetFloat(v0);
        float ay = MemoryUtil.memGetFloat(v1 + 4) - MemoryUtil.memGetFloat(v0 + 4);
        float az = MemoryUtil.memGetFloat(v1 + 8) - MemoryUtil.memGetFloat(v0 + 8);
        float bx = MemoryUtil.memGetFloat(v2) - MemoryUtil.memGetFloat(v0);
        float by = MemoryUtil.memGetFloat(v2 + 4) - MemoryUtil.memGetFloat(v0 + 4);
        float bz = MemoryUtil.memGetFloat(v2 + 8) - MemoryUtil.memGetFloat(v0 + 8);
        float nx = ay * bz - az * by;
        float ny = az * bx - ax * bz;
        float nz = ax * by - ay * bx;
        float length = (float) Math.sqrt(nx * nx + ny * ny + nz * nz);
        if (length < 1.0E-8f) {
            return;
        }
        nx /= length;
        ny /= length;
        nz /= length;
        float unshade = 1.0f;
        if (this.unshadeLighting != null) {
            float shade = Math.abs(ny) > 0.5f ? (ny > 0.0f ? this.unshadeLighting.up() : this.unshadeLighting.down())
                : this.unshadeLighting.up() * (Math.abs(nz) >= Math.abs(nx) ? this.unshadeLighting.north()
                    : this.unshadeLighting.west());
            unshade = shade > 0.0f ? 1.0f / shade : 1.0f;
        }
        for (int i = 0; i < 4; i++) {
            long v = v0 + (long) i * STRIDE;
            MemoryUtil.memPutInt(v + OFF_USE_NORM, 1);
            MemoryUtil.memPutFloat(v + OFF_NORM, nx);
            MemoryUtil.memPutFloat(v + OFF_NORM + 4, ny);
            MemoryUtil.memPutFloat(v + OFF_NORM + 8, nz);
            if (unshade != 1.0f && MemoryUtil.memGetInt(v + OFF_USE_COLOR) != 0) {
                for (int c = 0; c < 3; c++) {
                    long channel = v + OFF_COLOR + (long) c * 4;
                    MemoryUtil.memPutFloat(channel, Math.min(MemoryUtil.memGetFloat(channel) * unshade, 1.0f));
                }
            }
        }
    }

    @Override
    public VertexConsumer addVertex(float x, float y, float z) {
        finishQuadIfComplete();
        ensureCapacity((long) (this.vertexCount + 1) * STRIDE);
        long v = this.address + (long) this.vertexCount * STRIDE;
        this.vertexCount++;
        this.current = v;

        MemoryUtil.memSet(v, 0, STRIDE);
        if (!placeable(x) || !placeable(y) || !placeable(z)) {
            // Written as zeroes only so the buffer holds no rubbish; the whole quad goes away once it completes.
            this.quadHasUnplaceableVertex = true;
            x = 0.0f;
            y = 0.0f;
            z = 0.0f;
        }
        MemoryUtil.memPutFloat(v + OFF_POS, x);
        MemoryUtil.memPutFloat(v + OFF_POS + 4, y);
        MemoryUtil.memPutFloat(v + OFF_POS + 8, z);
        MemoryUtil.memPutInt(v + OFF_TEXTURE_ID, this.textureId);
        MemoryUtil.memPutInt(v + OFF_GLINT_TEXTURE, this.glintTextureId);
        MemoryUtil.memPutInt(v + OFF_COORDINATE, this.coordinate);
        MemoryUtil.memPutFloat(v + OFF_ALBEDO_EMISSION, this.albedoEmission);
        MemoryUtil.memPutFloat(v + OFF_WIND, windCode(y));
        if (this.glowLight) {
            MemoryUtil.memPutFloat(v + OFF_CAST_LIGHT, this.castLight);
            MemoryUtil.memPutInt(v + OFF_CAST_COLOR, this.castColor);
        }
        MemoryUtil.memPutInt(v + OFF_ALPHA_MODE,
            this.alphaMode | (this.water ? WATER_FLAG : 0) | (this.rain ? RAIN_FLAG : 0) | (this.held ? HELD_FLAG : 0) | (this.glass ? GLASS_FLAG : 0)
                | (this.plant ? PLANT_FLAG : 0) | this.paneFlags | (this.blend ? BLEND_FLAG : 0)
                | (this.unlit ? UNLIT_FLAG : 0) | (this.lava ? LAVA_FLAG : 0) | (this.layerOffset != 0.0f ? LAYER_FLAG : 0)
                | (this.leaves ? LEAVES_FLAG : 0) | (this.glowLight ? GLOW_LIGHT_FLAG : 0) | (this.frost ? FROST_FLAG : 0));
        if (this.colorOverride != 0) {
            setColor(255, 255, 255, 255);
        }
        return this;
    }

    @Override
    public VertexConsumer setColor(int r, int g, int b, int a) {
        if (this.colorOverride != 0) {
            r = this.colorOverride >> 16 & 0xFF;
            g = this.colorOverride >> 8 & 0xFF;
            b = this.colorOverride & 0xFF;
        }
        if (this.colorTint != 0xFFFFFF) {
            r = r * (this.colorTint >> 16 & 0xFF) / 255;
            g = g * (this.colorTint >> 8 & 0xFF) / 255;
            b = b * (this.colorTint & 0xFF) / 255;
        }
        MemoryUtil.memPutInt(this.current + OFF_USE_COLOR, 1);
        MemoryUtil.memPutFloat(this.current + OFF_COLOR, r / 255.0f);
        MemoryUtil.memPutFloat(this.current + OFF_COLOR + 4, g / 255.0f);
        MemoryUtil.memPutFloat(this.current + OFF_COLOR + 8, b / 255.0f);
        MemoryUtil.memPutFloat(this.current + OFF_COLOR + 12, a / 255.0f);
        return this;
    }

    @Override
    public VertexConsumer setColor(int color) {
        return setColor(color >> 16 & 0xFF, color >> 8 & 0xFF, color & 0xFF, color >>> 24);
    }

    @Override
    public VertexConsumer setUv(float u, float v) {
        MemoryUtil.memPutInt(this.current + OFF_USE_TEXTURE, 1);
        MemoryUtil.memPutFloat(this.current + OFF_TEXTURE_UV, u + this.uOffset);
        MemoryUtil.memPutFloat(this.current + OFF_TEXTURE_UV + 4, v + this.vOffset);
        if (this.glintEnabled) {
            // The standard glint has no UV of its own in vanilla either - glint.vsh scrolls the block or item's
            // own texture coordinate through a shared animation matrix (WorldUBO.textureMat here), so the glint
            // rides the quad's UV rather than reading anything new.
            MemoryUtil.memPutInt(this.current + OFF_USE_GLINT, 1);
            MemoryUtil.memPutFloat(this.current + OFF_GLINT_UV, u);
            MemoryUtil.memPutFloat(this.current + OFF_GLINT_UV + 4, v);
        }
        return this;
    }

    @Override
    public VertexConsumer setUv1(int u, int v) {
        if (!this.overlayEnabled) {
            return this;
        }
        MemoryUtil.memPutInt(this.current + OFF_USE_OVERLAY, 1);
        MemoryUtil.memPutInt(this.current + OFF_OVERLAY_UV, u);
        MemoryUtil.memPutInt(this.current + OFF_OVERLAY_UV + 4, v);
        return this;
    }

    @Override
    public VertexConsumer setUv2(int u, int v) {
        MemoryUtil.memPutInt(this.current + OFF_USE_LIGHT, 1);
        MemoryUtil.memPutInt(this.current + OFF_LIGHT_UV, u);
        MemoryUtil.memPutInt(this.current + OFF_LIGHT_UV + 4, v);
        return this;
    }

    // 26.2 port: VertexConsumer has no setUv3 yet; kept so the writer reads as on main.
    public VertexConsumer setUv3(float u, float v) {
        MemoryUtil.memPutInt(this.current + OFF_USE_GLINT, 1);
        MemoryUtil.memPutFloat(this.current + OFF_GLINT_UV, u);
        MemoryUtil.memPutFloat(this.current + OFF_GLINT_UV + 4, v);
        return this;
    }

    @Override
    public VertexConsumer setNormal(float x, float y, float z) {
        if (this.layerOffset != 0.0f) {
            // A layer drawn over the same model (see layerOffset): the vertex moves out along its own normal.
            MemoryUtil.memPutFloat(this.current + OFF_POS, MemoryUtil.memGetFloat(this.current + OFF_POS) + x * this.layerOffset);
            MemoryUtil.memPutFloat(this.current + OFF_POS + 4,
                MemoryUtil.memGetFloat(this.current + OFF_POS + 4) + y * this.layerOffset);
            MemoryUtil.memPutFloat(this.current + OFF_POS + 8,
                MemoryUtil.memGetFloat(this.current + OFF_POS + 8) + z * this.layerOffset);
        }
        MemoryUtil.memPutInt(this.current + OFF_USE_NORM, 1);
        MemoryUtil.memPutFloat(this.current + OFF_NORM, x);
        MemoryUtil.memPutFloat(this.current + OFF_NORM + 4, y);
        MemoryUtil.memPutFloat(this.current + OFF_NORM + 8, z);
        return this;
    }

    @Override
    public VertexConsumer setLineWidth(float width) {
        return this;
    }

    /** A coordinate the acceleration structure can hold: finite, and inside the world the renderer traces. */
    private static boolean placeable(float value) {
        return Float.isFinite(value) && Math.abs(value) <= POSITION_LIMIT;
    }

    /** Call once all vertices are written so a trailing quad gets its computed normal. */
    public void finish() {
        finishQuadIfComplete();
    }

    /**
     * 26.2 port: switches the glint on for every vertex written since {@code from}, riding the vertex's own texture
     * coordinate as setUv does for a glinting writer. 26.2 draws the glint as a separate pass over geometry already
     * written; see EntityCollector.lastModel.
     */
    public void glintSince(int from, int glintTextureId) {
        finishQuadIfComplete();
        glintRange(from, this.vertexCount, glintTextureId);
    }

    /** As glintSince, for the vertices [from, to). Quads dropped since then may have shortened the buffer. */
    public void glintRange(int from, int to, int glintTextureId) {
        finishQuadIfComplete();
        for (int i = Math.max(from, 0); i < Math.min(to, this.vertexCount); i++) {
            long v = this.address + (long) i * STRIDE;
            MemoryUtil.memPutInt(v + OFF_USE_GLINT, 1);
            MemoryUtil.memPutFloat(v + OFF_GLINT_UV, MemoryUtil.memGetFloat(v + OFF_TEXTURE_UV) - this.uOffset);
            MemoryUtil.memPutFloat(v + OFF_GLINT_UV + 4, MemoryUtil.memGetFloat(v + OFF_TEXTURE_UV + 4) - this.vOffset);
            MemoryUtil.memPutInt(v + OFF_GLINT_TEXTURE, glintTextureId);
        }
    }

    @Override
    public void close() {
        if (this.address != 0L) {
            MemoryUtil.nmemFree(this.address);
            this.address = 0L;
        }
    }
}
