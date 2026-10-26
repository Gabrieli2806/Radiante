[← Settings](../settings.md) · [Index](../README.md)

# Quality

![Radiante settings: Quality](../../images/ui/settings-quality.png)

| Control | Values | Default | What it does |
|---|---|---|---|
| **Ray Tracing** | On / Off | On | Switches between Radiante's ray traced renderer and Minecraft's own, at once, like the Toggle Ray Tracing key (unbound by default; set it in Controls). The world is rebuilt for the renderer that takes over. Shown in a world. |
| **Quality** | Low · Medium · High · Ultra · Custom | — | Sets the costliest settings at once. See [Quality and Reset to Defaults](../settings.md#quality-and-reset-to-defaults). |
| **Pipeline** | RT-DLSS · RT-Native · RT-FSR · RT-XeSS. Those not installed show as `(Install)` or `(Restart required)` | RT-DLSS when DLSS is installed and the GPU runs it; otherwise RT-XeSS if installed, else RT-FSR | Denoiser and upscaler. See below and [Upscalers](../upscalers.md). |
| **DLSS Mode** | Ultra Performance · Performance · Balanced · Quality · DLAA · Custom | Ultra Performance | RT-DLSS only. The resolution DLSS renders at before upscaling. Custom shows Render Resolution. |
| **Ray Reconstruction** | On / Off | On | RT-DLSS only. DLSS's own denoiser; off, NRD denoises and DLSS only upscales, which can help against ghosting. Shown only where the GPU can run both. |
| **Upscaling Mode** | FSR: Ultra Performance · Performance · Balanced · Quality · Native AA. XeSS adds Ultra Quality and Ultra Quality Plus. Both end with Custom | Ultra Performance | The same as DLSS Mode, for FSR and XeSS. The Quality level sets it too. |
| **Render Resolution** | 20% – 100% with DLSS, 33% – 100% with FSR and XeSS | 50% | Only in the **Custom** mode: the share of the screen resolution the world is traced at before the upscaler scales it up. Lower is faster and softer. Picking a Quality level or another mode leaves it. |
| **Output Resolution** | Native · 1440p · 1080p · 900p · 720p · Custom (only the sizes below the window's are listed) | Native | The size the world is rendered and upscaled to before a final FSR 1 upscale fills the window. The HUD and text stay sharp. On a 4K screen this saves the most. |
| **Output Scale** | 25% – 99% | 75% | Only with Output Resolution set to Custom: the share of the window's size the world is rendered at. Shows the size that comes out. |
| **Frame Generation Type** | Auto · DLSS · FSR | Auto | Shown where the GPU runs both. Auto picks DLSS when the GPU supports it, FSR otherwise. FSR works with any upscaler but only doubles. |
| **Frame Generation** | Off · 2x … 4x | Off | Extra frames between the rendered ones. Hidden with HDR Output. See [frame-generation-reflex.md](../frame-generation-reflex.md). |
| **NVIDIA Reflex** | On / Off | Off | Low latency. NVIDIA with `VK_NV_low_latency2` only; hidden on other GPUs. No restart. |

## The pipelines

- **RT-DLSS**: with Ray Reconstruction on, DLSS denoises and upscales at once, the least noisy;
  with it off, NRD denoises and DLSS upscales. NVIDIA only, downloaded in game.
- **RT-FSR**: NRD (RELAX) denoiser and FSR 3. Any GPU, built in. The default when DLSS is not
  installed.
- **RT-XeSS**: NRD and XeSS. Windows, downloaded in game. Recommended on Intel Arc.
- **RT-Native**: NRD with no upscaler, at native resolution. Any GPU; the most expensive.

## Picking a pipeline installs nothing by itself

Picking `RT-DLSS (Install)` puts **Install DLSS (115 MB)** in the bottom bar. Installing or
deleting is done outside a world and ends in a restart. On the next start Radiante asks whether to
switch to the DLSS (or XeSS) pipeline; you can also pick it here later. See
[Upscalers](../upscalers.md).
