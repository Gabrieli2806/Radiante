# Additional permissions under GNU GPL version 3 section 7

Radiante is licensed under the GNU General Public License version 3 (GPL-3.0).

## NVIDIA and Intel runtime libraries

As an additional permission under section 7 of the GPL-3.0, the copyright holders of Radiante grant you permission
to combine Radiante (or a modified version of it) with, and to convey it together with, the NVIDIA DLSS, NVIDIA
Streamline, NVIDIA Reflex and Intel XeSS runtime libraries listed in [THIRD-PARTY-NOTICES.md](common/src/main/resources/licenses/THIRD-PARTY-NOTICES.md)
(or modified versions of those libraries released by their vendors), even though those libraries are not licensed
under the GPL. The corresponding source for such a combination does not need to include the source code of those
libraries.

Those libraries remain under their own licences and are not made subject to the GPL by this permission.

## Shader packs

As an additional permission under section 7 of the GPL-3.0, the copyright holders of Radiante grant you permission
to convey a shader pack under terms of your choice, and to combine it with Radiante and Radiante PT when they load
it, if the pack uses them only through their pack interface: the headers under `radiante/v1/`
(`native/src/shader/include/radiante/v1/` in Radiante's source), included as `#include <radiante/v1/...>`, and
Radiante PT's hook interface version 1 (`radiante-pt@1`): its hook files under `hooks/`, which a pack replaces with
its own, and `common/pack_*.glsl`. In the spirit of the GCC Runtime Library Exception, this lets packs keep their own
licence.

Radiante, Radiante PT and the pack interface itself stay under the GPL-3.0. Files a pack copies from them, other than
the hook files it replaces, stay under the GPL-3.0 too, and a pack that uses other parts of Radiante is not covered
by this permission.

## Scope

These permissions cover only the parts of Radiante whose copyright holders have granted them. Code that remains
from Radiance and MCVR stays under its original licences unless their authors grant the same permissions. If you
modify Radiante, you may extend these permissions to your version, but you are not obliged to do so.
