# Third-Party Licenses and Attributions

This distribution includes or utilizes the following third-party software and linguistic data.

---

## 1. marisa-trie (Statically Linked)
- Copyright (c) 2010-2024, Susumu Yata
- License: BSD 2-Clause License (or LGPL-2.1+)
- Source: https://github.com/s-yata/marisa-trie

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice,
   this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.

---

## 2. Fcitx5 (Dynamically Linked)
- Copyright (c) Fcitx5 Authors / Weng Xuetian
- License: GNU Lesser General Public License v2.1 or later (LGPL-2.1+)
- Source: https://github.com/fcitx/fcitx5

---

## 3. KenLM Language Model Format / Binary
- Copyright (c) Kenneth Heafield
- License: GNU Lesser General Public License v2.1 or later (LGPL-2.1+)
- Source: https://github.com/kpu/kenlm
- Note: The bundled `kenlm.binary` language model was built using KenLM. Under LGPL-2.1+, the full license text is available at https://www.gnu.org/licenses/old-licenses/lgpl-2.1.html .

---

## 4. Dictionary & Connection Cost Data (`dict.bin`, `connection.bin`)
The compiled dictionary and connection matrices incorporate linguistic resources from:
- **Mozc** (Copyright Google Inc. / BSD 3-Clause License): https://github.com/google/mozc
- **SudachiDict** (Copyright Works Applications Co., Ltd. / Apache License 2.0): https://github.com/WorksApplications/SudachiDict
- **SKK Dictionary** (Copyright SKK Development Team / GNU General Public License v2.0 or later): https://github.com/skk-dev/dict
  - Note: Under GPL-2.0+, source vocabulary data and build scripts used to generate derivative dictionary binaries are made available in the project repository.
