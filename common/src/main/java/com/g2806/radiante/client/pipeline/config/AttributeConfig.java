package com.g2806.radiante.client.pipeline.config;

public class AttributeConfig {

    public String type;
    public String name;
    public String value;
    /**
     * The shader pack reads this value from a uniform instead of having it compiled in, so a change reaches the
     * renderer live (Pipeline.pushRuntimeChanges) without a build. The pack's attribute list (configs.json) says
     * which ones are.
     */
    public boolean runtime;
}
