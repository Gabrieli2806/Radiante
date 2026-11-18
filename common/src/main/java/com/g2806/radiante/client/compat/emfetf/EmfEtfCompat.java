package com.g2806.radiante.client.compat.emfetf;

import com.g2806.radiante.client.RadianteClient;
import com.g2806.radiante.client.compat.OtherMods;
import com.g2806.radiante.mixin.render.ModelPartAccessor;
import java.io.InputStream;
import java.lang.classfile.ClassFile;
import java.lang.constant.ClassDesc;
import java.lang.constant.ConstantDescs;
import java.lang.constant.MethodTypeDesc;
import java.lang.invoke.MethodHandle;
import java.lang.invoke.MethodHandles;
import java.lang.invoke.MethodType;
import java.lang.reflect.Method;
import java.nio.charset.StandardCharsets;
import java.util.Collections;
import java.util.Map;
import java.util.Optional;
import java.util.Properties;
import java.util.WeakHashMap;
import java.util.function.Supplier;
import net.minecraft.client.model.Model;
import net.minecraft.client.model.geom.ModelPart;
import net.minecraft.resources.Identifier;
import net.minecraft.server.packs.resources.Resource;
import net.minecraft.server.packs.resources.ResourceManager;

/**
 * Compatibility bridge for Entity Model Features (EMF), Entity Texture Features (ETF), custom {@link ModelPart}
 * mods (Fresh Animations, Fresh Animations Player Extension, 3D Skin Layers, Figura) and OptiFine/CEM {@code _e.png}
 * emissive entity textures.
 *
 * <p>When EMF or ETF is installed, entity and hand models run through {@link RadianteModelFeatureRenderer} (a real
 * {@code ModelFeatureRenderer} subclass) after all {@code RenderLayer} submissions for the entity have recorded their
 * poses. That lets ETF's {@code ModelFeatureRenderer.Submit} and {@code prepareModel} hooks mount the entity's
 * {@code ETFEntityRenderState}, lets EMF's {@code applyArmorBipedPose} copy custom player animations onto worn armor,
 * and routes per-part texture overrides and emissive/enchanted overlays into {@code EntityCollector} through a
 * zero-overhead {@code ETFVertexConsumer} bridge generated at start-up with {@link ClassFile}.
 */
public final class EmfEtfCompat {

    private static final String EMF_CLASS = "traben.entity_model_features.EMF";
    private static final String ETF_CLASS = "traben.entity_texture_features.ETF";
    private static final String ETF_VC_CLASS = "traben.entity_texture_features.utils.ETFVertexConsumer";
    private static final String ETF_SUBMIT_DATA_CLASS = "traben.entity_texture_features.features.state.ETFSubmitData";
    private static final String ETF_LAYER_TEX_CLASS = "traben.entity_texture_features.utils.ETFRenderLayerWithTexture";
    private static final String ETF_MANAGER_CLASS = "traben.entity_texture_features.features.ETFManager";

    /** ETF sentinel packed light coords: {@code FULL_BRIGHT + 1} (eyes) and {@code FULL_BRIGHT + 2} (emissive). */
    public static final int ETF_LIGHT_U_EYES = 241;
    public static final int ETF_LIGHT_U_EMISSIVE = 242;
    public static final int ETF_LIGHT_V_FULL = 240;

    /** True for ETF's {@code EYES_FEATURE_LIGHT_VALUE} or {@code EMISSIVE_FEATURE_LIGHT_VALUE} sentinel light coords. */
    public static boolean isEtfSpecialLight(int u, int v) {
        return (u == ETF_LIGHT_U_EYES || u == ETF_LIGHT_U_EMISSIVE) && v == ETF_LIGHT_V_FULL;
    }

    private static final Map<ModelPart, Boolean> CUSTOM_PART_CACHE =
        Collections.synchronizedMap(new WeakHashMap<>());

    private static volatile boolean emfInstalled;
    private static volatile boolean etfInstalled;
    public static volatile boolean devForcePipeline;
    private static Supplier<EtfVertexConsumerBase> wrapperFactory;
    private static MethodHandle recycleSubmitDataHandle;
    private static Class<?> etfLayerWithTextureType;
    private static MethodHandle etfLayerGetIdHandle;
    private static MethodHandle etfManagerInstanceHandle;
    private static MethodHandle etfGetTextureNoVariationHandle;

    private EmfEtfCompat() {
    }

    public static void init() {
        emfInstalled = OtherMods.has(EMF_CLASS);
        etfInstalled = OtherMods.has(ETF_CLASS);
        if (!emfInstalled && !etfInstalled) {
            return;
        }
        if (etfInstalled) {
            resolveEtfHandles();
        }
        RadianteClient.LOGGER.info(
            "EMF/ETF integration active (EMF={}, ETF={}, vertexBridge={})",
            emfInstalled, etfInstalled, wrapperFactory != null);
    }

    public static boolean isEmfInstalled() {
        return emfInstalled;
    }

    public static boolean isEtfInstalled() {
        return etfInstalled;
    }

    /**
     * True when {@code model} must execute through {@link RadianteModelFeatureRenderer} instead of rigid {@code Cube}
     * instancing: whenever EMF or ETF is installed, or when any node in {@code model.root()} subclasses
     * {@link ModelPart} (custom geometry, per-part render hooks or texture overrides).
     */
    public static boolean shouldUseFeaturePipeline(Model<?> model) {
        if (emfInstalled || etfInstalled || devForcePipeline) {
            return true;
        }
        return model != null && hasCustomModelParts(model.root());
    }

    /** Weakly cached check for any non-vanilla {@link ModelPart} subclass in a model's hierarchy. */
    public static boolean hasCustomModelParts(ModelPart root) {
        if (root == null) {
            return false;
        }
        Boolean cached = CUSTOM_PART_CACHE.get(root);
        if (cached != null) {
            return cached;
        }
        boolean custom = scanCustomParts(root);
        CUSTOM_PART_CACHE.put(root, custom);
        return custom;
    }

    private static boolean scanCustomParts(ModelPart part) {
        if (part.getClass() != ModelPart.class) {
            return true;
        }
        Map<String, ModelPart> children = ((ModelPartAccessor) (Object) part).radiante$children();
        if (children.isEmpty()) {
            return false;
        }
        for (ModelPart child : children.values()) {
            if (scanCustomParts(child)) {
                return true;
            }
        }
        return false;
    }

    /** Creates an {@code ETFVertexConsumer}-implementing wrapper when ETF is active, or null when not needed. */
    public static EtfVertexConsumerBase createEtfWrapper() {
        Supplier<EtfVertexConsumerBase> factory = wrapperFactory;
        return factory != null ? factory.get() : null;
    }

    /** Looks up the {@code ETFTexture} associated with a {@code RenderType} via ETF's own interface, if available. */
    static Object lookupEtfTexture(Object renderLayer) {
        if (renderLayer == null || etfLayerWithTextureType == null || !etfLayerWithTextureType.isInstance(renderLayer)) {
            return null;
        }
        try {
            Optional<?> optId = (Optional<?>) etfLayerGetIdHandle.invoke(renderLayer);
            if (optId == null || optId.isEmpty()) {
                return null;
            }
            Object manager = etfManagerInstanceHandle.invoke();
            return manager == null ? null : etfGetTextureNoVariationHandle.invoke(manager, optId.get());
        } catch (Throwable ignored) {
            return null;
        }
    }

    /** Recycles ETF's per-frame {@code ETFSubmitData} pool after Radiante finishes collecting entities. */
    public static void endFrame() {
        MethodHandle recycle = recycleSubmitDataHandle;
        if (recycle != null) {
            try {
                recycle.invokeExact();
            } catch (Throwable ignored) {
            }
        }
    }

    /**
     * Reads the emissive texture suffix configured by {@code optifine/emissive.properties} or
     * {@code mcpatcher/emissive.properties}, defaulting to {@code "_e"}.
     */
    public static String readEmissiveSuffix(ResourceManager resources) {
        if (resources == null) {
            return "_e";
        }
        for (String path : new String[] {"optifine/emissive.properties", "mcpatcher/emissive.properties"}) {
            Optional<Resource> res = resources.getResource(Identifier.withDefaultNamespace(path));
            if (res.isPresent()) {
                try (InputStream in = res.get().open()) {
                    Properties props = new Properties();
                    props.load(new java.io.InputStreamReader(in, StandardCharsets.UTF_8));
                    String suffix = parseEmissiveSuffix(props);
                    if (suffix != null) {
                        return suffix;
                    }
                } catch (Exception ignored) {
                }
            }
        }
        return "_e";
    }

    /** Extracts a valid {@code suffix.emissive} property value, or {@code "_e"} when absent or blank. */
    public static String parseEmissiveSuffix(Properties props) {
        if (props == null) {
            return "_e";
        }
        String raw = props.getProperty("suffix.emissive");
        if (raw == null) {
            return "_e";
        }
        String trimmed = raw.trim();
        if (trimmed.isEmpty() || trimmed.equals(".png")) {
            return "_e";
        }
        if (trimmed.endsWith(".png")) {
            trimmed = trimmed.substring(0, trimmed.length() - 4);
        }
        return trimmed.isEmpty() ? "_e" : trimmed;
    }

    /**
     * Folds an OptiFine/ETF {@code _e.png} pixel (ARGB) into a LabPBR specular pixel (RGBA in memory order) when the
     * specular map did not already author an explicit LabPBR emission ({@code alpha} in {@code 1..254}).
     */
    public static int applyEmissiveToLabPbrRgba(int baseSpecularRgba, int emissiveArgb) {
        int baseAlpha = (baseSpecularRgba >>> 24) & 0xFF;
        if (baseAlpha >= 1 && baseAlpha <= 254) {
            return baseSpecularRgba;
        }
        int eAlpha = (emissiveArgb >>> 24) & 0xFF;
        int eRed = (emissiveArgb >>> 16) & 0xFF;
        int eGreen = (emissiveArgb >>> 8) & 0xFF;
        int eBlue = emissiveArgb & 0xFF;
        int peak = Math.max(eRed, Math.max(eGreen, eBlue));
        if (eAlpha == 0 || peak == 0) {
            return baseSpecularRgba;
        }
        int labAlpha = Math.clamp(Math.round((eAlpha / 255.0f) * (peak / 255.0f) * 254.0f), 1, 254);
        return (baseSpecularRgba & 0x00FFFFFF) | (labAlpha << 24);
    }

    private static void resolveEtfHandles() {
        ClassLoader loader = EmfEtfCompat.class.getClassLoader();
        MethodHandles.Lookup lookup = MethodHandles.publicLookup();
        try {
            Class<?> vcInterface = Class.forName(ETF_VC_CLASS, false, loader);
            Supplier<EtfVertexConsumerBase> generated = buildBridgeFactory(vcInterface, "RuntimeBridge");
            wrapperFactory = generated;
        } catch (Throwable e) {
            RadianteClient.LOGGER.warn("Could not build ETFVertexConsumer bridge; per-part texture overrides disabled", e);
        }
        try {
            Class<?> submitData = Class.forName(ETF_SUBMIT_DATA_CLASS, false, loader);
            recycleSubmitDataHandle = lookup.findStatic(submitData, "recycle", MethodType.methodType(void.class));
        } catch (Throwable ignored) {
        }
        try {
            etfLayerWithTextureType = Class.forName(ETF_LAYER_TEX_CLASS, false, loader);
            etfLayerGetIdHandle = lookup.findVirtual(etfLayerWithTextureType, "etf$getId",
                MethodType.methodType(Optional.class));
            Class<?> managerClass = Class.forName(ETF_MANAGER_CLASS, false, loader);
            etfManagerInstanceHandle = lookup.findStatic(managerClass, "getInstance",
                MethodType.methodType(managerClass));
            etfGetTextureNoVariationHandle = lookup.findVirtual(managerClass, "getETFTextureNoVariation",
                MethodType.methodType(
                    Class.forName("traben.entity_texture_features.features.texture_handlers.ETFTexture", false, loader),
                    Identifier.class));
        } catch (Throwable ignored) {
            etfLayerWithTextureType = null;
        }
    }

    /**
     * Generates a concrete {@link EtfVertexConsumerBase} subclass implementing {@code vcInterface} using Java's
     * standard {@link ClassFile} API. Package-private for unit tests.
     */
    static Supplier<EtfVertexConsumerBase> buildBridgeFactory(Class<?> vcInterface, String suffix) throws Throwable {
        if (!vcInterface.isInterface()) {
            throw new IllegalArgumentException(vcInterface + " is not an interface");
        }
        EmfEtfCompat.class.getModule().addReads(vcInterface.getModule());

        ClassDesc baseDesc = ClassDesc.of(EtfVertexConsumerBase.class.getName());
        ClassDesc implDesc = ClassDesc.of(EtfVertexConsumerBase.class.getPackageName(),
            "EtfVertexConsumerBridge$" + suffix);
        ClassDesc ifaceDesc = ClassDesc.ofDescriptor(vcInterface.descriptorString());
        MethodTypeDesc voidNoArgs = MethodTypeDesc.of(ConstantDescs.CD_void);
        MethodTypeDesc initEtfDesc = MethodTypeDesc.of(ConstantDescs.CD_void,
            ConstantDescs.CD_Object, ConstantDescs.CD_Object);

        byte[] bytes = ClassFile.of().build(implDesc, cb -> {
            cb.withFlags(ClassFile.ACC_PUBLIC | ClassFile.ACC_FINAL | ClassFile.ACC_SUPER);
            cb.withSuperclass(baseDesc);
            cb.withInterfaceSymbols(ifaceDesc);

            cb.withMethodBody(ConstantDescs.INIT_NAME, voidNoArgs, ClassFile.ACC_PUBLIC, code -> {
                code.aload(0);
                code.invokespecial(baseDesc, ConstantDescs.INIT_NAME, voidNoArgs);
                code.return_();
            });

            for (Method method : vcInterface.getDeclaredMethods()) {
                if (method.isDefault() || java.lang.reflect.Modifier.isStatic(method.getModifiers())) {
                    continue;
                }
                String name = method.getName();
                Class<?> returnType = method.getReturnType();
                Class<?>[] params = method.getParameterTypes();
                ClassDesc retDesc = ClassDesc.ofDescriptor(returnType.descriptorString());
                ClassDesc[] paramDescs = new ClassDesc[params.length];
                for (int i = 0; i < params.length; i++) {
                    paramDescs[i] = ClassDesc.ofDescriptor(params[i].descriptorString());
                }
                MethodTypeDesc mDesc = MethodTypeDesc.of(retDesc, paramDescs);

                if (name.equals("etf$initETFVertexConsumer") && params.length == 2 && returnType == void.class) {
                    cb.withMethodBody(name, mDesc, ClassFile.ACC_PUBLIC, code -> {
                        code.aload(0);
                        code.aload(1);
                        code.aload(2);
                        code.invokevirtual(baseDesc, "initEtf", initEtfDesc);
                        code.return_();
                    });
                } else if (params.length == 0 && !returnType.isPrimitive()) {
                    String fieldName = switch (name) {
                        case "etf$getETFTexture" -> "etfTexture";
                        case "etf$getProvider" -> "etfProvider";
                        case "etf$getRenderLayer" -> "etfRenderLayer";
                        default -> null;
                    };
                    cb.withMethodBody(name, mDesc, ClassFile.ACC_PUBLIC, code -> {
                        if (fieldName != null) {
                            code.aload(0);
                            code.getfield(baseDesc, fieldName, ConstantDescs.CD_Object);
                            if (returnType != Object.class) {
                                code.checkcast(retDesc);
                            }
                        } else {
                            code.aconst_null();
                        }
                        code.areturn();
                    });
                } else {
                    throw new UnsupportedOperationException("Unexpected ETFVertexConsumer method: " + method);
                }
            }
        });

        MethodHandles.Lookup lookup = MethodHandles.privateLookupIn(EtfVertexConsumerBase.class, MethodHandles.lookup());
        Class<?> defined = lookup.defineClass(bytes);
        MethodHandle ctor = lookup.findConstructor(defined, MethodType.methodType(void.class))
            .asType(MethodType.methodType(EtfVertexConsumerBase.class));
        return () -> {
            try {
                return (EtfVertexConsumerBase) ctor.invokeExact();
            } catch (Throwable e) {
                throw new RuntimeException(e);
            }
        };
    }
}
