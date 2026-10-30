[← Volver al índice](README.md)

# Compilar y publicar

Para quien quiere compilar Radiante o publicar una versión. Jugar no necesita nada de esto.

El mod tiene dos partes: el renderizador nativo en C++, PrismRT (`native/`, compilado con CMake), y
el mod en Java (Gradle, un jar por loader). El renderizador se compila por sistema: `prismrt.dll` en
Windows, `libprismrt.so` en Linux.

## Requisitos comunes

- Java 25.
- Vulkan SDK `1.4.341` o más reciente, con `VULKAN_SDK` definido y `glslangValidator` en el `PATH`.
- Las librerías NGX del SDK de DLSS, que no están en el repositorio (los runtimes de DLSS nunca se
  distribuyen). Una vez, en la versión que espera la compilación:

```sh
git clone --depth 1 --branch v310.9.1 https://github.com/NVIDIA/DLSS.git /tmp/dlss
mkdir -p native/extern/DLSS/lib
cp -r /tmp/dlss/lib/Windows_x86_64 native/extern/DLSS/lib/   # o Linux_x86_64 en Linux
```

## Windows

Visual Studio 2026 con la carga de trabajo de C++.

```sh
native/build-windows.bat
./gradlew.bat build
```

`native/build-windows.bat` configura, compila e instala con CMake en `C:\rtb\radiante` (o en
`RADIANTE_BUILD_DIR` si está definido). El paso de instalación copia `prismrt.dll`, los shaders (el
trazador de rayos incluido como `shaders/world/ray_tracing/radiante-pt.zip`) y los módulos a
`common/src/main/resources/radiante-native/`, que no está en git. Los jars quedan en
`fabric/build/libs`, `neoforge/build/libs` y `forge/build/libs`.

Gradle no compila la parte nativa: cualquier cambio en `native/src/shader` o `native/src/prism`, y
cada cambio de rama, necesita la compilación nativa otra vez. Revisa después la fecha de
`prismrt.dll` y de `radiante-pt.zip`; si el reloj del sistema retrocedió, MSBuild se salta los
cambios y hace falta una compilación limpia. Desde un git worktree la ruta puede ser demasiado larga
para el compilador de shaders de FidelityFX: asigna el worktree a una ruta corta
(`subst W: <worktree>`) y pon en `RADIANTE_BUILD_DIR` una carpeta de compilación propia.

Para probar: `./gradlew.bat :fabric:runClient` (o `:neoforge:runClient`, `:forge:runClient`). Cada
uno usa su propia carpeta: `run-fabric/`, `run-neoforge/` y `run-forge/`, así que pueden correr a
la vez. Comparten `run-shared/`: mundos, resource packs, shader packs, los ajustes de Radiante, `options.txt` y la
lista de servidores (los mods y sus configs son de cada loader).

## Linux

cmake, ninja (o make), g++ 13 o más reciente, git y el Vulkan SDK.

```sh
native/build-linux.sh
./gradlew build
```

El script compila en `native/build/linux-release` (o en `RADIANTE_BUILD_DIR`) e instala
`libprismrt.so` en `radiante-native/linux-x64/`. Si CMake no encuentra Vulkan, pásale
`-DVulkan_LIBRARY=/usr/lib/x86_64-linux-gnu/libvulkan.so.1`. Para que el resultado cargue en
Ubuntu 22.04 compila en un sistema con glibc 2.35 (o comprueba con
`objdump -T libprismrt.so | grep -oE 'GLIBC_2\.[0-9]+' | sort -Vu | tail -1`).

## Un jar para los dos sistemas

Un jar compilado en un solo sistema trae solo su renderizador. Para uno que funcione en ambos, pon
`prismrt.dll` y `linux-x64/libprismrt.so` en `radiante-native/` antes de `gradlew build`. Las
librerías van comprimidas con xz junto a `natives.sha256`; el mod las desempaqueta y comprueba al
primer inicio.

Comprobar un jar: `unzip -l <jar> | grep -E 'prismrt.dll|libprismrt.so'`.

## GitHub Actions

`.github/workflows/build.yml` hace todo lo anterior en cada push a una rama de versión de Minecraft (`mc/26.2`, `mc/26.3`, `mc/26.4`) y en cada pull request:

| Job | Qué hace |
|---|---|
| `version` | Lee `mod_version` y `minecraft_version` y decide si es una versión nueva. |
| `native-windows` | Compila `prismrt.dll` en Windows con el Vulkan SDK y el SDK de DLSS. |
| `native-linux` | Compila `libprismrt.so` en Ubuntu 22.04 con g++ 13 y comprueba la versión de glibc que pide. |
| `jars` | Junta los dos renderizadores, compila los jars y falla si alguno no trae ambos. |
| `release` | Solo si la versión es nueva: release en GitHub. |
| `publish` | Solo si la versión es nueva, no es preliminar y está configurado: Modrinth y CurseForge. |

Los push que solo tocan Markdown, `docs/` o `screenshots/` no compilan.

## Publicar una versión

1. Añade `## [x.y.z] - fecha` a `changelog.md`.
2. Sube `mod_version` en `gradle.properties` y haz push a la rama de la versión de Minecraft.
3. El workflow ve que no existe la etiqueta `v<mod_version>+<minecraft_version>` (por ejemplo
   `v1.1.0+26.3`), compila y publica un release en GitHub con los jars. Las notas son la sección
   del changelog de esa versión. Una versión con `-` (`1.2.0-beta.1`) se marca como preliminar y se
   queda en GitHub.
4. Si están configurados, sube el jar de cada loader a Modrinth y CurseForge (una versión `0.x` como
   beta, cualquier otra como release). Un loader para el que la rama no compila jar se omite (el
   port a 26.4 es solo Fabric). Una vez, en los ajustes del repositorio:
   - Secrets: `MODRINTH_TOKEN`, `CURSEFORGE_TOKEN`.
   - Variables: `MODRINTH_ID`, `CURSEFORGE_ID`.

   Un sitio sin token o sin ID se omite; el release de GitHub no depende de ellos.

Los pull requests compilan pero nunca publican.
