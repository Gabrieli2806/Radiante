package com.g2806.radiante.client.compat.emfetf;

import java.lang.reflect.Proxy;
import java.util.Properties;
import java.util.function.Supplier;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.*;

class EmfEtfCompatTest {

    public static final class DummyProvider {
    }

    public static final class DummyRenderLayer {
    }

    public static final class DummyTexture {
    }

    public interface MockEtfVertexConsumer {
        DummyTexture etf$getETFTexture();

        DummyProvider etf$getProvider();

        DummyRenderLayer etf$getRenderLayer();

        void etf$initETFVertexConsumer(DummyProvider provider, DummyRenderLayer renderLayer);
    }

    @Test
    void generatedBridgeImplementsEtfVertexConsumerContractAndForwardsVertices() throws Throwable {
        Supplier<EtfVertexConsumerBase> factory =
            EmfEtfCompat.buildBridgeFactory(MockEtfVertexConsumer.class, "UnitTest");
        Object instance = factory.get();
        assertNotNull(instance);
        assertInstanceOf(MockEtfVertexConsumer.class, instance);

        Class<?> vcType = Class.forName("com.mojang.blaze3d.vertex.VertexConsumer");
        float[] recorded = new float[5];
        Object mockVc = Proxy.newProxyInstance(vcType.getClassLoader(), new Class<?>[] {vcType},
            (proxy, method, args) -> {
                switch (method.getName()) {
                    case "addVertex" -> {
                        if (args != null && args.length == 3) {
                            recorded[0] = (Float) args[0];
                            recorded[1] = (Float) args[1];
                            recorded[2] = (Float) args[2];
                        }
                    }
                    case "setUv" -> {
                        recorded[3] = (Float) args[0];
                        recorded[4] = (Float) args[1];
                    }
                    default -> {
                    }
                }
                return proxy;
            });

        EtfVertexConsumerBase.class.getMethod("bind", vcType).invoke(instance, mockVc);

        DummyProvider provider = new DummyProvider();
        DummyRenderLayer layer = new DummyRenderLayer();
        MockEtfVertexConsumer etf = (MockEtfVertexConsumer) instance;
        etf.etf$initETFVertexConsumer(provider, layer);
        assertSame(provider, etf.etf$getProvider());
        assertSame(layer, etf.etf$getRenderLayer());
        assertNull(etf.etf$getETFTexture());

        vcType.getMethod("addVertex", float.class, float.class, float.class).invoke(instance, 1.5f, 2.5f, -3.0f);
        vcType.getMethod("setUv", float.class, float.class).invoke(instance, 0.25f, 0.75f);
        assertEquals(1.5f, recorded[0], 1.0e-6f);
        assertEquals(2.5f, recorded[1], 1.0e-6f);
        assertEquals(-3.0f, recorded[2], 1.0e-6f);
        assertEquals(0.25f, recorded[3], 1.0e-6f);
        assertEquals(0.75f, recorded[4], 1.0e-6f);

        EtfVertexConsumerBase.class.getMethod("clear").invoke(instance);
        assertNull(etf.etf$getProvider());
        assertNull(etf.etf$getRenderLayer());
    }

    @Test
    void applyEmissiveToLabPbrPreservesMaterialChannelsAndExplicitSpecularGlow() {
        int baseMetalNoGlow = 0xFF00E6C0; // A=255 (no LabPBR emission), B=0, G=230 (iron), R=192 (smooth)
        int fullCyanEmissive = 0xFF00FFFF;
        int folded = EmfEtfCompat.applyEmissiveToLabPbrRgba(baseMetalNoGlow, fullCyanEmissive);
        assertEquals(0x00E6C0, folded & 0x00FFFFFF);
        assertEquals(254, (folded >>> 24) & 0xFF);

        // Explicit LabPBR emission (alpha 100) wins over _e.png fallback.
        int authoredGlow = (100 << 24) | 0x001080;
        assertEquals(authoredGlow, EmfEtfCompat.applyEmissiveToLabPbrRgba(authoredGlow, fullCyanEmissive));

        // Transparent or black _e.png texels leave base unchanged.
        assertEquals(0, EmfEtfCompat.applyEmissiveToLabPbrRgba(0, 0x00FFFFFF));
        assertEquals(baseMetalNoGlow, EmfEtfCompat.applyEmissiveToLabPbrRgba(baseMetalNoGlow, 0xFF000000));
    }

    @Test
    void parseEmissiveSuffixHandlesCustomAndMalformedProperties() {
        assertEquals("_e", EmfEtfCompat.parseEmissiveSuffix(null));
        Properties props = new Properties();
        assertEquals("_e", EmfEtfCompat.parseEmissiveSuffix(props));
        props.setProperty("suffix.emissive", " _emissive.png ");
        assertEquals("_emissive", EmfEtfCompat.parseEmissiveSuffix(props));
        props.setProperty("suffix.emissive", "   ");
        assertEquals("_e", EmfEtfCompat.parseEmissiveSuffix(props));
    }

    @Test
    void etfSpecialLightDetectsOnlyEtfSentinelPackedLightValues() {
        assertTrue(EmfEtfCompat.isEtfSpecialLight(241, 240));
        assertTrue(EmfEtfCompat.isEtfSpecialLight(242, 240));
        // Vanilla LightCoordsUtil.pack(15, 15) produces u=240, v=240 and must not be mistaken for ETF emissive.
        assertFalse(EmfEtfCompat.isEtfSpecialLight(240, 240));
        assertFalse(EmfEtfCompat.isEtfSpecialLight(0, 0));
        assertFalse(EmfEtfCompat.isEtfSpecialLight(242, 0));
    }
}
