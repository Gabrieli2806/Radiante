package com.g2806.radiante.mixin.backend;

import com.g2806.radiante.client.proxy.vulkan.RendererProxy;
import com.mojang.blaze3d.vulkan.VulkanBackend;
import org.lwjgl.PointerBuffer;
import org.lwjgl.vulkan.VkAllocationCallbacks;
import org.lwjgl.vulkan.VkDeviceCreateInfo;
import org.lwjgl.vulkan.VkPhysicalDevice;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.Redirect;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

@Mixin(VulkanBackend.class)
public class VulkanBackendMixin {

    @Redirect(method = "createDevice(Ljava/util/Collection;Lcom/mojang/blaze3d/vulkan/VulkanPhysicalDevice;Ljava/util/Set;)Lorg/lwjgl/vulkan/VkDevice;",
        at = @At(value = "INVOKE",
            target = "Lorg/lwjgl/vulkan/VK12;vkCreateDevice(Lorg/lwjgl/vulkan/VkPhysicalDevice;Lorg/lwjgl/vulkan/VkDeviceCreateInfo;Lorg/lwjgl/vulkan/VkAllocationCallbacks;Lorg/lwjgl/PointerBuffer;)I"))
    private static int radiante$createMergedDevice(VkPhysicalDevice physicalDevice, VkDeviceCreateInfo createInfo,
        VkAllocationCallbacks allocator, PointerBuffer device) {
        // Without the native renderer the device is created as Minecraft asked, with no ray tracing extensions.
        if (!com.g2806.radiante.client.RadianteClient.isNativeAvailable()) {
            return org.lwjgl.vulkan.VK12.vkCreateDevice(physicalDevice, createInfo, allocator, device);
        }
        return RendererProxy.createDevice(physicalDevice.address(), createInfo.address(),
            allocator == null ? 0L : allocator.address(), device.address());
    }

    /**
     * Devices without hardware ray tracing are rejected so a capable GPU gets picked. 26.2 port: 26.2 answers
     * suitability with a boolean (isDeviceSuitable) rather than 26.3's reason (checkDeviceSuitability).
     */
    @Inject(method = "isDeviceSuitable", at = @At("RETURN"), cancellable = true)
    private static void radiante$requireRayTracing(VkPhysicalDevice device, CallbackInfoReturnable<Boolean> cir) {
        // Nothing to require when the renderer cannot run anyway: any device Minecraft likes will do.
        if (cir.getReturnValueZ() && com.g2806.radiante.client.RadianteClient.isNativeAvailable()
            && !RendererProxy.isRayTracingCapable(device.address())) {
            radiante$rejectedForRayTracing = true;
            cir.setReturnValue(false);
        }
    }

    /** A device that Minecraft would have taken was turned down above for lacking hardware ray tracing. */
    @org.spongepowered.asm.mixin.Unique
    private static boolean radiante$rejectedForRayTracing;

    /**
     * 26.2 port: no device was left, so Minecraft gives up on Vulkan and falls back to OpenGL. When the check above
     * is what emptied the list, the warning screen names the missing ray tracing rather than OpenGL (26.3 carries
     * that in the BackendCreationException reason instead).
     */
    @Inject(method = "findPhysicalDevice", at = @At(value = "INVOKE",
        target = "Lcom/mojang/blaze3d/vulkan/VulkanBackend;throwForMissingRequrements(Lorg/lwjgl/vulkan/VkPhysicalDevice;)V"))
    private static void radiante$noRayTracingDevice(CallbackInfoReturnable<?> cir) {
        if (radiante$rejectedForRayTracing) {
            com.g2806.radiante.client.render.RadianteRenderer.markNoRayTracing();
        }
    }
}
