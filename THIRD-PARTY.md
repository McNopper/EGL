# Third-Party Licenses

This file aggregates the third-party licenses and copyright notices that apply to
the **McNopper/EGL** project.

EGL itself is distributed under the **MIT License** — see [`LICENSE`](LICENSE).

## Third-party dependency: ANGLE

EGL declares a single third-party dependency via its vcpkg manifest
(`vcpkg.json`): **ANGLE**, used only on Windows as the optional OpenGL ES
backend (loaded at runtime via `LoadLibrary`; it is not bundled in source).

- **Library:** ANGLE (Almost Native Graphics Layer Engine)
- **Upstream:** https://github.com/google/angle
- **License:** BSD-3-Clause
- **Copyright:** Copyright 2018 The ANGLE Project Authors. All rights reserved.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

* Redistributions of source code must retain the above copyright notice, this
  list of conditions and the following disclaimer.
* Redistributions in binary form must reproduce the above copyright notice,
  this list of conditions and the following disclaimer in the documentation
  and/or other materials provided with the distribution.
* Neither the name of Google Inc. nor the names of its contributors may be
  used to endorse or promote products derived from this software without
  specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

ANGLE additionally bundles its own third-party components (e.g. zlib, Vulkan
Headers, glslang, SPIRV-Tools/Headers). Their licenses are permissive and are
covered by ANGLE's own notices — see ANGLE's `LICENSE` and `third_party/*/LICENSE`
in the upstream repository. They are not re-listed here.

## Platform notes

- **Windows:** ANGLE (BSD-3-Clause) is the only third-party dependency, and only
  when the ES backend is enabled (`EGL_WIN_ENABLE_ANGLE=ON`, the default).
- **Linux / macOS / `EGL_WIN_ENABLE_ANGLE=OFF`:** EGL uses only system libraries
  (Vulkan, X11, OpenGL, Wayland) loaded via `find_package` / `dlopen` — there are
  **no** bundled third-party dependencies.
