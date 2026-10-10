#pragma once

struct ThirdPartyLicense {
    const char* name;
    const char* usage;
    const char* license;
    const char* copyright;
    const char* text;
};

static const char* const g_license_mit_text =
R"LICENSE(Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.)LICENSE";

static const char* const g_license_zlib_text =
R"LICENSE(This software is provided 'as-is', without any express or
implied warranty. In no event will the authors be held liable
for any damages arising from the use of this software.

Permission is granted to anyone to use this software for any
purpose, including commercial applications, and to alter it and
redistribute it freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you
   must not claim that you wrote the original software. If you use
   this software in a product, an acknowledgment in the product
   documentation would be appreciated but is not required.

2. Altered source versions must be plainly marked as such, and
   must not be misrepresented as being the original software.

3. This notice may not be removed or altered from any source
   distribution.)LICENSE";

static const char* const g_license_wavpack_text =
R"LICENSE(All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright notice,
      this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright notice,
      this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the copyright holder nor the names of its
      contributors may be used to endorse or promote products derived from
      this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.)LICENSE";

static const char* const g_license_lucide_text =
R"LICENSE(Permission to use, copy, modify, and/or distribute this software for any
purpose with or without fee is hereby granted, provided that the above
copyright notice and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.)LICENSE";

static const char* const g_license_trademarks_text =
R"LICENSE(VST is a trademark of Steinberg Media Technologies GmbH, registered in Europe and other countries.

ASIO is a trademark and software of Steinberg Media Technologies GmbH.

FastResampler declares the VST 2 plugin interface and the ASIO driver interface itself. It does not include the VST 2 SDK or the ASIO SDK.)LICENSE";

static const ThirdPartyLicense g_third_party_licenses[] = {
    { "Dear ImGui", "The user interface", "MIT License",
      "Copyright (c) 2014-2026 Omar Cornut", g_license_mit_text },
    { "stb libraries", "stb_truetype, stb_rect_pack and stb_textedit, part of Dear ImGui; also public domain",
      "MIT License", "Copyright (c) 2017 Sean Barrett", g_license_mit_text },
    { "ProggyClean font", "The interface font, part of Dear ImGui", "MIT License",
      "Copyright (c) 2004, 2005 Tristan Grimmer", g_license_mit_text },
    { "ProggyForever font", "The scalable interface font, part of Dear ImGui", "MIT License",
      "Copyright (c) 2026 Disco Hello, Copyright (c) 2019, 2023 Tristan Grimmer", g_license_mit_text },
    { "Lucide icons", "The icons", "ISC License",
      "Copyright (c) 2026 Lucide Icons and Contributors", g_license_lucide_text },
    { "Feather icons", "The Lucide icons derived from the Feather project", "MIT License",
      "Copyright (c) 2013-present Cole Bemis", g_license_mit_text },
    { "IconFontCppHeaders", "The names of the icons in the icon font", "zlib License",
      "Copyright (c) 2017 Juliette Foucaut and Doug Binks", g_license_zlib_text },
    { "VST 3 SDK", "Loading and running VST3 plugins", "MIT License",
      "Copyright (c) 2026, Steinberg Media Technologies GmbH", g_license_mit_text },
    { "WavPack", "Compressing the streamed samples of a FastSampler bank", "BSD 3-Clause License",
      "Copyright (c) 1998 - 2025 David Bryant", g_license_wavpack_text },
    { "nanopb", "Writing the FastSampler instrument files", "zlib License",
      "Copyright (c) 2011 Petteri Aimonen <jpa at nanopb.mail.kapsi.fi>", g_license_zlib_text },
    { "fastsampler-shared", "The FastSampler file formats", "MIT License",
      "Copyright (c) 2026 Matthias", g_license_mit_text },
    { "Trademarks", "", "", "", g_license_trademarks_text },
};

#define THIRD_PARTY_LICENSE_COUNT ((int)(sizeof(g_third_party_licenses) / sizeof(g_third_party_licenses[0])))
