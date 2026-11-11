package com.g2806.radiante.client.gui;

/**
 * The colours and measures Radiante's own screens share (the settings screen, Shader Packs, Share Settings, the wait
 * and download screens, the rebuild toast), so they read as one place and a change of look is made once. Colours are
 * ARGB. A screen's own colours and measures stay in the screen.
 */
final class Theme {

    private Theme() {
    }

    /** Selected entries, progress bars, what is on. */
    static final int ACCENT = 0xFF8FE3C8;
    static final int TEXT = 0xFFFFFFFF;
    /** Descriptions and secondary lines. */
    static final int TEXT_DIM = 0xFFB0B8B4;
    static final int PANEL = 0xC0101212;
    static final int PANEL_DARK = 0xD0060707;
    static final int ROW_HOVER = 0x30FFFFFF;
    /** A button that cannot be pressed now. */
    static final int DISABLED = 0xFF606060;
    /** What failed. */
    static final int BAD = 0xFFFF8080;

    /** A row of the list, a section's heading, the space between panels and inside them. */
    static final int ROW = 20;
    static final int HEADER = 22;
    static final int GAP = 6;
    static final int PAD = 8;
    /** A sidebar entry and a small button. */
    static final int ENTRY = 18;
    static final int BUTTON = 16;
}
