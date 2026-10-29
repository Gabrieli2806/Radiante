package com.g2806.radiante.client.render;

/**
 * How brightly things that glow on their own are traced, in one place and on one scale: the light level they read
 * as, 0 to 15, as vanilla gives blocks. Blocks get their glow from their emission maps (see EmissionTiles); these
 * are for what has no emission map of its own - lava's surface, far terrain, glowing entities and effects.
 *
 * The value is added to a surface as its texture colour times this, so a level 15 glow keeps the texture's colour
 * rather than burning to white. Effects that are brighter than any block (a beacon beam, lightning) are given as
 * multiples of a level 15 block.
 */
public final class Glow {

    /** A level 15 source's glow: lava at this reads as the molten orange of Bedrock RTX. */
    public static final float FULL = 2.0f;

    /** The glow of a light level, 0 to 15. */
    public static float ofLevel(float level) {
        return FULL * level / 15.0f;
    }

    public static final float LAVA = ofLevel(15);
    /** Enderman, spider and phantom eyes: bright enough to read in the dark, dim enough to stay purple or red. */
    public static final float MOB_EYES = ofLevel(4);
    public static final float NAME_TAG = ofLevel(8);
    public static final float GLOW_ITEM_FRAME = ofLevel(11);
    public static final float END_CRYSTAL = ofLevel(15);
    /** Debug gizmos and outlines drawn over the world. */
    public static final float GIZMO = ofLevel(15);
    /** The swirling shell of a charged creeper. */
    public static final float ENERGY_SWIRL = ofLevel(9);

    /** A wither's armour shell: an effect, brighter than any block. */
    public static final float WITHER_ARMOR = FULL * 2.0f;
    /** The flames on a burning entity. */
    public static final float ENTITY_FLAME = FULL * 3.0f;
    /** Bright enough to stand out at night. It is only seen (RenderTypeInfo.emission): the beacon block lights around. */
    public static final float BEACON_BEAM = FULL * 1.5f;
    /** Seen as a white-hot streak, and lighting only the land close by: at ten times a block it lit everything around
     *  it near white by night, the eye being opened up for the dark; radiante-pt's flash (LightningFlash) lights the
     *  rest of the landscape in cold blue. */
    public static final float LIGHTNING = FULL * 2.5f;

    private Glow() {
    }
}
